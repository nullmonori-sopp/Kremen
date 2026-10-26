#define _POSIX_C_SOURCE 200809L
#include "../../runtime/kremen_rt.h"
#include <assert.h>
#include <time.h>

static pthread_mutex_t barrier_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t barrier_condition = PTHREAD_COND_INITIALIZER;
static int arrived;
static const char *directory;
static kr_thread *foreign_handle;

static int64_t square(int64_t value) { return value * value; }
static int64_t overlap(int64_t value) {
    assert(pthread_mutex_lock(&barrier_mutex) == 0);
    arrived++;
    assert(pthread_cond_broadcast(&barrier_condition) == 0);
    while (arrived < 2) assert(pthread_cond_wait(&barrier_condition, &barrier_mutex) == 0);
    assert(pthread_mutex_unlock(&barrier_mutex) == 0);
    return value + 100;
}
static int64_t nested(int64_t value) {
    kr_thread *child; int64_t code, result;
    assert(kr_thread_spawn(square, value, &child, &code));
    assert(kr_thread_join(child, &result, &code));
    return result + 1;
}
static void announce(void *unused) { (void)unused; puts("worker cleanup"); }
static int64_t failing(int64_t value) {
    (void)value;
    kr_defer_node deferred = {0};
    kr_defer_push(&deferred, announce, NULL);
    kr_panic("worker failure", 17);
}
static void fail_cleanup(void *unused) {
    (void)unused;
    kr_panic("cleanup failure", 19);
}
static int64_t locked_failure(int64_t value) {
    (void)value;
    kr_defer_node deferred = {0};
    kr_defer_push(&deferred, announce, NULL);
    kr_output_begin();
    kr_input_begin();
    kr_panic("failure while holding I/O locks", 18);
}
static int64_t recursive_failure(int64_t value) {
    (void)value;
    kr_defer_node outer = {0}, inner = {0};
    kr_defer_push(&outer, announce, NULL);
    kr_defer_push(&inner, fail_cleanup, NULL);
    kr_panic("initial failure", 19);
}
static int64_t failing_child(int64_t value) {
    kr_thread *child; int64_t code;
    assert(kr_thread_spawn(failing, value, &child, &code));
    return 42;
}
static int64_t foreign_join(int64_t value) {
    (void)value;
    int64_t result, code;
    assert(!kr_thread_join(foreign_handle, &result, &code));
    assert(code == EBADF && result == 0);
    return 7;
}
static int64_t resource_worker(int64_t index) {
    assert(kr_defers == NULL && kr_allocations == NULL && kr_buffers == NULL && kr_files == NULL && kr_arenas == NULL);
    for (int64_t i = 0; i < 40; i++) {
        kr_slice values = kr_slice_alloc(8, sizeof(int64_t), 31);
        *(int64_t *)kr_slice_at(values, 3, sizeof(int64_t), 31) = index + i;
        assert(*(int64_t *)kr_slice_at(values, 3, sizeof(int64_t), 31) == index + i);
        kr_slice_free(values, 31);
        kr_arena *arena = kr_arena_new(32);
        kr_slice blocks = kr_arena_alloc(arena, 3, sizeof(int64_t), 32);
        *(int64_t *)kr_slice_at(blocks, 1, sizeof(int64_t), 32) = index;
        kr_arena_reset(arena, 32);
        kr_arena_free(arena, 32);
        kr_string description = kr_error_message(EINVAL);
        assert(description.len != 0);
    }
    char path[1024];
    int n = snprintf(path, sizeof path, "%s/worker-%" PRId64, directory, index);
    assert(n > 0 && (size_t)n < sizeof path);
    kr_string name = {path, (size_t)n}, mode = {"wb", 2}, data = {"owned", 5};
    kr_file *file; int64_t code, count;
    assert(kr_fs_open(name, mode, &file, &code));
    assert(kr_fs_write_all(file, data, &count, &code) && count == 5);
    /* Leave file and tracked allocation metadata for worker TLS cleanup. */
    return index + 1;
}
static int64_t output_worker(int64_t index) {
    for (int64_t i = 0; i < 80; i++) {
        kr_output_begin();
        kr_print_string((kr_string){"worker", 6}); kr_space(); kr_print_i64(index);
        kr_print_string((kr_string){" iteration ", 11}); kr_print_i64(i); kr_newline();
        kr_output_end();
    }
    return index;
}
static int64_t input_worker(int64_t unused) {
    (void)unused;
    int64_t sum = 0, value, code;
    while (kr_try_read_int(&value, &code)) sum += value;
    assert(code == 1);
    return sum;
}
static int64_t slow_worker(int64_t value) {
    struct timespec delay = {0, 50000000};
    (void)nanosleep(&delay, NULL);
    kr_output_begin(); puts("child finished"); kr_output_end();
    return value;
}
static void close_thread(void *data) { kr_thread_close(*(kr_thread **)data, 77); }

