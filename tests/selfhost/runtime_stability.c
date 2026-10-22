#define _POSIX_C_SOURCE 200809L
#include "../../selfhost/runtime.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t checked(const char *operation) {
    const char *path = "checked.kr";
    int64_t line = 17, column = 9;
    if (!strcmp(operation, "addhigh")) return rt_iadd(INT64_MAX, 1, path, line, column);
    if (!strcmp(operation, "addlow")) return rt_iadd(INT64_MIN, -1, path, line, column);
    if (!strcmp(operation, "subhigh")) return rt_isub(INT64_MAX, -1, path, line, column);
    if (!strcmp(operation, "sublow")) return rt_isub(INT64_MIN, 1, path, line, column);
    if (!strcmp(operation, "mulhigh")) return rt_imul(INT64_MAX, 2, path, line, column);
    if (!strcmp(operation, "mullow")) return rt_imul(INT64_MIN, 2, path, line, column);
    if (!strcmp(operation, "mulneg")) return rt_imul(INT64_MIN, -1, path, line, column);
    if (!strcmp(operation, "mulnegboth")) return rt_imul(-INT64_C(3037000500), -INT64_C(3037000500), path, line, column);
    if (!strcmp(operation, "divzero")) return rt_idiv(1, 0, path, line, column);
    if (!strcmp(operation, "modzero")) return rt_imod(1, 0, path, line, column);
    if (!strcmp(operation, "divmin")) return rt_idiv(INT64_MIN, -1, path, line, column);
    if (!strcmp(operation, "modmin")) return rt_imod(INT64_MIN, -1, path, line, column);
    if (!strcmp(operation, "negmin")) return rt_ineg(INT64_MIN, path, line, column);
    return 0;
}
static void normal(void) {
    assert(rt_equal(rt_arg(-1), "") && rt_equal(rt_arg(INT64_MAX), ""));
    assert(rt_argc() >= 1);
    assert(rt_len("") == 0 && rt_len("literal") == 7);
    assert(rt_at("literal", -1) == -1 && rt_at("literal", INT64_MAX) == -1);
    assert(rt_at("\377", 0) == 255 && rt_at("", 0) == -1);
    const char *combined = rt_cat("first", "second");
    assert(rt_len(combined) == 11 && rt_equal(combined, "firstsecond"));
    assert(rt_equal(rt_cat("", combined), combined));
    assert(rt_equal(rt_cat(combined, ""), combined));
    assert(rt_equal(rt_slice(combined, 0, 11), combined));
    assert(rt_equal(rt_slice(combined, 3, 3), ""));
    assert(rt_equal(rt_slice(combined, 4, 8), "tsec"));
    assert(rt_equal(rt_number(INT64_MIN), "-9223372036854775808"));
    assert(rt_equal(rt_number(INT64_MAX), "9223372036854775807"));
    const char *large = "q";
    for (int i = 0; i < 20; i++) large = rt_cat(large, large);
    const char *other = rt_cat(large, "r");
    for (int i = 0; i < 20000; i++) {
        assert(rt_at(large, i) == 'q' && rt_at(other, i) == 'q');
        assert(rt_len(large) == 1048576 && rt_len(other) == 1048577);
        const char *small = rt_cat("count=", rt_number(i));
        assert(rt_at(small, 0) == 'c');
    }
    assert(rt_equal(combined, "firstsecond"));
    assert(rt_at(other, 1048576) == 'r');
    assert(rt_iadd(INT64_MAX, 0, "test", 1, 1) == INT64_MAX);
    assert(rt_isub(INT64_MIN, 0, "test", 1, 1) == INT64_MIN);
    assert(rt_imul(INT64_MIN, 0, "test", 1, 1) == 0);
    assert(rt_imul(INT64_MIN, 1, "test", 1, 1) == INT64_MIN);
    assert(rt_imul(INT64_MAX, -1, "test", 1, 1) == -INT64_MAX);
    assert(rt_imul(-INT64_C(3037000499), -INT64_C(3037000499), "test", 1, 1) == INT64_C(9223372030926249001));
    assert(rt_idiv(INT64_MIN, 1, "test", 1, 1) == INT64_MIN);
    assert(rt_imod(INT64_MIN, 1, "test", 1, 1) == 0);
    assert(rt_ineg(-INT64_MAX, "test", 1, 1) == INT64_MAX);
    for (int64_t a = -37; a <= 37; a++) for (int64_t b = -37; b <= 37; b++) {
        assert(rt_iadd(a, b, "test", 1, 1) == a + b);
        assert(rt_isub(a, b, "test", 1, 1) == a - b);
        assert(rt_imul(a, b, "test", 1, 1) == a * b);
        assert(rt_ineg(a, "test", 1, 1) == -a);
        if (b != 0) {
            assert(rt_idiv(a, b, "test", 1, 1) == a / b);
            assert(rt_imod(a, b, "test", 1, 1) == a % b);
        }
    }
    rt_cleanup();
    rt_cleanup();
    assert(rt_len("after cleanup") == 13);
    assert(rt_equal(rt_cat("re", "initialized"), "reinitialized"));
    rt_cleanup();
}
int main(int argc, char **argv) {
    rt_init(argc, argv);
    if (argc > 1) {
        if (!strcmp(argv[1], "slice-negative")) (void)rt_slice("abc", -1, 2);
        else if (!strcmp(argv[1], "slice-bounds")) (void)rt_slice("abc", 0, 4);
        else if (!strcmp(argv[1], "overwrite")) { (void)rt_read(argv[2]); (void)rt_write(argv[3], "destroyed"); }
        else if (!strcmp(argv[1], "read")) { const char *value = rt_read(argv[2]); assert(rt_len(value) == (int64_t)strlen(value)); }
        else if (!strcmp(argv[1], "arena-limit")) {
            const char *large = "x";
            for (int i = 0; i < 30; i++) large = rt_cat(large, large);
        } else (void)checked(argv[1]);
    } else normal();
    rt_cleanup();
    return 0;
}
