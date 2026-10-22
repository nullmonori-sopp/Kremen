#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct Allocation {
    struct Allocation *next;
    size_t used;
    size_t capacity;
    char data[];
} Allocation;

static Allocation *allocations;
static Allocation *small_allocation;
static size_t allocated;
static int argument_count;
static char **argument_values;
typedef struct SourceIdentity {
    struct SourceIdentity *next;
    dev_t device;
    ino_t inode;
} SourceIdentity;
static SourceIdentity *source_identities;
/* The current input stays in the string arena until cleanup. Do not reopen a
 * file for diagnostics: it may have changed since the compiler read it. */
static const char *diagnostic_path, *diagnostic_source;
static size_t diagnostic_length;
/* Strings are immutable, but may also be C literals or process arguments.
 * Never inspect memory before an arbitrary char pointer to find a length.
 * A bounded two-way cache keeps source/table lengths across temporary strings. */
enum { LENGTH_CACHE_SETS = 128 };
typedef struct { const char *value; size_t length; } StringLength;
static StringLength string_lengths[LENGTH_CACHE_SETS][2];
static unsigned char string_length_next[LENGTH_CACHE_SETS];
static const char *last_string;
static size_t last_string_length;

void rt_init(int argc, char **argv) {
    argument_count = argc;
    argument_values = argv;
}

void rt_cleanup(void) {
    while (source_identities != NULL) {
        SourceIdentity *next = source_identities->next;
        free(source_identities);
        source_identities = next;
    }
    while (allocations != NULL) {
        Allocation *next = allocations->next;
        free(allocations);
        allocations = next;
    }
    small_allocation = NULL;
    allocated = 0;
    memset(string_lengths, 0, sizeof string_lengths);
    memset(string_length_next, 0, sizeof string_length_next);
    last_string = NULL;
    last_string_length = 0;
    diagnostic_path = NULL; diagnostic_source = NULL; diagnostic_length = 0;
}

int64_t rt_argc(void) { return argument_count; }

int64_t rt_error(const char *path, int64_t line, int64_t column, const char *message) {
    fprintf(stderr, "%s:%" PRId64 ":%" PRId64 ": error: %s\n", path, line, column, message);
    if (diagnostic_source != NULL && strcmp(path, diagnostic_path) == 0 && line > 0 && column > 0) {
        size_t start = 0;
        int64_t current = 1;
        while (start < diagnostic_length && current < line) {
            if (diagnostic_source[start++] == '\n') current++;
        }
        if (current == line) {
            size_t end = start;
            while (end < diagnostic_length && diagnostic_source[end] != '\n' && diagnostic_source[end] != '\r') end++;
            size_t available = end - start;
            size_t offset = (uint64_t)(column - 1) > available ? end : start + (size_t)(column - 1);
            bool clipped = offset - start > 64;
            if (clipped) start = offset - 64;
            while (start < offset && ((unsigned char)diagnostic_source[start] & 0xc0) == 0x80) start++;
            if (end - start > 160) end = start + 160;
            while (end > offset && end < diagnostic_length && ((unsigned char)diagnostic_source[end] & 0xc0) == 0x80) end--;
            int digits = 1;
            for (int64_t n = line; n >= 10; n /= 10) digits++;
            fprintf(stderr, " %*s |\n %" PRId64 " | ", digits, "", line);
            unsigned cells = 0, caret = 0;
            if (clipped) { fputs("...", stderr); cells = 3; }
            for (size_t i = start; i < end; i++) {
                unsigned char byte = (unsigned char)diagnostic_source[i];
                if (i == offset) caret = cells;
                if (byte == '\t') {
                    unsigned width = 4 - (cells - (clipped ? 3 : 0)) % 4;
                    for (unsigned j = 0; j < width; j++) fputc(' ', stderr);
                    cells += width;
                } else {
                    fputc(byte < 32 || byte == 127 ? '?' : byte, stderr);
                    if ((byte & 0xc0) != 0x80) cells++;
                }
            }
            if (offset == end) caret = cells;
            if (end < diagnostic_length && diagnostic_source[end] != '\n' && diagnostic_source[end] != '\r') fputs("...", stderr);
            fprintf(stderr, "\n %*s | ", digits, "");
            for (unsigned i = 0; i < caret; i++) fputc(' ', stderr);
            fputs("^\n", stderr);
        }
    }
    rt_cleanup();
    exit(1);
}

int64_t rt_iadd(int64_t left, int64_t right, const char *path, int64_t line, int64_t column) {
    if ((right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right))
        return rt_error(path, line, column, "integer overflow");
    return left + right;
}

