
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

typedef struct kr_defer_node {
    struct kr_defer_node *previous;
    void (*call)(void *);
    void *data;
} kr_defer_node;
static _Thread_local kr_defer_node *kr_defers = NULL;

typedef struct kr_thread {
    pthread_t id;
    int64_t (*worker)(int64_t);
    int64_t argument, result, code;
    bool joined;
    struct kr_thread *next;
} kr_thread;
static _Thread_local kr_thread *kr_threads, *kr_self_thread;
static _Thread_local kr_thread **kr_thread_slots;
static _Thread_local size_t kr_thread_slot_count;
static _Thread_local size_t kr_thread_active, kr_thread_count;
static _Thread_local bool kr_child_failure;
static pthread_mutex_t kr_output_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t kr_input_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t kr_diagnostic_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t kr_error_message_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local unsigned kr_output_depth, kr_input_depth;
static void kr_cleanup(void);
static void kr_release_io(void) {
    if (kr_output_depth) { kr_output_depth = 0; (void)pthread_mutex_unlock(&kr_output_mutex); }
    if (kr_input_depth) { kr_input_depth = 0; (void)pthread_mutex_unlock(&kr_input_mutex); }
}

/* The caller keeps this node alive until run_until. A registration boundary
 * avoids GCC treating the intentional scoped stack link as a dangling store. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void kr_defer_push(kr_defer_node *node, void (*call)(void *), void *data) {
    node->previous = kr_defers; node->call = call; node->data = data; kr_defers = node;
}
void kr_defer_run_until(kr_defer_node *mark) {
    while (kr_defers != mark) {
        kr_defer_node *node = kr_defers;
        if (node == NULL) abort();
        kr_defers = node->previous;
        node->call(node->data);
    }
}

typedef struct { const char *data; size_t len; } kr_string;

/* Owners remain registered after free so copied slice descriptors can report a
 * stale alias without dereferencing storage that has already been released. */
typedef struct kr_buffer {
    void *data;
    size_t length;
    size_t element_size;
    bool live;
    struct kr_buffer *next;
    struct kr_arena *arena;
    struct kr_buffer *arena_next;
} kr_buffer;

typedef struct kr_arena_chunk {
    struct kr_arena_chunk *next;
    size_t used, capacity;
    max_align_t alignment;
    unsigned char data[];
} kr_arena_chunk;
typedef struct kr_arena {
    kr_arena_chunk *chunks;
    kr_buffer *allocations;
    bool live;
    struct kr_arena *next;
} kr_arena;
typedef struct kr_file {
    FILE *stream;
    bool live;
    struct kr_file *next;
} kr_file;

typedef struct {
    kr_buffer *owner;
    size_t offset;
    size_t len;
} kr_slice;

/* Generated checks pass an immutable location ID. Resolving it only when a
 * check fails adds no lookup or mutable location state on the success path. */
typedef struct {
    const char *path, *source;
    int line, column;
    unsigned caret, width;
} kr_location;
static const kr_location *kr_source_locations;
static size_t kr_source_location_count;

/* Source paths are data, not terminal commands. This error-only renderer
 * mirrors the frontend's UTF-8/control policy without a runtime allocation. */
static void kr_report_path(const char *path) {
    const unsigned char *p = (const unsigned char *)path;
    size_t remaining = strlen(path);
    while (remaining) {
        unsigned char first = *p;
        size_t length = first < 128 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 :
            first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        bool valid = length != 0 && length <= remaining;
        for (size_t i = 1; valid && i < length; i++) if ((p[i] & 0xc0) != 0x80) valid = false;
        if (valid && ((first == 0xe0 && p[1] < 0xa0) || (first == 0xed && p[1] >= 0xa0) ||
            (first == 0xf0 && p[1] < 0x90) || (first == 0xf4 && p[1] >= 0x90))) valid = false;
        if (!valid) { (void)fprintf(stderr, "\\x%02x", (unsigned)first); p++; remaining--; continue; }
        uint32_t scalar = length == 1 ? first : first & (length == 2 ? 0x1fu : length == 3 ? 0x0fu : 0x07u);
        for (size_t i = 1; i < length; i++) scalar = (scalar << 6) | (p[i] & 0x3fu);
        if (scalar < 32 || (scalar >= 0x7f && scalar <= 0x9f) || scalar == 0x061c || scalar == 0x200e ||
            scalar == 0x200f || (scalar >= 0x2028 && scalar <= 0x202e) || (scalar >= 0x2066 && scalar <= 0x2069)) {
            if (scalar < 128) (void)fprintf(stderr, "\\x%02x", (unsigned)scalar);
            else (void)fprintf(stderr, "\\u%04x", (unsigned)scalar);
        } else (void)fwrite(p, 1, length, stderr);
        p += length; remaining -= length;
    }
}

static void kr_report_error(const char *message, size_t length, int id) {
    (void)pthread_mutex_lock(&kr_diagnostic_mutex);
    const kr_location *location = id > 0 && (size_t)id <= kr_source_location_count ? &kr_source_locations[id - 1] : NULL;
    if (location != NULL) {
        kr_report_path(location->path);
        (void)fprintf(stderr, ":%d:%d: runtime error: ", location->line, location->column);
    } else if (id > 0) {
        (void)fprintf(stderr, "runtime error at line %d: ", id);
    } else {
        (void)fputs("runtime error: ", stderr);
    }
    if (length) (void)fwrite(message, 1, length, stderr);
    (void)fputc('\n', stderr);
    if (location != NULL) {
        int digits = 1;
        for (int n = location->line; n >= 10; n /= 10) digits++;
        (void)fprintf(stderr, " %*s |\n %d | %s\n %*s | ", digits, "", location->line, location->source, digits, "");
        for (unsigned i = 0; i < location->caret; i++) (void)fputc(' ', stderr);
        (void)fputc('^', stderr);
        for (unsigned i = 1; i < location->width; i++) (void)fputc('~', stderr);
        (void)fputc('\n', stderr);
    }
    if (kr_self_thread != NULL) {
        (void)fprintf(stderr, " note: worker started with argument %" PRId64 "; threadJoin reports ECANCELED\n", kr_self_thread->argument);
    }
    (void)pthread_mutex_unlock(&kr_diagnostic_mutex);
}

_Noreturn void kr_panic(const char *msg, int line) {
    kr_release_io();
    kr_report_error(msg, strlen(msg), line);
    kr_defer_run_until(NULL);
    if (kr_self_thread != NULL) {
        kr_self_thread->code = ECANCELED;
        kr_cleanup();
        pthread_exit(NULL);
    }
    exit(EXIT_FAILURE);
}

void kr_output_begin(void) {
    if (kr_output_depth == 0 && pthread_mutex_lock(&kr_output_mutex) != 0)
        kr_panic("cannot lock output", 0);
    kr_output_depth++;
}
void kr_output_end(void) {
    if (kr_output_depth == 0) kr_panic("unbalanced output lock", 0);
    if (--kr_output_depth == 0) (void)pthread_mutex_unlock(&kr_output_mutex);
}
static void kr_input_begin(void) {
    if (kr_input_depth == 0 && pthread_mutex_lock(&kr_input_mutex) != 0)
        kr_panic("cannot lock input", 0);
    kr_input_depth++;
}
static void kr_input_end(void) {
    if (--kr_input_depth == 0) (void)pthread_mutex_unlock(&kr_input_mutex);
}

size_t kr_idx(int64_t index, size_t n, int line) {
    if (index < 0 || (uintmax_t)index >= (uintmax_t)n) {
        kr_panic("array index out of bounds", line);
    }
    return (size_t)index;
}

void *kr_nonnull(void *pointer, int line) {
    if (pointer == NULL) {
        kr_panic("null pointer dereference", line);
    }
    return pointer;
}

typedef struct kr_allocation {
    void *data;
    struct kr_allocation *next;
} kr_allocation;

