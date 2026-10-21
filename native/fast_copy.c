#include <stddef.h>
#include <string.h>

/* Portable implementation used when the System V x86-64 assembler is not
 * selected. Neither implementation permits overlapping memory ranges. */
void *km_fast_copy(void *destination, const void *source, size_t length) {
    if (length != 0) {
        return memcpy(destination, source, length);
    }
    return destination;
}
