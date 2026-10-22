#ifndef KREMEN_H
#define KREMEN_H
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <setjmp.h>

typedef struct Context Context;
typedef struct Source Source;
typedef struct Module Module;
typedef struct Node Node;
typedef struct Type Type;
typedef struct Symbol Symbol;
typedef struct ArenaBlock ArenaBlock;
typedef struct { Node **data; size_t len, cap; } Nodes;
typedef struct { Type **data; size_t len, cap; } Types;
typedef struct { Source **data; size_t len, cap; } Sources;
typedef struct { Module **data; size_t len, cap; } Modules;
typedef struct { char *data; size_t len, cap; } Buffer;
typedef struct { char *kind, *text; size_t len, offset; int line, col; Source *src; } Token;
typedef struct { char text[1024]; unsigned caret, width; } DiagnosticSpan;
typedef struct { Token *data; size_t len, cap; } Tokens;
typedef enum { TY_VOID, TY_INT, TY_BOOL, TY_STRING, TY_STRUCT, TY_ARRAY, TY_REF, TY_PTR, TY_SLICE, TY_RESULT, TY_FUNCTION, TY_TUPLE, TY_ENUM, TY_ARENA, TY_FILE } TypeKind;
struct Type { TypeKind kind; char *name; Type *elem; size_t count; int bits; bool sign, mut; unsigned id; Node *decl; Token tok; Types members; };
typedef enum { N_STRUCT, N_FN, N_PARAM, N_BLOCK, N_VAR, N_ASSIGN, N_EXPRSTMT, N_IF, N_FOR, N_RETURN, N_UNSAFE, N_BREAK, N_CONTINUE, N_INT, N_BOOL, N_STRING, N_NAME, N_BINARY, N_UNARY, N_CALL, N_FIELD, N_INDEX, N_STRUCTLIT, N_ARRAYLIT, N_IMPORT, N_TRY, N_DEFER, N_CATCH, N_ARENA, N_ENUM, N_VARIANT, N_MATCH, N_ARM, N_TUPLE, N_DESTRUCT } NodeKind;
struct Node {
    NodeKind kind; Token tok; char *name, *op, *text, *cname, *builtin;
    unsigned location_id;
    size_t text_len; uint64_t integer; bool flag, negative, external;
    Type *type, *annotation, *result, *typearg;
    Node *left, *right, *value, *base, *index, *callee, *condition;
    Node *body, *otherwise, *init, *post, *receiver, *function;
    Nodes items, params, args, fields, values;
    Symbol *symbol; Module *module;
    bool constant; uint64_t constant_value; bool constant_negative;
    Symbol *borrow_root, *borrow_via;
};
struct Symbol { char *name, *cname; Type *type; bool mutable, used; unsigned depth; Token tok; Symbol *root, *via; bool borrow_mut; };
typedef struct Import { char *alias; Module *target; Token tok; struct Import *next; } Import;
struct Source { char *path, *data; size_t len; bool header; Module *module; };
struct Module { char *path, *name, *cname; Nodes decls; Sources sources; Import *imports; bool loading, loaded; };
struct Context {
    ArenaBlock *arena; size_t arena_bytes; Sources sources; Modules modules; Nodes decls, structs, functions;
    Type *t_void, *t_bool, *t_string, *t_arena, *t_file, *ints[8]; unsigned next_type, next_symbol;
    jmp_buf failure; bool failure_ready; unsigned errors;
    char *project_root, *runtime_path, *cc; bool no_asm; int optimization;
    Token import_token;
    Token related_token;
    const char *related_message;
};

void *km_alloc(Context *, size_t);
char *km_strndup(Context *, const char *, size_t);
char *km_strdup(Context *, const char *);
char *km_sprintf(Context *, const char *, ...);
void km_nodes_push(Context *, Nodes *, Node *);
void km_types_push(Context *, Types *, Type *);
void km_sources_push(Context *, Sources *, Source *);
void km_modules_push(Context *, Modules *, Module *);
void km_buf_append(Buffer *, const char *, size_t);
void km_buf_puts(Buffer *, const char *);
void km_buf_printf(Buffer *, const char *, ...);
_Noreturn void km_error(Context *, Token, const char *, const char *, ...);
void km_diagnostic_span(Token, DiagnosticSpan *);
void km_init(Context *);
void km_destroy(Context *);
Node *km_node(Context *, NodeKind, Token);
Type *km_type(Context *, TypeKind, Type *);
Type *km_named_type(Context *, const char *, Token);
bool km_type_equal(Type *, Type *);
const char *km_type_name(Context *, Type *);
bool km_is_int(Type *);
bool km_is_address(Type *);

Tokens km_lex(Context *, Source *, bool comments);
Nodes km_parse(Context *, Source *);
Module *km_load(Context *, const char *path);
bool km_check(Context *);
char *km_codegen(Context *);
char *km_format(Context *, Source *);
Source *km_read_source(Context *, const char *, bool);
char *km_resolve(Context *, const char *, const char *);
void *km_fast_copy(void *, const void *, size_t);

#endif
