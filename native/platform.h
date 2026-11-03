#ifndef KREMEN_NATIVE_PLATFORM_H
#define KREMEN_NATIVE_PLATFORM_H
#if defined(_WIN32) && !defined(_CRT_RAND_S)
#define _CRT_RAND_S
#endif
/* Full-frontend host boundary. The Windows profile targets MinGW-w64/UCRT
 * and winpthreads; the bootstrap subset retains its separate POSIX host. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#undef PATH_MAX
#define PATH_MAX 4096
#define KM_EXE_SUFFIX ".exe"
#define KM_DEFAULT_OUTPUT "a.exe"

static inline void km_host_error(DWORD error) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: errno = ENOENT; break;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: errno = EACCES; break;
    case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: errno = EEXIST; break;
    case ERROR_FILENAME_EXCED_RANGE: case ERROR_INSUFFICIENT_BUFFER: errno = ENAMETOOLONG; break;
    case ERROR_DISK_FULL: errno = ENOSPC; break;
    default: errno = EIO; break;
    }
}
static inline void km_host_slashes(char *path) {
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
}
static inline char *km_host_realpath(const char *path, char output[PATH_MAX]) {
    HANDLE file = CreateFileA(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (file == INVALID_HANDLE_VALUE) { km_host_error(GetLastError()); return NULL; }
    char resolved[PATH_MAX];
    DWORD length = GetFinalPathNameByHandleA(file, resolved, sizeof resolved, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    DWORD error = GetLastError(); CloseHandle(file);
    if (!length || length >= sizeof resolved) { km_host_error(length ? ERROR_FILENAME_EXCED_RANGE : error); return NULL; }
    const char *name = resolved;
    if (!strncmp(name, "\\\\?\\UNC\\", 8)) {
        size_t tail = strlen(name + 8);
        if (tail + 3 > PATH_MAX) { errno = ENAMETOOLONG; return NULL; }
        output[0] = '/'; output[1] = '/'; memcpy(output + 2, name + 8, tail + 1);
    } else {
        if (!strncmp(name, "\\\\?\\", 4)) name += 4;
        size_t n = strlen(name);
        if (n >= PATH_MAX) { errno = ENAMETOOLONG; return NULL; }
        memcpy(output, name, n + 1);
    }
    km_host_slashes(output); return output;
}
static inline int km_host_path_compare(const char *a, const char *b) { return _stricmp(a, b); }
static inline int km_host_path_prefix(const char *a, const char *b, size_t n) { return _strnicmp(a, b, n); }
static inline bool km_host_absolute_import(const char *path) {
    return !path[0] || path[0] == '/' || path[0] == '\\' || strchr(path, ':') != NULL;
}
static inline bool km_host_same_file(const char *a, const char *b) {
    if (!a || !*a || !b || !*b) return false;
    HANDLE left = CreateFileA(a, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (left == INVALID_HANDLE_VALUE) return false;
    HANDLE right = CreateFileA(b, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    BY_HANDLE_FILE_INFORMATION x, y;
    bool same = right != INVALID_HANDLE_VALUE && GetFileInformationByHandle(left, &x) && GetFileInformationByHandle(right, &y) &&
                x.dwVolumeSerialNumber == y.dwVolumeSerialNumber && x.nFileIndexHigh == y.nFileIndexHigh && x.nFileIndexLow == y.nFileIndexLow;
    if (right != INVALID_HANDLE_VALUE) CloseHandle(right);
    CloseHandle(left); return same;
}
static inline int km_host_mkdir(const char *path) { return _mkdir(path); }
static inline int km_host_chmod(int fd, const char *path, int mode) {
    (void)fd;
    /* Windows cannot replace a read-only destination. Refuse before making
     * the staged file read-only too, so failure cleanup can always remove it. */
    if (!(mode & _S_IWRITE)) { errno = EACCES; return -1; }
    return _chmod(path, mode & (_S_IREAD | _S_IWRITE));
}
static inline int km_host_replace(const char *from, const char *to) {
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 0;
    km_host_error(GetLastError()); return -1;
}
/* Random candidates are exclusively created; no check-then-create race or
 * predictable shared temporary file is introduced by the portability layer. */
