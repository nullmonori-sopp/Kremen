#include "runtime.h"

#include <limits.h>
#include <stdio.h>

extern int64_t kr_main(void);

int main(int argc, char **argv) {
    rt_init(argc, argv);
    int64_t result = kr_main();
    rt_cleanup();
    /* A successful stdio write may only have reached its buffer. Check both
     * the final flush and an earlier sticky failure before reporting success.
     * Retain a program's deliberate nonzero status when output also failed. */
    if (fflush(stdout) == EOF || ferror(stdout)) {
        (void)fputs("kremen selfhost: standard output failed\n", stderr);
        if (result == 0) result = 1;
    }
    if (result < 0 || result > UCHAR_MAX) {
        return 1;
    }
    return (int)result;
}
