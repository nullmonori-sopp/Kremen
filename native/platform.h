#ifndef KREMEN_NATIVE_PLATFORM_H
#define KREMEN_NATIVE_PLATFORM_H
#if defined(_WIN32) && !defined(_CRT_RAND_S)
#define _CRT_RAND_S
#endif
/* Native host boundary. All public strings are UTF-8; Windows conversions use
 * the system's UTF-16 APIs, never an ANSI code page or a compatibility DLL. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#include <wchar.h>
#undef PATH_MAX
#define PATH_MAX 32768
#define KM_EXE_SUFFIX ".exe"
#define KM_DEFAULT_OUTPUT "a.exe"

static inline void km_host_error(DWORD error) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: errno = ENOENT; break;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: errno = EACCES; break;
    case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: errno = EEXIST; break;
    case ERROR_FILENAME_EXCED_RANGE: case ERROR_INSUFFICIENT_BUFFER: errno = ENAMETOOLONG; break;
    case ERROR_DISK_FULL: errno = ENOSPC; break;
    case ERROR_DIRECTORY: errno = ENOTDIR; break;
    case ERROR_NO_UNICODE_TRANSLATION: errno = EILSEQ; break;
    default: errno = EIO; break;
    }
}
static inline wchar_t *km_host_wide(const char *text) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!count) { errno = EILSEQ; return NULL; }
    wchar_t *wide = malloc((size_t)count * sizeof *wide);
    if (!wide) { errno = ENOMEM; return NULL; }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count)) {
        free(wide); errno = EILSEQ; return NULL;
    }
    return wide;
}
static inline bool km_host_utf8(const wchar_t *wide, char *output, size_t capacity) {
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, NULL, 0, NULL, NULL);
    if (!count) { errno = EILSEQ; return false; }
    if ((size_t)count > capacity || capacity > INT_MAX) { errno = ENAMETOOLONG; return false; }
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, output, (int)capacity, NULL, NULL)) {
        errno = EILSEQ; return false;
    }
    return true;
}
static inline void km_host_slashes(char *path) {
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
}
/* Add an extended absolute prefix for CRT and Win32 file calls. Canonical
 * boundary checks still resolve an existing handle, rather than trusting this
 * lexical expansion. Device namespaces are not accepted as ordinary files. */
