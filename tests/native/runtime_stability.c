#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* Record requested sizes, rather than depending on allocator-specific APIs.
 * calloc is deliberately unobserved: the regression concerns stream strings. */
typedef struct { void *pointer; size_t size; } ObservedAllocation;
static ObservedAllocation observed[512];
static size_t realloc_calls;
static size_t observed_slot(const void *pointer) {
    for (size_t i = 0; i < sizeof observed / sizeof *observed; i++)
        if (observed[i].pointer == pointer) return i;
    return sizeof observed / sizeof *observed;
}
static void *observed_malloc(size_t bytes) {
    void *pointer = malloc(bytes);
    if (pointer != NULL) {
        size_t slot = observed_slot(NULL);
        assert(slot < sizeof observed / sizeof *observed);
        observed[slot].pointer = pointer; observed[slot].size = bytes;
    }
    return pointer;
}
static void *observed_realloc(void *pointer, size_t bytes) {
    size_t slot = observed_slot(pointer);
    assert(slot < sizeof observed / sizeof *observed);
    void *replacement = realloc(pointer, bytes);
    realloc_calls++;
    if (replacement != NULL) { observed[slot].pointer = replacement; observed[slot].size = bytes; }
    return replacement;
}
static void observed_free(void *pointer) {
    if (pointer != NULL) {
        size_t slot = observed_slot(pointer);
        if (slot < sizeof observed / sizeof *observed) {
            observed[slot].pointer = NULL; observed[slot].size = 0;
        }
    }
    free(pointer);
}
#define malloc observed_malloc
#define realloc observed_realloc
#define free observed_free
#include "../../runtime/kremen_rt.h"
#undef malloc
#undef realloc
#undef free