static _Thread_local kr_allocation *kr_allocations = NULL;
static _Thread_local kr_buffer *kr_buffers = NULL;
static _Thread_local kr_arena *kr_arenas = NULL;
static _Thread_local kr_file *kr_files = NULL;
static _Thread_local bool kr_cleanup_registered = false;

static void kr_cleanup(void) {
    /* Each creator exclusively owns this registry. Results are read only
     * after pthread_join establishes synchronization with its child. */
    while (kr_threads != NULL) {
        kr_thread *thread = kr_threads; kr_threads = thread->next;
        if (!thread->joined) {
            int code = pthread_join(thread->id, NULL);
            if (code != 0) { kr_child_failure = true; continue; }
            if (thread->code != 0) kr_child_failure = true;
        }
        free(thread);
    }
    kr_thread_active = 0; kr_thread_count = 0;
    free(kr_thread_slots); kr_thread_slots = NULL; kr_thread_slot_count = 0;
    while (kr_files != NULL) {
        kr_file *file = kr_files; kr_files = file->next;
        if (file->live) (void)fclose(file->stream);
        free(file);
    }
    while (kr_arenas != NULL) {
        kr_arena *arena = kr_arenas; kr_arenas = arena->next;
        while (arena->chunks != NULL) { kr_arena_chunk *chunk = arena->chunks; arena->chunks = chunk->next; free(chunk); }
        free(arena);
    }
    while (kr_allocations != NULL) {
        kr_allocation *allocation = kr_allocations;
        kr_allocations = allocation->next;
        free(allocation->data);
        free(allocation);
    }
    while (kr_buffers != NULL) {
        kr_buffer *buffer = kr_buffers;
        kr_buffers = buffer->next;
        if (buffer->arena == NULL) free(buffer->data);
        free(buffer);
    }
    if (kr_self_thread != NULL && kr_child_failure) kr_self_thread->code = ECANCELED;
}

static void kr_process_cleanup(void) {
    kr_cleanup();
    /* Joining children above finishes their output and local cleanup before
     * checking the process stream. Small writes may have succeeded only into
     * stdio's buffer, so relying on exit's unchecked flush loses I/O errors. */
    bool output_failed = fflush(stdout) == EOF || ferror(stdout);
    if (output_failed) {
        const char *message = "output failed during final flush";
        kr_report_error(message, strlen(message), 0);
    }
    if (kr_child_failure || output_failed) {
        (void)fflush(stderr);
        /* Already inside atexit, with Kremen cleanup complete: do not panic
         * or call exit again and risk re-entering termination callbacks. */
        _Exit(EXIT_FAILURE);
    }
}

static bool kr_register_cleanup(void) {
    if (!kr_cleanup_registered) {
        if (kr_self_thread == NULL && atexit(kr_process_cleanup) != 0) {
            return false;
        }
        kr_cleanup_registered = true;
    }
    return true;
}

void kr_runtime_init(void) {
    if (!kr_register_cleanup()) kr_panic("cannot register runtime cleanup", 0);
}

static void *kr_thread_entry(void *data) {
    kr_thread *thread = data;
    kr_self_thread = thread;
    thread->result = thread->worker(thread->argument);
    kr_cleanup();
    return NULL;
}

static size_t kr_thread_slot(kr_thread *handle, size_t capacity) {
    uint64_t bits = (uint64_t)(uintptr_t)handle;
    bits = (bits >> 4) * UINT64_C(0x9e3779b97f4a7c15);
    bits ^= bits >> 32;
    return (size_t)bits & (capacity - 1);
}

/* A creator keeps handle metadata until exit, including joined handles. Use
 * an ownership index so repeated close/join does not scan that retained list.
 * Looking up a foreign pointer only hashes/compares it, never dereferences it. */
static bool kr_thread_reserve(void) {
    if (kr_thread_count < kr_thread_slot_count / 2) return true;
    size_t capacity = kr_thread_slot_count ? kr_thread_slot_count * 2 : 64;
    kr_thread **slots = calloc(capacity, sizeof *slots);
    if (slots == NULL) return false;
    for (kr_thread *thread = kr_threads; thread != NULL; thread = thread->next) {
        size_t slot = kr_thread_slot(thread, capacity);
        while (slots[slot] != NULL) slot = (slot + 1) & (capacity - 1);
        slots[slot] = thread;
    }
    free(kr_thread_slots); kr_thread_slots = slots; kr_thread_slot_count = capacity;
    return true;
}

bool kr_thread_spawn(int64_t (*worker)(int64_t), int64_t argument, kr_thread **out, int64_t *code) {
    *out = NULL; *code = 0;
    if (worker == NULL) { *code = EINVAL; return false; }
    if (kr_thread_active >= 256 || kr_thread_count >= 65536) { *code = EAGAIN; return false; }
    if (!kr_register_cleanup()) { *code = ENOMEM; return false; }
    if (!kr_thread_reserve()) { *code = ENOMEM; return false; }
    kr_thread *thread = calloc(1, sizeof *thread);
    if (thread == NULL) { *code = ENOMEM; return false; }
    thread->worker = worker; thread->argument = argument;
    int failure = pthread_create(&thread->id, NULL, kr_thread_entry, thread);
    if (failure != 0) { free(thread); *code = failure; return false; }
    thread->next = kr_threads; kr_threads = thread;
    size_t slot = kr_thread_slot(thread, kr_thread_slot_count);
    while (kr_thread_slots[slot] != NULL) slot = (slot + 1) & (kr_thread_slot_count - 1);
    kr_thread_slots[slot] = thread;
    kr_thread_active++; kr_thread_count++;
    *out = thread;
    return true;
}

static kr_thread *kr_thread_owned(kr_thread *handle) {
    if (handle == NULL || kr_thread_slot_count == 0) return NULL;
    size_t slot = kr_thread_slot(handle, kr_thread_slot_count);
    while (kr_thread_slots[slot] != NULL) {
        if (kr_thread_slots[slot] == handle) return handle;
        slot = (slot + 1) & (kr_thread_slot_count - 1);
    }
    return NULL;
}

bool kr_thread_join(kr_thread *handle, int64_t *out, int64_t *code) {
    *out = 0; *code = 0;
    kr_thread *thread = kr_thread_owned(handle);
    if (thread == NULL || thread->joined) { *code = EBADF; return false; }
    int failure = pthread_join(thread->id, NULL);
    if (failure != 0) { *code = failure; return false; }
    thread->joined = true; kr_thread_active--;
    if (thread->code != 0) { *code = thread->code; return false; }
    *out = thread->result;
    return true;
}

void kr_thread_close(kr_thread *handle, int location) {
    kr_thread *thread = kr_thread_owned(handle);
    if (thread == NULL) kr_panic("thread handle is invalid in this creator", location);
    if (!thread->joined) {
        int64_t value, code;
        if (!kr_thread_join(thread, &value, &code)) kr_panic("thread cleanup failed", location);
    }
}

static void kr_track(void *data, int line) {
    kr_allocation *allocation = malloc(sizeof *allocation);
    if (allocation == NULL) {
        free(data);
        kr_panic("out of memory", line);
    }
    if (!kr_register_cleanup()) {
        free(allocation);
        free(data);
        kr_panic("cannot register runtime cleanup", line);
    }
    allocation->data = data;
    allocation->next = kr_allocations;
    kr_allocations = allocation;
}

/* An all-zero descriptor is the initialized, empty slice for every type. */
static kr_buffer *kr_slice_validate(kr_slice value, size_t element_size, int line) {
    kr_buffer *owner = value.owner;
    if (owner == NULL) {
        if (value.offset != 0 || value.len != 0) {
            kr_panic("invalid slice descriptor", line);
        }
        return NULL;
    }
    if (!owner->live) {
        kr_panic("slice use after free", line);
    }
    if (element_size != 0 && element_size != owner->element_size) {
        kr_panic("slice element size mismatch", line);
    }
    if (value.offset > owner->length || value.len > owner->length - value.offset) {
        kr_panic("invalid slice bounds", line);
    }
    return owner;
}

