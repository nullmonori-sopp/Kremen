#include "../../selfhost/runtime.h"
#include <stdio.h>
#include <string.h>

/* Linked to the real subset C entry shim. Exercise stream faults, rather than
 * duplicating the shim's test for them. The sticky case is host-neutral. */
int64_t kr_main(void) {
    const char *mode = rt_arg(1);
    if (!strcmp(mode, "sticky") || !strcmp(mode, "sticky-status")) {
        FILE *file = fopen("readonly.txt", "wb");
        if (file == NULL) return 99;
        if (fputs("retained", file) == EOF || fclose(file)) return 99;
        if (freopen("readonly.txt", "rb", stdout) == NULL) return 99;
        if (fputs("blocked", stdout) != EOF || !ferror(stdout)) return 99;
        return !strcmp(mode, "sticky-status") ? 7 : 0;
    }
    if (!strcmp(mode, "large")) {
        for (unsigned i = 0; i < 10000; i++) (void)fputc('x', stdout);
        return 0;
    }
    if (fputs("buffered output\n", stdout) == EOF) return 99;
    return 0;
}
