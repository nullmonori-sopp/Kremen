#include "kremen.h"

typedef struct {
    Context *ctx;
    Source *src;
    Tokens tokens;
    size_t pos;
    unsigned depth;
} Parser;

static Token current(Parser *p) { return p->tokens.data[p->pos]; }
static Token peek(Parser *p, size_t n) {
    size_t at = n < p->tokens.len - p->pos ? p->pos + n : p->tokens.len - 1;
    return p->tokens.data[at];
}
static bool at(Parser *p, const char *s) { return strcmp(current(p).kind, s) == 0; }
static Token take(Parser *p) {
    Token t = current(p);
    if (!at(p, "eof")) p->pos++;
    return t;
}
static bool accept(Parser *p, const char *s) {
    if (!at(p, s)) return false;
    take(p);
    return true;
}
static Token expect(Parser *p, const char *s) {
    if (!at(p, s)) km_error(p->ctx, current(p), "E110", "Expected '%s', found %s", s,
        at(p, "eof") ? "end of file" : current(p).text);
    return take(p);
}
static void enter(Parser *p) {
    if (++p->depth > 256)
        km_error(p->ctx, current(p), "E111", "Program nesting exceeds the supported depth of 256");
}
static void leave(Parser *p) { p->depth--; }
static void separators(Parser *p) { while (accept(p, ";")) {} }
static void end_statement(Parser *p) {
    if (at(p, "}") || at(p, "eof")) return;
    expect(p, ";");
    separators(p);
}

static uint64_t integer(Parser *p, Token t) {
    size_t i = 0;
    unsigned base = 10;
    if (t.len >= 2 && t.text[0] == '0') {
        char prefix = t.text[1];
        if (prefix == 'x' || prefix == 'X') { base = 16; i = 2; }
        if (prefix == 'o' || prefix == 'O') { base = 8; i = 2; }
        if (prefix == 'b' || prefix == 'B') { base = 2; i = 2; }
    }
    uint64_t result = 0;
    for (; i < t.len; i++) {
        unsigned char c = (unsigned char)t.text[i];
        if (c == '_') continue;
        unsigned d = c >= '0' && c <= '9' ? (unsigned)(c - '0') :
            c >= 'a' && c <= 'f' ? (unsigned)(c - 'a') + 10 : (unsigned)(c - 'A') + 10;
        if (result > (UINT64_MAX - d) / base)
            km_error(p->ctx, t, "E110", "Integer literal exceeds u64 (18446744073709551615)");
        result = result * base + d;
    }
    return result;
}

static int hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