kr_slice kr_slice_alloc(int64_t length, size_t element_size, int line) {
    kr_buffer *owner;
    kr_slice result;
    if (length < 0) {
        kr_panic("negative slice allocation length", line);
    }
    if (element_size == 0) {
        kr_panic("zero slice element size", line);
    }
    if ((uintmax_t)length > (uintmax_t)(SIZE_MAX / element_size)) {
        kr_panic("slice allocation size overflow", line);
    }
    if (!kr_register_cleanup()) {
        kr_panic("cannot register runtime cleanup", line);
    }
    owner = malloc(sizeof *owner);
    if (owner == NULL) {
        kr_panic("out of memory", line);
    }
    owner->data = length == 0 ? NULL : calloc((size_t)length, element_size);
    if (length != 0 && owner->data == NULL) {
        free(owner);
        kr_panic("out of memory", line);
    }
    owner->length = (size_t)length;
    owner->element_size = element_size;
    owner->live = true;
    owner->arena = NULL;
    owner->arena_next = NULL;
    owner->next = kr_buffers;
    kr_buffers = owner;
    result.owner = owner;
    result.offset = 0;
    result.len = (size_t)length;
    return result;
}

void kr_slice_free(kr_slice value, int line) {
    kr_buffer *owner;
    if (value.owner != NULL && !value.owner->live) {
        kr_panic("slice double free", line);
    }
    owner = kr_slice_validate(value, 0, line);
    if (owner == NULL) {
        return;
    }
    if (owner->arena != NULL) kr_panic("arena allocation must be released by its arena", line);
    if (value.offset != 0 || value.len != owner->length) {
        kr_panic("cannot free a partial slice view", line);
    }
    free(owner->data);
    owner->data = NULL;
    owner->live = false;
}

void *kr_slice_at(kr_slice value, int64_t index, size_t element_size, int line) {
    kr_buffer *owner = kr_slice_validate(value, element_size, line);
    size_t position = kr_idx(index, value.len, line);
    /* The bounds check above rejects all indices when owner is NULL. */
    if (owner == NULL || element_size == 0) {
        kr_panic("invalid slice element size", line);
    }
    return (unsigned char *)owner->data + (value.offset + position) * element_size;
}

int64_t kr_slice_len(kr_slice value, int line) {
    (void)kr_slice_validate(value, 0, line);
    if ((uintmax_t)value.len > (uintmax_t)INT64_MAX) {
        kr_panic("slice length is outside the i64 range", line);
    }
    return (int64_t)value.len;
}

kr_slice kr_subslice(kr_slice value, int64_t low, int64_t high, int line) {
    kr_slice result;
    (void)kr_slice_validate(value, 0, line);
    if (low < 0 || high < low || (uintmax_t)high > (uintmax_t)value.len) {
        kr_panic("slice bounds out of range", line);
    }
    result.owner = value.owner;
    result.offset = value.offset + (size_t)low;
    result.len = (size_t)(high - low);
    return result;
}

kr_arena *kr_arena_new(int line) {
    if (!kr_register_cleanup()) kr_panic("cannot register runtime cleanup", line);
    kr_arena *arena = calloc(1, sizeof *arena);
    if (arena == NULL) kr_panic("out of memory", line);
    arena->live = true; arena->next = kr_arenas; kr_arenas = arena;
    return arena;
}

void kr_arena_reset(kr_arena *arena, int line) {
    if (arena == NULL || !arena->live) kr_panic("arena use after free", line);
    for (kr_buffer *b = arena->allocations; b != NULL; b = b->arena_next) { b->live = false; b->data = NULL; }
    arena->allocations = NULL;
    while (arena->chunks != NULL) { kr_arena_chunk *chunk = arena->chunks; arena->chunks = chunk->next; free(chunk); }
}
void kr_arena_free(kr_arena *arena, int line) {
    if (arena == NULL || !arena->live) kr_panic("arena double free", line);
    kr_arena_reset(arena, line); arena->live = false;
}
void kr_arena_defer(void *data) {
    kr_arena *arena = *(kr_arena **)data;
    if (arena != NULL && arena->live) kr_arena_free(arena, 0);
}
kr_slice kr_arena_alloc(kr_arena *arena, int64_t length, size_t element_size, int line) {
    if (arena == NULL || !arena->live) kr_panic("arena use after free", line);
    if (length < 0) kr_panic("negative arena allocation length", line);
    if (element_size == 0 || (uintmax_t)length > SIZE_MAX / element_size) kr_panic("arena allocation size overflow", line);
    size_t bytes = (size_t)length * element_size;
    const size_t align = _Alignof(max_align_t);
    kr_arena_chunk *chunk = arena->chunks;
    size_t offset = chunk == NULL ? 0 : (chunk->used + align - 1) / align * align;
    if (bytes != 0 && (chunk == NULL || offset > chunk->capacity || bytes > chunk->capacity - offset)) {
        size_t capacity = bytes > 65536 ? bytes : 65536;
        if (capacity > SIZE_MAX - sizeof *chunk) kr_panic("arena allocation size overflow", line);
        chunk = calloc(1, sizeof *chunk + capacity);
        if (chunk == NULL) kr_panic("out of memory", line);
        chunk->capacity = capacity; chunk->next = arena->chunks; arena->chunks = chunk; offset = 0;
    }
    kr_buffer *owner = calloc(1, sizeof *owner);
    if (owner == NULL) kr_panic("out of memory", line);
    owner->data = bytes == 0 ? NULL : chunk->data + offset;
    if (bytes != 0) chunk->used = offset + bytes;
    owner->length = (size_t)length; owner->element_size = element_size; owner->live = true; owner->arena = arena;
    owner->arena_next = arena->allocations; arena->allocations = owner;
    owner->next = kr_buffers; kr_buffers = owner;
    kr_slice result = {owner, 0, (size_t)length}; return result;
}

void kr_print_i64(int64_t value) {
    if (printf("%" PRId64, value) < 0) {
        kr_panic("output failed", 0);
    }
}

void kr_print_u64(uint64_t value) {
    if (printf("%" PRIu64, value) < 0) {
        kr_panic("output failed", 0);
    }
}

void kr_print_bool(bool value) {
    if (fputs(value ? "true" : "false", stdout) == EOF) {
        kr_panic("output failed", 0);
    }
}

void kr_print_string(kr_string value) {
    if (value.len != 0 && fwrite(value.data, 1, value.len, stdout) != value.len) {
        kr_panic("output failed", 0);
    }
}

void kr_space(void) {
    if (putchar(' ') == EOF) {
        kr_panic("output failed", 0);
    }
}

void kr_newline(void) {
    if (putchar('\n') == EOF) {
        kr_panic("output failed", 0);
    }
}

static bool kr_whitespace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'
        || ch == '\f' || ch == '\v';
}

static bool kr_try_read_int_unlocked(int64_t *out, int64_t *code) {
    int ch;
    bool negative = false;
    bool digits = false;
    bool malformed = false;
    bool overflow = false;
    uint64_t magnitude = UINT64_C(0);
    uint64_t limit;
    *out = INT64_C(0);
    *code = INT64_C(0);
    if (fflush(stdout) == EOF) {
        *code = INT64_C(4);
        return false;
    }
    do {
        ch = getchar();
    } while (ch != EOF && kr_whitespace(ch));
    if (ch == EOF) {
        *code = ferror(stdin) ? INT64_C(4) : INT64_C(1);
        return false;
    }
    if (ch == '+' || ch == '-') {
        negative = ch == '-';
        ch = getchar();
    }
    limit = negative ? (uint64_t)INT64_MAX + UINT64_C(1) : (uint64_t)INT64_MAX;
    while (ch != EOF && !kr_whitespace(ch)) {
        if (ch < '0' || ch > '9') {
            malformed = true;
        } else {
            uint64_t digit = (uint64_t)(ch - '0');
            digits = true;
            if (magnitude > (limit - digit) / UINT64_C(10)) {
                overflow = true;
            } else if (!overflow) {
                magnitude = magnitude * UINT64_C(10) + digit;
            }
        }
        ch = getchar();
    }
    if (ferror(stdin)) {
        *code = INT64_C(4);
        return false;
    }
    if (malformed || !digits) {
        *code = INT64_C(2);
        return false;
    }
    if (overflow) {
        *code = INT64_C(3);
        return false;
    }
    if (!negative) {
        *out = (int64_t)magnitude;
    } else if (magnitude == (uint64_t)INT64_MAX + UINT64_C(1)) {
        *out = INT64_MIN;
    } else {
        *out = -(int64_t)magnitude;
    }
    return true;
}