static inline wchar_t *km_host_wpath(const char *path) {
    wchar_t *input = km_host_wide(path);
    if (!input) return NULL;
    for (wchar_t *p = input; *p; p++) if (*p == L'/') *p = L'\\';
    if (!wcsncmp(input, L"\\\\.\\", 4)) { free(input); errno = EINVAL; return NULL; }
    if (!wcsncmp(input, L"\\\\?\\", 4)) return input;
    DWORD count = GetFullPathNameW(input, 0, NULL, NULL);
    if (!count || count >= 32760) {
        DWORD error = GetLastError(); free(input);
        km_host_error(count ? ERROR_FILENAME_EXCED_RANGE : error); return NULL;
    }
    wchar_t *full = malloc(((size_t)count + 1) * sizeof *full);
    if (!full) { free(input); errno = ENOMEM; return NULL; }
    DWORD length = GetFullPathNameW(input, count + 1, full, NULL);
    free(input);
    if (!length || length > count) { DWORD error = GetLastError(); free(full); km_host_error(error); return NULL; }
    bool unc = full[0] == L'\\' && full[1] == L'\\';
    size_t prefix = unc ? 8 : 4, skip = unc ? 2 : 0;
    wchar_t *result = malloc((prefix + wcslen(full + skip) + 1) * sizeof *result);
    if (!result) { free(full); errno = ENOMEM; return NULL; }
    memcpy(result, unc ? L"\\\\?\\UNC\\" : L"\\\\?\\", prefix * sizeof *result);
    wcscpy(result + prefix, full + skip);
    free(full); return result;
}
static inline HANDLE km_host_path_handle(const char *path) {
    wchar_t *wide = km_host_wpath(path);
    if (!wide) return INVALID_HANDLE_VALUE;
    HANDLE file = CreateFileW(wide, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    DWORD error = GetLastError(); free(wide);
    if (file == INVALID_HANDLE_VALUE) km_host_error(error);
    return file;
}
static inline char *km_host_realpath(const char *path, char output[PATH_MAX]) {
    HANDLE file = km_host_path_handle(path);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    wchar_t *resolved = malloc(32768 * sizeof *resolved);
    if (!resolved) { CloseHandle(file); errno = ENOMEM; return NULL; }
    DWORD length = GetFinalPathNameByHandleW(file, resolved, 32768, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    DWORD error = GetLastError(); CloseHandle(file);
    if (!length || length >= 32768) {
        free(resolved); km_host_error(length ? ERROR_FILENAME_EXCED_RANGE : error); return NULL;
    }
    wchar_t *name = resolved;
    if (!wcsncmp(name, L"\\\\?\\UNC\\", 8)) {
        name += 6; name[0] = L'\\';
    } else if (!wcsncmp(name, L"\\\\?\\", 4)) name += 4;
    bool ok = km_host_utf8(name, output, PATH_MAX);
    free(resolved);
    if (!ok) return NULL;
    km_host_slashes(output); return output;
}
static inline int km_host_path_compare(const char *a, const char *b) {
    wchar_t *x = km_host_wide(a), *y = km_host_wide(b);
    if (!x || !y) { free(x); free(y); return strcmp(a, b); }
    int result = CompareStringOrdinal(x, -1, y, -1, TRUE);
    free(x); free(y);
    return result == CSTR_EQUAL ? 0 : result == CSTR_LESS_THAN ? -1 : 1;
}
static inline int km_host_path_prefix(const char *a, const char *b, size_t n) {
    char *prefix = malloc(n + 1);
    if (!prefix) { errno = ENOMEM; return 1; }
    size_t length = strlen(a);
    if (length < n) { free(prefix); return -1; }
    memcpy(prefix, a, n); prefix[n] = 0;
    int result = km_host_path_compare(prefix, b);
    free(prefix); return result;
}
static inline bool km_host_absolute_import(const char *path) {
    return !path[0] || path[0] == '/' || path[0] == '\\' || strchr(path, ':') != NULL;
}
static inline bool km_host_same_file(const char *a, const char *b) {
    if (!a || !*a || !b || !*b) return false;
    HANDLE left = km_host_path_handle(a);
    if (left == INVALID_HANDLE_VALUE) return false;
    HANDLE right = km_host_path_handle(b);
    BY_HANDLE_FILE_INFORMATION x, y;
    bool same = right != INVALID_HANDLE_VALUE && GetFileInformationByHandle(left, &x) && GetFileInformationByHandle(right, &y) &&
        x.dwVolumeSerialNumber == y.dwVolumeSerialNumber && x.nFileIndexHigh == y.nFileIndexHigh && x.nFileIndexLow == y.nFileIndexLow;
    if (right != INVALID_HANDLE_VALUE) CloseHandle(right);
    CloseHandle(left); return same;
}
static inline bool km_host_path_mode(const char *path, int *mode) {
    wchar_t *wide = km_host_wpath(path);
    if (!wide) return false;
    DWORD attributes = GetFileAttributesW(wide), error = GetLastError(); free(wide);
    if (attributes == INVALID_FILE_ATTRIBUTES) { km_host_error(error); return false; }
    *mode = _S_IREAD | ((attributes & FILE_ATTRIBUTE_READONLY) ? 0 : _S_IWRITE);
    return true;
}
static inline bool km_host_path_exists(const char *path) {
    int mode; return km_host_path_mode(path, &mode);
}
static inline bool km_host_environment(const char *name, char output[PATH_MAX]) {
    wchar_t *key = km_host_wide(name);
    if (!key) return false;
    DWORD count = GetEnvironmentVariableW(key, NULL, 0);
    if (!count) { free(key); errno = ENOENT; return false; }
    wchar_t *wide = malloc((size_t)count * sizeof *wide);
    if (!wide) { free(key); errno = ENOMEM; return false; }
    DWORD used = GetEnvironmentVariableW(key, wide, count);
    free(key);
    if (!used || used >= count) { free(wide); errno = EIO; return false; }
    bool ok = km_host_utf8(wide, output, PATH_MAX); free(wide); return ok;
}
static inline bool km_host_path_is_dir(const char *path) {
    wchar_t *wide = km_host_wpath(path);
    if (!wide) return false;
    DWORD attributes = GetFileAttributesW(wide), error = GetLastError(); free(wide);
    if (attributes == INVALID_FILE_ATTRIBUTES) { km_host_error(error); return false; }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
static inline int km_host_mkdir(const char *path) {
    wchar_t *wide = km_host_wpath(path); if (!wide) return -1;
    int result = _wmkdir(wide); free(wide); return result;
}
static inline int km_host_rmdir(const char *path) {
    wchar_t *wide = km_host_wpath(path); if (!wide) return -1;
    int result = _wrmdir(wide); free(wide); return result;
}
static inline int km_host_unlink(const char *path) {
    wchar_t *wide = km_host_wpath(path); if (!wide) return -1;
    int result = _wunlink(wide); free(wide); return result;
}
static inline int km_host_close(int fd) { return _close(fd); }
static inline int km_host_chmod(int fd, const char *path, int mode) {
    (void)fd;
    if (!(mode & _S_IWRITE)) { errno = EACCES; return -1; }
    wchar_t *wide = km_host_wpath(path); if (!wide) return -1;
    int result = _wchmod(wide, mode & (_S_IREAD | _S_IWRITE)); free(wide); return result;
}
static inline int km_host_replace(const char *from, const char *to) {
    wchar_t *a = km_host_wpath(from), *b = km_host_wpath(to);
    if (!a || !b) { free(a); free(b); return -1; }
    BOOL ok = MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    DWORD error = GetLastError(); free(a); free(b);
    if (ok) return 0;
    km_host_error(error); return -1;
}
static inline FILE *km_host_fopen(const char *path, const char *mode) {
    wchar_t *name = km_host_wpath(path), *flags = km_host_wide(mode);
    if (!name || !flags) { free(name); free(flags); return NULL; }
    FILE *file = _wfopen(name, flags); free(name); free(flags); return file;
}
static inline FILE *km_host_fdopen(int fd, const char *mode) { return _fdopen(fd, mode); }
static inline bool km_host_fd_regular(int fd) {
    intptr_t value = _get_osfhandle(fd);
    if (value == -1) { errno = EBADF; return false; }
    HANDLE file = (HANDLE)value;
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { errno = EINVAL; return false; }
    return true;
}
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
        wchar_t *wide = km_host_wpath(pattern); if (!wide) return -1;
        int fd = _wopen(wide, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT, _S_IREAD | _S_IWRITE);
        free(wide);
        if (fd >= 0 || errno != EEXIST) return fd;
    }
    errno = EEXIST; return -1;
}
static inline char *km_host_mkdtemp(char *pattern) {
    size_t n = strlen(pattern);
    for (unsigned attempt = 0; attempt < 128; attempt++) {
        if (attempt && n >= 6) memcpy(pattern + n - 6, "XXXXXX", 6);
        if (!km_host_temp_name(pattern)) return NULL;
        if (!km_host_mkdir(pattern)) return pattern;
        if (errno != EEXIST) return NULL;
    }
    errno = EEXIST; return NULL;
}
static inline bool km_host_temp_pattern(char output[PATH_MAX]) {
    wchar_t *wide = malloc(32768 * sizeof *wide);
    if (!wide) { errno = ENOMEM; return false; }
    DWORD n = GetTempPathW(32768, wide);
    if (!n || n >= 32768) { DWORD error = GetLastError(); free(wide); km_host_error(error); return false; }
    bool ok = km_host_utf8(wide, output, PATH_MAX);
    free(wide);
    if (!ok) return false;
    size_t used = strlen(output);
    if (used + sizeof "kremen-run-XXXXXX" > PATH_MAX) { errno = ENAMETOOLONG; return false; }
    memcpy(output + used, "kremen-run-XXXXXX", sizeof "kremen-run-XXXXXX");
    return true;
}
static inline bool km_platform_executable(const char *argument, char *output, size_t capacity) {
    (void)argument;
    wchar_t *image = malloc(32768 * sizeof *image);
    char *path = malloc(PATH_MAX), *resolved = malloc(PATH_MAX);
    if (!image || !path || !resolved) { free(image); free(path); free(resolved); errno = ENOMEM; return false; }
    DWORD n = GetModuleFileNameW(NULL, image, 32768);
    bool found = n && n < 32768 && km_host_utf8(image, path, PATH_MAX) && km_host_realpath(path, resolved);
    if (found && strlen(resolved) >= capacity) { errno = ENAMETOOLONG; found = false; }
    if (found) memcpy(output, resolved, strlen(resolved) + 1);
    else if (capacity) output[0] = 0;
    free(image); free(path); free(resolved); return found;
}
static inline int km_host_child_impl(char *const args[], bool quiet) {
    size_t size = 1, count = 0;
    while (args[count]) count++;
    wchar_t **wide = calloc(count + 1, sizeof *wide);
    if (!wide) { errno = ENOMEM; return 1; }
    for (size_t i = 0; i < count; i++) {
        wide[i] = km_host_wide(args[i]);
        if (!wide[i]) { for (size_t j = 0; j < i; j++) free(wide[j]); free(wide); return 1; }
        size_t n = wcslen(wide[i]);
        if (size > 32763 || n > (32767 - size - 4) / 2) {
            for (size_t j = 0; j <= i; j++) free(wide[j]);
            free(wide); errno = E2BIG; return 1;
        }
        size += n * 2 + 4;
    }
    wchar_t *command = malloc(size * sizeof *command);
    if (!command) { for (size_t i = 0; i < count; i++) free(wide[i]); free(wide); errno = ENOMEM; return 1; }
    wchar_t *at = command;
    for (size_t i = 0; i < count; i++) {
        if (i) *at++ = L' ';
        *at++ = L'"';
        const wchar_t *p = wide[i];
        for (;;) {
            size_t slashes = 0;
            while (*p == L'\\') { slashes++; p++; }
            size_t copies = (*p == L'"' || !*p) ? slashes * 2 : slashes;
            while (copies--) *at++ = L'\\';
            if (*p == L'"') *at++ = L'\\';
            if (!*p) break;
            *at++ = *p++;
        }
        *at++ = L'"'; free(wide[i]);
    }
    free(wide); *at = 0;
    STARTUPINFOW start; PROCESS_INFORMATION process;
    memset(&start, 0, sizeof start); memset(&process, 0, sizeof process); start.cb = sizeof start;
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    start.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    start.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    BOOL launched = CreateProcessW(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &start, &process);
    DWORD error = GetLastError(); free(command);
    if (!launched) { km_host_error(error); if (!quiet) perror(args[0]); return 127; }
    DWORD code = 1;
    if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0)
        (void)GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return code > INT_MAX ? 1 : (int)code;
}
static inline int km_host_source_open(const char *path) {
    wchar_t *wide = km_host_wpath(path); if (!wide) return -1;
    int fd = _wopen(wide, _O_RDONLY | _O_BINARY | _O_NOINHERIT); free(wide); return fd;
}
static inline void km_host_streams(void) {
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
    (void)_setmode(_fileno(stderr), _O_BINARY);
}
typedef struct KmHostDirectory { HANDLE handle; WIN32_FIND_DATAW entry; bool first; } KmHostDirectory;
static inline bool km_host_dir_open(KmHostDirectory *dir, const char *path) {
    wchar_t *wide = km_host_wpath(path);
    if (!wide) return false;
    size_t n = wcslen(wide);
    wchar_t *pattern = malloc((n + 3) * sizeof *pattern);
    if (!pattern) { free(wide); errno = ENOMEM; return false; }
    memcpy(pattern, wide, n * sizeof *pattern);
    pattern[n] = L'\\'; pattern[n + 1] = L'*'; pattern[n + 2] = 0;
    free(wide);
    dir->handle = FindFirstFileW(pattern, &dir->entry); dir->first = true;
    DWORD error = GetLastError(); free(pattern);
    if (dir->handle == INVALID_HANDLE_VALUE) { km_host_error(error); return false; }
    return true;
}
static inline int km_host_dir_read(KmHostDirectory *dir, char name[PATH_MAX]) {
    if (!dir->first && !FindNextFileW(dir->handle, &dir->entry)) {
        DWORD error = GetLastError();
        if (error == ERROR_NO_MORE_FILES) return 0;
        km_host_error(error); return -1;
    }
    dir->first = false;
    return km_host_utf8(dir->entry.cFileName, name, PATH_MAX) ? 1 : -1;
}
static inline void km_host_dir_close(KmHostDirectory *dir) {
    if (dir->handle != INVALID_HANDLE_VALUE) { FindClose(dir->handle); dir->handle = INVALID_HANDLE_VALUE; }
}
static char **km_host_argument_storage;
static size_t km_host_argument_count;
static inline void km_host_free_arguments(void) {
    for (size_t i = 0; i < km_host_argument_count; i++) free(km_host_argument_storage[i]);
    free(km_host_argument_storage); km_host_argument_storage = NULL; km_host_argument_count = 0;
}
static inline bool km_host_arguments(int *argc, char ***argv) {
    if (km_host_argument_storage) { *argc = (int)km_host_argument_count; *argv = km_host_argument_storage; return true; }
    const wchar_t *input = GetCommandLineW();
    size_t length = wcslen(input);
    char **arguments = calloc(length + 2, sizeof *arguments);
    wchar_t *part = malloc((length + 1) * sizeof *part);
    if (!arguments || !part) { free(arguments); free(part); errno = ENOMEM; return false; }
    size_t count = 0; bool failed = false;
    while (*input) {
        while (*input == L' ' || *input == L'\t') input++;
        if (!*input) break;
        wchar_t *out = part; bool quoted = false;
        for (;;) {
            size_t slashes = 0;
            while (*input == L'\\') { slashes++; input++; }
            if (*input == L'"') {
                for (size_t i = 0; i < slashes / 2; i++) *out++ = L'\\';
                if (slashes % 2) { *out++ = *input++; }
                else if (quoted && input[1] == L'"') { *out++ = L'"'; input += 2; }
                else { quoted = !quoted; input++; }
            } else {
                while (slashes--) *out++ = L'\\';
                if (!*input || (!quoted && (*input == L' ' || *input == L'\t'))) break;
                *out++ = *input++;
            }
        }
        *out = 0;
        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, part, -1, NULL, 0, NULL, NULL);
        if (!bytes) { errno = EILSEQ; failed = true; break; }
        arguments[count] = malloc((size_t)bytes);
        if (!arguments[count]) { errno = ENOMEM; failed = true; break; }
        if (!km_host_utf8(part, arguments[count], (size_t)bytes)) { free(arguments[count]); arguments[count] = NULL; failed = true; break; }
        count++;
    }
    free(part);
    if (failed || *input || !count) {
        for (size_t i = 0; i < count; i++) free(arguments[i]);
        free(arguments); return false;
    }
    km_host_argument_storage = arguments; km_host_argument_count = count;
    if (atexit(km_host_free_arguments)) { km_host_free_arguments(); errno = ENOMEM; return false; }
    *argc = (int)count; *argv = arguments; return true;
}

