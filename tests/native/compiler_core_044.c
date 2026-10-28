#include "../../native/kremen.h"
#include <assert.h>

int main(void) {
    Context ctx;
    km_init(&ctx);
    assert(strcmp(km_sprintf(&ctx, "%s:%04d/%zu/%%", "item", 7, (size_t)19), "item:0007/19/%") == 0);
    assert(strcmp(km_sprintf(&ctx, "%.3s/%*d", "abcdef", 4, -2), "abc/  -2") == 0);
    assert(strcmp(km_sprintf(&ctx, "%s", ""), "") == 0);
    char data[8193];
    for (size_t i = 0; i < sizeof data - 1; i++) data[i] = (char)('a' + i % 26);
    data[sizeof data - 1] = 0;
    const size_t lengths[] = {1, 510, 511, 512, 513, 4096, 8192};
    for (size_t i = 0; i < sizeof lengths / sizeof *lengths; i++) {
        size_t n = lengths[i];
        char saved = data[n]; data[n] = 0;
        char *plain = km_sprintf(&ctx, "%s", data);
        assert(strlen(plain) == n && memcmp(plain, data, n + 1) == 0);
        char *wrapped = km_sprintf(&ctx, "[%s]:%" PRIu64 ":%d", data, UINT64_MAX, -37);
        assert(wrapped[0] == '[' && memcmp(wrapped + 1, data, n) == 0);
        assert(strcmp(wrapped + n + 1, "]:18446744073709551615:-37") == 0);
        data[n] = saved;
    }
    char *embedded = km_sprintf(&ctx, "a%cb", 0);
    assert(memcmp(embedded, "a\0b\0", 4) == 0);
    assert(strcmp(km_sprintf(&ctx, "%s/%d", "\xce\xbb", 42), "\xce\xbb/42") == 0);
    km_destroy(&ctx);
    return 0;
}
