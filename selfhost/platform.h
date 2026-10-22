#ifndef KREMEN_PLATFORM_H
#define KREMEN_PLATFORM_H
/* Small POSIX executable discovery boundary; no /proc requirement. */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static inline bool km_platform_copy_path(const char *path, char *output, size_t capacity) {
    char resolved[PATH_MAX];
    if (realpath(path, resolved) == NULL) return false;
    size_t n = strlen(resolved);
    if (n >= capacity) { errno = ENAMETOOLONG; return false; }
    memcpy(output, resolved, n + 1);
    return true;
}
static inline bool km_platform_executable(const char *argument, char *output, size_t capacity) {
    if (capacity < 2) { errno = ENAMETOOLONG; return false; }
    output[0] = 0;
#if !defined(KM_NO_PROC_SELF_EXE) && defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", output, capacity - 1);
    if (n >= 0 && (size_t)n < capacity - 1) { output[n] = 0; return true; }
#endif
#if defined(__APPLE__)
    char image[PATH_MAX];
    uint32_t n = (uint32_t)sizeof image;
    if (_NSGetExecutablePath(image, &n) == 0 && km_platform_copy_path(image, output, capacity)) return true;
#endif
    if (argument == NULL || !*argument) { output[0] = 0; errno = ENOENT; return false; }
    if (strchr(argument, '/') != NULL) {
        bool found = km_platform_copy_path(argument, output, capacity);
        if (!found) output[0] = 0;
        return found;
    }
    const char *paths = getenv("PATH");
    if (paths == NULL) paths = "/bin:/usr/bin";
    const char *part = paths;
    for (;;) {
        const char *end = strchr(part, ':');
        size_t length = end == NULL ? strlen(part) : (size_t)(end - part);
        char candidate[PATH_MAX];
        size_t name_length = strlen(argument);
        if (length < sizeof candidate && name_length < sizeof candidate - length - 1) {
            if (length != 0) { memcpy(candidate, part, length); candidate[length] = '/'; }
            memcpy(candidate + (length == 0 ? 0 : length + 1), argument, name_length + 1);
            if (access(candidate, X_OK) == 0 && km_platform_copy_path(candidate, output, capacity)) return true;
        }
        if (end == NULL) break;
        part = end + 1;
    }
    output[0] = 0; errno = ENOENT;
    return false;
}
#endif
