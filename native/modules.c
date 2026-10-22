#include "kremen.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

static bool suffix(const char *s, const char *ending) { size_t a = strlen(s), b = strlen(ending); return a >= b && !strcmp(s + a - b, ending); }
static bool exists(const char *path) { struct stat st; return stat(path, &st) == 0; }
static bool is_dir(const char *path) { struct stat st; return stat(path, &st) == 0 && S_ISDIR(st.st_mode); }
static char *parent(Context *ctx, const char *path) { const char *last = strrchr(path, '/'); return last ? km_strndup(ctx, path, last == path ? 1 : (size_t)(last - path)) : km_strdup(ctx, "."); }

char *km_resolve(Context *ctx, const char *from, const char *requested) {
    Token tok = ctx->import_token;
    if (!requested[0] || requested[0] == '/') km_error(ctx, tok, "E013", "imports require a relative local path: %s", requested);
    char *dir = is_dir(from) ? km_strdup(ctx, from) : parent(ctx, from);
    char *candidate = km_sprintf(ctx, "%s/%s", dir, requested);
    if (suffix(candidate, ".krh")) km_error(ctx, tok, "E013", "import the implementation .kr, its .krh is loaded automatically");
    if (!exists(candidate) && !suffix(candidate, ".kr")) candidate = km_sprintf(ctx, "%s.kr", candidate);
    char resolved[PATH_MAX];
    if (!realpath(candidate, resolved)) km_error(ctx, tok, "E013", "cannot resolve import '%s' relative to %s: %s", requested, from, strerror(errno));
    if (ctx->project_root) {
        size_t n = strlen(ctx->project_root);
        if (strncmp(resolved, ctx->project_root, n) || (resolved[n] && resolved[n] != '/' && !(n == 1 && ctx->project_root[0] == '/')))
            km_error(ctx, tok, "E013", "import escapes project root: %s (use --root to select a wider project)", requested);
    }
    return km_strdup(ctx, resolved);
}

static void load_source(Context *ctx, Module *module, const char *path, bool header) {
    Source *src = km_read_source(ctx, path, header); src->module = module;
    km_sources_push(ctx, &module->sources, src);
    Nodes decls = km_parse(ctx, src);
    for (size_t i = 0; i < decls.len; i++) {
        Node *n = decls.data[i]; n->module = module;
        if (n->kind != N_IMPORT) { km_nodes_push(ctx, &module->decls, n); km_nodes_push(ctx, &ctx->decls, n); }
    }
    for (size_t i = 0; i < decls.len; i++) {
        Node *n = decls.data[i];
        if (n->kind != N_IMPORT) continue;
        if (!n->text || strlen(n->text) != n->text_len) km_error(ctx, n->tok, "E013", "invalid import path");
        Token previous_origin = ctx->import_token; ctx->import_token = n->tok;
        char *resolved = km_resolve(ctx, path, n->text);
        Module *target = km_load(ctx, resolved);
        ctx->import_token = previous_origin;
        const char *alias = n->name ? n->name : target->name;
        for (Import *old = module->imports; old; old = old->next) {
            if (!strcmp(old->alias, alias)) {
                if (old->target != target) km_error(ctx, n->tok, "E013", "duplicate import alias '%s'", alias);
                target = NULL; break;
            }
        }
        if (!target) continue;
        Import *item = km_alloc(ctx, sizeof *item); item->alias = km_strdup(ctx, alias); item->target = target; item->tok = n->tok;
        item->next = module->imports; module->imports = item;
    }
}

static void load_implementation(Context *ctx, Module *module, const char *path) {
    size_t n = strlen(path);
    if (!suffix(path, ".kr")) { Token tok = {0}; km_error(ctx, tok, "E012", "source must have extension .kr: %s", path); }
    char *head = km_sprintf(ctx, "%.*s.krh", (int)(n - 3), path);
    if (exists(head)) load_source(ctx, module, head, true);
    load_source(ctx, module, path, false);
}

static int compare_names(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

Module *km_load(Context *ctx, const char *path) {
    Token tok = ctx->import_token;
    Source missing = {.path = (char *)path};
    if (!tok.src) tok = (Token){.src = &missing, .line = 1, .col = 1};
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) km_error(ctx, tok, "E010", "cannot resolve %s: %s", path, strerror(errno));
    for (size_t i = 0; i < ctx->modules.len; i++) {
        Module *m = ctx->modules.data[i];
        if (!strcmp(m->path, resolved)) {
            if (m->loading) km_error(ctx, tok, "E014", "import cycle involving %s", resolved);
            return m;
        }
    }
    if (ctx->modules.len >= 128) km_error(ctx, tok, "E014", "too many modules (limit 128)");
    Module *m = km_alloc(ctx, sizeof *m); m->path = km_strdup(ctx, resolved); m->loading = true;
    const char *base = strrchr(resolved, '/'); base = base ? base + 1 : resolved;
    m->name = km_strndup(ctx, base, strlen(base) - (suffix(base, ".kr") ? 3 : 0));
    m->cname = km_sprintf(ctx, "m%zu", ctx->modules.len);
    km_modules_push(ctx, &ctx->modules, m);
    if (!ctx->project_root) ctx->project_root = is_dir(resolved) ? km_strdup(ctx, resolved) : parent(ctx, resolved);
    if (is_dir(resolved)) {
        DIR *dir = opendir(resolved);
        if (!dir) km_error(ctx, tok, "E010", "cannot list %s", resolved);
        char **files = km_alloc(ctx, 512 * sizeof *files); size_t count = 0;
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (!suffix(entry->d_name, ".kr")) continue;
            if (count == 512) { closedir(dir); km_error(ctx, tok, "E010", "too many files in module"); }
            files[count++] = km_sprintf(ctx, "%s/%s", resolved, entry->d_name);
        }
        closedir(dir); qsort(files, count, sizeof *files, compare_names);
        if (!count) km_error(ctx, tok, "E012", "module directory has no .kr sources: %s", resolved);
        for (size_t i = 0; i < count; i++) load_implementation(ctx, m, files[i]);
    } else load_implementation(ctx, m, resolved);
    m->loading = false; m->loaded = true;
    return m;
}
