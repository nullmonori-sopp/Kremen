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
    const size_t limit = (size_t)512 * 1024 * 1024;
    if (n > SIZE_MAX - align) km_error(ctx, (Token){0}, "E011", "compiler allocation size overflow");
    n = (n + align - 1) / align * align;
    if (!n) n = align;
    ArenaBlock *block = ctx->arena;
    if (!block || n > block->capacity - block->used) {
        size_t cap = n > 65536 ? n : 65536;
        if (cap > limit - sizeof *block || ctx->arena_bytes > limit - sizeof *block - cap)
            km_error(ctx, (Token){0}, "E011", "compiler arena exceeds 512 MiB");
        block = calloc(1, sizeof *block + cap);
        if (!block) km_error(ctx, (Token){0}, "E011", "compiler out of memory");
        ctx->arena_bytes += sizeof *block + cap;
        block->capacity = cap; block->next = ctx->arena; ctx->arena = block;
    }
    void *result = block->data + block->used;
    block->used += n;
    return result;
}

char *km_strndup(Context *ctx, const char *s, size_t n) {
    if (n == SIZE_MAX) km_error(ctx, (Token){0}, "E011", "compiler string size overflow");
    char *result = km_alloc(ctx, n + 1);
    copy_bytes(result, s, n); result[n] = 0; return result;
}
char *km_strdup(Context *ctx, const char *s) { return km_strndup(ctx, s, strlen(s)); }
char *km_sprintf(Context *ctx, const char *format, ...) {
    va_list a, copy; va_start(a, format); va_copy(copy, a);
    char local[512];
    int n = vsnprintf(local, sizeof local, format, a); va_end(a);
    if (n < 0) { va_end(copy); fputs("kremen: formatting error\n", stderr); exit(2); }
    if ((size_t)n < sizeof local) { va_end(copy); return km_strndup(ctx, local, (size_t)n); }
    char *s = km_alloc(ctx, (size_t)n + 1); vsnprintf(s, (size_t)n + 1, format, copy); va_end(copy); return s;
}

