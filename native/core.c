#include "kremen.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

struct ArenaBlock { ArenaBlock *next; size_t used, capacity; max_align_t alignment; unsigned char data[]; };

/* libc/compiler intrinsics win for tiny copies on the measured host. */
static void copy_bytes(void *dst, const void *src, size_t n) {
    if (n < 65536) memcpy(dst, src, n); else (void)km_fast_copy(dst, src, n);
}

void *km_alloc(Context *ctx, size_t n) {
    size_t align = _Alignof(max_align_t);
    if (n > SIZE_MAX - align) { fputs("kremen: allocation overflow\n", stderr); exit(2); }
    n = (n + align - 1) / align * align;
    if (!n) n = align;
    ArenaBlock *block = ctx->arena;
    if (!block || n > block->capacity - block->used) {
        size_t cap = n > 65536 ? n : 65536;
        if (cap > SIZE_MAX - sizeof *block || !(block = calloc(1, sizeof *block + cap))) {
            fputs("kremen: out of memory\n", stderr); exit(2);
        }
        block->capacity = cap; block->next = ctx->arena; ctx->arena = block;
    }
    void *result = block->data + block->used;
    block->used += n;
    return result;
}

char *km_strndup(Context *ctx, const char *s, size_t n) {
    char *result = km_alloc(ctx, n + 1);
    copy_bytes(result, s, n); result[n] = 0; return result;
}
char *km_strdup(Context *ctx, const char *s) { return km_strndup(ctx, s, strlen(s)); }
char *km_sprintf(Context *ctx, const char *format, ...) {
    va_list a, copy; va_start(a, format); va_copy(copy, a);
    int n = vsnprintf(NULL, 0, format, a); va_end(a);
    if (n < 0) { va_end(copy); fputs("kremen: formatting error\n", stderr); exit(2); }
    char *s = km_alloc(ctx, (size_t)n + 1); vsnprintf(s, (size_t)n + 1, format, copy); va_end(copy); return s;
}

#define PUSH_IMPL(name, T, V) \
void name(Context *ctx, V *vec, T value) { \
    if (vec->len == vec->cap) { \
        size_t cap = vec->cap ? vec->cap * 2 : 16; \
        T *data = km_alloc(ctx, cap * sizeof *data); \
        if (vec->len) copy_bytes(data, vec->data, vec->len * sizeof *data); \
        vec->data = data; vec->cap = cap; \
    } vec->data[vec->len++] = value; \
}
PUSH_IMPL(km_nodes_push, Node *, Nodes)
PUSH_IMPL(km_types_push, Type *, Types)
PUSH_IMPL(km_sources_push, Source *, Sources)
PUSH_IMPL(km_modules_push, Module *, Modules)

void km_buf_append(Buffer *buf, const char *s, size_t n) {
    if (n > SIZE_MAX - buf->len - 1) { fputs("kremen: buffer overflow\n", stderr); exit(2); }
    size_t needed = buf->len + n + 1;
    if (needed > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 1024;
        while (cap < needed) { if (cap > SIZE_MAX / 2) { cap = needed; break; } cap *= 2; }
        char *data = realloc(buf->data, cap);
        if (!data) { fputs("kremen: out of memory\n", stderr); exit(2); }
        buf->data = data; buf->cap = cap;
    }
    copy_bytes(buf->data + buf->len, s, n); buf->len += n; buf->data[buf->len] = 0;
}
void km_buf_puts(Buffer *buf, const char *s) { km_buf_append(buf, s, strlen(s)); }
void km_buf_printf(Buffer *buf, const char *format, ...) {
    va_list a, copy; va_start(a, format); va_copy(copy, a);
    char local[512];
    int n = vsnprintf(local, sizeof local, format, a); va_end(a);
    if (n < 0) { va_end(copy); fputs("kremen: formatting error\n", stderr); exit(2); }
    if ((size_t)n < sizeof local) { va_end(copy); km_buf_append(buf, local, (size_t)n); return; }
    char *s = malloc((size_t)n + 1);
    if (!s) { va_end(copy); fputs("kremen: out of memory\n", stderr); exit(2); }
    vsnprintf(s, (size_t)n + 1, format, copy); va_end(copy);
    km_buf_append(buf, s, (size_t)n); free(s);
}

