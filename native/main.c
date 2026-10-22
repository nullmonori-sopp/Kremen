#include "kremen.h"
#include "../selfhost/platform.h"
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef KM_RUNTIME_PATH
#define KM_RUNTIME_PATH "runtime/kremen_rt.h"
#endif
static char current_executable[PATH_MAX];
#ifndef KM_SELFHOST_PATH
#define KM_SELFHOST_PATH "selfhost/build/kremen-selfhost"
#endif

static void usage(void) {
    puts("Kremen 0.4.2 — native C11 toolchain\n"
         "usage: kremen check|build|run|emit-c source.kr [options]\n"
         "       kremen fmt [--check|-w] [file.kr file.krh ...]\n"
         "       kremen selfhost check|build|run|emit-c|bootstrap [options]\n"
         "options: -o FILE, -O0..-O3 (default -O3), --root DIR, --cc COMPILER\n"
         "         -- ends compiler options (for source paths starting with -)\n"
         "C backend: -std=c11 -Wall -Wextra -Wpedantic -Werror\n"
         "source .kr; API declarations .krh; assembly .S; native objects .o/.a");
}
static bool extension(const char *path, const char *ext) { size_t n = strlen(path), m = strlen(ext); return n >= m && !strcmp(path + n - m, ext); }
static int child(char *const args[]) {
    pid_t pid = fork();
    if (pid < 0) { perror("kremen: fork"); return 1; }
    if (!pid) { execvp(args[0], args); perror(args[0]); _exit(127); }
    int status;
    while (waitpid(pid, &status, 0) < 0) { if (errno == EINTR) continue; perror("kremen: waitpid"); return 1; }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
static void mkdir_parents(const char *path) {
    char *copy = strdup(path); if (!copy) { perror("kremen"); exit(2); }
    for (char *p = copy + 1; *p; p++) if (*p == '/') { *p = 0; if (mkdir(copy, 0777) && errno != EEXIST) { perror(copy); free(copy); exit(1); } *p = '/'; }
    free(copy);
}
static bool same_file(const char *left, const char *right) {
    if (!left || !*left || !right || !*right) return false;
    struct stat a, b;
    return stat(left, &a) == 0 && stat(right, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}
static bool protected_output(Context *ctx, const char *output) {
    for (size_t i = 0; i < ctx->sources.len; i++) if (same_file(output, ctx->sources.data[i]->path)) return true;
    return same_file(output, ctx->runtime_path) || same_file(output, current_executable);
}
static int atomic_write(const char *path, const char *data, bool preserve_mode) {
    mkdir_parents(path); size_t n = strlen(path); char *temp = malloc(n + 32); if (!temp) return 1;
    snprintf(temp, n + 32, "%s.kremen-XXXXXX", path); int fd = mkstemp(temp);
    if (fd < 0) { perror(temp); free(temp); return 1; }
    struct stat st;
    if (preserve_mode && stat(path, &st) == 0 && fchmod(fd, st.st_mode & 0777)) { perror("kremen: fchmod"); close(fd); unlink(temp); free(temp); return 1; }
    FILE *file = fdopen(fd, "wb"); if (!file) { close(fd); unlink(temp); free(temp); return 1; }
    size_t bytes = strlen(data); bool failed = fwrite(data, 1, bytes, file) != bytes;
    if (fclose(file)) failed = true;
    if (!failed && rename(temp, path)) { perror(path); failed = true; }
    if (failed) unlink(temp);
    free(temp); return failed ? 1 : 0;
}
static int build(Context *ctx, const char *source, const char *output) {
    if (protected_output(ctx, output)) { fputs("kremen: output would overwrite source\n", stderr); return 1; }
    mkdir_parents(output); char *directory = km_sprintf(ctx, "%s.kremen-XXXXXX", output);
    if (!mkdtemp(directory)) { perror(directory); return 1; }
    char absolute[PATH_MAX];
    if (!realpath(directory, absolute)) { perror(directory); rmdir(directory); return 1; }
    directory = km_strdup(ctx, absolute);
    char *cfile = km_sprintf(ctx, "%s/program.c", directory), *binary = km_sprintf(ctx, "%s/program", directory);
    FILE *f = fopen(cfile, "wb"); if (!f) { perror(cfile); rmdir(directory); return 1; }
    size_t n = strlen(source); bool fail = fwrite(source, 1, n, f) != n; if (fclose(f)) fail = true;
    if (fail) { unlink(cfile); rmdir(directory); return 1; }
    char opt[4]; snprintf(opt, sizeof opt, "-O%d", ctx->optimization);
    char *args[] = {ctx->cc, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", opt, cfile, "-o", binary, NULL};
    int code = child(args); if (!code && rename(binary, output)) { perror(output); code = 1; }
    unlink(binary); unlink(cfile); rmdir(directory); return code;
}
static char *runtime_path(Context *ctx) {
    char resolved[PATH_MAX];
    char exe[PATH_MAX];
    if (!*current_executable) return km_strdup(ctx, KM_RUNTIME_PATH);
    snprintf(exe, sizeof exe, "%s", current_executable);
    char *slash = strrchr(exe, '/'); if (slash) *slash = 0;
    const char *locations[] = {"../share/kremen/runtime/kremen_rt.h", "../runtime/kremen_rt.h"};
    for (size_t i = 0; i < sizeof locations / sizeof *locations; i++) {
        char *candidate = km_sprintf(ctx, "%s/%s", exe, locations[i]);
        if (realpath(candidate, resolved)) return km_strdup(ctx, resolved);
    }
    return realpath(KM_RUNTIME_PATH, resolved) ? km_strdup(ctx, resolved) : km_strdup(ctx, KM_RUNTIME_PATH);
}
static int selfhost(int argc, char **argv) {
    char executable[PATH_MAX], candidate[PATH_MAX];
    if (!*current_executable) { fputs("kremen: cannot resolve the toolchain executable\n", stderr); return 1; }
    snprintf(executable, sizeof executable, "%s", current_executable);
    char *slash = strrchr(executable, '/'); if (slash) *slash = 0;
    const char *locations[] = {"kremen-selfhost", "../selfhost/build/kremen-selfhost", KM_SELFHOST_PATH};
    bool found = false;
    for (size_t i = 0; i < sizeof locations / sizeof *locations; i++) {
        int length = i == 2 ? snprintf(candidate, sizeof candidate, "%s", locations[i]) :
            snprintf(candidate, sizeof candidate, "%s/%s", executable, locations[i]);
        if (length >= 0 && (size_t)length < sizeof candidate && access(candidate, X_OK) == 0) { found = true; break; }
    }
    if (!found) { fputs("kremen: selfhost driver unavailable; run make selfhost or reinstall the complete toolchain\n", stderr); return 1; }
    char **arguments = calloc((size_t)argc, sizeof *arguments);
    if (!arguments) return 2;
    arguments[0] = candidate;
    for (int i = 2; i < argc; i++) arguments[i - 1] = argv[i];
    execv(candidate, arguments);
    perror("kremen: selfhost driver"); free(arguments); return 127;
}
int main(int argc, char **argv) {
    (void)km_platform_executable(argv[0], current_executable, sizeof current_executable);
    if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) { usage(); return argc < 2 ? 1 : 0; }
    if (!strcmp(argv[1], "--version")) { puts("Kremen 0.4.2 (native C11)"); return 0; }
    if (!strcmp(argv[1], "selfhost")) return selfhost(argc, argv);
    const char *command = argv[1]; bool fmt = !strcmp(command, "fmt");
    if (!fmt && strcmp(command, "check") && strcmp(command, "build") && strcmp(command, "run") && strcmp(command, "emit-c")) { usage(); return 1; }
    Context *ctx = calloc(1, sizeof *ctx); if (!ctx) return 2;
    km_init(ctx); ctx->failure_ready = true;
    if (setjmp(ctx->failure)) { km_destroy(ctx); free(ctx); return 1; }
    ctx->runtime_path = runtime_path(ctx);
    const char *cc = getenv("CC"); if (cc && *cc) ctx->cc = km_strdup(ctx, cc);
    const char *output = NULL; const char **files = km_alloc(ctx, (size_t)argc * sizeof *files); size_t count = 0;
    bool format_check = false, write_format = false, positional_only = false;
    for (int i = 2; i < argc; i++) {
        if (positional_only) files[count++] = argv[i];
        else if (!strcmp(argv[i], "--")) positional_only = true;
        else if ((!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) && i + 1 < argc && !fmt && strcmp(command, "check") && strcmp(command, "run") && argv[i + 1][0]) output = argv[++i];
        else if (!strcmp(argv[i], "--cc") && i + 1 < argc && !fmt && argv[i + 1][0]) ctx->cc = km_strdup(ctx, argv[++i]);
        else if (!strcmp(argv[i], "--root") && i + 1 < argc) {
            if (fmt) { fputs("kremen fmt: --root is not a formatter option\n", stderr); km_destroy(ctx); free(ctx); return 1; }
            char resolved[PATH_MAX]; if (!realpath(argv[++i], resolved)) { perror("kremen: --root"); km_destroy(ctx); free(ctx); return 1; }
            ctx->project_root = km_strdup(ctx, resolved);
        } else if (!fmt && !strncmp(argv[i], "-O", 2) && strlen(argv[i]) == 3 && argv[i][2] >= '0' && argv[i][2] <= '3') ctx->optimization = argv[i][2] - '0';
        else if (fmt && !strcmp(argv[i], "--check")) format_check = true;
        else if (fmt && (!strcmp(argv[i], "-w") || !strcmp(argv[i], "--write"))) write_format = true;
        else if (argv[i][0] == '-') { fprintf(stderr, "kremen: unknown/incomplete option %s\n", argv[i]); km_destroy(ctx); free(ctx); return 1; }
        else files[count++] = argv[i];
    }
    if ((!fmt && count != 1) || (fmt && format_check && write_format) || (fmt && count > 1 && !format_check && !write_format)) { usage(); km_destroy(ctx); free(ctx); return 1; }
    int result = 0;
    if (fmt) {
        if (!count) {
            if (write_format) { fputs("kremen fmt: -w requires a file path\n", stderr); km_destroy(ctx); free(ctx); return 1; }
            Buffer input = {0}; char chunk[4096]; size_t n;
            while ((n = fread(chunk, 1, sizeof chunk, stdin))) {
                if (input.len + n > 4 * 1024 * 1024 || memchr(chunk, 0, n)) { free(input.data); fputs("kremen fmt: invalid or oversized stdin\n", stderr); km_destroy(ctx); free(ctx); return 1; }
                km_buf_append(&input, chunk, n);
            }
            if (ferror(stdin)) { free(input.data); perror("kremen fmt: stdin"); km_destroy(ctx); free(ctx); return 1; }
            Source *src = km_alloc(ctx, sizeof *src); src->path = "<stdin>"; src->len = input.len;
            src->data = km_strndup(ctx, input.data ? input.data : "", input.len); free(input.data);
            km_sources_push(ctx, &ctx->sources, src);
            char *formatted = km_format(ctx, src);
            if (format_check) result = strcmp(src->data, formatted) != 0;
            else fputs(formatted, stdout);
        }
        for (size_t i = 0; i < count; i++) {
            char path[PATH_MAX];
            if (!realpath(files[i], path)) {
                Source missing = {.path = (char *)files[i]};
                km_error(ctx, (Token){.src = &missing, .line = 1, .col = 1}, "E010", "cannot open source: %s", strerror(errno));
            }
            if (!extension(path, ".kr") && !extension(path, ".krh")) { fputs("kremen fmt: expected .kr or .krh\n", stderr); result = 1; break; }
            Source *src = km_read_source(ctx, path, extension(path, ".krh")); char *formatted = km_format(ctx, src);
            bool changed = strcmp(src->data, formatted) != 0;
            if (format_check && changed) { fprintf(stderr, "%s: requires formatting\n", files[i]); result = 1; }
            else if (write_format && changed) { if (atomic_write(path, formatted, true)) result = 1; }
            else if (!format_check && !write_format) fputs(formatted, stdout);
        }
    } else {
        (void)km_load(ctx, files[0]);
        if (!km_check(ctx)) result = 1;
        else if (!strcmp(command, "check")) printf("%s: OK\n", files[0]);
        else {
            char *generated = km_codegen(ctx);
            if (!strcmp(command, "emit-c")) {
                if (output && protected_output(ctx, output)) { fputs("kremen: output would overwrite source\n", stderr); result = 1; }
                else if (output) result = atomic_write(output, generated, false); else fputs(generated, stdout);
            } else if (!strcmp(command, "build")) {
                const char *path = output ? output : "a.out"; result = build(ctx, generated, path); if (!result) printf("Built %s\n", path);
            } else {
                char temporary[] = "/tmp/kremen-run-XXXXXX";
                if (!mkdtemp(temporary)) { perror("kremen: temporary directory"); result = 1; }
                else { char *binary = km_sprintf(ctx, "%s/program", temporary); result = build(ctx, generated, binary); if (!result) { char *args[] = {binary, NULL}; result = child(args); } unlink(binary); rmdir(temporary); }
            }
        }
    }
    km_destroy(ctx); free(ctx); return result;
}