bool kr_try_read_int(int64_t *out, int64_t *code) {
    kr_input_begin();
    bool success = kr_try_read_int_unlocked(out, code);
    kr_input_end();
    return success;
}

int64_t kr_read_int(int line) {
    int64_t value;
    int64_t code;
    if (!kr_try_read_int(&value, &code)) {
        switch (code) {
        case 1: kr_panic("unexpected end of input", line);
        case 2: kr_panic("invalid integer input", line);
        case 3: kr_panic("integer input is outside the i64 range", line);
        default: kr_panic("input failed", line);
        }
    }
    return value;
}

static kr_string kr_read_line_unlocked(int line) {
    size_t capacity = 64;
    size_t length = 0;
    char *data = malloc(capacity);
    int ch;
    if (data == NULL) {
        kr_panic("out of memory", line);
    }
    if (fflush(stdout) == EOF) {
        free(data);
        kr_panic("output failed", line);
    }
    while ((ch = getchar()) != EOF && ch != '\n') {
        if (length == capacity - 1) {
            char *grown;
            if (capacity > SIZE_MAX / 2) {
                free(data);
                kr_panic("input line is too long", line);
            }
            capacity *= 2;
            grown = realloc(data, capacity);
            if (grown == NULL) {
                free(data);
                kr_panic("out of memory", line);
            }
            data = grown;
        }
        ((unsigned char *)data)[length++] = (unsigned char)ch;
    }
    if (ferror(stdin)) {
        free(data);
        kr_panic("input failed", line);
    }
    if (ch == EOF && length == 0) {
        free(data);
        kr_panic("unexpected end of input", line);
    }
    if (ch == '\n' && length != 0 && data[length - 1] == '\r') {
        --length;
    }
    if (length + 1 < capacity) {
        char *trimmed = realloc(data, length + 1);
        if (trimmed != NULL) data = trimmed;
    }
    data[length] = '\0';
    kr_track(data, line);
    {
        kr_string result = {data, length};
        return result;
    }
}

kr_string kr_read_line(int line) {
    kr_input_begin();
    kr_string result = kr_read_line_unlocked(line);
    kr_input_end();
    return result;
}

bool kr_str_eq(kr_string left, kr_string right) {
    return left.len == right.len
        && (left.len == 0 || memcmp(left.data, right.data, left.len) == 0);
}

int64_t kr_str_len(kr_string value, int line) {
    if ((uintmax_t)value.len > (uintmax_t)INT64_MAX) {
        kr_panic("string length is outside the i64 range", line);
    }
    return (int64_t)value.len;
}

void kr_assert(bool condition, int line) {
    if (!condition) {
        kr_panic("assertion failed", line);
    }
}

_Noreturn void kr_user_panic(kr_string message, int line) {
    kr_release_io();
    kr_report_error(message.data, message.len, line);
    kr_defer_run_until(NULL);
    if (kr_self_thread != NULL) {
        kr_self_thread->code = ECANCELED;
        kr_cleanup();
        pthread_exit(NULL);
    }
    exit(EXIT_FAILURE);
}

