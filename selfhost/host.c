#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "host.h"
#include "runtime.h"
#if !defined(_WIN32)
#include <dirent.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "platform.h"

#if defined(_WIN32)
#define sh_realpath km_host_realpath
#define sh_unlink km_host_unlink
#define sh_rmdir km_host_rmdir
#define sh_mkdtemp km_host_mkdtemp
#define sh_fopen km_host_fopen
#else
#define sh_realpath realpath
#define sh_unlink unlink
#define sh_rmdir rmdir
#define sh_mkdtemp mkdtemp
#define sh_fopen fopen
#endif

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
static const char **host_arguments;
static size_t host_argument_count, host_argument_capacity;
static bool host_command_invalid, host_command_started;

static int64_t host_error(const char *operation, const char *path) {
    int code = errno;
    fprintf(stderr, "kremen selfhost: %s: %s: %s\n", operation, path, strerror(code));
    return 1;
}
static void host_cleanup(void) {
    free(host_arguments); host_arguments = NULL; host_argument_capacity = 0;
    while (host_directories != NULL) {
        HostDirectory *item = host_directories;
        host_directories = item->next;
#if defined(_WIN32)
        KmHostDirectory directory;
        if (km_host_dir_open(&directory, item->path)) {
            char name[PATH_MAX];
            while (km_host_dir_read(&directory, name) > 0) {
                if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
                size_t n = strlen(item->path) + strlen(name) + 2;
                char *path = malloc(n);
                if (path != NULL) {
                    snprintf(path, n, "%s/%s", item->path, name);
                    (void)sh_unlink(path);
                    free(path);
                }
            }
            km_host_dir_close(&directory);
            (void)sh_rmdir(item->path);
        }
#else
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
#endif
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
#if defined(_WIN32)
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, NULL, 0);
    if (!count) return host_keep("");
    wchar_t *wide = malloc((size_t)count * sizeof *wide);
    if (wide == NULL) return host_keep("");
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide, count)) { free(wide); return host_keep(""); }
    DWORD length = GetEnvironmentVariableW(wide, NULL, 0);
    if (!length) { free(wide); return host_keep(""); }
    wchar_t *value = malloc((size_t)length * sizeof *value);
    if (value == NULL) { free(wide); return host_keep(""); }
    DWORD got = GetEnvironmentVariableW(wide, value, length);
    free(wide);
    if (!got || got >= length) { free(value); return host_keep(""); }
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, NULL, 0, NULL, NULL);
    if (!bytes) { free(value); return host_keep(""); }
    char *utf8 = malloc((size_t)bytes);
    if (utf8 == NULL) { free(value); return host_keep(""); }
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, utf8, bytes, NULL, NULL)) { free(value); free(utf8); return host_keep(""); }
    const char *kept = host_keep(utf8);
    free(value); free(utf8); return kept;
#else
    const char *value = getenv(name);
    return host_keep(value == NULL ? "" : value);
#endif
}
int64_t host_setenv(const char *name, const char *value) {
#if defined(_WIN32)
    int names = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, NULL, 0);
    int values = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, NULL, 0);
    if (!names || !values) { errno = EINVAL; return host_error("setenv", name); }
    wchar_t *wide_name = malloc((size_t)names * sizeof *wide_name);
    wchar_t *wide_value = malloc((size_t)values * sizeof *wide_value);
    if (wide_name == NULL || wide_value == NULL) { free(wide_name); free(wide_value); errno = ENOMEM; return host_error("setenv", name); }
    bool valid = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide_name, names) &&
                 MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, wide_value, values);
    bool ok = valid && SetEnvironmentVariableW(wide_name, wide_value);
    DWORD error = GetLastError(); free(wide_name); free(wide_value);
    if (!ok) { km_host_error(error); return host_error("setenv", name); }
#else
    if (setenv(name, value, 1) != 0) return host_error("setenv", name);
