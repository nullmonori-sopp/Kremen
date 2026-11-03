#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include "../../runtime/kremen_rt.h"

static kr_string text(const char *value) { return (kr_string){value, strlen(value)}; }
static const char payload[] = "first\r\nsecond\n\032\0last";

static void write_path(const char *path, kr_string value, bool append) {
    int64_t code = -1;
    assert(kr_fs_write(text(path), value, append, &code) && code == 0);
}
static void expect_path(const char *path, kr_string expected) {
    kr_string value = {0}; int64_t code = -1;
    assert(kr_fs_read(text(path), &value, &code) && code == 0 && kr_str_eq(value, expected));
}
static void close_file(kr_file *file) {
    int64_t code = -1; assert(kr_fs_close(file, &code) && code == 0);
}
static void mode_case(const char *mode) {
    kr_file *file = NULL; int64_t code = -1, count = -1;
    kr_string bytes = {payload, sizeof payload - 1};
    assert(kr_fs_open(text("modes.bin"), text(mode), &file, &code) && code == 0);
    assert(kr_fs_write_all(file, bytes, &count, &code) && code == 0 && count == (int64_t)bytes.len);
    close_file(file);
    expect_path("modes.bin", bytes);
    assert(kr_fs_open(text("modes.bin"), text("a"), &file, &code));
    assert(kr_fs_write_all(file, text("!"), &count, &code) && count == 1 && code == 0);
    close_file(file);
    assert(kr_fs_open(text("modes.bin"), text("r"), &file, &code));
    kr_string read = {0};
    assert(kr_fs_read_all(file, &read, &code) && code == 0 && read.len == bytes.len + 1);
    assert(memcmp(read.data, payload, bytes.len) == 0 && read.data[bytes.len] == '!');
    close_file(file);
}
static int64_t worker(int64_t value) { return value * value; }
int main(int argc, char **argv) {
    assert(argc == 2);
    kr_runtime_init();
    const char *mode = argv[1]; int64_t code = -1;
    if (!strcmp(mode, "sticky-write")) {
        write_path("readonly.bin", text("unchanged"), false);
        kr_file *file = NULL;
        assert(kr_fs_open(text("readonly.bin"), text("r"), &file, &code));
        assert(fputc('x', file->stream) == EOF && ferror(file->stream));
        int64_t count = -99;
        assert(!kr_fs_write_all(file, text(""), &count, &code) && code > 0 && count == -99);
        /* Sticky errors must remain visible rather than being cleared to
         * manufacture success for the next operation. */
        assert(ferror(file->stream));
        close_file(file);
        expect_path("readonly.bin", text("unchanged"));
    } else if (!strcmp(mode, "modes-short")) mode_case("w");
    else if (!strcmp(mode, "modes-binary")) mode_case("wb");
    else if (!strcmp(mode, "write-append-bytes")) {
        kr_string bytes = {payload, sizeof payload - 1};
        write_path("bytes.bin", bytes, false); write_path("bytes.bin", text("!"), true);
        kr_string read = {0};
        assert(kr_fs_read(text("bytes.bin"), &read, &code) && read.len == bytes.len + 1);
        assert(memcmp(read.data, payload, bytes.len) == 0 && read.data[bytes.len] == '!');
    } else if (!strcmp(mode, "rename-replace")) {
        write_path("from.bin", text("replacement"), false); write_path("to.bin", text("old"), false);
        assert(kr_fs_rename(text("from.bin"), text("to.bin"), &code) && code == 0);
        expect_path("to.bin", text("replacement"));
        int64_t exists = -1;
        assert(kr_fs_stat(text("from.bin"), 0, &exists, &code) && exists == 0 && code == 0);
    } else if (!strcmp(mode, "rename-same")) {
        write_path("same.bin", text("retained"), false);
        assert(kr_fs_rename(text("same.bin"), text("same.bin"), &code) && code == 0);
        expect_path("same.bin", text("retained"));
    } else if (!strcmp(mode, "rename-missing")) {
        write_path("kept.bin", text("retained"), false);
        assert(!kr_fs_rename(text("absent.bin"), text("kept.bin"), &code) && code == ENOENT);
        expect_path("kept.bin", text("retained"));
    } else if (!strcmp(mode, "directory-lifecycle")) {
        assert(kr_fs_modify(text("folder with spaces"), 0, &code) && code == 0);
        int64_t directory = -1;
        assert(kr_fs_stat(text("folder with spaces"), 1, &directory, &code) && directory == 1 && code == 0);
        assert(kr_fs_modify(text("folder with spaces"), 1, &code) && code == 0);
        assert(kr_fs_stat(text("folder with spaces"), 0, &directory, &code) && directory == 0 && code == 0);
    } else if (!strcmp(mode, "directory-nonempty")) {
        assert(kr_fs_modify(text("nonempty"), 0, &code));
        write_path("nonempty/inside", text("retained"), false);
        assert(!kr_fs_modify(text("nonempty"), 1, &code) && code > 0);
        expect_path("nonempty/inside", text("retained"));
    } else if (!strcmp(mode, "sorted-list")) {
        assert(kr_fs_modify(text("listing"), 0, &code));
        write_path("listing/z", text(""), false); write_path("listing/a", text(""), false);
        kr_slice names;
        assert(kr_fs_list(text("listing"), &names, &code) && code == 0 && names.len == 2);
        assert(kr_str_eq(*(kr_string *)kr_slice_at(names, 0, sizeof(kr_string), 0), text("a")));
        assert(kr_str_eq(*(kr_string *)kr_slice_at(names, 1, sizeof(kr_string), 0), text("z")));
        kr_slice_free(names, 0);
    } else if (!strcmp(mode, "invalid-path-mode")) {
        kr_string nul = {"part\0other", 10}; kr_file *file = NULL;
        assert(!kr_fs_open(nul, text("w"), &file, &code) && code == EINVAL);
        assert(!kr_fs_open(text("invalid.bin"), text("w+"), &file, &code) && code == EINVAL);
    } else if (!strcmp(mode, "thread-join")) {
        kr_thread *thread = NULL; int64_t result = -1;
        assert(kr_thread_spawn(worker, 9, &thread, &code) && code == 0);
        assert(kr_thread_join(thread, &result, &code) && code == 0 && result == 81);
    } else abort();
    return 0;
}
