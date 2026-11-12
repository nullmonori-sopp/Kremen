#include "kremen.h"
#include "platform.h"

/* Exercise the selected C backend and the same embedded runtime that a normal
 * build uses. No shell, user source, network, or persistent output is needed. */
int km_doctor(Context *ctx) {
    Source *runtime = km_read_source(ctx, ctx->runtime_path, false);
    char pattern[PATH_MAX], directory[PATH_MAX];
    if (!km_host_temp_pattern(pattern) || !km_host_mkdtemp(pattern))
        km_error(ctx, (Token){0}, "E016", "cannot create toolchain probe directory: %s", strerror(errno));
    if (!km_host_realpath(pattern, directory)) {
        int failure = errno; km_host_rmdir(pattern);
        km_error(ctx, (Token){0}, "E016", "cannot resolve toolchain probe directory: %s", strerror(failure));
    }
    char *source = km_sprintf(ctx, "%s/probe.c", directory);
    char *binary = km_sprintf(ctx, "%s/probe%s", directory, KM_EXE_SUFFIX);
    static const char probe[] =
        "\nstatic int64_t km_probe_worker(int64_t value) { return value + 1; }\n"
        "int main(void) {\n"
        "  kr_runtime_init(); kr_thread *thread = NULL; int64_t value = 0, code = 0;\n"
        "  if (!kr_thread_spawn(km_probe_worker, 41, &thread, &code)) return 1;\n"
        "  if (!kr_thread_join(thread, &value, &code)) return 2;\n"
        "  kr_thread_close(thread, 0); return value == 42 ? 0 : 3;\n"
        "}\n";
    int error = 0, compiled = -1, ran = -1;
    FILE *file = km_host_fopen(source, "wb");
    if (!file) error = errno;
    else {
        if (fwrite(runtime->data, 1, runtime->len, file) != runtime->len ||
            fwrite(probe, 1, sizeof probe - 1, file) != sizeof probe - 1) error = errno ? errno : EIO;
        if (fclose(file) && !error) error = errno ? errno : EIO;
    }
    if (!error) {
        compiled = km_host_compile(ctx->cc, "-O1", source, binary, true);
        if (!compiled) { char *run[] = {binary, NULL}; ran = km_host_child_quiet(run); }
    }
    km_host_unlink(binary); km_host_unlink(source); km_host_rmdir(directory);
    ctx->diagnostic_hint = "Use the matching portable backend or a working native C11 compiler. Windows uses system thread APIs; Linux uses its system pthread implementation. This is a compile-and-run host check, not a cross-compilation command.";
    if (error) km_error(ctx, (Token){0}, "E016", "cannot write toolchain probe: %s", strerror(error));
    if (compiled) km_error(ctx, (Token){0}, "E016", "C backend '%s' failed the strict C11/runtime probe (status %d)", ctx->cc, compiled);
    if (ran) km_error(ctx, (Token){0}, "E016", "compiled runtime probe failed to run (status %d); check native executable dependencies", ran);
    ctx->diagnostic_hint = NULL;
    puts("Kremen doctor");
#ifdef _WIN32
    puts("Host: Windows (native UTF-8/Win32 profile)");
#else
    puts("Host: POSIX");
#endif
    fputs("C compiler: ", stdout); km_diagnostic_text(stdout, ctx->cc); fputc('\n', stdout);
    fputs("Runtime: ", stdout); km_diagnostic_text(stdout, ctx->runtime_path); fputc('\n', stdout);
    puts("Strict C11 + native thread runtime: OK\nWorker spawn/join: OK");
    return 0;
}