/* Filesystem errors are ordinary errno values in result.code, never unwinds. */
static int64_t kr_io_code(void) { return errno > 0 ? (int64_t)errno : (int64_t)EIO; }
static char *kr_fs_path(kr_string value, int64_t *code) {
    if (value.len == SIZE_MAX || (value.len && memchr(value.data, 0, value.len))) { *code = EINVAL; return NULL; }
    char *path = malloc(value.len + 1);
    if (path == NULL) { *code = ENOMEM; return NULL; }
    if (value.len) memcpy(path, value.data, value.len);
    path[value.len] = 0; return path;
}
static bool kr_fs_read_stream(FILE *stream, kr_string *out, int64_t *code) {
    const size_t limit = 64u * 1024u * 1024u;
    size_t size = 0, capacity = 4096;
    char *data = malloc(capacity + 1);
    if (data == NULL) { *code = ENOMEM; return false; }
    for (;;) {
        size_t n = fread(data + size, 1, capacity - size, stream); size += n;
        if (ferror(stream)) { *code = kr_io_code(); free(data); return false; }
        if (feof(stream)) break;
        if (size == capacity) {
            /* Check EOF before growing. Exact power-of-two files should not
             * retain twice their size merely to discover the end of input. */
            int ch = fgetc(stream);
            if (ch == EOF) {
                if (ferror(stream)) { *code = kr_io_code(); free(data); return false; }
                break;
            }
            if (capacity == limit) { *code = EFBIG; free(data); return false; }
            capacity *= 2;
            char *grown = realloc(data, capacity + 1);
            if (grown == NULL) { free(data); *code = ENOMEM; return false; }
            data = grown;
            data[size++] = (char)(unsigned char)ch;
        }
    }
    if (size < capacity) {
        /* Strings are retained until exit. Return unused capacity when the
         * allocator permits it; a failed optional shrink is still success. */
        char *trimmed = realloc(data, size + 1);
        if (trimmed != NULL) data = trimmed;
    }
    data[size] = 0; kr_track(data, 0); out->data = data; out->len = size; *code = 0; return true;
}
bool kr_fs_read(kr_string path, kr_string *out, int64_t *code) {
    *code = 0; char *name = kr_fs_path(path, code); if (name == NULL) return false;
    FILE *stream = fopen(name, "rb"); int64_t error = stream == NULL ? kr_io_code() : 0; free(name);
    if (stream == NULL) { *code = error; return false; }
    bool ok = kr_fs_read_stream(stream, out, code);
    if (fclose(stream) && ok) { *code = kr_io_code(); return false; }
    return ok;
}
bool kr_fs_write(kr_string path, kr_string value, bool append, int64_t *code) {
    *code = 0; char *name = kr_fs_path(path, code); if (name == NULL) return false;
    FILE *stream = fopen(name, append ? "ab" : "wb"); int64_t error = stream == NULL ? kr_io_code() : 0; free(name);
    if (stream == NULL) { *code = error; return false; }
    bool ok = value.len == 0 || fwrite(value.data, 1, value.len, stream) == value.len;
    if (!ok) *code = kr_io_code();
    if (fclose(stream) && ok) { *code = kr_io_code(); ok = false; }
    return ok;
}
bool kr_fs_stat(kr_string path, int operation, int64_t *value, int64_t *code) {
    *code = 0; char *name = kr_fs_path(path, code); if (name == NULL) return false;
    struct stat info; int result = stat(name, &info); int64_t error = result ? kr_io_code() : 0; free(name);
    if (result) {
        if (operation == 0 && (error == ENOENT || error == ENOTDIR)) { *value = 0; return true; }
        *code = error; return false;
    }
    if (operation == 0) *value = 1;
    else if (operation == 1) *value = S_ISDIR(info.st_mode) ? 1 : 0;
    else { if (info.st_size < 0 || (uintmax_t)info.st_size > INT64_MAX) { *code = EOVERFLOW; return false; } *value = (int64_t)info.st_size; }
    return true;
}
bool kr_fs_modify(kr_string path, int operation, int64_t *code) {
    *code = 0; char *name = kr_fs_path(path, code); if (name == NULL) return false;
    int result = operation == 0 ? mkdir(name, 0777) : remove(name);
    int64_t error = result ? kr_io_code() : 0; free(name); *code = error; return result == 0;
}
bool kr_fs_rename(kr_string from, kr_string to, int64_t *code) {
    *code = 0; char *a = kr_fs_path(from, code); if (a == NULL) return false;
    char *b = kr_fs_path(to, code); if (b == NULL) { free(a); return false; }
    int result = rename(a, b); int64_t error = result ? kr_io_code() : 0;
    free(a); free(b); *code = error; return result == 0;
}
bool kr_fs_cwd(kr_string *out, int64_t *code) {
    *code = 0; size_t capacity = 128; char *data = NULL;
    for (;;) {
        char *grown = realloc(data, capacity);
        if (grown == NULL) { free(data); *code = ENOMEM; return false; } data = grown;
        if (getcwd(data, capacity) != NULL) break;
        if (errno != ERANGE || capacity >= 1024u * 1024u) { *code = kr_io_code(); free(data); return false; }
        capacity *= 2;
    }
    kr_track(data, 0); out->data = data; out->len = strlen(data); return true;
}
static int kr_fs_compare(const void *a, const void *b) {
    const kr_string *x = a, *y = b; size_t n = x->len < y->len ? x->len : y->len;
    int cmp = n ? memcmp(x->data, y->data, n) : 0;
    return cmp ? cmp : (x->len > y->len) - (x->len < y->len);
}
bool kr_fs_list(kr_string path, kr_slice *out, int64_t *code) {
    *code = 0; char *name = kr_fs_path(path, code); if (name == NULL) return false;
    DIR *dir = opendir(name); int64_t error = dir == NULL ? kr_io_code() : 0; free(name);
    if (dir == NULL) { *code = error; return false; }
    kr_string *names = NULL; size_t count = 0, capacity = 0;
    for (;;) {
        errno = 0; struct dirent *entry = readdir(dir);
        if (entry == NULL) { if (errno) error = kr_io_code(); break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (count == 1000000) { error = EFBIG; break; }
        if (count == capacity) {
            size_t cap = capacity ? capacity * 2 : 16;
            kr_string *grown = realloc(names, cap * sizeof *grown);
            if (grown == NULL) { error = ENOMEM; break; } names = grown; capacity = cap;
        }
        size_t n = strlen(entry->d_name); char *copy = malloc(n + 1);
        if (copy == NULL) { error = ENOMEM; break; }
        memcpy(copy, entry->d_name, n + 1); kr_track(copy, 0); names[count].data = copy; names[count++].len = n;
    }
    if (closedir(dir) && error == 0) error = kr_io_code();
    if (error != 0) { free(names); *code = error; return false; }
    if (count > 1) qsort(names, count, sizeof *names, kr_fs_compare);
    *out = kr_slice_alloc((int64_t)count, sizeof(kr_string), 0);
    if (count) memcpy(out->owner->data, names, count * sizeof *names);
    free(names); return true;
}
bool kr_fs_open(kr_string path, kr_string mode, kr_file **out, int64_t *code) {
    *code = 0; char *name = kr_fs_path(path, code); if (name == NULL) return false;
    char *m = kr_fs_path(mode, code); if (m == NULL) { free(name); return false; }
    if (strcmp(m,"r") && strcmp(m,"w") && strcmp(m,"a") && strcmp(m,"rb") && strcmp(m,"wb") && strcmp(m,"ab")) {
        free(name); free(m); *code = EINVAL; return false;
    }
    FILE *stream = fopen(name, m); int64_t error = stream == NULL ? kr_io_code() : 0; free(name); free(m);
    if (stream == NULL) { *code = error; return false; }
    kr_file *file = malloc(sizeof *file);
    if (file == NULL) { (void)fclose(stream); *code = ENOMEM; return false; }
    if (!kr_register_cleanup()) { free(file); (void)fclose(stream); *code = ENOMEM; return false; }
    file->stream = stream; file->live = true; file->next = kr_files; kr_files = file; *out = file; return true;
}
bool kr_fs_close(kr_file *file, int64_t *code) {
    *code = 0;
    if (file == NULL || !file->live) { *code = EBADF; return false; }
    file->live = false; FILE *stream = file->stream; file->stream = NULL;
    if (fclose(stream)) { *code = kr_io_code(); return false; } return true;
}
void kr_file_close(kr_file *file, int line) {
    if (file != NULL && file->live) { int64_t code; if (!kr_fs_close(file, &code)) kr_panic("file close failed", line); }
}
bool kr_fs_read_all(kr_file *file, kr_string *out, int64_t *code) {
    if (file == NULL || !file->live) { *code = EBADF; return false; }
    return kr_fs_read_stream(file->stream, out, code);
}
bool kr_fs_write_all(kr_file *file, kr_string data, int64_t *out, int64_t *code) {
    *code = 0;
    if (file == NULL || !file->live) { *code = EBADF; return false; }
    if ((uintmax_t)data.len > INT64_MAX) { *code = EFBIG; return false; }
    if ((data.len && fwrite(data.data, 1, data.len, file->stream) != data.len) || fflush(file->stream)) { *code = kr_io_code(); return false; }
    *out = (int64_t)data.len; return true;
}
kr_string kr_error_message(int64_t code) {
    (void)pthread_mutex_lock(&kr_error_message_mutex);
    const char *message = code > INT_MAX || code < 0 ? "unknown error" : strerror((int)code);
    size_t n = strlen(message); char *copy = malloc(n + 1);
    if (copy != NULL) memcpy(copy, message, n + 1);
    (void)pthread_mutex_unlock(&kr_error_message_mutex);
    if (copy == NULL) kr_panic("out of memory", 0);
    kr_track(copy, 0); kr_string result = {copy, n}; return result;
}

int8_t kr_add_i8(int8_t a, int8_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if ((y > 0 && x > INT8_MAX - y) || (y < 0 && x < INT8_MIN - y)) { kr_panic("integer overflow", line); }
    return (int8_t)(x + y);
}

int8_t kr_sub_i8(int8_t a, int8_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if ((y > 0 && x < INT8_MIN + y) || (y < 0 && x > INT8_MAX + y)) { kr_panic("integer overflow", line); }
    return (int8_t)(x - y);
}

int8_t kr_mul_i8(int8_t a, int8_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if ((x > 0 && ((y > 0 && x > INT8_MAX / y) || (y < 0 && y < INT8_MIN / x))) || (x < 0 && ((y > 0 && x < INT8_MIN / y) || (y < 0 && x < INT8_MAX / y)))) { kr_panic("integer overflow", line); }
    return (int8_t)(x * y);
}

int8_t kr_div_i8(int8_t a, int8_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    if (x == INT8_MIN && y == -1) { kr_panic("integer overflow", line); }
    return (int8_t)(x / y);
}

int8_t kr_mod_i8(int8_t a, int8_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    if (x == INT8_MIN && y == -1) { kr_panic("integer overflow", line); }
    return (int8_t)(x % y);
}

int8_t kr_neg_i8(int8_t a, int line) {
    if (a == INT8_MIN) { kr_panic("integer overflow", line); }
    return (int8_t)(-(int64_t)a);
}

int8_t kr_shl_i8(int8_t a, int8_t b, int line) {
    if (a < 0) { kr_panic("cannot shift a negative integer", line); }
    if (b < 0 || (uint64_t)b >= UINT64_C(8)) { kr_panic("shift count out of range", line); }
    if ((uint64_t)a > ((uint64_t)INT8_MAX >> (unsigned)b)) { kr_panic("integer overflow", line); }
    return (int8_t)((uint64_t)a << (unsigned)b);
}

int8_t kr_shr_i8(int8_t a, int8_t b, int line) {
    if (a < 0) { kr_panic("cannot shift a negative integer", line); }
    if (b < 0 || (uint64_t)b >= UINT64_C(8)) { kr_panic("shift count out of range", line); }
    return (int8_t)((uint64_t)a >> (unsigned)b);
}

int16_t kr_add_i16(int16_t a, int16_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if ((y > 0 && x > INT16_MAX - y) || (y < 0 && x < INT16_MIN - y)) { kr_panic("integer overflow", line); }
    return (int16_t)(x + y);
}

int16_t kr_sub_i16(int16_t a, int16_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if ((y > 0 && x < INT16_MIN + y) || (y < 0 && x > INT16_MAX + y)) { kr_panic("integer overflow", line); }
    return (int16_t)(x - y);
}

int16_t kr_mul_i16(int16_t a, int16_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if ((x > 0 && ((y > 0 && x > INT16_MAX / y) || (y < 0 && y < INT16_MIN / x))) || (x < 0 && ((y > 0 && x < INT16_MIN / y) || (y < 0 && x < INT16_MAX / y)))) { kr_panic("integer overflow", line); }
    return (int16_t)(x * y);
}

int16_t kr_div_i16(int16_t a, int16_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    if (x == INT16_MIN && y == -1) { kr_panic("integer overflow", line); }
    return (int16_t)(x / y);
}

int16_t kr_mod_i16(int16_t a, int16_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    if (x == INT16_MIN && y == -1) { kr_panic("integer overflow", line); }
    return (int16_t)(x % y);
}

int16_t kr_neg_i16(int16_t a, int line) {
    if (a == INT16_MIN) { kr_panic("integer overflow", line); }
    return (int16_t)(-(int64_t)a);
}

int16_t kr_shl_i16(int16_t a, int16_t b, int line) {
    if (a < 0) { kr_panic("cannot shift a negative integer", line); }
    if (b < 0 || (uint64_t)b >= UINT64_C(16)) { kr_panic("shift count out of range", line); }
    if ((uint64_t)a > ((uint64_t)INT16_MAX >> (unsigned)b)) { kr_panic("integer overflow", line); }
    return (int16_t)((uint64_t)a << (unsigned)b);
}

int16_t kr_shr_i16(int16_t a, int16_t b, int line) {
    if (a < 0) { kr_panic("cannot shift a negative integer", line); }
    if (b < 0 || (uint64_t)b >= UINT64_C(16)) { kr_panic("shift count out of range", line); }
    return (int16_t)((uint64_t)a >> (unsigned)b);
}

int32_t kr_add_i32(int32_t a, int32_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if ((y > 0 && x > INT32_MAX - y) || (y < 0 && x < INT32_MIN - y)) { kr_panic("integer overflow", line); }
    return (int32_t)(x + y);
}

int32_t kr_sub_i32(int32_t a, int32_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if ((y > 0 && x < INT32_MIN + y) || (y < 0 && x > INT32_MAX + y)) { kr_panic("integer overflow", line); }
    return (int32_t)(x - y);
}

int32_t kr_mul_i32(int32_t a, int32_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if ((x > 0 && ((y > 0 && x > INT32_MAX / y) || (y < 0 && y < INT32_MIN / x))) || (x < 0 && ((y > 0 && x < INT32_MIN / y) || (y < 0 && x < INT32_MAX / y)))) { kr_panic("integer overflow", line); }
    return (int32_t)(x * y);
}

int32_t kr_div_i32(int32_t a, int32_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    if (x == INT32_MIN && y == -1) { kr_panic("integer overflow", line); }
    return (int32_t)(x / y);
}

int32_t kr_mod_i32(int32_t a, int32_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    if (x == INT32_MIN && y == -1) { kr_panic("integer overflow", line); }
    return (int32_t)(x % y);
}

int32_t kr_neg_i32(int32_t a, int line) {
    if (a == INT32_MIN) { kr_panic("integer overflow", line); }
    return (int32_t)(-(int64_t)a);
}

int32_t kr_shl_i32(int32_t a, int32_t b, int line) {
    if (a < 0) { kr_panic("cannot shift a negative integer", line); }
    if (b < 0 || (uint64_t)b >= UINT64_C(32)) { kr_panic("shift count out of range", line); }
    if ((uint64_t)a > ((uint64_t)INT32_MAX >> (unsigned)b)) { kr_panic("integer overflow", line); }
    return (int32_t)((uint64_t)a << (unsigned)b);
}

int32_t kr_shr_i32(int32_t a, int32_t b, int line) {
    if (a < 0) { kr_panic("cannot shift a negative integer", line); }
    if (b < 0 || (uint64_t)b >= UINT64_C(32)) { kr_panic("shift count out of range", line); }
    return (int32_t)((uint64_t)a >> (unsigned)b);
}

int64_t kr_add_i64(int64_t a, int64_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if ((y > 0 && x > INT64_MAX - y) || (y < 0 && x < INT64_MIN - y)) { kr_panic("integer overflow", line); }
    return (int64_t)(x + y);
}

int64_t kr_sub_i64(int64_t a, int64_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if ((y > 0 && x < INT64_MIN + y) || (y < 0 && x > INT64_MAX + y)) { kr_panic("integer overflow", line); }
    return (int64_t)(x - y);
}

int64_t kr_mul_i64(int64_t a, int64_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if ((x > 0 && ((y > 0 && x > INT64_MAX / y) || (y < 0 && y < INT64_MIN / x))) || (x < 0 && ((y > 0 && x < INT64_MIN / y) || (y < 0 && x < INT64_MAX / y)))) { kr_panic("integer overflow", line); }
    return (int64_t)(x * y);
}

int64_t kr_div_i64(int64_t a, int64_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    if (x == INT64_MIN && y == -1) { kr_panic("integer overflow", line); }
    return (int64_t)(x / y);
}

int64_t kr_mod_i64(int64_t a, int64_t b, int line) {
    int64_t x = (int64_t)a;
    int64_t y = (int64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    if (x == INT64_MIN && y == -1) { kr_panic("integer overflow", line); }
    return (int64_t)(x % y);
}

int64_t kr_neg_i64(int64_t a, int line) {
    if (a == INT64_MIN) { kr_panic("integer overflow", line); }
    return (int64_t)(-(int64_t)a);
}

int64_t kr_shl_i64(int64_t a, int64_t b, int line) {
    if (a < 0) { kr_panic("cannot shift a negative integer", line); }
    if (b < 0 || (uint64_t)b >= UINT64_C(64)) { kr_panic("shift count out of range", line); }
    if ((uint64_t)a > ((uint64_t)INT64_MAX >> (unsigned)b)) { kr_panic("integer overflow", line); }
    return (int64_t)((uint64_t)a << (unsigned)b);
}

int64_t kr_shr_i64(int64_t a, int64_t b, int line) {
    if (a < 0) { kr_panic("cannot shift a negative integer", line); }
    if (b < 0 || (uint64_t)b >= UINT64_C(64)) { kr_panic("shift count out of range", line); }
    return (int64_t)((uint64_t)a >> (unsigned)b);
}

uint8_t kr_add_u8(uint8_t a, uint8_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (x > UINT8_MAX - y) { kr_panic("integer overflow", line); }
    return (uint8_t)(x + y);
}

uint8_t kr_sub_u8(uint8_t a, uint8_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (x < y) { kr_panic("integer overflow", line); }
    return (uint8_t)(x - y);
}

uint8_t kr_mul_u8(uint8_t a, uint8_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (y != 0 && x > UINT8_MAX / y) { kr_panic("integer overflow", line); }
    return (uint8_t)(x * y);
}

uint8_t kr_div_u8(uint8_t a, uint8_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    return (uint8_t)(x / y);
}

uint8_t kr_mod_u8(uint8_t a, uint8_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    return (uint8_t)(x % y);
}

uint8_t kr_neg_u8(uint8_t a, int line) {
    if (a != 0) { kr_panic("integer overflow", line); }
    return 0;
}

uint8_t kr_shl_u8(uint8_t a, uint8_t b, int line) {
    if ((uint64_t)b >= UINT64_C(8)) { kr_panic("shift count out of range", line); }
    if ((uint64_t)a > ((uint64_t)UINT8_MAX >> (unsigned)b)) { kr_panic("integer overflow", line); }
    return (uint8_t)((uint64_t)a << (unsigned)b);
}

uint8_t kr_shr_u8(uint8_t a, uint8_t b, int line) {
    if ((uint64_t)b >= UINT64_C(8)) { kr_panic("shift count out of range", line); }
    return (uint8_t)((uint64_t)a >> (unsigned)b);
}

uint16_t kr_add_u16(uint16_t a, uint16_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (x > UINT16_MAX - y) { kr_panic("integer overflow", line); }
    return (uint16_t)(x + y);
}

uint16_t kr_sub_u16(uint16_t a, uint16_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (x < y) { kr_panic("integer overflow", line); }
    return (uint16_t)(x - y);
}

uint16_t kr_mul_u16(uint16_t a, uint16_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (y != 0 && x > UINT16_MAX / y) { kr_panic("integer overflow", line); }
    return (uint16_t)(x * y);
}

uint16_t kr_div_u16(uint16_t a, uint16_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    return (uint16_t)(x / y);
}

uint16_t kr_mod_u16(uint16_t a, uint16_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    return (uint16_t)(x % y);
}

uint16_t kr_neg_u16(uint16_t a, int line) {
    if (a != 0) { kr_panic("integer overflow", line); }
    return 0;
}

uint16_t kr_shl_u16(uint16_t a, uint16_t b, int line) {
    if ((uint64_t)b >= UINT64_C(16)) { kr_panic("shift count out of range", line); }
    if ((uint64_t)a > ((uint64_t)UINT16_MAX >> (unsigned)b)) { kr_panic("integer overflow", line); }
    return (uint16_t)((uint64_t)a << (unsigned)b);
}

uint16_t kr_shr_u16(uint16_t a, uint16_t b, int line) {
    if ((uint64_t)b >= UINT64_C(16)) { kr_panic("shift count out of range", line); }
    return (uint16_t)((uint64_t)a >> (unsigned)b);
}

uint32_t kr_add_u32(uint32_t a, uint32_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (x > UINT32_MAX - y) { kr_panic("integer overflow", line); }
    return (uint32_t)(x + y);
}

uint32_t kr_sub_u32(uint32_t a, uint32_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (x < y) { kr_panic("integer overflow", line); }
    return (uint32_t)(x - y);
}

uint32_t kr_mul_u32(uint32_t a, uint32_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (y != 0 && x > UINT32_MAX / y) { kr_panic("integer overflow", line); }
    return (uint32_t)(x * y);
}

uint32_t kr_div_u32(uint32_t a, uint32_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    return (uint32_t)(x / y);
}

uint32_t kr_mod_u32(uint32_t a, uint32_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    return (uint32_t)(x % y);
}

uint32_t kr_neg_u32(uint32_t a, int line) {
    if (a != 0) { kr_panic("integer overflow", line); }
    return 0;
}

uint32_t kr_shl_u32(uint32_t a, uint32_t b, int line) {
    if ((uint64_t)b >= UINT64_C(32)) { kr_panic("shift count out of range", line); }
    if ((uint64_t)a > ((uint64_t)UINT32_MAX >> (unsigned)b)) { kr_panic("integer overflow", line); }
    return (uint32_t)((uint64_t)a << (unsigned)b);
}

uint32_t kr_shr_u32(uint32_t a, uint32_t b, int line) {
    if ((uint64_t)b >= UINT64_C(32)) { kr_panic("shift count out of range", line); }
    return (uint32_t)((uint64_t)a >> (unsigned)b);
}

uint64_t kr_add_u64(uint64_t a, uint64_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (x > UINT64_MAX - y) { kr_panic("integer overflow", line); }
    return (uint64_t)(x + y);
}

uint64_t kr_sub_u64(uint64_t a, uint64_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (x < y) { kr_panic("integer overflow", line); }
    return (uint64_t)(x - y);
}

uint64_t kr_mul_u64(uint64_t a, uint64_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (y != 0 && x > UINT64_MAX / y) { kr_panic("integer overflow", line); }
    return (uint64_t)(x * y);
}

uint64_t kr_div_u64(uint64_t a, uint64_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    return (uint64_t)(x / y);
}

uint64_t kr_mod_u64(uint64_t a, uint64_t b, int line) {
    uint64_t x = (uint64_t)a;
    uint64_t y = (uint64_t)b;
    if (y == 0) { kr_panic("division by zero", line); }
    return (uint64_t)(x % y);
}

uint64_t kr_neg_u64(uint64_t a, int line) {
    if (a != 0) { kr_panic("integer overflow", line); }
    return 0;
}

uint64_t kr_shl_u64(uint64_t a, uint64_t b, int line) {
    if ((uint64_t)b >= UINT64_C(64)) { kr_panic("shift count out of range", line); }
    if ((uint64_t)a > ((uint64_t)UINT64_MAX >> (unsigned)b)) { kr_panic("integer overflow", line); }
    return (uint64_t)((uint64_t)a << (unsigned)b);
}

uint64_t kr_shr_u64(uint64_t a, uint64_t b, int line) {
    if ((uint64_t)b >= UINT64_C(64)) { kr_panic("shift count out of range", line); }
    return (uint64_t)((uint64_t)a >> (unsigned)b);
}

int8_t kr_cast_i8_i8(int8_t value, int line) {
    (void)line;
    return (int8_t)value;
}

int16_t kr_cast_i8_i16(int8_t value, int line) {
    (void)line;
    return (int16_t)value;
}

int32_t kr_cast_i8_i32(int8_t value, int line) {
    (void)line;
    return (int32_t)value;
}

int64_t kr_cast_i8_i64(int8_t value, int line) {
    (void)line;
    return (int64_t)value;
}

uint8_t kr_cast_i8_u8(int8_t value, int line) {
    (void)line;
    if (value < 0) { kr_panic("integer conversion out of range", line); }
    return (uint8_t)value;
}

uint16_t kr_cast_i8_u16(int8_t value, int line) {
    (void)line;
    if (value < 0) { kr_panic("integer conversion out of range", line); }
    return (uint16_t)value;
}

uint32_t kr_cast_i8_u32(int8_t value, int line) {
    (void)line;
    if (value < 0) { kr_panic("integer conversion out of range", line); }
    return (uint32_t)value;
}

uint64_t kr_cast_i8_u64(int8_t value, int line) {
    (void)line;
    if (value < 0) { kr_panic("integer conversion out of range", line); }
    return (uint64_t)value;
}

int8_t kr_cast_i16_i8(int16_t value, int line) {
    (void)line;
    if ((int64_t)value < INT8_MIN || (int64_t)value > INT8_MAX) { kr_panic("integer conversion out of range", line); }
    return (int8_t)value;
}

int16_t kr_cast_i16_i16(int16_t value, int line) {
    (void)line;
    return (int16_t)value;
}

int32_t kr_cast_i16_i32(int16_t value, int line) {
    (void)line;
    return (int32_t)value;
}

int64_t kr_cast_i16_i64(int16_t value, int line) {
    (void)line;
    return (int64_t)value;
}

uint8_t kr_cast_i16_u8(int16_t value, int line) {
    (void)line;
    if (value < 0 || (uint64_t)value > (uint64_t)UINT8_MAX) { kr_panic("integer conversion out of range", line); }
    return (uint8_t)value;
}

uint16_t kr_cast_i16_u16(int16_t value, int line) {
    (void)line;
    if (value < 0) { kr_panic("integer conversion out of range", line); }
    return (uint16_t)value;
}

uint32_t kr_cast_i16_u32(int16_t value, int line) {
    (void)line;
    if (value < 0) { kr_panic("integer conversion out of range", line); }
    return (uint32_t)value;
}

uint64_t kr_cast_i16_u64(int16_t value, int line) {
    (void)line;
    if (value < 0) { kr_panic("integer conversion out of range", line); }
    return (uint64_t)value;
}

int8_t kr_cast_i32_i8(int32_t value, int line) {
    (void)line;
    if ((int64_t)value < INT8_MIN || (int64_t)value > INT8_MAX) { kr_panic("integer conversion out of range", line); }
    return (int8_t)value;
}

int16_t kr_cast_i32_i16(int32_t value, int line) {
    (void)line;
    if ((int64_t)value < INT16_MIN || (int64_t)value > INT16_MAX) { kr_panic("integer conversion out of range", line); }
    return (int16_t)value;
}

int32_t kr_cast_i32_i32(int32_t value, int line) {
    (void)line;
    return (int32_t)value;
}

int64_t kr_cast_i32_i64(int32_t value, int line) {
    (void)line;
    return (int64_t)value;
}

uint8_t kr_cast_i32_u8(int32_t value, int line) {
    (void)line;
    if (value < 0 || (uint64_t)value > (uint64_t)UINT8_MAX) { kr_panic("integer conversion out of range", line); }
    return (uint8_t)value;
}

uint16_t kr_cast_i32_u16(int32_t value, int line) {
    (void)line;
    if (value < 0 || (uint64_t)value > (uint64_t)UINT16_MAX) { kr_panic("integer conversion out of range", line); }
    return (uint16_t)value;
}

uint32_t kr_cast_i32_u32(int32_t value, int line) {
    (void)line;
    if (value < 0) { kr_panic("integer conversion out of range", line); }
    return (uint32_t)value;
}

uint64_t kr_cast_i32_u64(int32_t value, int line) {
    (void)line;
    if (value < 0) { kr_panic("integer conversion out of range", line); }
    return (uint64_t)value;
}

int8_t kr_cast_i64_i8(int64_t value, int line) {
    (void)line;
    if ((int64_t)value < INT8_MIN || (int64_t)value > INT8_MAX) { kr_panic("integer conversion out of range", line); }
    return (int8_t)value;
}

int16_t kr_cast_i64_i16(int64_t value, int line) {
    (void)line;
    if ((int64_t)value < INT16_MIN || (int64_t)value > INT16_MAX) { kr_panic("integer conversion out of range", line); }
    return (int16_t)value;
}

int32_t kr_cast_i64_i32(int64_t value, int line) {
    (void)line;
    if ((int64_t)value < INT32_MIN || (int64_t)value > INT32_MAX) { kr_panic("integer conversion out of range", line); }
    return (int32_t)value;
}

int64_t kr_cast_i64_i64(int64_t value, int line) {
    (void)line;
    return (int64_t)value;
}

uint8_t kr_cast_i64_u8(int64_t value, int line) {
    (void)line;
    if (value < 0 || (uint64_t)value > (uint64_t)UINT8_MAX) { kr_panic("integer conversion out of range", line); }
    return (uint8_t)value;
}

uint16_t kr_cast_i64_u16(int64_t value, int line) {
    (void)line;
    if (value < 0 || (uint64_t)value > (uint64_t)UINT16_MAX) { kr_panic("integer conversion out of range", line); }
    return (uint16_t)value;
}

uint32_t kr_cast_i64_u32(int64_t value, int line) {
    (void)line;
    if (value < 0 || (uint64_t)value > (uint64_t)UINT32_MAX) { kr_panic("integer conversion out of range", line); }
    return (uint32_t)value;
}

uint64_t kr_cast_i64_u64(int64_t value, int line) {
    (void)line;
    if (value < 0) { kr_panic("integer conversion out of range", line); }
    return (uint64_t)value;
}

int8_t kr_cast_u8_i8(uint8_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)INT8_MAX) { kr_panic("integer conversion out of range", line); }
    return (int8_t)value;
}

