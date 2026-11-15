/* Fake SDK for transport/package tests; never used to compile Kremen code. */
#include "../../native/kremen.h"
#include "../../native/platform.h"

int main(int argc, char **argv) {
    if (!km_host_arguments(&argc, &argv)) return 2;
    if (argc == 2 && !strcmp(argv[1], "version")) {
        const char *version = getenv("KREMEN_FAKE_SDK_VERSION");
        puts(version ? version : "0.15.2");
        return 0;
    }
    static const char *const prefix[] = {"cc", "-target", "x86_64-linux-musl"};
    if (argc < 4) return 3;
    for (size_t i = 0; i < sizeof prefix / sizeof *prefix; i++)
        if (strcmp(argv[i + 1], prefix[i])) return 4;
    if (argc == 5 && (!strcmp(argv[4], "-c") || !strcmp(argv[4], "-fsyntax-only"))) { puts("compile-only ok"); return 0; }
    if (argc < 5 || strcmp(argv[4], "-static")) return 4;
    if (argc == 6 && !strcmp(argv[5], "exit17")) return 17;
    static const char *const expected[] = {"space value", "", "quote\"and\\", "данные/κείμενο", "--literal=;&|$()"};
    if (argc != 10) return 3;
    for (size_t i = 0; i < sizeof expected / sizeof *expected; i++)
        if (strcmp(argv[i + 5], expected[i])) return 4;
    puts("argv ok");
    return 0;
}
