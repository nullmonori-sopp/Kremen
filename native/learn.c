#include "kremen.h"

/* Learning requirements run only after the shared semantic checker succeeds.
 * This pass reads the typed tree and never changes lowering or runtime rules.
 * They are structural requirements, not a proof of the programmer's intent. */
static bool named_code(Symbol *symbol) { return symbol && symbol->used; }

typedef struct { Context *ctx; unsigned char *result_cache; } Inspector;

/* All by-value layouts have already passed cycle checks. Slices and addresses
 * end this value-shape query; their reachable storage is not being copied or
 * discarded here. Cache shared type graphs so a diagnostic pass stays linear. */
static bool contains_result(Inspector *inspector, Type *type) {
    if (inspector->result_cache[type->id]) return inspector->result_cache[type->id] == 2;
    bool found = type->kind == TY_RESULT;
    inspector->result_cache[type->id] = 1;
    if (type->kind == TY_ARRAY) found = contains_result(inspector, type->elem);
    else if (type->kind == TY_TUPLE) {
        for (size_t i = 0; i < type->members.len && !found; i++) found = contains_result(inspector, type->members.data[i]);
    } else if (type->kind == TY_STRUCT) {
        for (size_t i = 0; i < type->decl->fields.len && !found; i++) found = contains_result(inspector, type->decl->fields.data[i]->type);
    } else if (type->kind == TY_ENUM) {
        for (size_t i = 0; i < type->decl->fields.len && !found; i++) {
            Nodes fields = type->decl->fields.data[i]->params;
            for (size_t j = 0; j < fields.len && !found; j++) found = contains_result(inspector, fields.data[j]->type);
        }
    }
    inspector->result_cache[type->id] = found ? 2 : 1;
    return found;
}

static void learn_error(Context *ctx, Token token, const char *code, const char *message, const char *hint) {
    ctx->diagnostic_hint = hint;
    km_error(ctx, token, code, "%s", message);
}

static void inspect(Inspector *inspector, Node *node) {
    if (!node) return;
    Context *ctx = inspector->ctx;
    switch (node->kind) {
    case N_VAR:
        if (!node->value)
            learn_error(ctx, node->tok, "L001", "learn mode requires an explicit initializer",
                        "Write the intended initial value. Work mode's implicit zero initialization is memory-safe, but learn asks you to choose it explicitly.");
        if (!node->symbol->used && contains_result(inspector, node->type))
            learn_error(ctx, node->tok, "L002", "learn mode does not allow an unread result binding",
                        "Inspect, handle, or propagate the !T value or its containing aggregate. A leading underscore does not satisfy this learning requirement.");
        break;
    case N_ASSIGN:
        if (node->left->kind == N_NAME && !strcmp(node->left->name, "_") && contains_result(inspector, node->value->type))
            learn_error(ctx, node->tok, "L002", "learn mode does not allow discarding a result",
                        "Use match, a catch that uses its error code, or ? in a result-returning function.");
        break;
    case N_DESTRUCT:
        for (size_t i = 0; i < node->params.len; i++) {
            Node *binding = node->params.data[i];
            if (!named_code(binding->symbol) && contains_result(inspector, binding->type))
                learn_error(ctx, binding->tok, "L002", "learn mode does not allow discarding a tuple's result member",
                            "Bind the !T member to a name and inspect, handle, or propagate it.");
        }
        break;
    case N_CATCH:
        if (!named_code(node->symbol))
            learn_error(ctx, node->tok, "L003", "learn mode requires catch to bind and use its error code",
                        "Use catch code => expression and use code in that expression, or propagate the result with ?.");
        break;
    case N_MATCH:
        for (size_t i = 0; i < node->items.len; i++) {
            Node *arm = node->items.data[i];
            if (arm->integer == UINT64_MAX)
                learn_error(ctx, arm->tok, "L004", "learn mode requires explicit match cases",
                            "List every enum variant, or both ok(value) and fail(code) for a result. This makes newly added variants visible during checking.");
            if (node->type->kind == TY_RESULT && arm->integer == 1 && !named_code(arm->params.data[0]->symbol))
                learn_error(ctx, arm->tok, "L003", "learn mode requires the fail pattern to bind and use its error code",
                            "Write fail(code) and use code in the failure arm. A _ binding intentionally ignores a code in work mode only.");
        }
        break;
    case N_CALL:
        if (node->builtin && !strcmp(node->builtin, "readInt"))
            learn_error(ctx, node->tok, "L005", "learn mode requires fallible integer input",
                        "Use readIntResult(), then inspect, handle, or propagate its !i64 result. Invalid input should be an explicit path in this exercise.");
        break;
    default: break;
    }
    Node *children[] = {node->left, node->right, node->value, node->base, node->index, node->callee,
                        node->condition, node->body, node->otherwise, node->init, node->post};
    for (size_t i = 0; i < sizeof children / sizeof *children; i++) inspect(inspector, children[i]);
    /* Checked call receivers alias their callee's base; visit the syntax once. */
    if (node->kind != N_CALL) inspect(inspector, node->receiver);
    Nodes lists[] = {node->items, node->params, node->args, node->fields, node->values};
    for (size_t i = 0; i < sizeof lists / sizeof *lists; i++)
        for (size_t j = 0; j < lists[i].len; j++) inspect(inspector, lists[i].data[j]);
}

void km_check_learn(Context *ctx) {
    Inspector inspector = {.ctx = ctx, .result_cache = km_alloc(ctx, (size_t)ctx->next_type + 1)};
    for (size_t i = 0; i < ctx->functions.len; i++) inspect(&inspector, ctx->functions.data[i]->body);
}
