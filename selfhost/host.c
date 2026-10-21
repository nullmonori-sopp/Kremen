#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "host.h"
#include "runtime.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef KREMEN_SELFHOST_ROOT
#define KREMEN_SELFHOST_ROOT "selfhost"
#endif

/* OS primitives only. Command parsing, compilation and self-building are Kremen. */
typedef struct HostString { struct HostString *next; char value[]; } HostString;
typedef struct HostDirectory { struct HostDirectory *next; char path[]; } HostDirectory;
static HostString *host_strings;
static HostDirectory *host_directories;
static bool host_initialized;
static const char *host_support;
static const char *host_program;
static const char *host_arguments[256];
static size_t host_argument_count;
static bool host_command_invalid;

static int64_t host_error(const char *operation, const char *path) {
    int code = errno;
    fprintf(stderr, "kremen selfhost: %s: %s: %s\n", operation, path, strerror(code));
    return 1;
}
static void host_cleanup(void) {
    while (host_directories != NULL) {
        HostDirectory *item = host_directories;
        host_directories = item->next;
        DIR *directory = opendir(item->path);
        if (directory != NULL) {
            struct dirent *entry;
            while ((entry = readdir(directory)) != NULL) {
                if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
                size_t n = strlen(item->path) + strlen(entry->d_name) + 2;
                char *path = malloc(n);
                if (path != NULL) {
                    snprintf(path, n, "%s/%s", item->path, entry->d_name);
                    (void)unlink(path); /* Never follow links or recurse into user directories. */
                    free(path);
                }
            }
            closedir(directory);
            (void)rmdir(item->path);
        }
        free(item);
    }
    while (host_strings != NULL) {
        HostString *item = host_strings;
        host_strings = item->next;
        free(item);
    }
}
static void host_init(void) {
    if (!host_initialized) {
        if (atexit(host_cleanup) != 0) {
            fputs("kremen selfhost: cannot register cleanup\n", stderr);
            exit(1);
        }
        host_initialized = true;
    }
}
static const char *host_keep(const char *value) {
    host_init();
    size_t n = strlen(value);
    if (n > SIZE_MAX - sizeof(HostString) - 1) exit(1);
    HostString *item = malloc(sizeof *item + n + 1);
    if (item == NULL) { fputs("kremen selfhost: out of memory\n", stderr); exit(1); }
    memcpy(item->value, value, n + 1);
    item->next = host_strings; host_strings = item;
    return item->value;
}
int64_t host_argc(void) { return rt_argc(); }
const char *host_env(const char *name) {
    const char *value = getenv(name);
    return host_keep(value == NULL ? "" : value);
}
int64_t host_setenv(const char *name, const char *value) {
    if (setenv(name, value, 1) != 0) return host_error("setenv", name);
    return 0;
}
const char *host_executable(void) {
    if (host_program != NULL) return host_program;
    char buffer[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buffer, sizeof buffer - 1);
    if (n < 0 || (size_t)n >= sizeof buffer - 1) {
        if (realpath(rt_arg(0), buffer) == NULL) { host_error("resolve executable", rt_arg(0)); return ""; }
    } else buffer[n] = 0;
    host_program = host_keep(buffer);
    return host_program;
}
static bool regular_file(const char *path) {
    struct stat state;
    return stat(path, &state) == 0 && S_ISREG(state.st_mode);
}
static bool support_directory(const char *path) {
    static const char *names[] = {"runtime.c", "runtime.h", "driver.c", "host.c", "host.h", "bootstrap.kr", "toolchain.kr"};
    char buffer[PATH_MAX];
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        int n = snprintf(buffer, sizeof buffer, "%s/%s", path, names[i]);
        if (n < 0 || (size_t)n >= sizeof buffer || !regular_file(buffer)) return false;
    }
    return true;
}
const char *host_resource(const char *name) {
    char candidate[PATH_MAX], resolved[PATH_MAX];
    if (host_support == NULL) {
        const char *override = getenv("KREMEN_SELFHOST_ROOT");
        if (override != NULL && *override != 0) {
            if (realpath(override, resolved) == NULL || !support_directory(resolved)) {
                fputs("kremen selfhost: KREMEN_SELFHOST_ROOT must contain the complete selfhost support sources\n", stderr);
                return "";
            }
            host_support = host_keep(resolved);
        } else {
            const char *executable = host_executable();
            if (!*executable) return "";
            char directory[PATH_MAX];
            snprintf(directory, sizeof directory, "%s", executable);
            char *slash = strrchr(directory, '/'); if (slash != NULL) *slash = 0;
            const char *suffixes[] = {"../share/kremen/selfhost", "..", KREMEN_SELFHOST_ROOT};
            for (size_t i = 0; i < sizeof suffixes / sizeof *suffixes; i++) {
                int n = i == 2 ? snprintf(candidate, sizeof candidate, "%s", suffixes[i]) :
                    snprintf(candidate, sizeof candidate, "%s/%s", directory, suffixes[i]);
                if (n >= 0 && (size_t)n < sizeof candidate && realpath(candidate, resolved) != NULL && support_directory(resolved)) {
                    host_support = host_keep(resolved); break;
                }
            }
            if (host_support == NULL) { fputs("kremen selfhost: support sources not found; set KREMEN_SELFHOST_ROOT\n", stderr); return ""; }
        }
    }
    if (!*name) return host_support;
    int n = snprintf(candidate, sizeof candidate, "%s/%s", host_support, name);
    if (n < 0 || (size_t)n >= sizeof candidate || !regular_file(candidate)) {
        fprintf(stderr, "kremen selfhost: support file unavailable: %s\n", name); return "";
    }
    return host_keep(candidate);
}
static const char *make_temporary(const char *pattern) {
    host_init();
    char *path = strdup(pattern);
    if (path == NULL) { fputs("kremen selfhost: out of memory\n", stderr); return ""; }
    if (mkdtemp(path) == NULL) { host_error("create temporary directory", pattern); free(path); return ""; }
    char absolute[PATH_MAX];
    if (realpath(path, absolute) == NULL) { host_error("resolve temporary directory", path); rmdir(path); free(path); return ""; }
    size_t n = strlen(absolute);
    HostDirectory *item = malloc(sizeof *item + n + 1);
    if (item == NULL) { rmdir(path); free(path); fputs("kremen selfhost: out of memory\n", stderr); return ""; }
    memcpy(item->path, absolute, n + 1); item->next = host_directories; host_directories = item;
    free(path);
    return item->path;
}
const char *host_tempdir(void) {
    const char *base = getenv("TMPDIR"); if (base == NULL || !*base) base = "/tmp";
    char buffer[PATH_MAX];
    int n = snprintf(buffer, sizeof buffer, "%s/kremen-selfhost-XXXXXX", base);
    if (n < 0 || (size_t)n >= sizeof buffer) { errno = ENAMETOOLONG; host_error("temporary directory", base); return ""; }
    return make_temporary(buffer);
}
const char *host_stage_dir(const char *output) {
    char buffer[PATH_MAX]; int n = snprintf(buffer, sizeof buffer, "%s.kremen-XXXXXX", output);
    if (n < 0 || (size_t)n >= sizeof buffer) { errno = ENAMETOOLONG; host_error("output staging", output); return ""; }
    return make_temporary(buffer);
}
int64_t host_remove(const char *path) {
    if (unlink(path) != 0 && errno != ENOENT) return host_error("remove", path);
    return 0;
}
int64_t host_rmdir(const char *path) {
    if (rmdir(path) != 0 && errno != ENOENT) return host_error("remove directory", path);
    return 0;
}
bool host_same_file(const char *left, const char *right) {
    struct stat a, b;
    if (stat(left, &a) == 0 && stat(right, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino) return true;
    char x[PATH_MAX], y[PATH_MAX];
    return realpath(left, x) != NULL && realpath(right, y) != NULL && strcmp(x, y) == 0;
}
int64_t host_publish(const char *temporary, const char *destination) {
    if (rename(temporary, destination) != 0) return host_error("publish output", destination);
    return 0;
}
int64_t host_exec_begin(const char *program) {
    host_argument_count = 0; host_command_invalid = !*program;
    if (host_command_invalid) { fputs("kremen selfhost: empty executable\n", stderr); return 1; }
    host_arguments[host_argument_count++] = program;
    return 0;
}
int64_t host_exec_arg(const char *argument) {
    if (host_command_invalid || host_argument_count == 0 || host_argument_count >= sizeof host_arguments / sizeof *host_arguments - 1) {
        host_command_invalid = true; fputs("kremen selfhost: command exceeds 255 arguments\n", stderr); return 1;
    }
    host_arguments[host_argument_count++] = argument;
    return 0;
}
int64_t host_exec_run(void) {
    if (host_command_invalid || host_argument_count == 0) return 1;
    host_arguments[host_argument_count] = NULL;
    pid_t pid = fork();
    if (pid < 0) return host_error("fork", host_arguments[0]);
    if (pid == 0) {
        execvp(host_arguments[0], (char *const *)host_arguments);
        int code = errno;
        fprintf(stderr, "kremen selfhost: exec %s: %s\n", host_arguments[0], strerror(code));
        _exit(code == ENOENT ? 127 : 126);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0) { if (errno != EINTR) return host_error("waitpid", host_arguments[0]); }
    host_argument_count = 0;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
}
int64_t host_print(const char *text, bool to_stderr) {
    FILE *stream = to_stderr ? stderr : stdout;
    if (fputs(text, stream) == EOF || fflush(stream) != 0) return host_error("write output", "stream");
    return 0;
}
bool host_equal_files(const char *left, const char *right) {
    FILE *a = fopen(left, "rb");
    if (a == NULL) { host_error("open comparison", left); return false; }
    FILE *b = fopen(right, "rb");
    if (b == NULL) { host_error("open comparison", right); fclose(a); return false; }
    unsigned char x[8192], y[8192]; bool equal = true;
    for (;;) {
        size_t nx = fread(x, 1, sizeof x, a), ny = fread(y, 1, sizeof y, b);
        if (nx != ny || memcmp(x, y, nx) != 0) { equal = false; break; }
        if (nx < sizeof x) { if (ferror(a) || ferror(b)) equal = false; break; }
    }
    if (fclose(a) != 0) equal = false;
    if (fclose(b) != 0) equal = false;
    return equal;
}