int16_t kr_cast_u8_i16(uint8_t value, int line) {
    (void)line;
    return (int16_t)value;
}

int32_t kr_cast_u8_i32(uint8_t value, int line) {
    (void)line;
    return (int32_t)value;
}

int64_t kr_cast_u8_i64(uint8_t value, int line) {
    (void)line;
    return (int64_t)value;
}

uint8_t kr_cast_u8_u8(uint8_t value, int line) {
    (void)line;
    return (uint8_t)value;
}

uint16_t kr_cast_u8_u16(uint8_t value, int line) {
    (void)line;
    return (uint16_t)value;
}

uint32_t kr_cast_u8_u32(uint8_t value, int line) {
    (void)line;
    return (uint32_t)value;
}

uint64_t kr_cast_u8_u64(uint8_t value, int line) {
    (void)line;
    return (uint64_t)value;
}

int8_t kr_cast_u16_i8(uint16_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)INT8_MAX) { kr_panic("integer conversion out of range", line); }
    return (int8_t)value;
}

int16_t kr_cast_u16_i16(uint16_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)INT16_MAX) { kr_panic("integer conversion out of range", line); }
    return (int16_t)value;
}

int32_t kr_cast_u16_i32(uint16_t value, int line) {
    (void)line;
    return (int32_t)value;
}

