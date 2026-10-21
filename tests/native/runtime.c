/* Runtime invariants that are intentionally not expressible as safe source. */
#include "../../runtime/kremen_rt.h"
#include <assert.h>

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "normal";
    kr_slice zero = {0};
    if (strcmp(mode, "input") == 0) {
        const int64_t values[] = {0, INT64_MIN, INT64_MAX, 0, 0, 0, 2, 0, 0};
        const int64_t codes[] = {0, 0, 0, 2, 3, 2, 0, 1, 1};
        for (size_t i = 0; i < sizeof codes / sizeof *codes; ++i) {
            int64_t value = 987;
            int64_t code = 987;
            bool ok = kr_try_read_int(&value, &code);
            assert(ok == (codes[i] == 0));
            assert(code == codes[i] && value == values[i]);
        }
        return 0;
    }
    if (strcmp(mode, "ioerror") == 0) {
        int64_t value = 987;
        int64_t code = 987;
        assert(freopen("/dev/null", "w", stdin) != NULL);
        assert(!kr_try_read_int(&value, &code));
        assert(value == 0 && code == 4);
        return 0;
    }
    if (strcmp(mode, "overflow") == 0) {
        (void)kr_slice_alloc(INT64_MAX, 8, 9);
        return 99;
    }
    if (strcmp(mode, "zeroindex") == 0) {
        (void)kr_slice_at(zero, 0, 8, 9);
        return 99;
    }
    kr_slice owner = kr_slice_alloc(4, sizeof(int64_t), 3);
    kr_slice alias = owner;
    kr_slice view = kr_subslice(owner, 1, 3, 4);
    assert(kr_slice_len(owner, 5) == 4 && kr_slice_len(view, 5) == 2);
    for (int64_t i = 0; i < 4; ++i) {
        assert(*(int64_t *)kr_slice_at(owner, i, sizeof(int64_t), 6) == 0);
    }
    *(int64_t *)kr_slice_at(view, 0, sizeof(int64_t), 7) = 42;
    assert(*(int64_t *)kr_slice_at(alias, 1, sizeof(int64_t), 7) == 42);
    if (strcmp(mode, "mismatch") == 0) {
        (void)kr_slice_at(view, 0, sizeof(int32_t), 9);
        return 99;
    }
    if (strcmp(mode, "reverse") == 0) {
        (void)kr_subslice(view, 1, 0, 9);
        return 99;
    }
    kr_slice_free(owner, 8);
    if (strcmp(mode, "uafview") == 0) {
        (void)kr_subslice(alias, 0, 0, 9);
        return 99;
    }
    kr_slice_free(zero, 9);
    assert(kr_slice_len(zero, 9) == 0);
    zero = kr_subslice(zero, 0, 0, 9);
    assert(zero.owner == NULL && zero.offset == 0 && zero.len == 0);
    kr_slice empty = kr_slice_alloc(0, 8, 9);
    assert(kr_slice_len(empty, 9) == 0);
    kr_slice_free(empty, 9);
    return 0;
}
