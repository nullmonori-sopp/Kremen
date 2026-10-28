#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reproducible mutations, not a replacement for a coverage-guided fuzzer.
 * Keep explicit lengths so inserted NUL and invalid UTF-8 bytes reach the CLI. */
static uint32_t state = UINT32_C(0x4b524d45);
static uint32_t next_number(void) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    static const char *const seeds[] = {
        "func main() { var number = 7; println(number) }\n",
        "type Box struct { value i64 }\nfunc (b Box) next() Box { return Box{value: b.value + 1} }\nfunc main() { var b = Box{value: 1}; println(b.next().next().value) }\n",
        "func read() !i64 { return ok(readIntResult()?) }\nfunc main() { var value = read() catch code => -code; println(value) }\n",
        "type Choice enum { Left(value i64); Right }\nfunc main() { var x = Choice.Left(3); match x { Left(value) => { println(value) }; Right => { println(0) } } }\n",
        "func main() { unsafe { var values = alloc[i64](2); defer free(values); values[1] = 4; println(values[1]) } }\n",
        "func main() { for var i = 0; i < 3; i += 1 { println(i) } }\n",
        "func main() { var tuple = (7, true); var (value, _) = tuple; println(value) }\n",
        "func main() { var input = readIntResult(); match input { ok(value) => { println(value) }; fail(code) => { println(code) } } }\n"
    };
    static const unsigned char bytes[] = {'{','}', '(', ')', '[', ']', ';', '"', '\\', '/', '*', '&', '?', '=', '<', '>', '\n', '\r', '\t', ' ', 'a', '0', '9', 0, 0x1b, 0x7f, 0x9b, 0xff};
    for (unsigned case_id = 0; case_id < 128; case_id++) {
        unsigned char data[4096];
        const char *seed = seeds[case_id % (sizeof seeds / sizeof *seeds)];
        size_t length = strlen(seed); memcpy(data, seed, length);
        if (case_id >= sizeof seeds / sizeof *seeds) {
            unsigned edits = 1 + next_number() % 6;
            for (unsigned edit = 0; edit < edits; edit++) {
                unsigned action = next_number() % 4;
                size_t position = (size_t)next_number() % (length + 1);
                unsigned char byte = bytes[next_number() % (sizeof bytes / sizeof *bytes)];
                if (action == 0 && length && position < length) {
                    memmove(data + position, data + position + 1, length - position - 1); length--;
                } else if (action == 1 && length < sizeof data) {
                    memmove(data + position + 1, data + position, length - position); data[position] = byte; length++;
                } else if (action == 2 && position < length) data[position] = byte;
                else length = position;
            }
        }
        char path[4096];
        int count = snprintf(path, sizeof path, "%s/case-%03u.kr", argv[1], case_id);
        if (count < 0 || (size_t)count >= sizeof path) return 2;
        FILE *file = fopen(path, "wb");
        if (!file) return 2;
        int failed = fwrite(data, 1, length, file) != length;
        if (fclose(file)) failed = 1;
        if (failed) return 2;
    }
    return 0;
}