int64_t rt_isub(int64_t left, int64_t right, const char *path, int64_t line, int64_t column) {
    if ((right > 0 && left < INT64_MIN + right) ||
        (right < 0 && left > INT64_MAX + right))
        return rt_error(path, line, column, "integer overflow");
    return left - right;
}

int64_t rt_imul(int64_t left, int64_t right, const char *path, int64_t line, int64_t column) {
    bool overflow = left > 0 ?
        (right > 0 ? left > INT64_MAX / right : right < INT64_MIN / left) :
        (left < 0 && (right > 0 ? left < INT64_MIN / right : right < 0 && left < INT64_MAX / right));
    if (overflow) return rt_error(path, line, column, "integer overflow");
    return left * right;
}

int64_t rt_idiv(int64_t left, int64_t right, const char *path, int64_t line, int64_t column) {
    if (right == 0) return rt_error(path, line, column, "division by zero");
    if (left == INT64_MIN && right == -1) return rt_error(path, line, column, "integer overflow");
    return left / right;
}

int64_t rt_imod(int64_t left, int64_t right, const char *path, int64_t line, int64_t column) {
    if (right == 0) return rt_error(path, line, column, "remainder by zero");
    if (left == INT64_MIN && right == -1) return rt_error(path, line, column, "integer overflow");
    return left % right;
}

int64_t rt_ineg(int64_t value, const char *path, int64_t line, int64_t column) {
    if (value == INT64_MIN) return rt_error(path, line, column, "integer overflow");
    return -value;
}

static size_t length_set(const char *value) {
    uintptr_t bits = (uintptr_t)value;
    bits ^= bits >> 11;
    bits ^= bits >> 5;
    return (size_t)bits & (LENGTH_CACHE_SETS - 1);
}

static void remember_length(const char *value, size_t length) {
    size_t set = length_set(value);
    unsigned slot = string_length_next[set];
    if (string_lengths[set][0].value == value) slot = 0;
    else if (string_lengths[set][1].value == value) slot = 1;
    string_lengths[set][slot].value = value;
    string_lengths[set][slot].length = length;
    string_length_next[set] = (unsigned char)(slot ^ 1);
    last_string = value;
    last_string_length = length;
}

static size_t string_length(const char *value) {
    if (value == last_string) return last_string_length;
    size_t set = length_set(value);
    for (unsigned slot = 0; slot < 2; slot++) {
        if (string_lengths[set][slot].value == value) {
            last_string = value;
            last_string_length = string_lengths[set][slot].length;
            return last_string_length;
        }
    }
    size_t length = strlen(value);
    if ((uintmax_t)length > INT64_MAX) {
        (void)rt_error("<bootstrap>", 1, 1, "string length overflow");
    }
    remember_length(value, length);
    return length;
}

static char *allocate(size_t length) {
    const size_t limit = (size_t)512 * 1024 * 1024;
    const size_t chunk_size = (size_t)64 * 1024;
    const size_t small_string_limit = (size_t)4 * 1024;
    if (length >= limit) {
        (void)rt_error("<bootstrap>", 1, 1, "512 MiB string arena limit exceeded");
    }
    size_t needed = length + 1;
    Allocation *item = needed <= small_string_limit ? small_allocation : NULL;
    if (item == NULL || needed > item->capacity - item->used) {
        size_t available = limit - allocated;
        if (available <= sizeof(Allocation) || needed > available - sizeof(Allocation)) {
            (void)rt_error("<bootstrap>", 1, 1, "512 MiB string arena limit exceeded");
        }
        size_t capacity = needed <= small_string_limit ? chunk_size : needed;
        if (capacity > available - sizeof(Allocation)) capacity = available - sizeof(Allocation);
        item = malloc(sizeof *item + capacity);
        if (item == NULL) {
            (void)rt_error("<bootstrap>", 1, 1, "out of memory");
        }
        item->next = allocations;
        item->used = 0;
        item->capacity = capacity;
        allocations = item;
        allocated += sizeof *item + capacity;
        if (needed <= small_string_limit) small_allocation = item;
    }
    char *result = item->data + item->used;
    item->used += needed;
    result[length] = '\0';
    return result;
}

int64_t rt_len(const char *value) {
    return (int64_t)string_length(value);
}

int64_t rt_at(const char *value, int64_t index) {
    if (index < 0 || index >= rt_len(value)) {
        return -1;
    }
    return (unsigned char)value[index];
}

const char *rt_slice(const char *value, int64_t start, int64_t end) {
    if (start < 0 || end < start || end > rt_len(value)) {
        (void)rt_error("<bootstrap>", 1, 1, "string slice outside bounds");
    }
    size_t length = (size_t)(end - start);
    if (length == 0) return "";
    if (start == 0 && (size_t)end == string_length(value)) return value;
    char *result = allocate(length);
    memcpy(result, value + start, length);
    remember_length(result, length);
    return result;
}