#else
#include <unistd.h>
#include <dirent.h>
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
static inline bool km_host_path_mode(const char *path, int *mode) {
    struct stat state; if (stat(path, &state)) return false;
    *mode = (int)state.st_mode; return true;
}
static inline bool km_host_path_exists(const char *path) { struct stat state; return !stat(path, &state); }
static inline bool km_host_environment(const char *name, char output[PATH_MAX]) {
    const char *value = getenv(name);
    if (!value) { errno = ENOENT; return false; }
    size_t n = strlen(value);
    if (n >= PATH_MAX) { errno = ENAMETOOLONG; return false; }
    memcpy(output, value, n + 1); return true;
}
static inline bool km_host_path_is_dir(const char *path) { struct stat state; return !stat(path, &state) && S_ISDIR(state.st_mode); }
static inline int km_host_mkdir(const char *path) { return mkdir(path, 0777); }
static inline int km_host_rmdir(const char *path) { return rmdir(path); }
static inline int km_host_unlink(const char *path) { return unlink(path); }
static inline int km_host_close(int fd) { return close(fd); }
static inline int km_host_chmod(int fd, const char *path, int mode) { (void)path; return fchmod(fd, (mode_t)mode); }
static inline int km_host_replace(const char *from, const char *to) { return rename(from, to); }
static inline FILE *km_host_fopen(const char *path, const char *mode) { return fopen(path, mode); }
static inline FILE *km_host_fdopen(int fd, const char *mode) { return fdopen(fd, mode); }
static inline bool km_host_fd_regular(int fd) { struct stat state; return !fstat(fd, &state) && S_ISREG(state.st_mode); }
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
static inline bool km_host_arguments(int *argc, char ***argv) { (void)argc; (void)argv; return true; }
typedef struct KmHostDirectory { DIR *directory; } KmHostDirectory;
static inline bool km_host_dir_open(KmHostDirectory *dir, const char *path) { dir->directory = opendir(path); return dir->directory != NULL; }
static inline int km_host_dir_read(KmHostDirectory *dir, char name[PATH_MAX]) {
    errno = 0; struct dirent *entry = readdir(dir->directory);
    if (!entry) return errno ? -1 : 0;
    size_t n = strlen(entry->d_name);
    if (n >= PATH_MAX) { errno = ENAMETOOLONG; return -1; }
    memcpy(name, entry->d_name, n + 1); return 1;
}
static inline void km_host_dir_close(KmHostDirectory *dir) { if (dir->directory) { closedir(dir->directory); dir->directory = NULL; } }
#endif

