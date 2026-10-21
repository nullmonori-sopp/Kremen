#include "runtime.h"

#include <limits.h>

extern int64_t kr_main(void);

int main(int argc, char **argv) {
    rt_init(argc, argv);
    int64_t result = kr_main();
    rt_cleanup();
    if (result < 0 || result > UCHAR_MAX) {
        return 1;
    }
    return (int)result;
}
