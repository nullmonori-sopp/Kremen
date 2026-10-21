/* Build with native/fast_copy.S on System V x86-64, or native/fast_copy.c for
 * the portable implementation. This compares one copy primitive on the host;
 * the result is not a compiler benchmark or a portable speed guarantee. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void *km_fast_copy(void *destination, const void *source, size_t length);

typedef void *(*copy_function)(void *, const void *, size_t);

static volatile unsigned char copy_observer;

static void require(bool condition) {
    if (!condition) {
        (void)fputs("copy benchmark: correctness check failed\n", stderr);
        exit(EXIT_FAILURE);
    }
}

static void check_copy(void) {
    const size_t maximum = 1024 * 1024;
    unsigned char *source = malloc(maximum + 32);
    unsigned char *actual = malloc(maximum + 32);
    unsigned char *expected = malloc(maximum + 32);
    const size_t large_lengths[] = {1023, 4096, 65535, 1024 * 1024};
    if (source == NULL || actual == NULL || expected == NULL) {
        (void)fputs("copy benchmark: out of memory\n", stderr);
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < maximum + 32; ++i) {
        source[i] = (unsigned char)((i * 131 + 17) % 251);
    }
    require(km_fast_copy(NULL, NULL, 0) == NULL);
    for (size_t step = 0; step < 258 + sizeof large_lengths / sizeof *large_lengths; ++step) {
        size_t length = step < 258 ? step : large_lengths[step - 258];
        for (size_t source_offset = 0; source_offset < 8; ++source_offset) {
            for (size_t destination_offset = 0; destination_offset < 8; ++destination_offset) {
                size_t checked = length + 32;
                memset(actual, 0xa5, checked);
                memset(expected, 0xa5, checked);
                (void)memcpy(expected + destination_offset, source + source_offset, length);
                require(km_fast_copy(actual + destination_offset, source + source_offset, length)
                    == actual + destination_offset);
                require(memcmp(actual, expected, checked) == 0);
            }
        }
    }
    free(expected);
    free(actual);
    free(source);
}

static double measure(copy_function function, void *destination, const void *source,
                      size_t length, size_t repetitions) {
    /* A volatile function pointer keeps both copies out of line, including
     * libc memcpy, and prevents eliminating copies whose data is overwritten. */
    copy_function volatile call = function;
    clock_t start = clock();
    if (start == (clock_t)-1) {
        (void)fputs("copy benchmark: CPU timer unavailable\n", stderr);
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < repetitions; ++i) {
        (void)call(destination, source, length);
    }
    clock_t end = clock();
    if (end == (clock_t)-1 || end <= start) {
        (void)fputs("copy benchmark: insufficient CPU timer resolution\n", stderr);
        exit(EXIT_FAILURE);
    }
    copy_observer ^= ((unsigned char *)destination)[length - 1];
    return (double)(end - start) / (double)CLOCKS_PER_SEC;
}

int main(void) {
    const size_t lengths[] = {16, 64, 1024, 65536};
    const size_t target_bytes = 128 * 1024 * 1024;
    unsigned char *source;
    unsigned char *destination;
    check_copy();
    (void)puts("copy correctness: ok (16,768 aligned/unaligned non-overlapping cases)");
    source = malloc(65536);
    destination = malloc(65536);
    if (source == NULL || destination == NULL) {
        free(source);
        free(destination);
        (void)fputs("copy benchmark: out of memory\n", stderr);
        return EXIT_FAILURE;
    }
    memset(source, 0x5a, 65536);
    (void)puts("bytes\tkm_fast_copy MiB/s\tlibc memcpy MiB/s\tcopy/libc speed");
    for (size_t i = 0; i < sizeof lengths / sizeof *lengths; ++i) {
        size_t length = lengths[i];
        size_t repetitions = target_bytes / length;
        double seconds_copy;
        double seconds_libc;
        double mib = (double)(repetitions * length) / (1024.0 * 1024.0);
        /* Warm each implementation before timing. Timing order alternates to
         * reduce a persistent first/second bias; caches remain deliberately hot. */
        (void)km_fast_copy(destination, source, length);
        (void)memcpy(destination, source, length);
        if (i % 2 == 0) {
            seconds_copy = measure(km_fast_copy, destination, source, length, repetitions);
            seconds_libc = measure(memcpy, destination, source, length, repetitions);
        } else {
            seconds_libc = measure(memcpy, destination, source, length, repetitions);
            seconds_copy = measure(km_fast_copy, destination, source, length, repetitions);
        }
        (void)printf("%zu\t%.1f\t\t%.1f\t\t%.3fx\n", length,
                     mib / seconds_copy, mib / seconds_libc, seconds_libc / seconds_copy);
    }
    free(destination);
    free(source);
    return EXIT_SUCCESS;
}
