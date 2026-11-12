#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdatomic.h>
#include "../../runtime/kremen_rt.h"

static const char unicode_file[] = "data-\xcf\x80-\xf0\x9f\x8c\x8d.bin";
static const char unicode_other[] = "renamed-\xcf\x80-\xf0\x9f\x8c\x8d.bin";
static const char unicode_dir[] = "folder-\xcf\x80-\xf0\x9f\x8c\x8d";
static const char bytes[] = "a\r\nb\n\0\032z";
static atomic_int finished;
static kr_thread *foreign;
static kr_string text(const char *value) { return (kr_string){value, strlen(value)}; }
static void write_file(const char *path, kr_string value) {
    int64_t code = -1;
    assert(kr_fs_write(text(path), value, false, &code) && code == 0);
}
static void expect_file(const char *path, kr_string expected) {
    kr_string value = {0}; int64_t code = -1;
    assert(kr_fs_read(text(path), &value, &code) && code == 0 && kr_str_eq(value, expected));
}
static int64_t worker(int64_t value) {
    kr_slice data = kr_slice_alloc(4, sizeof(int64_t), 0);
    *(int64_t *)kr_slice_at(data, 0, sizeof(int64_t), 0) = value;
    atomic_fetch_add(&finished, 1);
    return value * 2;
}
static int64_t nested(int64_t value) {
    kr_thread *child = NULL; int64_t code = -1, answer = -1;
    assert(kr_thread_spawn(worker, value, &child, &code) && code == 0);
    assert(kr_thread_join(child, &answer, &code) && code == 0);
    return answer + 1;
}
static int64_t automatic(int64_t value) {
    kr_thread *child = NULL; int64_t code = -1;
    assert(kr_thread_spawn(worker, value, &child, &code) && code == 0);
    return value;
}
static int64_t check_foreign(int64_t value) {
    int64_t code = -1, answer = -1;
    assert(!kr_thread_join(foreign, &answer, &code) && code == EBADF);
    return value;
}
static int64_t failed(int64_t value) { (void)value; kr_panic("native worker failure", 0); }
static int64_t nested_failure(int64_t value) {
    kr_thread *child = NULL; int64_t code = -1;
    assert(kr_thread_spawn(failed, value, &child, &code) && code == 0);
    return value;
}
static void check_exit(void) { assert(atomic_load(&finished) == 1); }
int main(int argc, char **argv) {
    assert(argc == 2);
    const char *mode = argv[1];
    if (!strcmp(mode, "autojoin-exit")) assert(atexit(check_exit) == 0);
    kr_runtime_init();
    int64_t code = -1, value = -1;
    kr_string payload = {bytes, sizeof bytes - 1};
    if (!strcmp(mode, "unicode-file")) {
        write_file(unicode_file, payload); expect_file(unicode_file, payload);
    } else if (!strcmp(mode, "unicode-open")) {
        kr_file *file = NULL;
        assert(kr_fs_open(text(unicode_file), text("w"), &file, &code));
        assert(kr_fs_write_all(file, payload, &value, &code) && value == (int64_t)payload.len);
        assert(kr_fs_close(file, &code));
        expect_file(unicode_file, payload);
    } else if (!strcmp(mode, "unicode-rename")) {
        write_file(unicode_file, payload); write_file(unicode_other, text("old"));
        assert(kr_fs_rename(text(unicode_file), text(unicode_other), &code) && code == 0);
        expect_file(unicode_other, payload);
        assert(kr_fs_stat(text(unicode_file), 0, &value, &code) && value == 0);
    } else if (!strcmp(mode, "unicode-same-file")) {
        write_file(unicode_file, payload);
        assert(kr_fs_rename(text(unicode_file), text(unicode_file), &code) && code == 0);
        expect_file(unicode_file, payload);
    } else if (!strcmp(mode, "unicode-directory")) {
        assert(kr_fs_modify(text(unicode_dir), 0, &code) && code == 0);
        assert(kr_fs_stat(text(unicode_dir), 1, &value, &code) && code == 0 && value == 1);
        assert(kr_fs_modify(text(unicode_dir), 1, &code) && code == 0);
    } else if (!strcmp(mode, "unicode-list")) {
        write_file(unicode_file, text("")); write_file("A", text(""));
        kr_slice names;
        assert(kr_fs_list(text("."), &names, &code) && names.len == 2 && code == 0);
        assert(kr_str_eq(*(kr_string *)kr_slice_at(names, 0, sizeof(kr_string), 0), text("A")));
        assert(kr_str_eq(*(kr_string *)kr_slice_at(names, 1, sizeof(kr_string), 0), text(unicode_file)));
    } else if (!strcmp(mode, "unicode-size")) {
        write_file(unicode_file, payload);
        assert(kr_fs_stat(text(unicode_file), 2, &value, &code) && code == 0 && value == (int64_t)payload.len);
    } else if (!strcmp(mode, "unicode-missing")) {
        assert(!kr_fs_rename(text(unicode_file), text(unicode_other), &code) && code == ENOENT);
        assert(kr_fs_stat(text(unicode_file), 0, &value, &code) && code == 0 && value == 0);
    } else if (!strcmp(mode, "unicode-cwd")) {
        assert(kr_fs_modify(text(unicode_dir), 0, &code));
#if defined(_WIN32)
        wchar_t wide[128];
        assert(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, unicode_dir, -1, wide, 128) > 0);
        assert(SetCurrentDirectoryW(wide));
#else
        assert(chdir(unicode_dir) == 0);
#endif
        kr_string directory = {0};
        assert(kr_fs_cwd(&directory, &code) && code == 0);
        assert(strstr(directory.data, unicode_dir) != NULL);
    } else if (!strcmp(mode, "invalid-path")) {
        kr_string path = {"bad\0path", 8}; kr_file *file = NULL;
        assert(!kr_fs_open(path, text("r"), &file, &code) && code == EINVAL);
#if defined(_WIN32)
        path = (kr_string){"\xff.bin", 5};
        assert(!kr_fs_open(path, text("r"), &file, &code) && code == EINVAL);
#endif
    } else if (!strcmp(mode, "native-workers")) {
        kr_thread *threads[32];
        for (int i = 0; i < 32; i++) assert(kr_thread_spawn(worker, i, &threads[i], &code));
        for (int i = 0; i < 32; i++) assert(kr_thread_join(threads[i], &value, &code) && value == i * 2 && code == 0);
        assert(atomic_load(&finished) == 32);
    } else if (!strcmp(mode, "nested-workers")) {
        kr_thread *thread = NULL;
        assert(kr_thread_spawn(nested, 41, &thread, &code));
        assert(kr_thread_join(thread, &value, &code) && value == 83 && code == 0);
        assert(atomic_load(&finished) == 1);
    } else if (!strcmp(mode, "nested-cleanup")) {
        kr_thread *thread = NULL;
        assert(kr_thread_spawn(automatic, 41, &thread, &code));
        assert(kr_thread_join(thread, &value, &code) && value == 41 && code == 0);
        assert(atomic_load(&finished) == 1);
    } else if (!strcmp(mode, "foreign-owner")) {
        assert(kr_thread_spawn(worker, 4, &foreign, &code));
        kr_thread *thread = NULL;
        assert(kr_thread_spawn(check_foreign, 7, &thread, &code));
        assert(kr_thread_join(thread, &value, &code) && value == 7 && code == 0);
        assert(kr_thread_join(foreign, &value, &code) && value == 8 && code == 0);
    } else if (!strcmp(mode, "repeat-join")) {
        kr_thread *thread = NULL;
        assert(kr_thread_spawn(worker, 1, &thread, &code));
        assert(kr_thread_join(thread, &value, &code) && value == 2);
        assert(!kr_thread_join(thread, &value, &code) && code == EBADF);
    } else if (!strcmp(mode, "invalid-worker")) {
        kr_thread *thread = (kr_thread *)(uintptr_t)1;
        assert(!kr_thread_spawn(NULL, 1, &thread, &code) && code == EINVAL && thread == NULL);
    } else if (!strcmp(mode, "autojoin-exit")) {
        kr_thread *thread = NULL;
        assert(kr_thread_spawn(worker, 1, &thread, &code));
    } else if (!strcmp(mode, "nested-failure")) {
        kr_thread *thread = NULL;
        assert(kr_thread_spawn(nested_failure, 1, &thread, &code));
        assert(!kr_thread_join(thread, &value, &code) && code == ECANCELED);
    } else abort();
    return 0;
}