static char *decode_string(Parser *p, Token t, size_t *length) {
    char *out = km_alloc(p->ctx, t.len + 1);
    size_t n = 0;
    for (size_t i = 1; i + 1 < t.len; i++) {
        unsigned char c = (unsigned char)t.text[i];
        if (c != '\\') { out[n++] = (char)c; continue; }
        c = (unsigned char)t.text[++i];
        switch (c) {
        case 'n': out[n++] = '\n'; break;
        case 'r': out[n++] = '\r'; break;
        case 't': out[n++] = '\t'; break;
        case '0': out[n++] = '\0'; break;
        case '\\': out[n++] = '\\'; break;
        case '"': out[n++] = '"'; break;
        default: {
            size_t digits = c == 'x' ? 2 : c == 'u' ? 4 : 8;
            uint32_t cp = 0;
            for (size_t j = 0; j < digits; j++) cp = (cp << 4) | (uint32_t)hex_digit((unsigned char)t.text[++i]);
            if (c == 'x' || cp < 0x80) out[n++] = (char)cp;
            else if (cp < 0x800) {
                out[n++] = (char)(0xc0u | (cp >> 6));
                out[n++] = (char)(0x80u | (cp & 0x3fu));
            } else if (cp < 0x10000) {
                out[n++] = (char)(0xe0u | (cp >> 12));
                out[n++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
                out[n++] = (char)(0x80u | (cp & 0x3fu));
            } else {
                out[n++] = (char)(0xf0u | (cp >> 18));
                out[n++] = (char)(0x80u | ((cp >> 12) & 0x3fu));
                out[n++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
                out[n++] = (char)(0x80u | (cp & 0x3fu));
            }
            break;
        }
        }
    }
    out[n] = '\0';
    *length = n;
    return out;
}

static Type *parse_type(Parser *p) {
    enter(p);
    Token start = current(p);
    Type *t;
    if (accept(p, "&")) {
        bool mut = accept(p, "mut");
        t = km_type(p->ctx, TY_REF, parse_type(p));
        t->mut = mut;
    } else if (accept(p, "*")) t = km_type(p->ctx, TY_PTR, parse_type(p));
    else if (accept(p, "!")) t = km_type(p->ctx, TY_RESULT, parse_type(p));
    else if (accept(p, "[")) {
        if (accept(p, "]")) t = km_type(p->ctx, TY_SLICE, parse_type(p));
        else {
            Token size = expect(p, "int");
            uint64_t count = integer(p, size);
            if (count > SIZE_MAX) km_error(p->ctx, size, "E110", "Array size exceeds the target address space");
            expect(p, "]");
            t = km_type(p->ctx, TY_ARRAY, parse_type(p));
            t->count = (size_t)count;
        }
    } else if (accept(p, "(")) {
        t = km_type(p->ctx, TY_TUPLE, NULL);
        if (!at(p, ")")) {
            Type *first = parse_type(p);
            if (!accept(p, ",")) { expect(p, ")"); leave(p); return first; }
            km_types_push(p->ctx, &t->members, first);
            while (!at(p, ")")) {
                if (t->members.len == 32) km_error(p->ctx, start, "E140", "tuple has more than 32 members");
                km_types_push(p->ctx, &t->members, parse_type(p));
                if (!accept(p, ",")) break;
            }
        }
        expect(p, ")");
    } else {
        Token name = expect(p, "ident");
        char *text = name.text;
        Buffer qualified = {0};
        while (accept(p, ".")) {
            Token part = expect(p, "ident");
            if (!qualified.len) km_buf_append(&qualified, name.text, name.len);
            km_buf_puts(&qualified, ".");
            km_buf_append(&qualified, part.text, part.len);
            start.len = part.offset + part.len - start.offset;
        }
        if (qualified.len) text = qualified.data;
        t = km_named_type(p->ctx, text, name);
        free(qualified.data);
    }
    t->tok = start;
    leave(p);
    return t;
}

static Node *expression(Parser *, int, bool);
static Node *block(Parser *);
static Node *statement(Parser *);

static Node *param(Parser *p) {
    Token name = expect(p, "ident");
    Node *n = km_node(p->ctx, N_PARAM, name);
    n->name = name.text;
    n->annotation = parse_type(p);
    return n;
}

static Node *structure(Parser *p) {
    Token start = expect(p, "type");
    Node *n = km_node(p->ctx, N_STRUCT, start);
    n->name = expect(p, "ident").text;
    bool algebraic = accept(p, "enum");
    if (algebraic) n->kind = N_ENUM; else expect(p, "struct");
    expect(p, "{");
    separators(p);
    while (!at(p, "}")) {
        if (at(p, "eof")) km_error(p->ctx, current(p), "E110", "Unterminated struct; expected '}'");
        if (algebraic) {
            Token tag = expect(p, "ident");
            Node *v = km_node(p->ctx, N_VARIANT, tag); v->name = tag.text; v->integer = n->fields.len;
            if (accept(p, "(")) {
                while (!at(p, ")")) {
                    km_nodes_push(p->ctx, &v->params, param(p));
                    if (!accept(p, ",")) break;
                }
                expect(p, ")");
            }
            km_nodes_push(p->ctx, &n->fields, v);
        } else km_nodes_push(p->ctx, &n->fields, param(p));
        end_statement(p);
    }
    take(p);
    return n;
}

static Node *function(Parser *p, bool external) {
    Token start = expect(p, "func");
    Node *n = km_node(p->ctx, N_FN, start);
    n->external = external;
    if (accept(p, "(")) {
        n->receiver = param(p);
        expect(p, ")");
    }
    n->name = expect(p, "ident").text;
    expect(p, "(");
    while (!at(p, ")")) {
        km_nodes_push(p->ctx, &n->params, param(p));
        if (!accept(p, ",")) break;
    }
    expect(p, ")");
    n->result = at(p, "{") || at(p, ";") || at(p, "eof") ? p->ctx->t_void : parse_type(p);
    if (at(p, "{")) n->body = block(p);
    return n;
}

static Node *block(Parser *p) {
    enter(p);
    Node *n = km_node(p->ctx, N_BLOCK, expect(p, "{"));
    separators(p);
    while (!at(p, "}")) {
        if (at(p, "eof")) km_error(p->ctx, current(p), "E110", "Unterminated block; expected '}'");
        km_nodes_push(p->ctx, &n->items, statement(p));
        end_statement(p);
    }
    take(p);
    leave(p);
    return n;
}

static bool assignment(Parser *p) {
    static const char *const ops[] = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="};
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) if (at(p, ops[i])) return true;
    return false;
}

static Node *declaration(Parser *p) {
    Token start = take(p);
    Node *n = km_node(p->ctx, N_VAR, start);
    if (accept(p, "(")) {
        n->kind = N_DESTRUCT; n->flag = strcmp(start.kind, "const") == 0;
        while (!at(p, ")")) {
            Token name = expect(p, "ident"); Node *part = km_node(p->ctx, N_PARAM, name); part->name = name.text;
            km_nodes_push(p->ctx, &n->params, part);
            if (!accept(p, ",")) break;
        }
        expect(p, ")"); expect(p, "="); n->value = expression(p, 0, true); return n;
    }
    n->name = expect(p, "ident").text;
    n->flag = strcmp(start.kind, "const") == 0;
    if (!at(p, "=") && !at(p, ";") && !at(p, "}")) n->annotation = parse_type(p);
    if (accept(p, "=")) n->value = expression(p, 0, true);
    if (n->flag && !n->value) km_error(p->ctx, start, "E110", "A constant requires an initializer");
    if (!n->annotation && !n->value) km_error(p->ctx, start, "E110", "A variable requires a type or initializer");
    return n;
}

static Node *simple_statement(Parser *p, bool allow_struct) {
    if (at(p, "var") || at(p, "const")) return declaration(p);
    Node *value = expression(p, 0, allow_struct);
    if (accept(p, ":=")) {
        if (value->kind != N_NAME) km_error(p->ctx, value->tok, "E110", "The left side of ':=' must be a new variable name");
        Node *n = km_node(p->ctx, N_VAR, value->tok);
        n->name = value->name;
        n->value = expression(p, 0, allow_struct);
        return n;
    }
    if (assignment(p)) {
        Token op = take(p);
        Node *n = km_node(p->ctx, N_ASSIGN, op);
        n->left = value;
        n->op = op.text;
        n->value = expression(p, 0, allow_struct);
        return n;
    }
    if (at(p, "++") || at(p, "--")) {
        Token op = take(p);
        Node *n = km_node(p->ctx, N_ASSIGN, op);
        n->left = value;
        n->op = strcmp(op.text, "++") == 0 ? "+=" : "-=";
        n->value = km_node(p->ctx, N_INT, op);
        n->value->integer = 1;
        return n;
    }
    Node *n = km_node(p->ctx, N_EXPRSTMT, value->tok);
    n->value = value;
    return n;
}

static Node *if_statement(Parser *p) {
    enter(p);
    Node *n = km_node(p->ctx, N_IF, expect(p, "if"));
    n->condition = expression(p, 0, false);
    n->body = block(p);
    if (accept(p, "else")) n->otherwise = at(p, "if") ? if_statement(p) : block(p);
    leave(p);
    return n;
}

static Node *for_statement(Parser *p) {
    Node *n = km_node(p->ctx, N_FOR, expect(p, "for"));
    if (!at(p, "{")) {
        Node *first = at(p, ";") ? NULL : simple_statement(p, false);
        if (accept(p, ";")) {
            n->init = first;
            if (!at(p, ";")) n->condition = expression(p, 0, false);
            expect(p, ";");
            if (!at(p, "{")) n->post = simple_statement(p, false);
        } else if (first && first->kind == N_EXPRSTMT) n->condition = first->value;
        else km_error(p->ctx, current(p), "E110", "Expected a for condition or three clauses separated by ';'");
    }
    n->body = block(p);
    return n;
}

static Node *statement(Parser *p) {
    if (at(p, "{")) return block(p);
    if (at(p, "if")) return if_statement(p);
    if (at(p, "for")) return for_statement(p);
    if (at(p, "defer")) {
        Node *n = km_node(p->ctx, N_DEFER, take(p)); n->value = expression(p, 0, true); return n;
    }
    if (at(p, "arenablock")) {
        Node *n = km_node(p->ctx, N_ARENA, take(p)); n->name = expect(p, "ident").text;
        n->body = block(p); return n;
    }
    if (at(p, "match")) {
        Node *n = km_node(p->ctx, N_MATCH, take(p)); n->value = expression(p, 0, false);
        expect(p, "{"); separators(p);
        while (!at(p, "}")) {
            Token tag = take(p);
            if (strcmp(tag.kind, "ident") && strcmp(tag.kind, "ok") && strcmp(tag.kind, "fail")) km_error(p->ctx, tag, "E141", "expected a variant pattern or '_'");
            Node *arm = km_node(p->ctx, N_ARM, tag); arm->name = tag.text;
            Buffer qualified = {0};
            while (accept(p, ".")) {
                Token part = expect(p, "ident");
                if (!qualified.len) km_buf_append(&qualified, tag.text, tag.len);
                km_buf_puts(&qualified, ".");
                km_buf_append(&qualified, part.text, part.len);
            }
            if (qualified.len) arm->name = km_strndup(p->ctx, qualified.data, qualified.len);
            free(qualified.data);
            if (accept(p, "(")) {
                while (!at(p, ")")) {
                    Token binding = expect(p, "ident"); Node *b = km_node(p->ctx, N_PARAM, binding); b->name = binding.text;
                    km_nodes_push(p->ctx, &arm->params, b);
                    if (!accept(p, ",")) break;
                }
                expect(p, ")");
            }
            expect(p, "=>"); arm->body = block(p); km_nodes_push(p->ctx, &n->items, arm);
            end_statement(p);
        }
        expect(p, "}"); return n;
    }
    if (at(p, "return")) {
        Node *n = km_node(p->ctx, N_RETURN, take(p));
        if (!at(p, ";") && !at(p, "}") && !at(p, "eof")) n->value = expression(p, 0, true);
        return n;
    }
    if (at(p, "break") || at(p, "continue")) {
        bool is_break = at(p, "break");
        return km_node(p->ctx, is_break ? N_BREAK : N_CONTINUE, take(p));
    }
    if (at(p, "unsafe")) {
        Node *n = km_node(p->ctx, N_UNSAFE, take(p));
        n->body = block(p);
        return n;
    }
    return simple_statement(p, true);
}

static int precedence(Token t) {
    if (strcmp(t.kind, "||") == 0) return 1;
    if (strcmp(t.kind, "&&") == 0) return 2;
    if (strcmp(t.kind, "==") == 0 || strcmp(t.kind, "!=") == 0 || strcmp(t.kind, "<") == 0 ||
        strcmp(t.kind, "<=") == 0 || strcmp(t.kind, ">") == 0 || strcmp(t.kind, ">=") == 0) return 3;
    if (strcmp(t.kind, "|") == 0 || strcmp(t.kind, "^") == 0) return 4;
    if (strcmp(t.kind, "+") == 0 || strcmp(t.kind, "-") == 0) return 5;
    if (strcmp(t.kind, "*") == 0 || strcmp(t.kind, "/") == 0 || strcmp(t.kind, "%") == 0 ||
        strcmp(t.kind, "<<") == 0 || strcmp(t.kind, ">>") == 0 || strcmp(t.kind, "&") == 0) return 6;
    return 0;
}

static char *qualified_expr_name(Parser *p, Node *n) {
    Node *parts[256];
    size_t count = 0;
    while (n->kind == N_FIELD) {
        if (count == sizeof(parts) / sizeof(parts[0]))
            km_error(p->ctx, n->tok, "E111", "Qualified name exceeds the supported depth of 256");
        parts[count++] = n;
        n = n->base;
    }
    if (n->kind != N_NAME) return NULL;
    if (!count) return n->name;
    Buffer text = {0};
    km_buf_puts(&text, n->name);
    while (count) {
        km_buf_puts(&text, ".");
        km_buf_puts(&text, parts[--count]->name);
    }
    char *result = km_strndup(p->ctx, text.data, text.len);
    free(text.data);
    return result;
}

static bool generic_builtin(Node *n) {
    return n->kind == N_NAME && (strcmp(n->name, "alloc") == 0 ||
        strcmp(n->name, "arenaAlloc") == 0 || strcmp(n->name, "byteptr") == 0 || strcmp(n->name, "ptr") == 0);
}

static Node *expression(Parser *p, int level, bool allow_struct) {
    enter(p);
    Token start = take(p);
    Node *value;
    if (strcmp(start.kind, "int") == 0) {
        value = km_node(p->ctx, N_INT, start);
        value->integer = integer(p, start);
    } else if (strcmp(start.kind, "string") == 0) {
        value = km_node(p->ctx, N_STRING, start);
        value->text = decode_string(p, start, &value->text_len);
    } else if (strcmp(start.kind, "true") == 0 || strcmp(start.kind, "false") == 0) {
        value = km_node(p->ctx, N_BOOL, start);
        value->flag = strcmp(start.kind, "true") == 0;
    } else if (strcmp(start.kind, "ident") == 0) {
        value = km_node(p->ctx, N_NAME, start);
        value->name = start.text;
    } else if (strcmp(start.kind, "(") == 0) {
        if (accept(p, ")")) value = km_node(p->ctx, N_TUPLE, start);
        else {
            value = expression(p, 0, true);
            if (accept(p, ",")) {
                Node *tuple = km_node(p->ctx, N_TUPLE, start); km_nodes_push(p->ctx, &tuple->values, value);
                while (!at(p, ")")) {
                    if (tuple->values.len == 32) km_error(p->ctx, start, "E140", "tuple has more than 32 members");
                    km_nodes_push(p->ctx, &tuple->values, expression(p, 0, true));
                    if (!accept(p, ",")) break;
                }
                value = tuple;
            }
            expect(p, ")");
        }
    } else if (strcmp(start.kind, "[") == 0) {
        p->pos--;
        value = km_node(p->ctx, N_ARRAYLIT, start);
        value->annotation = parse_type(p);
        expect(p, "{");
        while (!at(p, "}")) {
            km_nodes_push(p->ctx, &value->values, expression(p, 0, true));
            if (!accept(p, ",")) break;
        }
        expect(p, "}");
    } else if (strcmp(start.kind, "&") == 0 || strcmp(start.kind, "*") == 0 ||
        strcmp(start.kind, "-") == 0 || strcmp(start.kind, "+") == 0 ||
        strcmp(start.kind, "!") == 0 || strcmp(start.kind, "~") == 0) {
        value = km_node(p->ctx, N_UNARY, start);
        value->op = strcmp(start.kind, "&") == 0 && accept(p, "mut") ? "&mut" : start.text;
        value->value = expression(p, 7, allow_struct);
    } else {
        km_error(p->ctx, start, "E110", "Expected an expression");
    }
    while (true) {
        if (allow_struct && at(p, "{") && (strcmp(peek(p, 1).kind, "}") == 0 ||
            (strcmp(peek(p, 1).kind, "ident") == 0 && strcmp(peek(p, 2).kind, ":") == 0))) {
            char *name = qualified_expr_name(p, value);
            if (!name) break;
            take(p);
            Node *n = km_node(p->ctx, N_STRUCTLIT, value->tok);
            n->name = name;
            while (!at(p, "}")) {
                Token field = expect(p, "ident");
                expect(p, ":");
                Node *f = km_node(p->ctx, N_PARAM, field);
                f->name = field.text;
                f->value = expression(p, 0, true);
                km_nodes_push(p->ctx, &n->fields, f);
                if (!accept(p, ",")) break;
            }
            expect(p, "}");
            value = n;
        } else if (at(p, "[") && generic_builtin(value)) {
            take(p);
            Type *typearg = parse_type(p);
            expect(p, "]");
            expect(p, "(");
            Node *n = km_node(p->ctx, N_CALL, value->tok);
            n->callee = value;
            n->typearg = typearg;
            while (!at(p, ")")) {
                km_nodes_push(p->ctx, &n->args, expression(p, 0, true));
                if (!accept(p, ",")) break;
            }
            expect(p, ")");
            value = n;
        } else if (accept(p, "(")) {
            Node *n = km_node(p->ctx, N_CALL, value->tok);
            n->callee = value;
            while (!at(p, ")")) {
                km_nodes_push(p->ctx, &n->args, expression(p, 0, true));
                if (!accept(p, ",")) break;
            }
            expect(p, ")");
            value = n;
        } else if (accept(p, ".")) {
            Token name = at(p, "int") ? take(p) : expect(p, "ident");
            Node *n = km_node(p->ctx, N_FIELD, name);
            n->base = value;
            n->name = name.text;
            value = n;
        } else if (accept(p, "[")) {
            Node *n = km_node(p->ctx, N_INDEX, value->tok);
            n->base = value;
            n->index = expression(p, 0, true);
            expect(p, "]");
            value = n;
        } else if (accept(p, "?")) {
            Node *n = km_node(p->ctx, N_TRY, value->tok);
            n->value = value;
            value = n;
        } else if (level == 0 && at(p, "catch")) {
            Node *n = km_node(p->ctx, N_CATCH, take(p)); n->value = value;
            if (at(p, "ident") && !strcmp(peek(p, 1).kind, "=>")) {
                n->name = take(p).text; expect(p, "=>");
            }
            n->right = expression(p, 0, allow_struct); value = n;
        } else {
            Token op = current(p);
            int next = precedence(op);
            if (!next || next <= level) break;
            take(p);
            Node *n = km_node(p->ctx, N_BINARY, op);
            n->op = op.text;
            n->left = value;
            n->right = expression(p, next, allow_struct);
            value = n;
        }
    }
    leave(p);
    return value;
}

/* Pratt parsing builds left-associated chains without using the C stack. Check
 * their depth too, before later compiler passes recursively visit the AST. */
static void validate_ast_depth(Parser *p, Node *root) {
    struct Frame { Node *node; size_t next; } stack[257];
    size_t depth = 1;
    stack[0].node = root;
    stack[0].next = 0;
    while (depth) {
        struct Frame *f = &stack[depth - 1];
        Node *n = f->node;
        Node *fixed[] = {n->left, n->right, n->value, n->base, n->index,
            n->callee, n->condition, n->body, n->otherwise, n->init, n->post, n->receiver};
        Nodes vectors[] = {n->items, n->params, n->args, n->fields, n->values};
        size_t child_index = f->next++;
        Node *child = NULL;
        bool found = false;
        if (child_index < sizeof(fixed) / sizeof(fixed[0])) {
            child = fixed[child_index];
            found = true;
        } else {
            child_index -= sizeof(fixed) / sizeof(fixed[0]);
            for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
                if (child_index < vectors[i].len) {
                    child = vectors[i].data[child_index];
                    found = true;
                    break;
                }
                child_index -= vectors[i].len;
            }
        }
        if (!found) { depth--; continue; }
        if (!child) continue;
        if (depth >= 256) km_error(p->ctx, child->tok, "E111", "AST nesting exceeds the supported depth of 256");
        stack[depth].node = child;
        stack[depth].next = 0;
        depth++;
    }
}

Nodes km_parse(Context *ctx, Source *src) {
    Parser p = {0};
    p.ctx = ctx;
    p.src = src;
    p.tokens = km_lex(ctx, src, false);
    Nodes result = {0};
    separators(&p);
    if (accept(&p, "package")) {
        expect(&p, "ident");
        end_statement(&p);
    }
    while (!at(&p, "eof")) {
        Node *n;
        if (at(&p, "import")) {
            n = km_node(ctx, N_IMPORT, take(&p));
            if (at(&p, "ident")) n->name = take(&p).text;
            Token path = expect(&p, "string");
            n->text = decode_string(&p, path, &n->text_len);
            if (memchr(n->text, '\0', n->text_len)) km_error(ctx, path, "E110", "Import paths cannot contain NUL bytes");
        } else if (at(&p, "type")) n = structure(&p);
        else if (at(&p, "func")) n = function(&p, false);
        else if (accept(&p, "extern")) n = function(&p, true);
        else km_error(ctx, current(&p), "E110", "Expected an import, type or function declaration");
        validate_ast_depth(&p, n);
        km_nodes_push(ctx, &result, n);
        end_statement(&p);
    }
    return result;
}
