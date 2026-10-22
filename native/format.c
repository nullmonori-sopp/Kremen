#include "kremen.h"
#include <ctype.h>

typedef struct { Buffer out; int indent; bool start; } Writer;
static void trim(Writer *w) { while (w->out.len && (w->out.data[w->out.len - 1] == ' ' || w->out.data[w->out.len - 1] == '\t')) w->out.data[--w->out.len] = 0; }
static void emit(Writer *w, const char *s) { if (w->start) { for (int j = 0; j < w->indent; j++) km_buf_puts(&w->out, "\t"); w->start = false; } km_buf_puts(&w->out, s); }
static void space(Writer *w) { if (!w->start && w->out.len && w->out.data[w->out.len - 1] != ' ' && w->out.data[w->out.len - 1] != '\n' && w->out.data[w->out.len - 1] != '\t') km_buf_puts(&w->out, " "); }
static void newline(Writer *w) { trim(w); if (w->out.len && w->out.data[w->out.len - 1] != '\n') km_buf_puts(&w->out, "\n"); w->start = true; }
static bool word(Token *t) { return t && (isalnum((unsigned char)t->text[0]) || t->text[0] == '_' || !strcmp(t->kind, "string")); }
static bool ends_expr(Token *t) { return word(t) || (t && (!strcmp(t->text, ")") || !strcmp(t->text, "]") || !strcmp(t->text, "?") || !strcmp(t->text, "}"))); }
static bool operator_token(const char *s) { return strchr("=+-*/%&|^<>!~", s[0]) != NULL || !strcmp(s, ":="); }

char *km_format(Context *ctx, Source *src) {
    (void)km_parse(ctx, src);
    Tokens tokens = km_lex(ctx, src, true); Writer w = {0}; w.start = true;
    bool literals[512] = {0}; size_t depth = 0; int parens = 0;
    bool control = false, for_header = false, pending_unary = false, declaration = false, function_sig = false;
    Token *previous = NULL;
    for (size_t i = 0; i < tokens.len; i++) {
        Token *t = &tokens.data[i]; const char *s = t->text;
        Token *next = i + 1 < tokens.len ? &tokens.data[i + 1] : NULL;
        if (!strcmp(t->kind, "eof")) break;
        if (!strcmp(t->kind, "comment")) {
            bool same_line = previous && t->line == previous->line;
            if (same_line) space(&w); else newline(&w);
            emit(&w, s);
            if (!strncmp(s, "//", 2) || strchr(s, '\n')) newline(&w); else space(&w);
            continue;
        }
        if (!strcmp(s, ";")) {
            if (for_header && parens == 0) { trim(&w); emit(&w, ";"); space(&w); }
            else { newline(&w); function_sig = false; }
            pending_unary = false; declaration = false; previous = t; continue;
        }
        if (!strcmp(s, "{")) {
            bool literal = (!control || parens > 0) && previous && (word(previous) || !strcmp(previous->text, "]"));
            if (depth >= 512) km_error(ctx, *t, "E111", "formatter nesting limit exceeded");
            literals[depth++] = literal;
            if (!literal) { space(&w); emit(&w, "{"); w.indent++; newline(&w); control = false; for_header = false; function_sig = false; }
            else { trim(&w); emit(&w, "{"); }
            declaration = false;
        } else if (!strcmp(s, "}")) {
            bool literal = depth ? literals[--depth] : false;
            if (literal) { trim(&w); emit(&w, "}"); }
            else {
                /* Comments do not separate `}` from `else`. Looking only at
                 * the physical token would insert a semicolon before them. */
                size_t following = i + 1;
                while (following < tokens.len && !strcmp(tokens.data[following].kind, "comment")) following++;
                Token *significant_next = following < tokens.len ? &tokens.data[following] : NULL;
                newline(&w); if (w.indent) w.indent--; emit(&w, "}");
                if (significant_next && !strcmp(significant_next->text, "else")) space(&w);
                else if (next && (!strcmp(next->text, ")") || !strcmp(next->text, ","))) { /* expression block not currently supported */ }
                else newline(&w);
            }
        } else if (!strcmp(s, "(")) {
            if (previous && (!strcmp(previous->text, "if") || !strcmp(previous->text, "for") || !strcmp(previous->text, "func") || !strcmp(previous->text, "var") || !strcmp(previous->text,"const"))) space(&w);
            else trim(&w);
            emit(&w, "("); parens++;
        } else if (!strcmp(s, ")")) { trim(&w); emit(&w, ")"); parens--; }
        else if (!strcmp(s, "[")) { if (declaration && previous && word(previous)) space(&w); else trim(&w); emit(&w, "["); }
        else if (!strcmp(s, "]")) { trim(&w); emit(&w, "]"); }
        else if (!strcmp(s, ".") || !strcmp(s, "?")) { trim(&w); emit(&w, s); }
        else if (!strcmp(s, ",")) { trim(&w); emit(&w, ","); space(&w); }
        else if (!strcmp(s, ":")) { trim(&w); emit(&w, ":"); space(&w); }
        else if (!strcmp(s, "++") || !strcmp(s, "--")) { trim(&w); emit(&w, s); }
        else if (operator_token(s)) {
            bool unary_op = !strcmp(s, "&") || !strcmp(s, "*") || !strcmp(s, "-") || !strcmp(s, "+") || !strcmp(s, "!") || !strcmp(s, "~");
            bool unary_context = !ends_expr(previous) || (previous && (!strcmp(previous->text, "return") || !strcmp(previous->text, "var") || !strcmp(previous->text, "const") || !strcmp(previous->text, "mut")));
            bool unary_type = (declaration || function_sig) && previous && ends_expr(previous) && (!strcmp(s, "&") || !strcmp(s, "*") || !strcmp(s, "!"));
            bool unary = unary_op && (unary_context || unary_type || (!strcmp(s, "&") && next && !strcmp(next->text, "mut")));
            if (unary) { if (previous && (word(previous) || ((!strcmp(s, "-") || !strcmp(s, "+") || !strcmp(s, "&")) && !strcmp(previous->text, s)))) space(&w); emit(&w, s); pending_unary = true; }
            else { space(&w); emit(&w, s); space(&w); pending_unary = false; }
        } else {
            if (previous && !pending_unary && (word(previous) || !strcmp(previous->text, ")") || !strcmp(previous->text, "}"))) space(&w);
            emit(&w, s); pending_unary = false;
            if (!strcmp(s, "mut")) space(&w);
            if (!strcmp(s, "var") || !strcmp(s, "const")) declaration = true;
            if (!strcmp(s, "for")) { control = true; for_header = true; }
            if (!strcmp(s, "if") || !strcmp(s, "else") || !strcmp(s, "func") || !strcmp(s, "unsafe") || !strcmp(s, "struct") || !strcmp(s,"enum") || !strcmp(s,"match") || !strcmp(s,"arenablock")) control = true;
            if (!strcmp(s, "func")) function_sig = true;
        }
        previous = t;
    }
    newline(&w);
    char *result = km_strndup(ctx, w.out.data ? w.out.data : "", w.out.len); free(w.out.data);
    Source formatted = *src; formatted.data = result; formatted.len = strlen(result);
    (void)km_parse(ctx, &formatted); /* A formatter never writes syntactically damaged output. */
    return result;
}