_Noreturn void km_error(Context *ctx, Token tok, const char *code, const char *format, ...) {
    fprintf(stderr, "%s:%d:%d: error[%s]: ", tok.src ? tok.src->path : "<program>", tok.line ? tok.line : 1, tok.col ? tok.col : 1, code);
    va_list a; va_start(a, format); vfprintf(stderr, format, a); va_end(a); fputc('\n', stderr);
    if (tok.src && tok.offset <= tok.src->len) {
        size_t start = tok.offset, end = tok.offset;
        while (start && tok.src->data[start - 1] != '\n') start--;
        while (end < tok.src->len && tok.src->data[end] != '\n' && end - start < 200) end++;
        if (tok.offset - start > 100) start = tok.offset - 80;
        fputs("  ", stderr); fwrite(tok.src->data + start, 1, end - start, stderr); fputs("\n  ", stderr);
        for (size_t i = start; i < tok.offset; i++) fputc(tok.src->data[i] == '\t' ? '\t' : ' ', stderr);
        fputs("^\n", stderr);
    }
    ctx->errors++;
    if (ctx->failure_ready) longjmp(ctx->failure, 1);
    exit(1);
}

Node *km_node(Context *ctx, NodeKind kind, Token tok) { Node *n = km_alloc(ctx, sizeof *n); n->kind = kind; n->tok = tok; return n; }
Type *km_type(Context *ctx, TypeKind kind, Type *elem) { Type *t = km_alloc(ctx, sizeof *t); t->kind = kind; t->elem = elem; t->id = ++ctx->next_type; return t; }
Type *km_named_type(Context *ctx, const char *name, Token tok) {
    if (!strcmp(name, "void")) return ctx->t_void;
    if (!strcmp(name, "bool")) return ctx->t_bool;
    if (!strcmp(name, "string")) return ctx->t_string;
    if (!strcmp(name, "arena")) return ctx->t_arena;
    if (!strcmp(name, "file")) return ctx->t_file;
    for (size_t i = 0; i < 8; i++) if (!strcmp(name, ctx->ints[i]->name)) return ctx->ints[i];
    Type *t = km_type(ctx, TY_STRUCT, NULL); t->name = km_strdup(ctx, name); t->tok = tok; return t;
}
bool km_type_equal(Type *a, Type *b) {
    if (a == b) return true;
    if (!a || !b || a->kind != b->kind) return false;
    switch (a->kind) {
    case TY_INT: return a->bits == b->bits && a->sign == b->sign;
    case TY_STRUCT: case TY_ENUM: return a->decl && b->decl ? a->decl == b->decl : a->name && b->name && !strcmp(a->name, b->name);
    case TY_TUPLE:
        if (a->members.len != b->members.len) return false;
        for (size_t i = 0; i < a->members.len; i++) if (!km_type_equal(a->members.data[i], b->members.data[i])) return false;
        return true;
    case TY_ARRAY: return a->count == b->count && km_type_equal(a->elem, b->elem);
    case TY_REF: return a->mut == b->mut && km_type_equal(a->elem, b->elem);
    case TY_PTR: case TY_SLICE: case TY_RESULT: return km_type_equal(a->elem, b->elem);
    default: return true;
    }
}
bool km_is_int(Type *t) { return t && t->kind == TY_INT; }
bool km_is_address(Type *t) { return t && (t->kind == TY_REF || t->kind == TY_PTR); }
const char *km_type_name(Context *ctx, Type *t) {
    if (!t) return "<unknown>";
    switch (t->kind) {
    case TY_ARRAY: return km_sprintf(ctx, "[%zu]%s", t->count, km_type_name(ctx, t->elem));
    case TY_SLICE: return km_sprintf(ctx, "[]%s", km_type_name(ctx, t->elem));
    case TY_RESULT: return km_sprintf(ctx, "!%s", km_type_name(ctx, t->elem));
    case TY_REF: return km_sprintf(ctx, "&%s%s", t->mut ? "mut " : "", km_type_name(ctx, t->elem));
    case TY_PTR: return km_sprintf(ctx, "*%s", km_type_name(ctx, t->elem));
    case TY_TUPLE: {
        Buffer b = {0}; km_buf_puts(&b, "(");
        for (size_t i = 0; i < t->members.len; i++) km_buf_printf(&b, "%s%s", i ? ", " : "", km_type_name(ctx, t->members.data[i]));
        if (t->members.len == 1) km_buf_puts(&b, ",");
        km_buf_puts(&b, ")"); char *s = km_strndup(ctx, b.data, b.len); free(b.data); return s;
    }
    default: return t->name ? t->name : "void";
    }
}
void km_init(Context *ctx) {
    memset(ctx, 0, sizeof *ctx); ctx->optimization = 3; ctx->cc = "cc";
    ctx->t_void = km_type(ctx, TY_VOID, NULL); ctx->t_void->name = "void";
    ctx->t_bool = km_type(ctx, TY_BOOL, NULL); ctx->t_bool->name = "bool";
    ctx->t_string = km_type(ctx, TY_STRING, NULL); ctx->t_string->name = "string";
    ctx->t_arena = km_type(ctx, TY_ARENA, NULL); ctx->t_arena->name = "arena";
    ctx->t_file = km_type(ctx, TY_FILE, NULL); ctx->t_file->name = "file";
    static const char *names[] = {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64"};
    for (size_t i = 0; i < 8; i++) { Type *t = km_type(ctx, TY_INT, NULL); t->name = (char *)names[i]; t->bits = 8 << (i % 4); t->sign = i < 4; ctx->ints[i] = t; }
}
void km_destroy(Context *ctx) { ArenaBlock *b = ctx->arena; while (b) { ArenaBlock *next = b->next; free(b); b = next; } }

Source *km_read_source(Context *ctx, const char *path, bool header) {
    Token tok = {0};
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) km_error(ctx, tok, "E010", "cannot open %s: %s", path, strerror(errno));
    struct stat state;
    if (fstat(fd, &state) || !S_ISREG(state.st_mode)) {
        close(fd); km_error(ctx, tok, "E010", "source must be a regular file: %s", path);
    }
    FILE *f = fdopen(fd, "rb");
    if (!f) { close(fd); km_error(ctx, tok, "E010", "cannot open stream %s: %s", path, strerror(errno)); }
    if (fseek(f, 0, SEEK_END)) { fclose(f); km_error(ctx, tok, "E010", "cannot seek %s", path); }
    long n = ftell(f);
    if (n < 0 || n > 4 * 1024 * 1024) { fclose(f); km_error(ctx, tok, "E010", "source exceeds 4 MiB: %s", path); }
    rewind(f);
    Source *src = km_alloc(ctx, sizeof *src); src->path = km_strdup(ctx, path); src->len = (size_t)n; src->header = header;
    src->data = km_alloc(ctx, src->len + 1);
    size_t got = fread(src->data, 1, src->len, f); bool failed = ferror(f) != 0; fclose(f);
    if (got != src->len || failed) km_error(ctx, tok, "E010", "cannot read %s", path);
    if (memchr(src->data, 0, src->len)) km_error(ctx, tok, "E100", "NUL byte in source %s", path);
    if (ctx->sources.len >= 512) km_error(ctx, tok, "E010", "too many source files (limit 512)");
    km_sources_push(ctx, &ctx->sources, src); return src;
}