static void stream_case(size_t length, bool failure) {
    FILE *stream = tmpfile();
    assert(stream != NULL);
    assert(ftruncate(fileno(stream), (off_t)length) == 0);
    assert(fseek(stream, 0, SEEK_SET) == 0);
    kr_string value = {"unchanged", 9};
    int64_t code = -99;
    size_t before = realloc_calls;
    bool ok = kr_fs_read_stream(stream, &value, &code);
    assert(ok != failure);
    if (failure) {
        assert(code == EFBIG && value.len == 9 && !strcmp(value.data, "unchanged"));
    } else {
        assert(code == 0 && value.len == length && value.data[length] == 0);
        size_t slot = observed_slot(value.data);
        assert(slot < sizeof observed / sizeof *observed);
        assert(observed[slot].size == length + 1);
        for (size_t i = 0; i < length; i++) assert(value.data[i] == 0);
        if (length == 4096) assert(realloc_calls == before);
    }
    assert(fclose(stream) == 0);
}
static void defer_record(void *data) { printf("defer %d\n", *(int *)data); }
static void line_case(size_t length, bool crlf) {
    FILE *stream = tmpfile(); assert(stream != NULL);
    for (size_t i = 0; i < length; i++) assert(fputc('x', stream) != EOF);
    if (crlf) assert(fputc('\r', stream) != EOF);
    assert(fputc('\n', stream) != EOF && fflush(stream) == 0);
    assert(fseek(stream, 0, SEEK_SET) == 0);
    int saved_input = dup(STDIN_FILENO); assert(saved_input >= 0);
    assert(dup2(fileno(stream), STDIN_FILENO) == STDIN_FILENO);
    clearerr(stdin);
    kr_string value = kr_read_line(1);
    assert(value.len == length && value.data[length] == 0);
    size_t slot = observed_slot(value.data);
    assert(slot < sizeof observed / sizeof *observed && observed[slot].size == length + 1);
    for (size_t i = 0; i < length; i++) assert(value.data[i] == 'x');
    assert(dup2(saved_input, STDIN_FILENO) == STDIN_FILENO);
    assert(close(saved_input) == 0 && fclose(stream) == 0);
    clearerr(stdin);
}
static void arena_case(void) {
    kr_arena *arena = kr_arena_new(1);
    for (size_t round = 0; round < 4; round++) {
        kr_slice bytes = kr_arena_alloc(arena, 65537, 1, 1);
        kr_slice values = kr_arena_alloc(arena, 31, sizeof(max_align_t), 1);
        assert((uintptr_t)kr_slice_at(values, 0, sizeof(max_align_t), 1) % _Alignof(max_align_t) == 0);
        for (int64_t i = 0; i < 65537; i++) assert(*(unsigned char *)kr_slice_at(bytes, i, 1, 1) == 0);
        *(unsigned char *)kr_slice_at(bytes, 0, 1, 1) = 0xff;
        kr_arena_reset(arena, 1);
    }
    kr_arena_free(arena, 1);
}
static void arithmetic_case(void) {
    for (int a = INT8_MIN; a <= INT8_MAX; a++) {
        for (int b = INT8_MIN; b <= INT8_MAX; b++) {
            int result = a + b;
            if (result >= INT8_MIN && result <= INT8_MAX) assert(kr_add_i8((int8_t)a, (int8_t)b, 1) == result);
            result = a - b;
            if (result >= INT8_MIN && result <= INT8_MAX) assert(kr_sub_i8((int8_t)a, (int8_t)b, 1) == result);
            result = a * b;
            if (result >= INT8_MIN && result <= INT8_MAX) assert(kr_mul_i8((int8_t)a, (int8_t)b, 1) == result);
            if (b != 0 && !(a == INT8_MIN && b == -1)) {
                assert(kr_div_i8((int8_t)a, (int8_t)b, 1) == a / b);
                assert(kr_mod_i8((int8_t)a, (int8_t)b, 1) == a % b);
            }
        }
    }
    assert(kr_add_i64(INT64_MIN, INT64_MAX, 1) == -1);
    assert(kr_sub_i64(INT64_MIN, INT64_MIN, 1) == 0);
    assert(kr_mul_i64(INT64_MIN, 1, 1) == INT64_MIN);
    assert(kr_mul_i64(-INT64_MAX, -1, 1) == INT64_MAX);
    assert(kr_add_u64(UINT64_MAX - 1, 1, 1) == UINT64_MAX);
    assert(kr_mul_u64(UINT64_MAX, 1, 1) == UINT64_MAX);
    assert(kr_shl_u64(1, 63, 1) == UINT64_C(9223372036854775808));
    assert(kr_shr_u64(UINT64_MAX, 63, 1) == 1);
    assert(kr_cast_u64_i64((uint64_t)INT64_MAX, 1) == INT64_MAX);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const char *mode = argv[1];
    if (!strcmp(mode, "arithmetic")) { arithmetic_case(); return 0; }
    if (!strcmp(mode, "arena")) { arena_case(); return 0; }
    if (!strcmp(mode, "line-empty")) { line_case(0, false); return 0; }
    if (!strcmp(mode, "line-crlf")) { line_case(7, true); return 0; }
    if (!strcmp(mode, "line-4096")) { line_case(4096, false); return 0; }
    if (!strncmp(mode, "stream-", 7) && strcmp(mode, "stream-error")) {
        char *end; unsigned long length = strtoul(mode + 7, &end, 10);
        assert(*end == 0 && length <= 67108865);
        stream_case((size_t)length, length > 67108864);
        return 0;
    }
    if (!strcmp(mode, "stream-error")) {
        FILE *stream = fopen("/dev/null", "wb"); assert(stream != NULL);
        kr_string value = {"unchanged", 9}; int64_t code = 0;
        assert(!kr_fs_read_stream(stream, &value, &code));
        assert(code > 0 && value.len == 9 && !strcmp(value.data, "unchanged"));
        assert(fclose(stream) == 0); return 0;
    }
    int first = 1, second = 2;
    kr_defer_node a, b;
    kr_defer_push(&a, defer_record, &first);
    kr_defer_push(&b, defer_record, &second);
    if (!strcmp(mode, "overflow-mul")) (void)kr_mul_i64(INT64_MIN, -1, 7);
    if (!strcmp(mode, "overflow-add")) (void)kr_add_i64(INT64_MAX, 1, 7);
    if (!strcmp(mode, "overflow-sub")) (void)kr_sub_i64(INT64_MIN, 1, 7);
    if (!strcmp(mode, "overflow-shift")) (void)kr_shl_u64(UINT64_MAX, 1, 7);
    if (!strcmp(mode, "zero-divide")) (void)kr_div_i64(1, 0, 7);
    if (!strcmp(mode, "invalid-cast")) (void)kr_cast_u64_i64(UINT64_MAX, 7);
    if (!strcmp(mode, "arena-after-reset")) {
        kr_arena *arena = kr_arena_new(7);
        kr_slice value = kr_arena_alloc(arena, 1, sizeof(int64_t), 7);
        kr_arena_reset(arena, 7);
        (void)kr_slice_at(value, 0, sizeof(int64_t), 7);
    }
    abort();
}