int64_t kr_cast_u16_i64(uint16_t value, int line) {
    (void)line;
    return (int64_t)value;
}

uint8_t kr_cast_u16_u8(uint16_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)UINT8_MAX) { kr_panic("integer conversion out of range", line); }
    return (uint8_t)value;
}

uint16_t kr_cast_u16_u16(uint16_t value, int line) {
    (void)line;
    return (uint16_t)value;
}

uint32_t kr_cast_u16_u32(uint16_t value, int line) {
    (void)line;
    return (uint32_t)value;
}

uint64_t kr_cast_u16_u64(uint16_t value, int line) {
    (void)line;
    return (uint64_t)value;
}

int8_t kr_cast_u32_i8(uint32_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)INT8_MAX) { kr_panic("integer conversion out of range", line); }
    return (int8_t)value;
}

int16_t kr_cast_u32_i16(uint32_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)INT16_MAX) { kr_panic("integer conversion out of range", line); }
    return (int16_t)value;
}

int32_t kr_cast_u32_i32(uint32_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)INT32_MAX) { kr_panic("integer conversion out of range", line); }
    return (int32_t)value;
}

int64_t kr_cast_u32_i64(uint32_t value, int line) {
    (void)line;
    return (int64_t)value;
}

uint8_t kr_cast_u32_u8(uint32_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)UINT8_MAX) { kr_panic("integer conversion out of range", line); }
    return (uint8_t)value;
}