static inline bool km_host_temp_name(char *pattern) {
    size_t n = strlen(pattern);
    if (n < 6 || strcmp(pattern + n - 6, "XXXXXX")) { errno = EINVAL; return false; }
    unsigned int bits = 0;
    if (rand_s(&bits)) { errno = EIO; return false; }
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (size_t i = 0; i < 6; i++) { pattern[n - 6 + i] = alphabet[bits % 62]; bits /= 62; }
    return true;
}
static inline int km_host_mkstemp(char *pattern) {
    size_t n = strlen(pattern);
    for (unsigned attempt = 0; attempt < 128; attempt++) {
        if (attempt && n >= 6) memcpy(pattern + n - 6, "XXXXXX", 6);
        if (!km_host_temp_name(pattern)) return -1;
        int fd = _open(pattern, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT, _S_IREAD | _S_IWRITE);
        if (fd >= 0 || errno != EEXIST) return fd;
    }
    errno = EEXIST; return -1;
}
static inline char *km_host_mkdtemp(char *pattern) {
    size_t n = strlen(pattern);
    for (unsigned attempt = 0; attempt < 128; attempt++) {
        if (attempt && n >= 6) memcpy(pattern + n - 6, "XXXXXX", 6);
        if (!km_host_temp_name(pattern)) return NULL;
        if (!_mkdir(pattern)) return pattern;
        if (errno != EEXIST) return NULL;
    }
    errno = EEXIST; return NULL;
}
static inline bool km_host_temp_pattern(char output[PATH_MAX]) {
    DWORD n = GetTempPathA(PATH_MAX, output);
    if (!n || n >= PATH_MAX || n + sizeof "kremen-run-XXXXXX" > PATH_MAX) { km_host_error(n ? ERROR_FILENAME_EXCED_RANGE : GetLastError()); return false; }
    memcpy(output + n, "kremen-run-XXXXXX", sizeof "kremen-run-XXXXXX");
    return true;
}
static inline bool km_platform_executable(const char *argument, char *output, size_t capacity) {
    (void)argument;
    if (capacity < 2) { errno = ENAMETOOLONG; return false; }
    char image[PATH_MAX], resolved[PATH_MAX];
    DWORD n = GetModuleFileNameA(NULL, image, sizeof image);
    if (!n || n >= sizeof image) { output[0] = 0; km_host_error(n ? ERROR_FILENAME_EXCED_RANGE : GetLastError()); return false; }
    if (!km_host_realpath(image, resolved)) { output[0] = 0; return false; }
    size_t length = strlen(resolved);
    if (length >= capacity) { output[0] = 0; errno = ENAMETOOLONG; return false; }
    memcpy(output, resolved, length + 1); return true;
}
/* Windows accepts one command-line string. Quote every argv element according
 * to the C runtime's backslash/quote grammar; never pass it through cmd.exe. */
