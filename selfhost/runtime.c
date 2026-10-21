#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct Allocation {
    struct Allocation *next;
    char data[];
} Allocation;

static Allocation *allocations;
static size_t allocated;
static int argument_count;
static char **argument_values;
typedef struct SourceIdentity {
    struct SourceIdentity *next;
    dev_t device;
    ino_t inode;
} SourceIdentity;
static SourceIdentity *source_identities;
// Language strings are immutable. The lexer repeatedly indexes the same input,
// so remember its length instead of scanning the whole input for every byte.
static const char *last_string;
static size_t last_string_length;

void rt_init(int argc, char **argv) {
    argument_count = argc;
    argument_values = argv;
}

void rt_cleanup(void) {
    while (source_identities != NULL) {
        SourceIdentity *next = source_identities->next;
        free(source_identities);
        source_identities = next;
    }
    while (allocations != NULL) {
        Allocation *next = allocations->next;
        free(allocations);
        allocations = next;
    }
    allocated = 0;
    last_string = NULL;
    last_string_length = 0;
}

int64_t rt_argc(void) { return argument_count; }

int64_t rt_error(const char *path, int64_t line, int64_t column, const char *message) {
    fprintf(stderr, "%s:%" PRId64 ":%" PRId64 ": error: %s\n", path, line, column, message);
    rt_cleanup();
    exit(1);
}

static char *allocate(size_t length) {
    const size_t limit = (size_t)512 * 1024 * 1024;
    if (length > limit - sizeof(Allocation) - 1 ||
        allocated > limit - sizeof(Allocation) - 1 - length) {
        (void)rt_error("<bootstrap>", 1, 1, "512 MiB string arena limit exceeded");
    }
    Allocation *item = malloc(sizeof(Allocation) + length + 1);
    if (item == NULL) {
        (void)rt_error("<bootstrap>", 1, 1, "out of memory");
    }
    item->next = allocations;
    allocations = item;
    allocated += sizeof(Allocation) + length + 1;
    item->data[length] = '\0';
    return item->data;
}

int64_t rt_len(const char *value) {
    if (value != last_string) {
        last_string = value;
        last_string_length = strlen(value);
    }
    return (int64_t)last_string_length;
}

int64_t rt_at(const char *value, int64_t index) {
    if (index < 0 || index >= rt_len(value)) {
        return -1;
    }
    return (unsigned char)value[index];
}

const char *rt_slice(const char *value, int64_t start, int64_t end) {
    if (start < 0 || end < start || end > rt_len(value)) {
        (void)rt_error("<bootstrap>", 1, 1, "string slice outside bounds");
    }
    size_t length = (size_t)(end - start);
    char *result = allocate(length);
    memcpy(result, value + start, length);
    return result;
}

const char *rt_cat(const char *left, const char *right) {
    size_t a = strlen(left);
    size_t b = strlen(right);
    if (a > SIZE_MAX - b) {
        (void)rt_error("<bootstrap>", 1, 1, "string length overflow");
    }
    char *result = allocate(a + b);
    memcpy(result, left, a);
    memcpy(result + a, right, b);
    return result;
}

bool rt_equal(const char *left, const char *right) {
    return strcmp(left, right) == 0;
}

const char *rt_number(int64_t value) {
    char buffer[32];
    int length = snprintf(buffer, sizeof(buffer), "%" PRId64, value);
    if (length < 0 || (size_t)length >= sizeof(buffer)) {
        (void)rt_error("<bootstrap>", 1, 1, "cannot format integer");
    }
    char *result = allocate((size_t)length);
    memcpy(result, buffer, (size_t)length);
    return result;
}

const char *rt_arg(int64_t index) {
    if (index < 0 || index >= argument_count) {
        return "";
    }
    return argument_values[index];
}

const char *rt_read(const char *path) {
    /* Opening a FIFO for reading can block before fstat gets to reject it. */
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        (void)rt_error(path, 1, 1, strerror(errno));
    }
    struct stat state;
    if (fstat(fd, &state) != 0 || !S_ISREG(state.st_mode)) {
        (void)close(fd);
        (void)rt_error(path, 1, 1, "input must be a regular file of at most 8 MiB");
    }
    FILE *file = fdopen(fd, "rb");
    if (file == NULL) { (void)close(fd); (void)rt_error(path, 1, 1, "cannot open input stream"); }
    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        (void)rt_error(path, 1, 1, "cannot seek input file");
    }
    long size = ftell(file);
    if (size < 0 || size > 8 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        (void)rt_error(path, 1, 1, "input must be a regular file of at most 8 MiB");
    }
    char *result = allocate((size_t)size);
    if (fread(result, 1, (size_t)size, file) != (size_t)size) {
        (void)fclose(file);
        (void)rt_error(path, 1, 1, "cannot read input file");
    }
    if (fclose(file) != 0) {
        (void)rt_error(path, 1, 1, "cannot close input file");
    }
    if (memchr(result, '\0', (size_t)size) != NULL) {
        (void)rt_error(path, 1, 1, "NUL bytes are not allowed in source files");
    }
    SourceIdentity *identity = malloc(sizeof *identity);
    if (identity == NULL) (void)rt_error(path, 1, 1, "out of memory");
    identity->device = state.st_dev; identity->inode = state.st_ino;
    identity->next = source_identities; source_identities = identity;
    return result;
}

int64_t rt_write(const char *path, const char *value) {
    struct stat state;
    bool exists = stat(path, &state) == 0;
    if (exists) {
        for (SourceIdentity *input = source_identities; input != NULL; input = input->next) {
            if (input->device == state.st_dev && input->inode == state.st_ino)
                return rt_error(path, 1, 1, "output would overwrite a source file");
        }
    }
    size_t n = strlen(path);
    if (n > SIZE_MAX - 32) return rt_error(path, 1, 1, "output path too long");
    char *temporary = allocate(n + 32);
    snprintf(temporary, n + 32, "%s.kremen-XXXXXX", path);
    int fd = mkstemp(temporary);
    if (fd < 0) return rt_error(path, 1, 1, strerror(errno));
    if (exists && fchmod(fd, state.st_mode & 0777) != 0) {
        close(fd); unlink(temporary); return rt_error(path, 1, 1, "cannot preserve output permissions");
    }
    FILE *file = fdopen(fd, "wb");
    if (file == NULL) { close(fd); unlink(temporary); return rt_error(path, 1, 1, "cannot open temporary output"); }
    size_t length = strlen(value);
    bool failed = fwrite(value, 1, length, file) != length;
    if (fclose(file) != 0) {
        failed = true;
    }
    if (failed) {
        unlink(temporary);
        return rt_error(path, 1, 1, "cannot write output file");
    }
    if (rename(temporary, path) != 0) {
        int code = errno; unlink(temporary); return rt_error(path, 1, 1, strerror(code));
    }
    return 0;
}
