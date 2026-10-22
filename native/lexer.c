#include "kremen.h"

typedef struct {
    Context *ctx;
    Source *src;
    Tokens tokens;
    size_t pos;
    int line, col;
    bool comments, can_end;
} Lexer;

static bool ascii_alpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool ascii_digit(unsigned char c) { return c >= '0' && c <= '9'; }

static int digit_value(unsigned char c) {
    if (ascii_digit(c)) return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

static Token lex_token(Lexer *l, const char *kind, size_t start, int line, int col) {
    Token t = {0};
    t.kind = (char *)kind;
    t.len = l->pos - start;
    t.text = km_strndup(l->ctx, l->src->data + start, t.len);
    t.offset = start;
    t.line = line;
    t.col = col;
    t.src = l->src;
    return t;
}

static void push_token(Lexer *l, Token t) {
    if (l->tokens.len >= 1000000)
        km_error(l->ctx, t, "E111", "Source exceeds the supported limit of 1000000 tokens");
    if (l->tokens.len == l->tokens.cap) {
        size_t cap = l->tokens.cap ? l->tokens.cap * 2 : 64;
        Token *data = km_alloc(l->ctx, cap * sizeof(*data));
        if (l->tokens.len) memcpy(data, l->tokens.data, l->tokens.len * sizeof(*data));
        l->tokens.data = data;
        l->tokens.cap = cap;
    }
    l->tokens.data[l->tokens.len++] = t;
    if (strcmp(t.kind, "comment") == 0) return;
    l->can_end = strcmp(t.kind, "ident") == 0 || strcmp(t.kind, "int") == 0 ||
        strcmp(t.kind, "string") == 0 || strcmp(t.kind, ")") == 0 ||
        strcmp(t.kind, "]") == 0 || strcmp(t.kind, "}") == 0 ||
        strcmp(t.kind, "++") == 0 || strcmp(t.kind, "--") == 0 ||
        strcmp(t.kind, "true") == 0 || strcmp(t.kind, "false") == 0 ||
        strcmp(t.kind, "return") == 0 || strcmp(t.kind, "break") == 0 ||
        strcmp(t.kind, "continue") == 0 || strcmp(t.kind, "?") == 0;
}

static _Noreturn void lex_error(Lexer *l, size_t start, int line, int col, const char *msg) {
    Token t = lex_token(l, "invalid", start, line, col);
    if (!t.len && start < l->src->len) {
        t.len = 1;
        t.text = km_strndup(l->ctx, l->src->data + start, 1);
    }
    km_error(l->ctx, t, "E100", "%s", msg);
}

static void insert_semicolon(Lexer *l) {
    if (l->can_end) {
        Token t = lex_token(l, ";", l->pos, l->line, l->col);
        t.text = ";";
        t.len = 1;
        push_token(l, t);
    }
}

static void newline(Lexer *l) {
    insert_semicolon(l);
    if (l->src->data[l->pos] == '\r' && l->pos + 1 < l->src->len &&
        l->src->data[l->pos + 1] == '\n') l->pos++;
    l->pos++;
    l->line++;
    l->col = 1;
}

static bool keyword(const char *s) {
    static const char *const words[] = {
        "package", "import", "extern", "type", "struct", "func", "var", "const",
        "return", "if", "else", "for", "unsafe", "break", "continue", "mut",
        "true", "false", "defer", "catch", "arenablock", "enum", "match"
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (strcmp(s, words[i]) == 0) return true;
    return false;
}

static void integer_token(Lexer *l, size_t start, int line, int col) {
    while (l->pos < l->src->len) {
        unsigned char c = (unsigned char)l->src->data[l->pos];
        if (!ascii_alpha(c) && !ascii_digit(c)) break;
        l->pos++;
        l->col++;
    }
    size_t first = start;
    unsigned base = 10;
    if (l->pos - start >= 2 && l->src->data[start] == '0') {
        char c = l->src->data[start + 1];
        if (c == 'x' || c == 'X') { base = 16; first += 2; }
        if (c == 'o' || c == 'O') { base = 8; first += 2; }
        if (c == 'b' || c == 'B') { base = 2; first += 2; }
    }
    if (first == l->pos || l->pos - first > 256)
        lex_error(l, start, line, col, "Invalid integer literal (at most 256 digits are allowed)");
    bool underscore = true;
    for (size_t j = first; j < l->pos; j++) {
        unsigned char c = (unsigned char)l->src->data[j];
        if (c == '_') {
            if (underscore) lex_error(l, start, line, col, "Invalid separator in integer literal");
            underscore = true;
        } else {
            int d = digit_value(c);
            if (d < 0 || (unsigned)d >= base)
                lex_error(l, start, line, col, "Invalid digit in integer literal");
            underscore = false;
        }
    }
    if (underscore) lex_error(l, start, line, col, "Integer literal cannot end with '_'");
    push_token(l, lex_token(l, "int", start, line, col));
}

static size_t utf8_width(Lexer *l) {
    const unsigned char *s = (const unsigned char *)l->src->data;
    unsigned char first = s[l->pos];
    size_t n;
    uint32_t cp;
    if (first >= 0xc2 && first <= 0xdf) { n = 2; cp = first & 0x1fu; }
    else if (first >= 0xe0 && first <= 0xef) { n = 3; cp = first & 0x0fu; }
    else if (first >= 0xf0 && first <= 0xf4) { n = 4; cp = first & 0x07u; }
    else return 0;
    if (n > l->src->len - l->pos) return 0;
    for (size_t j = 1; j < n; j++) {
        unsigned char c = s[l->pos + j];
        if ((c & 0xc0u) != 0x80u) return 0;
        cp = (cp << 6) | (c & 0x3fu);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
    return n;
}

static void string_token(Lexer *l, size_t start, int line, int col) {
    l->pos++;
    l->col++;
    while (l->pos < l->src->len && l->src->data[l->pos] != '"') {
        unsigned char c = (unsigned char)l->src->data[l->pos];
        if (c < 32 || c == 127)
            lex_error(l, l->pos, l->line, l->col, "Control character in string; use an escape sequence");
        if (c == '\\') {
            size_t escape = l->pos;
            int escape_col = l->col;
            l->pos++;
            l->col++;
            if (l->pos == l->src->len)
                lex_error(l, escape, line, escape_col, "Unterminated escape sequence");
            char e = l->src->data[l->pos];
            if (e == 'n' || e == 'r' || e == 't' || e == '0' || e == '\\' || e == '"') {
                l->pos++;
                l->col++;
                continue;
            }
            size_t digits = e == 'x' ? 2 : e == 'u' ? 4 : e == 'U' ? 8 : 0;
            if (!digits) lex_error(l, escape, line, escape_col, "Unknown escape sequence");
            if (digits >= l->src->len - l->pos)
                lex_error(l, escape, line, escape_col, "Expected hexadecimal escape digits");
            uint32_t cp = 0;
            for (size_t j = 1; j <= digits; j++) {
                int d = digit_value((unsigned char)l->src->data[l->pos + j]);
                if (d < 0) lex_error(l, escape, line, escape_col, "Expected hexadecimal escape digits");
                cp = (cp << 4) | (uint32_t)d;
            }
            if (e != 'x' && (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)))
                lex_error(l, escape, line, escape_col, "Escape does not denote a valid Unicode scalar");
            l->pos += digits + 1;
            l->col += (int)digits + 1;
        } else if (c >= 128) {
            size_t n = utf8_width(l);
            if (!n) lex_error(l, l->pos, l->line, l->col, "Invalid UTF-8 in string literal");
            l->pos += n;
            l->col += (int)n;
        } else {
            l->pos++;
            l->col++;
        }
    }
    if (l->pos == l->src->len)
        lex_error(l, start, line, col, "Unterminated string literal; expected '\"'");
    l->pos++;
    l->col++;
    push_token(l, lex_token(l, "string", start, line, col));
}

Tokens km_lex(Context *ctx, Source *src, bool comments) {
    Lexer l = {0};
    l.ctx = ctx;
    l.src = src;
    l.line = l.col = 1;
    l.comments = comments;
    static const char *const operators[] = {
        "<<=", ">>=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
        "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", ":=", "++", "--", "=>",
        "+", "-", "*", "/", "%", "&", "|", "^", "~", "!", "=", "<", ">",
        "(", ")", "[", "]", "{", "}", ",", ".", ":", ";", "?"
    };
    while (l.pos < src->len) {
        unsigned char c = (unsigned char)src->data[l.pos];
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f') { l.pos++; l.col++; continue; }
        if (c == '\r' || c == '\n') { newline(&l); continue; }
        size_t start = l.pos;
        int line = l.line, col = l.col;
        if (c == '/' && l.pos + 1 < src->len && src->data[l.pos + 1] == '/') {
            while (l.pos < src->len && src->data[l.pos] != '\r' && src->data[l.pos] != '\n') {
                l.pos++;
                l.col++;
            }
            if (comments) push_token(&l, lex_token(&l, "comment", start, line, col));
            continue;
        }
        if (c == '/' && l.pos + 1 < src->len && src->data[l.pos + 1] == '*') {
            l.pos += 2;
            l.col += 2;
            bool ended = false;
            while (l.pos < src->len) {
                if (src->data[l.pos] == '*' && l.pos + 1 < src->len && src->data[l.pos + 1] == '/') {
                    l.pos += 2;
                    l.col += 2;
                    ended = true;
                    break;
                }
                if (src->data[l.pos] == '\r' || src->data[l.pos] == '\n') newline(&l);
                else { l.pos++; l.col++; }
            }
            if (!ended) lex_error(&l, start, line, col, "Unterminated block comment; expected '*/'");
            if (comments) push_token(&l, lex_token(&l, "comment", start, line, col));
            continue;
        }
        if (ascii_alpha(c)) {
            do { l.pos++; l.col++; } while (l.pos < src->len &&
                (ascii_alpha((unsigned char)src->data[l.pos]) || ascii_digit((unsigned char)src->data[l.pos])));
            Token t = lex_token(&l, "ident", start, line, col);
            if (keyword(t.text)) t.kind = t.text;
            push_token(&l, t);
            continue;
        }
        if (ascii_digit(c)) { integer_token(&l, start, line, col); continue; }
        if (c == '"') { string_token(&l, start, line, col); continue; }
        bool matched = false;
        for (size_t j = 0; j < sizeof(operators) / sizeof(operators[0]); j++) {
            size_t n = strlen(operators[j]);
            if (n <= src->len - l.pos && memcmp(src->data + l.pos, operators[j], n) == 0) {
                l.pos += n;
                l.col += (int)n;
                push_token(&l, lex_token(&l, operators[j], start, line, col));
                matched = true;
                break;
            }
        }
        if (!matched) lex_error(&l, start, line, col, "Invalid character; names must use ASCII letters, digits and '_'");
    }
    insert_semicolon(&l);
    push_token(&l, lex_token(&l, "eof", l.pos, l.line, l.col));
    return l.tokens;
}