#define PUSH_IMPL(name, T, V) \
void name(Context *ctx, V *vec, T value) { \
    if (vec->len == vec->cap) { \
        if (vec->cap > SIZE_MAX / 2) km_error(ctx, (Token){0}, "E011", "compiler vector size overflow"); \
        size_t cap = vec->cap ? vec->cap * 2 : 16; \
        if (cap > SIZE_MAX / sizeof(T)) km_error(ctx, (Token){0}, "E011", "compiler vector size overflow"); \
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

/* Names are ASCII identifiers. A fixed workspace prevents a misspelled name
 * from turning error reporting into an unbounded allocation or quadratic job. */
unsigned km_name_distance(const char *left, const char *right) {
    size_t a = 0, b = 0;
    while (a <= 64 && left[a]) a++;
    while (b <= 64 && right[b]) b++;
    if (a > 64 || b > 64 || a > b + 2 || b > a + 2) return 3;
    unsigned before[65], previous[65], current[65];
    for (size_t j = 0; j <= b; j++) before[j] = previous[j] = (unsigned)j;
    for (size_t i = 1; i <= a; i++) {
        current[0] = (unsigned)i;
        for (size_t j = 1; j <= b; j++) {
            unsigned value = previous[j - 1] + (left[i - 1] != right[j - 1]);
            if (previous[j] + 1 < value) value = previous[j] + 1;
            if (current[j - 1] + 1 < value) value = current[j - 1] + 1;
            if (i > 1 && j > 1 && left[i - 1] == right[j - 2] && left[i - 2] == right[j - 1] && before[j - 2] + 1 < value)
                value = before[j - 2] + 1;
            current[j] = value;
        }
        memcpy(before, previous, (b + 1) * sizeof *before);
        memcpy(previous, current, (b + 1) * sizeof *previous);
    }
    return previous[b] > 2 ? 3 : previous[b];
}

/* Validate one scalar without consulting the process locale. */
static size_t diagnostic_utf8(const unsigned char *data, size_t available) {
    if (!available) return 0;
    unsigned char first = data[0];
    size_t length = first < 128 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 :
                    first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (!length || length > available) return 0;
    for (size_t i = 1; i < length; i++) if ((data[i] & 0xc0) != 0x80) return 0;
    if ((first == 0xe0 && data[1] < 0xa0) || (first == 0xed && data[1] >= 0xa0) ||
        (first == 0xf0 && data[1] < 0x90) || (first == 0xf4 && data[1] >= 0x90)) return 0;
    return length;
}

static uint32_t diagnostic_scalar(const unsigned char *data, size_t length) {
    uint32_t scalar = length == 1 ? data[0] : data[0] & (length == 2 ? 0x1fu : length == 3 ? 0x0fu : 0x07u);
    for (size_t i = 1; i < length; i++) scalar = (scalar << 6) | (data[i] & 0x3fu);
    return scalar;
}

static bool diagnostic_control(uint32_t scalar) {
    return scalar < 32 || (scalar >= 0x7f && scalar <= 0x9f) || scalar == 0x061c ||
        scalar == 0x200e || scalar == 0x200f || (scalar >= 0x2028 && scalar <= 0x202e) ||
        (scalar >= 0x2066 && scalar <= 0x2069);
}

/* File names and error messages are untrusted terminal text. Keep ordinary
 * UTF-8 readable, but make controls, bidi directives and invalid bytes visible. */
void km_diagnostic_text(FILE *output, const char *value) {
    const unsigned char *p = (const unsigned char *)value;
    size_t remaining = strlen(value);
    while (remaining) {
        size_t length = diagnostic_utf8(p, remaining);
        if (!length) { fprintf(output, "\\x%02x", (unsigned)*p); p++; remaining--; continue; }
        uint32_t scalar = diagnostic_scalar(p, length);
        if (diagnostic_control(scalar)) {
            if (scalar < 128) fprintf(output, "\\x%02x", (unsigned)scalar);
            else fprintf(output, "\\u%04x", (unsigned)scalar);
        } else fwrite(p, 1, length, output);
        p += length; remaining -= length;
    }
}

static void diagnostic_json_string(const char *value) {
    if (!value) { fputs("null", stderr); return; }
    const unsigned char *p = (const unsigned char *)value;
    size_t remaining = strlen(value);
    fputc('"', stderr);
    while (remaining) {
        unsigned char byte = *p;
        if (byte == '"' || byte == '\\') { fputc('\\', stderr); fputc(byte, stderr); }
        else if (byte < 32 || byte == 127) fprintf(stderr, "\\u%04x", (unsigned)byte);
        else if (byte >= 128) {
            size_t length = diagnostic_utf8(p, remaining);
            if (length) {
                uint32_t scalar = diagnostic_scalar(p, length);
                if (diagnostic_control(scalar)) fprintf(stderr, "\\u%04x", (unsigned)scalar);
                else fwrite(p, 1, length, stderr);
                p += length; remaining -= length; continue;
            }
            fprintf(stderr, "\\u%04x", (unsigned)byte);
        } else fputc(byte, stderr);
        p++; remaining--;
    }
    fputc('"', stderr);
}

static void diagnostic_end(Token tok, int *line, int *column, size_t *length) {
    *line = tok.line > 0 ? tok.line : 1;
    *column = tok.col > 0 ? tok.col : 1;
    *length = 0;
    if (!tok.src || !tok.src->data || tok.offset > tok.src->len) return;
    size_t available = tok.src->len - tok.offset;
    *length = tok.len < available ? tok.len : available;
    for (size_t i = 0; i < *length; i++) {
        unsigned char byte = (unsigned char)tok.src->data[tok.offset + i];
        if (byte == '\r') {
            if (i + 1 < *length && tok.src->data[tok.offset + i + 1] == '\n') i++;
            (*line)++; *column = 1;
        } else if (byte == '\n') { (*line)++; *column = 1; }
        else (*column)++;
    }
}

static void diagnostic_span(Token, DiagnosticSpan *, bool);

static void diagnostic_json_location(Token tok) {
    int end_line, end_column; size_t length;
    diagnostic_end(tok, &end_line, &end_column, &length);
    fputs("\"file\":", stderr); diagnostic_json_string(tok.src ? tok.src->path : "<program>");
    fprintf(stderr, ",\"line\":%d,\"column\":%d,\"end_line\":%d,\"end_column\":%d,\"offset\":%zu,\"length\":%zu",
            tok.line > 0 ? tok.line : 1, tok.col > 0 ? tok.col : 1, end_line, end_column, tok.offset, length);
    DiagnosticSpan span; diagnostic_span(tok, &span, true);
    fputs(",\"excerpt\":", stderr); diagnostic_json_string(span.text);
    fprintf(stderr, ",\"caret\":%u,\"underline_width\":%u", span.caret, span.width);
}

static unsigned diagnostic_digits(int number) {
    unsigned digits = 1;
    while (number >= 10) { number /= 10; digits++; }
    return digits;
}

static void diagnostic_source_line(Token tok, unsigned digits) {
    DiagnosticSpan span; km_diagnostic_span(tok, &span);
    fprintf(stderr, " %*d | %s\n", (int)digits, tok.line > 0 ? tok.line : 1, span.text);
}

static void diagnostic_text_location(Token tok, bool context) {
    if (!tok.src || !tok.src->data || tok.offset > tok.src->len) return;
    size_t line_start = tok.offset;
    while (line_start && tok.src->data[line_start - 1] != '\n' && tok.src->data[line_start - 1] != '\r') line_start--;
    size_t next = tok.offset;
    while (next < tok.src->len && tok.src->data[next] != '\n' && tok.src->data[next] != '\r') next++;
    if (next < tok.src->len && tok.src->data[next] == '\r') next++;
    if (next < tok.src->len && tok.src->data[next] == '\n') next++;
    int number = tok.line > 0 ? tok.line : 1;
    unsigned digits = diagnostic_digits(number < INT_MAX && next < tok.src->len ? number + 1 : number);
    fprintf(stderr, " %*s |\n", (int)digits, "");
    if (context && number > 1 && line_start) {
        size_t previous_end = line_start;
        if (previous_end && tok.src->data[previous_end - 1] == '\n') previous_end--;
        if (previous_end && tok.src->data[previous_end - 1] == '\r') previous_end--;
        size_t previous = previous_end;
        /* A context line is informational. Bound scanning as well as output. */
        while (previous && previous_end - previous < 160 && tok.src->data[previous - 1] != '\n' && tok.src->data[previous - 1] != '\r') previous--;
        Token before = {.src = tok.src, .offset = previous, .line = number - 1, .col = 1};
        diagnostic_source_line(before, digits);
    }
    diagnostic_source_line(tok, digits);
    DiagnosticSpan span; km_diagnostic_span(tok, &span);
    fprintf(stderr, " %*s | ", (int)digits, "");
    for (unsigned i = 0; i < span.caret; i++) fputc(' ', stderr);
    fputc('^', stderr);
    for (unsigned i = 1; i < span.width; i++) fputc('~', stderr);
    fputc('\n', stderr);
    if (context && next < tok.src->len && number < INT_MAX) {
        Token after = {.src = tok.src, .offset = next, .line = number + 1, .col = 1};
        diagnostic_source_line(after, digits);
    }
}

static const char *diagnostic_hint(Context *ctx, const char *code, const char *format) {
    if (ctx->diagnostic_hint) return ctx->diagnostic_hint;
    if (strstr(format, "type mismatch")) return "Check the value and its declared type; Kremen does not convert types implicitly.";
    if (!strcmp(code, "E107")) return "Check the spelling, declaration scope, and any import qualifier.";
    if (!strcmp(code, "E131")) return "A shared reference allows reads only. A loan lasts until the end of its lexical block.";
    if (!strcmp(code, "E132")) return "This operation needs an explicit unsafe { ... } block; its memory must remain valid.";
    if (strstr(format, "contextual result")) return "Declare a result type, for example: var answer !i64 = ok(42).";
    if (strstr(format, "exhaustive")) return "Handle every variant, or add a final _ => { ... } arm.";
    if (strstr(format, "Unterminated block")) return "Add the closing } for this block before the end of the file.";
    if (strstr(format, "NUL byte")) return "Remove the NUL byte from the source file; use \\0 inside a string literal when you need a zero byte.";
    if (!strcmp(code, "W001")) return "Use this value, remove the declaration, or prefix an intentionally unused name with _.";
    return NULL;
}

/* Choose a bounded window around the token before computing its end. Keeping
 * these offsets ordered matters for errors near the end of very long lines. */
static void diagnostic_span(Token tok, DiagnosticSpan *span, bool raw) {
    memset(span, 0, sizeof *span);
    span->width = 1;
    if (!tok.src || !tok.src->data || tok.offset > tok.src->len) return;
    const unsigned char *data = (const unsigned char *)tok.src->data;
    size_t start = tok.offset, end = tok.offset;
    while (start && tok.offset - start < 64 && data[start - 1] != '\n' && data[start - 1] != '\r') start--;
    bool clipped = start && data[start - 1] != '\n' && data[start - 1] != '\r';
    while (start < tok.offset && (data[start] & 0xc0) == 0x80) start++;
    while (end < tok.src->len && data[end] != '\n' && data[end] != '\r' && end - start < 160) end++;
    while (end > tok.offset && end < tok.src->len && (data[end] & 0xc0) == 0x80) end--;
    size_t token_end = tok.len > end - tok.offset ? end : tok.offset + tok.len;
    unsigned used = 0, cells = 0, token_cells = 0;
    if (clipped) { memcpy(span->text, "...", 3); used = 3; cells = 3; }
    /* Expand tabs and replace unsafe scalars with one visible cell. Never
     * emit a raw invalid continuation byte as a terminal control. */
    unsigned column = 0;
    for (size_t i = start; i < end;) {
        size_t length = diagnostic_utf8(data + i, end - i);
        if (!length) length = 1;
        if (tok.offset >= i && tok.offset < i + length) span->caret = cells;
        unsigned width = data[i] == '\t' ? 4 - column % 4 : 1;
        if (data[i] == '\t') {
            for (unsigned j = 0; j < width; j++) span->text[used++] = ' ';
        } else if ((!raw || data[i] == 0) && (!diagnostic_utf8(data + i, end - i) || diagnostic_control(diagnostic_scalar(data + i, length)))) {
            span->text[used++] = '?';
        } else { memcpy(span->text + used, data + i, length); used += (unsigned)length; }
        column += width; cells += width;
        if (i + length > tok.offset && i < token_end) token_cells += width;
        i += length;
    }
    if (tok.offset == end) span->caret = cells;
    if (end < tok.src->len && data[end] != '\n' && data[end] != '\r') {
        memcpy(span->text + used, "...", 3); used += 3;
    }
    span->text[used] = 0;
    span->width = token_cells ? token_cells : 1;
}

void km_diagnostic_span(Token tok, DiagnosticSpan *span) { diagnostic_span(tok, span, false); }

_Noreturn void km_error(Context *ctx, Token tok, const char *code, const char *format, ...) {
    const char *hint = diagnostic_hint(ctx, code, format);
    Token imports[128]; size_t import_count = 0;
    Source *origin = tok.src;
    while (origin && origin->import_origin.src && import_count < 128) {
        Token item = origin->import_origin;
        if (item.src == origin) break;
        imports[import_count++] = item;
        origin = item.src;
    }
    va_list args; va_start(args, format);
    char message[4096];
    int needed = vsnprintf(message, sizeof message, format, args);
    bool truncated = needed < 0 || (size_t)needed >= sizeof message;
    if (needed < 0) strcpy(message, "cannot format diagnostic");
    else if (truncated) {
        size_t end = sizeof message - 4;
        while (end && ((unsigned char)message[end] & 0xc0) == 0x80) end--;
        memcpy(message + end, "...", 4);
    }
    va_end(args);
    if (ctx->diagnostic_json) {
        fputs("{\"kind\":\"error\",\"code\":", stderr); diagnostic_json_string(code);
        fputs(",\"message\":", stderr); diagnostic_json_string(message);
        fprintf(stderr, ",\"message_truncated\":%s,", truncated ? "true" : "false");
        diagnostic_json_location(tok);
        fputs(",\"hint\":", stderr); diagnostic_json_string(hint);
        fputs(",\"explain\":", stderr);
        if (km_has_explanation(code)) fprintf(stderr, "\"kremen explain %s\"", code);
        else fputs("null", stderr);
        fprintf(stderr, ",\"mode\":\"%s\"", ctx->mode == KM_MODE_LEARN ? "learn" : "work");
        fputs(",\"related\":[", stderr);
        if (ctx->related_message && ctx->related_token.src) {
            fputs("{\"message\":", stderr); diagnostic_json_string(ctx->related_message);
            fputc(',', stderr); diagnostic_json_location(ctx->related_token); fputc('}', stderr);
        }
        fputs("],\"imports\":[", stderr);
        for (size_t i = 0; i < import_count; i++) {
            if (i) fputc(',', stderr);
            fputc('{', stderr); diagnostic_json_location(imports[i]); fputc('}', stderr);
        }
        fputs("]}\n", stderr);
    } else {
        km_diagnostic_text(stderr, tok.src ? tok.src->path : "<program>");
        fprintf(stderr, ":%d:%d: error[%s]: ", tok.line ? tok.line : 1, tok.col ? tok.col : 1, code);
        km_diagnostic_text(stderr, message); fputc('\n', stderr);
        diagnostic_text_location(tok, true);
        if (ctx->related_message && ctx->related_token.src) {
            Token related = ctx->related_token;
            km_diagnostic_text(stderr, related.src->path);
            fprintf(stderr, ":%d:%d: note: ", related.line > 0 ? related.line : 1, related.col > 0 ? related.col : 1);
            km_diagnostic_text(stderr, ctx->related_message); fputc('\n', stderr);
            diagnostic_text_location(related, false);
        }
        for (size_t i = 0; i < import_count; i++) {
            Token item = imports[i];
            km_diagnostic_text(stderr, item.src->path);
            fprintf(stderr, ":%d:%d: note: imported from here\n", item.line, item.col);
            diagnostic_text_location(item, false);
        }
        if (hint) { fputs(" help: ", stderr); km_diagnostic_text(stderr, hint); fputc('\n', stderr); }
        if (km_has_explanation(code)) fprintf(stderr, " explain: kremen explain %s\n", code);
    }
    ctx->related_message = NULL; ctx->diagnostic_hint = NULL;
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
    if (!strcmp(name, "thread")) return ctx->t_thread;
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
    ctx->t_thread = km_type(ctx, TY_THREAD, NULL); ctx->t_thread->name = "thread";
    static const char *names[] = {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64"};
    for (size_t i = 0; i < 8; i++) { Type *t = km_type(ctx, TY_INT, NULL); t->name = (char *)names[i]; t->bits = 8 << (i % 4); t->sign = i < 4; ctx->ints[i] = t; }
}
void km_destroy(Context *ctx) { ArenaBlock *b = ctx->arena; while (b) { ArenaBlock *next = b->next; free(b); b = next; } ctx->arena = NULL; ctx->arena_bytes = 0; }

void km_validate_source(Context *ctx, Source *src) {
    const char *nul = memchr(src->data, 0, src->len);
    if (nul) {
        Token invalid = {.src = src, .len = 1, .offset = (size_t)(nul - src->data), .line = 1, .col = 1};
        for (size_t i = 0; i < invalid.offset; i++) {
            if (src->data[i] == '\r') {
                if (i + 1 < invalid.offset && src->data[i + 1] == '\n') i++;
                invalid.line++; invalid.col = 1;
            } else if (src->data[i] == '\n') { invalid.line++; invalid.col = 1; }
            else invalid.col++;
        }
        km_error(ctx, invalid, "E100", "NUL byte in source %s", src->path);
    }
}

Source *km_read_source(Context *ctx, const char *path, bool header) {
    Source missing = {.path = (char *)path};
    Token tok = {.src = &missing, .line = 1, .col = 1};
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
    src->import_origin = ctx->import_token;
    src->data = km_alloc(ctx, src->len + 1);
    size_t got = fread(src->data, 1, src->len, f); bool failed = ferror(f) != 0; fclose(f);
    if (got != src->len || failed) km_error(ctx, tok, "E010", "cannot read %s", path);
    km_validate_source(ctx, src);
    if (ctx->sources.len >= 512) km_error(ctx, tok, "E010", "too many source files (limit 512)");
    km_sources_push(ctx, &ctx->sources, src); return src;
}