uint16_t kr_cast_u32_u16(uint32_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)UINT16_MAX) { kr_panic("integer conversion out of range", line); }
    return (uint16_t)value;
}

uint32_t kr_cast_u32_u32(uint32_t value, int line) {
    (void)line;
    return (uint32_t)value;
}

uint64_t kr_cast_u32_u64(uint32_t value, int line) {
    (void)line;
    return (uint64_t)value;
}

int8_t kr_cast_u64_i8(uint64_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)INT8_MAX) { kr_panic("integer conversion out of range", line); }
    return (int8_t)value;
}

int16_t kr_cast_u64_i16(uint64_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)INT16_MAX) { kr_panic("integer conversion out of range", line); }
    return (int16_t)value;
}

int32_t kr_cast_u64_i32(uint64_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)INT32_MAX) { kr_panic("integer conversion out of range", line); }
    return (int32_t)value;
}

int64_t kr_cast_u64_i64(uint64_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)INT64_MAX) { kr_panic("integer conversion out of range", line); }
    return (int64_t)value;
}

uint8_t kr_cast_u64_u8(uint64_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)UINT8_MAX) { kr_panic("integer conversion out of range", line); }
    return (uint8_t)value;
}

uint16_t kr_cast_u64_u16(uint64_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)UINT16_MAX) { kr_panic("integer conversion out of range", line); }
    return (uint16_t)value;
}

uint32_t kr_cast_u64_u32(uint64_t value, int line) {
    (void)line;
    if ((uint64_t)value > (uint64_t)UINT32_MAX) { kr_panic("integer conversion out of range", line); }
    return (uint32_t)value;
}

uint64_t kr_cast_u64_u64(uint64_t value, int line) {
    (void)line;
    return (uint64_t)value;
}