static inline int km_host_child_impl(char *const args[], bool quiet) {
    size_t size = 1;
    for (size_t i = 0; args[i]; i++) {
        size_t n = strlen(args[i]);
        if (n > (SIZE_MAX - size - 4) / 2) { errno = E2BIG; return 1; }
        size += n * 2 + 4;
    }
    if (size > 32767) { errno = E2BIG; return 1; }
    char *command = malloc(size);
    if (!command) return 1;
    char *at = command;
    for (size_t i = 0; args[i]; i++) {
        if (i) *at++ = ' ';
        *at++ = '"';
        const char *p = args[i];
        for (;;) {
            size_t slashes = 0;
            while (*p == '\\') { slashes++; p++; }
            size_t count = (*p == '"' || !*p) ? slashes * 2 : slashes;
            while (count--) *at++ = '\\';
            if (*p == '"') *at++ = '\\';
            if (!*p) break;
            *at++ = *p++;
        }
        *at++ = '"';
    }
    *at = 0;
    STARTUPINFOA start; PROCESS_INFORMATION process;
    memset(&start, 0, sizeof start); memset(&process, 0, sizeof process); start.cb = sizeof start;
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = GetStdHandle(STD_INPUT_HANDLE); start.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE); start.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    BOOL launched = CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &start, &process);
    DWORD error = GetLastError(); free(command);
    if (!launched) { km_host_error(error); if (!quiet) perror(args[0]); return 127; }
    DWORD code = 1;
    DWORD waited = WaitForSingleObject(process.hProcess, INFINITE);
    if (waited == WAIT_OBJECT_0) (void)GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return code > INT_MAX ? 1 : (int)code;
}
static inline int km_host_source_open(const char *path) { return _open(path, _O_RDONLY | _O_BINARY | _O_NOINHERIT); }
static inline void km_host_streams(void) {
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
    (void)_setmode(_fileno(stderr), _O_BINARY);
}
#else
#include "../selfhost/platform.h"
#include <sys/wait.h>
#define KM_EXE_SUFFIX ""
#define KM_DEFAULT_OUTPUT "a.out"
static inline char *km_host_realpath(const char *path, char output[PATH_MAX]) { return realpath(path, output); }
static inline int km_host_path_compare(const char *a, const char *b) { return strcmp(a, b); }
static inline int km_host_path_prefix(const char *a, const char *b, size_t n) { return strncmp(a, b, n); }
static inline bool km_host_absolute_import(const char *path) { return !path[0] || path[0] == '/'; }
static inline bool km_host_same_file(const char *a, const char *b) {
    if (!a || !*a || !b || !*b) return false;
    struct stat x, y;
    return !stat(a, &x) && !stat(b, &y) && x.st_dev == y.st_dev && x.st_ino == y.st_ino;
}
static inline int km_host_mkdir(const char *path) { return mkdir(path, 0777); }
static inline int km_host_chmod(int fd, const char *path, int mode) { (void)path; return fchmod(fd, (mode_t)mode); }
static inline int km_host_replace(const char *from, const char *to) { return rename(from, to); }
static inline int km_host_mkstemp(char *pattern) { return mkstemp(pattern); }
static inline char *km_host_mkdtemp(char *pattern) { return mkdtemp(pattern); }
static inline bool km_host_temp_pattern(char output[PATH_MAX]) { memcpy(output, "/tmp/kremen-run-XXXXXX", sizeof "/tmp/kremen-run-XXXXXX"); return true; }
static inline int km_host_child_impl(char *const args[], bool quiet) {
    pid_t pid = fork();
    if (pid < 0) { if (!quiet) perror("kremen: fork"); return 1; }
    if (!pid) { execvp(args[0], args); if (!quiet) perror(args[0]); _exit(127); }
    int status;
    while (waitpid(pid, &status, 0) < 0) { if (errno == EINTR) continue; if (!quiet) perror("kremen: waitpid"); return 1; }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
static inline int km_host_source_open(const char *path) { return open(path, O_RDONLY | O_NONBLOCK); }
static inline void km_host_streams(void) { }
#endif

static inline int km_host_child(char *const args[]) { return km_host_child_impl(args, false); }
static inline int km_host_child_quiet(char *const args[]) { return km_host_child_impl(args, true); }

static inline bool km_host_inside(const char *resolved, const char *root) {
    size_t n = strlen(root);
    if (km_host_path_prefix(resolved, root, n) || (resolved[n] && resolved[n] != '/' && !(n && root[n - 1] == '/'))) return false;
#if defined(_WIN32)
    /* NTFS can opt individual directories into case-sensitive lookup. A
     * folded prefix alone could then admit a distinct sibling such as Root
     * versus root. Confirm the resolved ancestor is the actual selected root. */
    char ancestor[PATH_MAX];
    if (!n || n >= sizeof ancestor) return false;
    memcpy(ancestor, resolved, n); ancestor[n] = 0;
    return km_host_same_file(ancestor, root);
#else
    return true;
#endif
}
#endif
