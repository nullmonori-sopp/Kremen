#include "../../native/kremen.h"
#include "../../selfhost/platform.h"
#include "../../selfhost/host.h"
#include "../../selfhost/runtime.h"
#include <assert.h>

static void allocation_limits(void) {
    Context *ctx = calloc(1, sizeof *ctx);
    assert(ctx != NULL);
    km_init(ctx); ctx->failure_ready = true;
    size_t before = ctx->arena_bytes;
    if (!setjmp(ctx->failure)) { km_alloc(ctx, SIZE_MAX); abort(); }
    assert(ctx->arena_bytes == before);
    if (!setjmp(ctx->failure)) { km_alloc(ctx, (size_t)513 * 1024 * 1024); abort(); }
    assert(ctx->arena_bytes == before);
    if (!setjmp(ctx->failure)) { km_strndup(ctx, "", SIZE_MAX); abort(); }
    Nodes nodes = {NULL, SIZE_MAX, SIZE_MAX};
    if (!setjmp(ctx->failure)) { km_nodes_push(ctx, &nodes, NULL); abort(); }
    assert(ctx->errors == 4);
    km_destroy(ctx); km_destroy(ctx);
    assert(ctx->arena == NULL && ctx->arena_bytes == 0);
    free(ctx);
}
int main(int argc, char **argv) {
    rt_init(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "limits")) { allocation_limits(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "count")) {
        assert(argc == 514);
        for (int i = 2; i < argc; i++) assert(!strcmp(argv[i], "literal $; ' argument"));
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "argv")) {
        assert(host_exec_begin(host_executable()) == 0);
        assert(host_exec_arg("count") == 0);
        for (int i = 0; i < 512; i++) assert(host_exec_arg("literal $; ' argument") == 0);
        assert(host_exec_run() == 0);
        assert(host_exec_arg("without begin") != 0);
        assert(host_exec_begin("/nonexistent/kremen-041-command") == 0);
        assert(host_exec_run() == 127);
        rt_cleanup(); return 0;
    }
    char path[PATH_MAX], tiny[1];
    assert(km_platform_executable(argv[0], path, sizeof path));
    assert(!km_platform_executable(argv[0], tiny, sizeof tiny));
    puts(path);
    return 0;
}