#endif
    return 0;
}
const char *host_executable(void) {
    if (host_program != NULL) return host_program;
    char buffer[PATH_MAX];
    if (!km_platform_executable(rt_arg(0), buffer, sizeof buffer)) { host_error("resolve executable", rt_arg(0)); return ""; }
    host_program = host_keep(buffer);
    return host_program;
}
bool host_windows(void) {
#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}
const char *host_exe_suffix(void) {
    return host_windows() ? ".exe" : "";
}
const char *host_default_cc(void) {
    const char *executable = host_executable();
    if (*executable) {
        char directory[PATH_MAX], candidate[PATH_MAX], resolved[PATH_MAX];
        size_t length = strlen(executable);
        if (length < sizeof directory) {
            memcpy(directory, executable, length + 1);
            char *slash = strrchr(directory, '/');
            if (slash != NULL) {
                *slash = 0;
                int n = snprintf(candidate, sizeof candidate, "%s/../backend/bin/clang%s", directory, host_exe_suffix());
                if (n > 0 && (size_t)n < sizeof candidate && sh_realpath(candidate, resolved) != NULL) return host_keep(resolved);
            }
        }
    }
    return host_windows() ? "clang" : "cc";
}
static bool regular_file(const char *path) {
#if defined(_WIN32)
    return km_host_path_exists(path) && !km_host_path_is_dir(path);
#else
    struct stat state;
    return stat(path, &state) == 0 && S_ISREG(state.st_mode);
#endif
}
static bool support_directory(const char *path) {
    static const char *names[] = {"runtime.c", "runtime.h", "driver.c", "host.c", "host.h", "platform.h", "bootstrap.kr", "toolchain.kr"};
    char buffer[PATH_MAX];
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        int n = snprintf(buffer, sizeof buffer, "%s/%s", path, names[i]);
        if (n < 0 || (size_t)n >= sizeof buffer || !regular_file(buffer)) return false;
    }
#if defined(_WIN32)
    int n = snprintf(buffer, sizeof buffer, "%s/../native/platform.h", path);
    if (n < 0 || (size_t)n >= sizeof buffer || !regular_file(buffer)) return false;
#endif
    return true;
}
const char *host_resource(const char *name) {
    char candidate[PATH_MAX], resolved[PATH_MAX];
    if (host_support == NULL) {
        const char *override = host_env("KREMEN_SELFHOST_ROOT");
        if (override != NULL && *override != 0) {
            if (sh_realpath(override, resolved) == NULL || !support_directory(resolved)) {
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
                if (n >= 0 && (size_t)n < sizeof candidate && sh_realpath(candidate, resolved) != NULL && support_directory(resolved)) {
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
    size_t bytes = strlen(pattern) + 1;
    char *path = malloc(bytes);
    if (path == NULL) { fputs("kremen selfhost: out of memory\n", stderr); return ""; }
    memcpy(path, pattern, bytes);
    if (sh_mkdtemp(path) == NULL) { host_error("create temporary directory", pattern); free(path); return ""; }
    char absolute[PATH_MAX];
    if (sh_realpath(path, absolute) == NULL) { host_error("resolve temporary directory", path); sh_rmdir(path); free(path); return ""; }
    size_t n = strlen(absolute);
    HostDirectory *item = malloc(sizeof *item + n + 1);
    if (item == NULL) { sh_rmdir(path); free(path); fputs("kremen selfhost: out of memory\n", stderr); return ""; }
    memcpy(item->path, absolute, n + 1); item->next = host_directories; host_directories = item;
    free(path);
    return item->path;
}
const char *host_tempdir(void) {
#if defined(_WIN32)
    char buffer[PATH_MAX];
    if (!km_host_temp_pattern(buffer)) { host_error("temporary directory", "Windows TEMP"); return ""; }
    return make_temporary(buffer);
#else
    const char *base = getenv("TMPDIR"); if (base == NULL || !*base) base = "/tmp";
    char buffer[PATH_MAX];
    int n = snprintf(buffer, sizeof buffer, "%s/kremen-selfhost-XXXXXX", base);
    if (n < 0 || (size_t)n >= sizeof buffer) { errno = ENAMETOOLONG; host_error("temporary directory", base); return ""; }
    return make_temporary(buffer);
#endif
}
const char *host_stage_dir(const char *output) {
    char buffer[PATH_MAX]; int n = snprintf(buffer, sizeof buffer, "%s.kremen-XXXXXX", output);
    if (n < 0 || (size_t)n >= sizeof buffer) { errno = ENAMETOOLONG; host_error("output staging", output); return ""; }
    return make_temporary(buffer);
}
int64_t host_remove(const char *path) {
    if (sh_unlink(path) != 0 && errno != ENOENT) return host_error("remove", path);
    return 0;
}
int64_t host_rmdir(const char *path) {
    if (sh_rmdir(path) != 0 && errno != ENOENT) return host_error("remove directory", path);
    return 0;
}
bool host_same_file(const char *left, const char *right) {
#if defined(_WIN32)
    return km_host_same_file(left, right);
#else
    struct stat a, b;
    if (stat(left, &a) == 0 && stat(right, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino) return true;
    char x[PATH_MAX], y[PATH_MAX];
    return realpath(left, x) != NULL && realpath(right, y) != NULL && strcmp(x, y) == 0;
#endif
}
int64_t host_publish(const char *temporary, const char *destination) {
#if defined(_WIN32)
    if (km_host_replace(temporary, destination) != 0) return host_error("publish output", destination);
#else
    if (rename(temporary, destination) != 0) return host_error("publish output", destination);
#endif
    return 0;
}
int64_t host_exec_begin(const char *program) {
    host_init();
    host_argument_count = 0; host_command_invalid = !*program; host_command_started = !host_command_invalid;
    if (host_command_invalid) { fputs("kremen selfhost: empty executable\n", stderr); return 1; }
    return host_exec_arg(program);
}
int64_t host_exec_arg(const char *argument) {
    if (!host_command_started) { fputs("kremen selfhost: command has not been started\n", stderr); return 1; }
    if (host_command_invalid) return 1;
    if (host_argument_count + 1 >= host_argument_capacity) {
        size_t capacity = host_argument_capacity ? host_argument_capacity * 2 : 16;
        if (capacity <= host_argument_capacity || capacity > SIZE_MAX / sizeof *host_arguments) {
            host_command_invalid = true; fputs("kremen selfhost: command argument size overflow\n", stderr); return 1;
        }
        const char **arguments = realloc(host_arguments, capacity * sizeof *arguments);
        if (arguments == NULL) { host_command_invalid = true; fputs("kremen selfhost: out of memory for command arguments\n", stderr); return 1; }
        host_arguments = arguments; host_argument_capacity = capacity;
    }
    host_arguments[host_argument_count++] = argument;
    return 0;
}
int64_t host_exec_run(void) {
    if (host_command_invalid || host_argument_count == 0) return 1;
    host_arguments[host_argument_count] = NULL;
#if defined(_WIN32)
    int status = km_host_child((char *const *)host_arguments);
    host_argument_count = 0; host_command_started = false;
    return status;
#else
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
    host_command_started = false;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
#endif
}
int64_t host_print(const char *text, bool to_stderr) {
    FILE *stream = to_stderr ? stderr : stdout;
    if (fputs(text, stream) == EOF || fflush(stream) != 0) return host_error("write output", "stream");
    return 0;
}
bool host_equal_files(const char *left, const char *right) {
    FILE *a = sh_fopen(left, "rb");
    if (a == NULL) { host_error("open comparison", left); return false; }
    FILE *b = sh_fopen(right, "rb");
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
