/* The portable package keeps its complete Zig SDK beside this small adapter.
 * Kremen's backend interface expects one executable, while Zig exposes `cc`
 * as a subcommand. Forward argv directly and retain the SDK's own diagnostics. */
#include "../native/kremen.h"
#include "../native/platform.h"

int main(int argc, char **argv) {
    km_host_streams();
    if (!km_host_arguments(&argc, &argv)) {
        fputs("kremen backend: cannot decode command-line arguments\n", stderr);
        return 1;
    }
    char executable[PATH_MAX], candidate[PATH_MAX], backend[PATH_MAX];
    if (!km_platform_executable(argv[0], executable, sizeof executable)) {
        fputs("kremen backend: cannot locate this executable\n", stderr);
        return 127;
    }
    char *slash = strrchr(executable, '/');
    if (!slash) slash = strrchr(executable, '\\');
    if (!slash) {
        fputs("kremen backend: executable path has no parent directory\n", stderr);
        return 127;
    }
    *slash = 0;
    int length = snprintf(candidate, sizeof candidate, "%s/../zig%s", executable, KM_EXE_SUFFIX);
    if (length < 0 || (size_t)length >= sizeof candidate || !km_host_realpath(candidate, backend)) {
        fputs("kremen backend: bundled Zig SDK is missing; extract the complete portable package\n", stderr);
        return 127;
    }
    /* A Linux portable program should not acquire the builder's glibc ABI.
     * The official SDK contains the matching musl headers and static runtime. */
#ifdef _WIN32
    const size_t prefix = 4;
#else
    bool linking = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--")) break;
        if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "-S") || !strcmp(argv[i], "-E") ||
            !strcmp(argv[i], "-fsyntax-only") || !strcmp(argv[i], "-M") || !strcmp(argv[i], "-MM")) linking = false;
    }
    const size_t prefix = linking ? 5 : 4;
#endif
    char **arguments = calloc((size_t)argc + prefix, sizeof *arguments);
    if (!arguments) {
        fputs("kremen backend: cannot allocate argument list\n", stderr);
        return 1;
    }
    arguments[0] = backend;
    arguments[1] = "cc";
    arguments[2] = "-target";
#ifdef _WIN32
    arguments[3] = "x86_64-windows-gnu";
#else
    arguments[3] = "x86_64-linux-musl";
    if (linking) arguments[4] = "-static";
#endif
    for (int i = 1; i < argc; i++) arguments[(size_t)i + prefix - 1] = argv[i];
    int status = km_host_child(arguments);
    free(arguments);
    return status;
}