int main(int argc, char **argv) {
    assert(argc >= 2);
    kr_runtime_init();
    const char *mode = argv[1];
    kr_thread *first, *second; int64_t code, value;
    if (!strcmp(mode, "overlap")) {
        assert(kr_thread_spawn(overlap, 3, &first, &code));
        assert(kr_thread_spawn(overlap, 7, &second, &code));
        assert(!pthread_equal(first->id, second->id));
        assert(kr_thread_join(first, &value, &code) && value == 103);
        assert(kr_thread_join(second, &value, &code) && value == 107);
        assert(!kr_thread_join(first, &value, &code) && code == EBADF);
        kr_thread_close(first, 3); kr_thread_close(first, 3);
    } else if (!strcmp(mode, "nested")) {
        assert(kr_thread_spawn(nested, 8, &first, &code));
        assert(kr_thread_join(first, &value, &code) && value == 65);
    } else if (!strcmp(mode, "repeat")) {
        kr_thread *old = NULL, *oldest = NULL;
        for (int64_t index = 0; index < 100; index++) {
            assert(kr_thread_spawn(square, index, &first, &code));
            if (oldest == NULL) oldest = first;
            assert(kr_thread_join(first, &value, &code) && value == index * index);
            if (old != NULL) {
                assert(!kr_thread_join(old, &value, &code) && value == 0 && code == EBADF);
                kr_thread_close(old, 2);
            }
            old = first;
        }
        kr_thread_close(oldest, 2);
        assert(!kr_thread_join(oldest, &value, &code) && code == EBADF);
    } else if (!strcmp(mode, "resources")) {
        assert(argc == 3); directory = argv[2];
        kr_thread *workers[8];
        for (int64_t i = 0; i < 8; i++) assert(kr_thread_spawn(resource_worker, i, &workers[i], &code));
        for (int64_t i = 0; i < 8; i++) assert(kr_thread_join(workers[i], &value, &code) && value == i + 1);
        assert(kr_allocations == NULL && kr_buffers == NULL && kr_files == NULL && kr_arenas == NULL);
    } else if (!strcmp(mode, "panic")) {
        assert(kr_thread_spawn(failing, 0, &first, &code));
        assert(!kr_thread_join(first, &value, &code) && code == ECANCELED);
        kr_thread_close(first, 3);
    } else if (!strcmp(mode, "locked-panic")) {
        assert(kr_thread_spawn(locked_failure, 0, &first, &code));
        assert(!kr_thread_join(first, &value, &code) && code == ECANCELED);
        kr_output_begin(); puts("creator output remains usable"); kr_output_end();
        assert(kr_try_read_int(&value, &code) && value == 9);
    } else if (!strcmp(mode, "recursive-panic")) {
        assert(kr_thread_spawn(recursive_failure, 0, &first, &code));
        assert(!kr_thread_join(first, &value, &code) && code == ECANCELED);
    } else if (!strcmp(mode, "unobserved")) {
        assert(kr_thread_spawn(failing, 0, &first, &code));
    } else if (!strcmp(mode, "nested-panic")) {
        assert(kr_thread_spawn(failing_child, 0, &first, &code));
        assert(!kr_thread_join(first, &value, &code) && code == ECANCELED);
    } else if (!strcmp(mode, "foreign")) {
        assert(kr_thread_spawn(square, 4, &foreign_handle, &code));
        assert(kr_thread_spawn(foreign_join, 0, &first, &code));
        assert(kr_thread_join(first, &value, &code) && value == 7);
        assert(kr_thread_join(foreign_handle, &value, &code) && value == 16);
    } else if (!strcmp(mode, "output")) {
        kr_thread *workers[8];
        for (int64_t i = 0; i < 8; i++) assert(kr_thread_spawn(output_worker, i, &workers[i], &code));
        for (int64_t i = 0; i < 8; i++) assert(kr_thread_join(workers[i], &value, &code) && value == i);
    } else if (!strcmp(mode, "input")) {
        int64_t total;
        assert(kr_thread_spawn(input_worker, 0, &first, &code));
        assert(kr_thread_spawn(input_worker, 0, &second, &code));
        assert(kr_thread_join(first, &total, &code));
        assert(kr_thread_join(second, &value, &code));
        assert(total + value == 500500);
    } else if (!strcmp(mode, "implicit")) {
        assert(kr_thread_spawn(slow_worker, 9, &first, &code));
    } else if (!strcmp(mode, "main-panic")) {
        assert(kr_thread_spawn(slow_worker, 9, &first, &code));
        kr_panic("main failure", 41);
    } else if (!strcmp(mode, "deferred-panic")) {
        kr_defer_node cleanup = {0};
        assert(kr_thread_spawn(failing, 0, &first, &code));
        kr_defer_push(&cleanup, close_thread, &first);
        kr_defer_run_until(NULL);
    } else if (!strcmp(mode, "invalid")) {
        assert(!kr_thread_spawn(NULL, 0, &first, &code) && first == NULL && code == EINVAL);
        assert(!kr_thread_join(NULL, &value, &code) && value == 0 && code == EBADF);
        assert(!kr_thread_join((kr_thread *)(uintptr_t)1, &value, &code) && value == 0 && code == EBADF);
        kr_thread_count = 65536;
        assert(!kr_thread_spawn(square, 1, &first, &code) && first == NULL && code == EAGAIN);
        kr_thread_count = 0;
        kr_thread_active = 256;
        assert(!kr_thread_spawn(square, 1, &first, &code) && code == EAGAIN);
        kr_thread_active = 0;
    } else { return 2; }
    return 0;
}