static inline int km_host_child(char *const args[]) { return km_host_child_impl(args, false); }
static inline int km_host_child_quiet(char *const args[]) { return km_host_child_impl(args, true); }
static inline int km_host_compile(const char *cc, const char *optimization, const char *source, const char *binary, bool quiet) {
    char *args[] = {(char *)cc, "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
#if defined(_WIN32)
        "-D_CRT_SECURE_NO_WARNINGS", "-D_CRT_NONSTDC_NO_WARNINGS",
#else
        "-pthread",
#endif
        (char *)optimization, (char *)source, "-o", (char *)binary, NULL};
    return km_host_child_impl(args, quiet);
}
static inline bool km_host_inside(const char *resolved, const char *root) {
#if defined(_WIN32)
    wchar_t *path = km_host_wide(resolved), *base = km_host_wide(root);
    if (!path || !base) { free(path); free(base); return false; }
    size_t length = wcslen(base), n = wcslen(path);
    bool match = length && n >= length && CompareStringOrdinal(path, (int)length, base, (int)length, TRUE) == CSTR_EQUAL &&
        (!path[length] || path[length] == L'/' || base[length - 1] == L'/');
    if (match) {
        path[length] = 0;
        char *ancestor = malloc(PATH_MAX);
        match = ancestor && km_host_utf8(path, ancestor, PATH_MAX) && km_host_same_file(ancestor, root);
        free(ancestor);
    }
    free(path); free(base); return match;
#else
    size_t n = strlen(root);
    return !strncmp(resolved, root, n) && (!resolved[n] || resolved[n] == '/' || (n && root[n - 1] == '/'));
#endif
}
#endif