const char *rt_cat(const char *left, const char *right) {
    size_t a = string_length(left);
    size_t b = string_length(right);
    if (a == 0) return right;
    if (b == 0) return left;
    if (a > SIZE_MAX - b) {
        (void)rt_error("<bootstrap>", 1, 1, "string length overflow");
    }
    char *result = allocate(a + b);
    memcpy(result, left, a);
    memcpy(result + a, right, b);
    remember_length(result, a + b);
    return result;
}

bool rt_equal(const char *left, const char *right) {
    return left == right || strcmp(left, right) == 0;
}

const char *rt_number(int64_t value) {
    char buffer[32];
    int length = snprintf(buffer, sizeof(buffer), "%" PRId64, value);
    if (length < 0 || (size_t)length >= sizeof(buffer)) {
        (void)rt_error("<bootstrap>", 1, 1, "cannot format integer");
    }
    char *result = allocate((size_t)length);
    memcpy(result, buffer, (size_t)length);
    remember_length(result, (size_t)length);
    return result;
}

const char *rt_arg(int64_t index) {
    if (index < 0 || index >= argument_count) {
        return "";
    }
    return argument_values[index];
}

const char *rt_read(const char *path) {
    diagnostic_path = NULL; diagnostic_source = NULL; diagnostic_length = 0;
    /* Opening a FIFO for reading can block before fstat gets to reject it. */
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        (void)rt_error(path, 1, 1, strerror(errno));
    }
    struct stat state;
    if (fstat(fd, &state) != 0 || !S_ISREG(state.st_mode)) {
        (void)close(fd);
        (void)rt_error(path, 1, 1, "input must be a regular file of at most 8 MiB");
    }
    FILE *file = fdopen(fd, "rb");
    if (file == NULL) { (void)close(fd); (void)rt_error(path, 1, 1, "cannot open input stream"); }
    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        (void)rt_error(path, 1, 1, "cannot seek input file");
    }
    long size = ftell(file);
    if (size < 0 || size > 8 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        (void)rt_error(path, 1, 1, "input must be a regular file of at most 8 MiB");
    }
    char *result = allocate((size_t)size);
    if (fread(result, 1, (size_t)size, file) != (size_t)size) {
        (void)fclose(file);
        (void)rt_error(path, 1, 1, "cannot read input file");
    }
    if (fclose(file) != 0) {
        (void)rt_error(path, 1, 1, "cannot close input file");
    }
    if (memchr(result, '\0', (size_t)size) != NULL) {
        (void)rt_error(path, 1, 1, "NUL bytes are not allowed in source files");
    }
    remember_length(result, (size_t)size);
    diagnostic_path = path; diagnostic_source = result; diagnostic_length = (size_t)size;
    for (SourceIdentity *input = source_identities; input != NULL; input = input->next) {
        if (input->device == state.st_dev && input->inode == state.st_ino) return result;
    }
    SourceIdentity *identity = malloc(sizeof *identity);
    if (identity == NULL) (void)rt_error(path, 1, 1, "out of memory");
    identity->device = state.st_dev; identity->inode = state.st_ino;
    identity->next = source_identities; source_identities = identity;
    return result;
}

int64_t rt_write(const char *path, const char *value) {
    struct stat state;
    bool exists = stat(path, &state) == 0;
    if (exists) {
        for (SourceIdentity *input = source_identities; input != NULL; input = input->next) {
            if (input->device == state.st_dev && input->inode == state.st_ino)
                return rt_error(path, 1, 1, "output would overwrite a source file");
        }
    }
    size_t n = strlen(path);
    if (n > SIZE_MAX - 32) return rt_error(path, 1, 1, "output path too long");
    char *temporary = allocate(n + 32);
    snprintf(temporary, n + 32, "%s.kremen-XXXXXX", path);
    int fd = mkstemp(temporary);
    if (fd < 0) return rt_error(path, 1, 1, strerror(errno));
    if (exists && fchmod(fd, state.st_mode & 0777) != 0) {
        close(fd); unlink(temporary); return rt_error(path, 1, 1, "cannot preserve output permissions");
    }
    FILE *file = fdopen(fd, "wb");
    if (file == NULL) { close(fd); unlink(temporary); return rt_error(path, 1, 1, "cannot open temporary output"); }
    size_t length = string_length(value);
    bool failed = fwrite(value, 1, length, file) != length;
    if (fclose(file) != 0) {
        failed = true;
    }
    if (failed) {
        unlink(temporary);
        return rt_error(path, 1, 1, "cannot write output file");
    }
    if (rename(temporary, path) != 0) {
        int code = errno; unlink(temporary); return rt_error(path, 1, 1, strerror(code));
    }
    return 0;
}
