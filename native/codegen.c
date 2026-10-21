#include "kremen.h"

/* Lower through C11, sequencing every source expression explicitly.  In
 * particular, C's unspecified argument evaluation order never leaks into the
 * language, and a compound assignment evaluates its address exactly once. */
typedef struct {
    Type *type;
    char *name;
    unsigned state;
} GenType;

typedef struct {
    Context *ctx;
    Buffer out;
    GenType *types;
    size_t type_count, type_cap;
    unsigned indent, counter;
    char **loops;
    size_t *loop_scopes;
    size_t loop_count, loop_cap;
    char **scope_marks;
    size_t scope_count;
    Node *function;
} Generator;

static char *expression(Generator *, Node *);
static char *lvalue(Generator *, Node *, Buffer *);
static void statement(Generator *, Node *);
static void cleanup_to(Generator *, size_t);

static void line(Generator *g, const char *format, ...)
{
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0) {
        va_end(args);
        abort();
    }
    char *text = km_alloc(g->ctx, (size_t)length + 1);
    (void)vsnprintf(text, (size_t)length + 1, format, args);
    va_end(args);
    for (unsigned i = 0; i < g->indent; ++i) km_buf_puts(&g->out, "    ");
    km_buf_append(&g->out, text, (size_t)length);
    km_buf_puts(&g->out, "\n");
}

static char *fresh(Generator *g, const char *prefix)
{
    return km_sprintf(g->ctx, "kr_%s_%u", prefix, ++g->counter);
}

static size_t remember_type(Generator *g, Type *type)
{
    for (size_t i = 0; i < g->type_count; ++i) {
        if (km_type_equal(g->types[i].type, type)) return i;
    }
    if (g->type_count == g->type_cap) {
        size_t capacity = g->type_cap ? g->type_cap * 2 : 32;
        GenType *grown = km_alloc(g->ctx, capacity * sizeof *grown);
        if (g->type_count) memcpy(grown, g->types, g->type_count * sizeof *grown);
        g->types = grown;
        g->type_cap = capacity;
    }
    size_t index = g->type_count++;
    GenType *entry = &g->types[index];
    entry->type = type;
    entry->state = 0;
    if (type->kind == TY_STRUCT || type->kind == TY_ENUM) {
        entry->name = type->decl ? type->decl->cname : NULL;
        if (!entry->name) entry->name = km_sprintf(g->ctx, "kr_struct_%zu", index);
    } else if (type->kind == TY_ARRAY) {
        entry->name = km_sprintf(g->ctx, "kr_array_%zu", index);
    } else if (type->kind == TY_RESULT) {
        entry->name = km_sprintf(g->ctx, "kr_result_%zu", index);
    } else if (type->kind == TY_TUPLE) {
        entry->name = km_sprintf(g->ctx, "kr_tuple_%zu", index);
    } else {
        entry->name = NULL;
    }
    return index;
}

static char *ctype(Generator *g, Type *type)
{
    if (!type) return "void";
    switch (type->kind) {
    case TY_VOID: return "void";
    case TY_INT: return km_sprintf(g->ctx, "%sint%d_t", type->sign ? "" : "u", type->bits);
    case TY_BOOL: return "bool";
    case TY_STRING: return "kr_string";
    case TY_SLICE: return "kr_slice";
    case TY_ARENA: return "kr_arena *";
    case TY_FILE: return "kr_file *";
    case TY_REF:
    case TY_PTR: return km_sprintf(g->ctx, "%s *", ctype(g, type->elem));
    case TY_ARRAY:
    case TY_RESULT:
    case TY_STRUCT: return g->types[remember_type(g, type)].name;
    case TY_ENUM: case TY_TUPLE: return g->types[remember_type(g, type)].name;
    case TY_FUNCTION: break;
    }
    km_error(g->ctx, type->tok, "E900", "cannot lower this type to C");
}

static void scan_type(Generator *g, Type *type)
{
    if (!type) return;
    size_t index = remember_type(g, type);
    if (g->types[index].state) return;
    g->types[index].state = 1;
    scan_type(g, type->elem);
    for(size_t i=0;i<type->members.len;i++)scan_type(g,type->members.data[i]);
    if(type->kind==TY_ENUM&&type->decl){for(size_t i=0;i<type->decl->fields.len;i++){Node *v=type->decl->fields.data[i];for(size_t j=0;j<v->params.len;j++)scan_type(g,v->params.data[j]->type);}}
    if (type->kind == TY_STRUCT && type->decl) {
        for (size_t i = 0; i < type->decl->fields.len; ++i) {
            Node *field = type->decl->fields.data[i];
            scan_type(g, field->type);
        }
    }
}

static void scan_node(Generator *g, Node *node)
{
    if (!node) return;
    scan_type(g, node->type);
    scan_type(g, node->result);
    scan_type(g, node->typearg);
    if (node->symbol) scan_type(g, node->symbol->type);
    Node *children[] = {node->left, node->right, node->value, node->base,
                        node->index, node->callee, node->condition, node->body,
                        node->otherwise, node->init, node->post, node->receiver};
    for (size_t i = 0; i < sizeof children / sizeof children[0]; ++i) scan_node(g, children[i]);
    Nodes lists[] = {node->items, node->params, node->args, node->fields, node->values};
    for (size_t i = 0; i < sizeof lists / sizeof lists[0]; ++i) {
        for (size_t j = 0; j < lists[i].len; ++j) scan_node(g, lists[i].data[j]);
    }
}

static void define_type(Generator *g, Type *type)
{
    if (!type) return;
    if (type->kind != TY_STRUCT && type->kind != TY_ARRAY && type->kind != TY_RESULT && type->kind != TY_ENUM && type->kind != TY_TUPLE) return;
    size_t index = remember_type(g, type);
    if (g->types[index].state == 3) return;
    if (g->types[index].state == 2) {
        km_error(g->ctx, type->tok, "E900", "recursive value type cannot be represented in C");
    }
    g->types[index].state = 2;
    if (type->kind == TY_STRUCT && type->decl) {
        for (size_t i = 0; i < type->decl->fields.len; ++i) {
            Node *field = type->decl->fields.data[i];
            define_type(g, field->type);
        }
        line(g, "struct %s {", ctype(g, type));
        ++g->indent;
        if (!type->decl->fields.len) line(g, "unsigned char kr_empty;");
        for (size_t i = 0; i < type->decl->fields.len; ++i) {
            Node *field = type->decl->fields.data[i];
            line(g, "%s kr_f_%s;", ctype(g, field->type), field->name);
        }
        --g->indent;
        line(g, "};");
    } else if(type->kind==TY_TUPLE) {
        for(size_t i=0;i<type->members.len;i++)define_type(g,type->members.data[i]);
        line(g,"struct %s {",ctype(g,type));g->indent++;
        if(!type->members.len)line(g,"unsigned char kr_unit;");
        for(size_t i=0;i<type->members.len;i++)line(g,"%s kr_%zu;",ctype(g,type->members.data[i]),i);
        g->indent--;line(g,"};");
    } else if(type->kind==TY_ENUM) {
        for(size_t i=0;i<type->decl->fields.len;i++){Node *v=type->decl->fields.data[i];for(size_t j=0;j<v->params.len;j++)define_type(g,v->params.data[j]->type);}
        line(g,"struct %s { uint32_t tag; union {",ctype(g,type));g->indent++;
        for(size_t i=0;i<type->decl->fields.len;i++){
            Node *v=type->decl->fields.data[i];line(g,"struct {");g->indent++;
            if(!v->params.len)line(g,"unsigned char kr_unit;");
            for(size_t j=0;j<v->params.len;j++)line(g,"%s kr_%zu;",ctype(g,v->params.data[j]->type),j);
            g->indent--;line(g,"} kr_v_%zu;",i);
        }
        g->indent--;line(g,"} payload; };");
    } else if (type->kind == TY_ARRAY) {
        define_type(g, type->elem);
        line(g, "struct %s { %s data[%zu]; };", ctype(g, type), ctype(g, type->elem), type->count ? type->count : 1);
    } else if (type->kind == TY_RESULT) {
        define_type(g, type->elem);
        if (type->elem->kind == TY_VOID) {
            line(g, "struct %s { bool ok; int64_t code; };", ctype(g, type));
        } else {
            line(g, "struct %s { bool ok; %s value; int64_t code; };", ctype(g, type), ctype(g, type->elem));
        }
    }
    g->types[index].state = 3;
}

static char *temporary(Generator *g, Type *type, const char *value)
{
    char *name = fresh(g, "t");
    line(g, "%s %s = %s;", ctype(g, type), name, value);
    return name;
}

static char *integer_literal(Generator *g, Type *type, uint64_t magnitude, bool negative)
{
    char *literal;
    if (negative && magnitude == UINT64_C(9223372036854775808)) {
        literal = "(-INT64_C(9223372036854775807) - INT64_C(1))";
    } else if (negative) {
        literal = km_sprintf(g->ctx, "(-INT64_C(%" PRIu64 "))", magnitude);
    } else {
        literal = km_sprintf(g->ctx, "UINT64_C(%" PRIu64 ")", magnitude);
    }
    return km_sprintf(g->ctx, "((%s)%s)", ctype(g, type), literal);
}

static char *int_suffix(Generator *g, Type *type)
{
    return km_sprintf(g->ctx, "%c%d", type->sign ? 'i' : 'u', type->bits);
}

static char *checked_index(Generator *g, Node *node, char *value)
{
    if (node->type->kind == TY_INT && !node->type->sign) {
        return km_sprintf(g->ctx, "kr_cast_%s_i64(%s, %d)", int_suffix(g, node->type), value, node->tok.line);
    }
    return value;
}

/* A saved storage address may outlive calls in the assignment's right side.
 * Keep snapshots of every slice backing that address so those calls cannot
 * turn a checked address into an unchecked use after free.  Expressions used
 * to obtain a slice descriptor intentionally start a separate place: copying
 * an inner slice out of an outer allocation does not borrow that allocation. */
static char *lvalue(Generator *g, Node *node, Buffer *guards)
{
    switch (node->kind) {
    case N_NAME:
        return node->symbol->cname;
    case N_FIELD: {
        Type *type = node->base->type;
        char *base;
        const char *access;
        if (type->kind == TY_REF || type->kind == TY_PTR) {
            base = expression(g, node->base);
            if (type->kind == TY_PTR) {
                base = km_sprintf(g->ctx, "((%s)kr_nonnull(%s, %d))", ctype(g, type), base, node->tok.line);
            }
            access = "->";
            type = type->elem;
        } else {
            base = lvalue(g, node->base, guards);
            access = ".";
        }
        if (type->kind == TY_RESULT) {
            if (strcmp(node->name, "value") == 0) {
                line(g, "if (!(%s)%sok) kr_panic(\"access to failed result\", %d);", base, access, node->tok.line);
            }
            return km_sprintf(g->ctx, "(%s)%s%s", base, access, node->name);
        }
        if(type->kind==TY_TUPLE)return km_sprintf(g->ctx,"(%s)%skr_%" PRIu64,base,access,node->integer);
        return km_sprintf(g->ctx, "(%s)%skr_f_%s", base, access, node->name);
    }
    case N_INDEX: {
        Type *type = node->base->type;
        char *base;
        if (type->kind == TY_REF || type->kind == TY_PTR) {
            base = expression(g, node->base);
            if (type->kind == TY_PTR) {
                base = km_sprintf(g->ctx, "((%s)kr_nonnull(%s, %d))", ctype(g, type), base, node->tok.line);
            }
            base = km_sprintf(g->ctx, "(*%s)", base);
            type = type->elem;
        } else if (type->kind == TY_SLICE) {
            base = expression(g, node->base);
        } else {
            base = lvalue(g, node->base, guards);
        }
        if (type->kind == TY_SLICE && node->base->type->kind != TY_SLICE) {
            /* A reference points to a descriptor that the index or right side
             * may replace.  Preserve the descriptor selected by the source's
             * left-to-right evaluation order. */
            base = temporary(g, type, base);
        }
        if(type->kind==TY_TUPLE){char *index=expression(g,node->index);line(g,"(void)%s;",index);return km_sprintf(g->ctx,"(%s).kr_%" PRIu64,base,node->integer);}
        char *index = checked_index(g, node->index, expression(g, node->index));
        if (type->kind == TY_SLICE) {
            if (guards) {
                km_buf_printf(guards, "(void)kr_slice_len(%s, %d);\n", base, node->tok.line);
            }
            return km_sprintf(g->ctx, "(*(%s *)kr_slice_at(%s, %s, sizeof(%s), %d))", ctype(g, type->elem), base, index, ctype(g, type->elem), node->tok.line);
        }
        return km_sprintf(g->ctx, "(%s).data[kr_idx(%s, %zu, %d)]", base, index, type->count, node->tok.line);
    }
    case N_UNARY:
        if (strcmp(node->op, "*") == 0) {
            char *pointer = expression(g, node->value);
            if (node->value->type->kind == TY_PTR) {
                pointer = km_sprintf(g->ctx, "((%s)kr_nonnull(%s, %d))", ctype(g, node->value->type), pointer, node->tok.line);
            }
            return km_sprintf(g->ctx, "(*%s)", pointer);
        }
        break;
    default: break;
    }
    return expression(g, node);
}

static const char *arithmetic(const char *op)
{
    if (!strcmp(op, "+")) return "add";
    if (!strcmp(op, "-")) return "sub";
    if (!strcmp(op, "*")) return "mul";
    if (!strcmp(op, "/")) return "div";
    if (!strcmp(op, "%")) return "mod";
    if (!strcmp(op, "<<")) return "shl";
    if (!strcmp(op, ">>")) return "shr";
    return NULL;
}

static char *call(Generator *g, Node *node)
{
    const char *builtin = node->builtin;
    int source_line = node->tok.line;
    Buffer arguments = {0};
    char **values = km_alloc(g->ctx, (node->args.len + 1) * sizeof *values);
    if (!builtin && node->receiver) {
        Type *expected = node->function->receiver->type;
        char *receiver;
        if (expected->kind == TY_REF && node->receiver->type->kind != TY_REF) {
            receiver = temporary(g, expected, km_sprintf(g->ctx, "&(%s)", lvalue(g, node->receiver, NULL)));
        } else {
            receiver = expression(g, node->receiver);
        }
        km_buf_puts(&arguments, receiver);
    }
    for (size_t i = 0; i < node->args.len; ++i) {
        values[i] = expression(g, node->args.data[i]);
        if (arguments.len) km_buf_puts(&arguments, ", ");
        km_buf_puts(&arguments, values[i]);
    }
    char *args = km_strndup(g->ctx, arguments.data ? arguments.data : "", arguments.len);
    free(arguments.data);
    if (builtin && (!strcmp(builtin, "print") || !strcmp(builtin, "println"))) {
        for (size_t i = 0; i < node->args.len; ++i) {
            if (i) line(g, "kr_space();");
            Type *type = node->args.data[i]->type;
            const char *suffix = type->kind == TY_INT ? (type->sign ? "i64" : "u64") : (type->kind == TY_BOOL ? "bool" : "string");
            line(g, "kr_print_%s(%s);", suffix, values[i]);
        }
        if (!strcmp(builtin, "println")) line(g, "kr_newline();");
        return "";
    }
    if (builtin && !strcmp(builtin, "len")) {
        Type *type = node->args.data[0]->type;
        if (type->kind == TY_ARRAY) {
            line(g, "(void)%s;", values[0]);
            return temporary(g, node->type, integer_literal(g, node->type, (uint64_t)type->count, false));
        }
        return temporary(g, node->type, km_sprintf(g->ctx, "kr_%s_len(%s, %d)", type->kind == TY_SLICE ? "slice" : "str", values[0], source_line));
    }
    if (builtin && !strcmp(builtin, "readInt")) return temporary(g, node->type, km_sprintf(g->ctx, "kr_read_int(%d)", source_line));
    if (builtin && !strcmp(builtin, "readLine")) return temporary(g, node->type, km_sprintf(g->ctx, "kr_read_line(%d)", source_line));
    if (builtin && !strcmp(builtin, "readIntResult")) {
        char *result = temporary(g, node->type, "{0}");
        line(g, "%s.ok = kr_try_read_int(&%s.value, &%s.code);", result, result, result);
        return result;
    }
    if (builtin && !strcmp(builtin, "assert")) {
        line(g, "kr_assert(%s, %d);", values[0], source_line);
        return "";
    }
    if (builtin && !strcmp(builtin, "panic")) {
        line(g, "kr_user_panic(%s, %d);", values[0], source_line);
        return "";
    }
    if (builtin && !strcmp(builtin, "cast")) {
        return temporary(g, node->type, km_sprintf(g->ctx, "kr_cast_%s_%s(%s, %d)", int_suffix(g, node->args.data[0]->type), int_suffix(g, node->type), values[0], source_line));
    }
    if (builtin && !strcmp(builtin, "raw")) return temporary(g, node->type, values[0]);
    if(builtin&&!strcmp(builtin,"arenaNew"))return temporary(g,node->type,km_sprintf(g->ctx,"kr_arena_new(%d)",source_line));
    if(builtin&&(!strcmp(builtin,"arenaFree")||!strcmp(builtin,"arenaReset"))){line(g,"kr_arena_%s(%s, %d);",!strcmp(builtin,"arenaFree")?"free":"reset",values[0],source_line);return "";}
    if(builtin&&!strcmp(builtin,"arenaAlloc"))return temporary(g,node->type,km_sprintf(g->ctx,"kr_arena_alloc(%s, %s, sizeof(%s), %d)",values[0],values[1],ctype(g,node->type->elem),source_line));
    if(builtin&&!strcmp(builtin,"fileClose")){line(g,"kr_file_close(%s, %d);",values[0],source_line);return "";}
    if(builtin&&!strcmp(builtin,"errorMessage"))return temporary(g,node->type,km_sprintf(g->ctx,"kr_error_message(%s)",values[0]));
    if(builtin&&!strncmp(builtin,"fs",2)){
        char *r=temporary(g,node->type,"{0}");
        if(!strcmp(builtin,"fsRead"))line(g,"%s.ok = kr_fs_read(%s, &%s.value, &%s.code);",r,values[0],r,r);
        else if(!strcmp(builtin,"fsReadAll"))line(g,"%s.ok = kr_fs_read_all(%s, &%s.value, &%s.code);",r,values[0],r,r);
        else if(!strcmp(builtin,"fsWriteAll"))line(g,"%s.ok = kr_fs_write_all(%s, %s, &%s.value, &%s.code);",r,values[0],values[1],r,r);
        else if(!strcmp(builtin,"fsOpen"))line(g,"%s.ok = kr_fs_open(%s, %s, &%s.value, &%s.code);",r,values[0],values[1],r,r);
        else if(!strcmp(builtin,"fsList"))line(g,"%s.ok = kr_fs_list(%s, &%s.value, &%s.code);",r,values[0],r,r);
        else if(!strcmp(builtin,"fsCwd"))line(g,"%s.ok = kr_fs_cwd(&%s.value, &%s.code);",r,r,r);
        else if(!strcmp(builtin,"fsExists")||!strcmp(builtin,"fsIsDir")||!strcmp(builtin,"fsSize")){
            char *raw=temporary(g,g->ctx->ints[3],"0");int op=!strcmp(builtin,"fsExists")?0:!strcmp(builtin,"fsIsDir")?1:2;
            line(g,"%s.ok = kr_fs_stat(%s, %d, &%s, &%s.code);",r,values[0],op,raw,r);
            line(g,"%s.value = %s%s;",r,raw,op==2?"":" != 0");
        }else{
            if(!strcmp(builtin,"fsWrite")||!strcmp(builtin,"fsAppend"))line(g,"%s.ok = kr_fs_write(%s, %s, %s, &%s.code);",r,values[0],values[1],!strcmp(builtin,"fsAppend")?"true":"false",r);
            else if(!strcmp(builtin,"fsMkdir")||!strcmp(builtin,"fsRemove"))line(g,"%s.ok = kr_fs_modify(%s, %d, &%s.code);",r,values[0],!strcmp(builtin,"fsMkdir")?0:1,r);
            else if(!strcmp(builtin,"fsRename"))line(g,"%s.ok = kr_fs_rename(%s, %s, &%s.code);",r,values[0],values[1],r);
            else if(!strcmp(builtin,"fsClose"))line(g,"%s.ok = kr_fs_close(%s, &%s.code);",r,values[0],r);
            else km_error(g->ctx,node->tok,"E900","filesystem builtin missing in backend");
            line(g,"%s.value = %s.ok;",r,r);
        }
        return r;
    }
    if(builtin&&!strcmp(builtin,"variant")){
        Buffer payload={0};
        for(size_t i=0;i<node->args.len;i++)km_buf_printf(&payload,"%s.kr_%zu = %s",i?", ":"",i,values[i]);
        char *r=temporary(g,node->type,km_sprintf(g->ctx,"(%s){.tag = %" PRIu64 ", .payload.kr_v_%" PRIu64 " = {%s}}",ctype(g,node->type),node->integer,node->integer,payload.len?payload.data:"0"));free(payload.data);return r;
    }
    if (builtin && !strcmp(builtin, "alloc")) {
        char *count = checked_index(g, node->args.data[0], values[0]);
        return temporary(g, node->type, km_sprintf(g->ctx, "kr_slice_alloc(%s, sizeof(%s), %d)", count, ctype(g, node->type->elem), source_line));
    }
    if (builtin && !strcmp(builtin, "free")) {
        line(g, "kr_slice_free(%s, %d);", values[0], source_line);
        return "";
    }
    if (builtin && !strcmp(builtin, "slice")) {
        char *low = checked_index(g, node->args.data[1], values[1]);
        char *high = checked_index(g, node->args.data[2], values[2]);
        return temporary(g, node->type, km_sprintf(g->ctx, "kr_subslice(%s, %s, %s, %d)", values[0], low, high, source_line));
    }
    if (builtin && !strcmp(builtin, "ok")) {
        return temporary(g, node->type, km_sprintf(g->ctx, "(%s){.ok = true, .value = %s, .code = 0}", ctype(g, node->type), values[0]));
    }
    if (builtin && !strcmp(builtin, "fail")) {
        line(g, "if (%s <= 0) kr_panic(\"failure code must be positive\", %d);", values[0], source_line);
        return temporary(g, node->type, km_sprintf(g->ctx, "(%s){.ok = false, .code = %s}", ctype(g, node->type), values[0]));
    }
    if (builtin) km_error(g->ctx, node->tok, "E900", "unknown builtin in backend: %s", builtin);
    Node *function = node->function;
    char *code = km_sprintf(g->ctx, "%s(%s)", function->cname, args);
    if (node->type->kind == TY_VOID) {
        line(g, "%s;", code);
        return "";
    }
    return temporary(g, node->type, code);
}

static char *expression(Generator *g, Node *node)
{
    Type *type = node->type;
    int source_line = node->tok.line;
    switch (node->kind) {
    case N_INT:
        return temporary(g, type, integer_literal(g, type, node->integer, node->negative));
    case N_BOOL:
        return temporary(g, type, node->flag ? "true" : "false");
    case N_STRING: {
        Buffer bytes = {0};
        if (node->text_len <= 4000) {
            km_buf_puts(&bytes, "(kr_string){\"");
            for (size_t i = 0; i < node->text_len; ++i) km_buf_printf(&bytes, "\\%03o", (unsigned)(unsigned char)node->text[i]);
            km_buf_printf(&bytes, "\", %zu}", node->text_len);
        } else {
            char *data = fresh(g, "bytes");
            km_buf_printf(&bytes, "static const unsigned char %s[] = {", data);
            for (size_t i = 0; i < node->text_len; ++i) km_buf_printf(&bytes, "%s%u", i ? "," : "", (unsigned)(unsigned char)node->text[i]);
            km_buf_puts(&bytes, "};");
            line(g, "%s", bytes.data);
            bytes.len = 0;
            bytes.data[0] = '\0';
            km_buf_printf(&bytes, "(kr_string){(const char *)%s, %zu}", data, node->text_len);
        }
        char *value = temporary(g, type, bytes.data);
        free(bytes.data);
        return value;
    }
    case N_NAME:
    case N_FIELD:
    case N_INDEX:
        return temporary(g, type, lvalue(g, node, NULL));
    case N_UNARY: {
        if (!strcmp(node->op, "-") && node->constant) {
            return temporary(g, type, integer_literal(g, type, node->constant_value, node->constant_negative));
        }
        if (!strcmp(node->op, "&") || !strcmp(node->op, "&mut")) {
            return temporary(g, type, km_sprintf(g->ctx, "&(%s)", lvalue(g, node->value, NULL)));
        }
        if (!strcmp(node->op, "*")) return temporary(g, type, lvalue(g, node, NULL));
        char *value = expression(g, node->value);
        if (!strcmp(node->op, "-")) return temporary(g, type, km_sprintf(g->ctx, "kr_neg_%s(%s, %d)", int_suffix(g, type), value, source_line));
        return temporary(g, type, km_sprintf(g->ctx, "(%s)(%s%s)", ctype(g, type), node->op, value));
    }
    case N_BINARY: {
        char *left = expression(g, node->left);
        if (!strcmp(node->op, "&&") || !strcmp(node->op, "||")) {
            char *result = temporary(g, type, left);
            line(g, "if (%s%s) {", !strcmp(node->op, "||") ? "!" : "", result);
            ++g->indent;
            char *right = expression(g, node->right);
            line(g, "%s = %s;", result, right);
            --g->indent;
            line(g, "}");
            return result;
        }
        char *right = expression(g, node->right);
        const char *operation = arithmetic(node->op);
        if (operation) return temporary(g, type, km_sprintf(g->ctx, "kr_%s_%s(%s, %s, %d)", operation, int_suffix(g, type), left, right, source_line));
        if (node->left->type->kind == TY_STRING) {
            return temporary(g, type, km_sprintf(g->ctx, "%skr_str_eq(%s, %s)", !strcmp(node->op, "!=") ? "!" : "", left, right));
        }
        return temporary(g, type, km_sprintf(g->ctx, "(%s)(%s %s %s)", ctype(g, type), left, node->op, right));
    }
    case N_STRUCTLIT: {
        Buffer fields = {0};
        for (size_t i = 0; i < node->fields.len; ++i) {
            Node *field = node->fields.data[i];
            char *value = expression(g, field->value);
            km_buf_printf(&fields, "%s.kr_f_%s = %s", i ? ", " : "", field->name, value);
        }
        char *result = temporary(g, type, km_sprintf(g->ctx, "(%s){%s}", ctype(g, type), fields.len ? fields.data : "0"));
        free(fields.data);
        return result;
    }
    case N_ARRAYLIT: {
        Buffer values = {0};
        for (size_t i = 0; i < node->values.len; ++i) {
            char *value = expression(g, node->values.data[i]);
            km_buf_printf(&values, "%s%s", i ? ", " : "", value);
        }
        char *result = temporary(g, type, km_sprintf(g->ctx, "(%s){.data = {%s}}", ctype(g, type), values.len ? values.data : "0"));
        free(values.data);
        return result;
    }
    case N_TUPLE: {
        Buffer fields={0};
        for(size_t i=0;i<node->values.len;i++){char *v=expression(g,node->values.data[i]);km_buf_printf(&fields,"%s.kr_%zu = %s",i?", ":"",i,v);}
        char *r=temporary(g,type,km_sprintf(g->ctx,"(%s){%s}",ctype(g,type),fields.len?fields.data:"0"));free(fields.data);return r;
    }
    case N_CALL: return call(g, node);
    case N_TRY: {
        Node *operand = node->value ? node->value : node->base;
        char *result = expression(g, operand);
        line(g, "if (!%s.ok) {", result);g->indent++;cleanup_to(g,0);
        line(g,"return (%s){.ok = false, .code = %s.code};",ctype(g,g->function->result),result);g->indent--;line(g,"}");
        return temporary(g, type, km_sprintf(g->ctx, "%s.value", result));
    }
    case N_CATCH: {
        char *r=expression(g,node->value);char *out=temporary(g,type,"{0}");
        line(g,"if (%s.ok) { %s = %s.value; } else {",r,out,r);g->indent++;
        if(node->symbol){line(g,"int64_t %s = %s.code;",node->symbol->cname,r);line(g,"(void)%s;",node->symbol->cname);}
        char *fallback=expression(g,node->right);line(g,"%s = %s;",out,fallback);g->indent--;line(g,"}");return out;
    }
    default:
        km_error(g->ctx, node->tok, "E900", "expression is unsupported by the C backend");
    }
}

static void cleanup_to(Generator *g,size_t baseline) {
    if(baseline<g->scope_count)line(g,"kr_defer_run_until(%s);",g->scope_marks[baseline]);
}
static void open_cleanup(Generator *g) {
    if(g->scope_count>=257)km_error(g->ctx,(Token){0},"E111","cleanup nesting limit exceeded");
    char *mark=fresh(g,"cleanup_mark");line(g,"kr_defer_node *%s = kr_defers;",mark);g->scope_marks[g->scope_count++]=mark;
}
static void close_cleanup(Generator *g) { cleanup_to(g,g->scope_count-1);g->scope_count--; }
static Node *captured_name(Generator *g,Type *type,const char *field,Token tok) {
    Node *n=km_node(g->ctx,N_NAME,tok);n->type=type;n->symbol=km_alloc(g->ctx,sizeof *n->symbol);n->symbol->type=type;n->symbol->cname=km_strdup(g->ctx,field);return n;
}
static void defer_callbacks(Generator *g,Node *node) {
    if(!node)return;
    if(node->kind==N_DEFER){
        Node *source=node->value;node->cname=fresh(g,"defer");
        line(g,"typedef struct {");g->indent++;
        if(source->receiver)line(g,"%s receiver;",ctype(g,source->function->receiver->type));
        for(size_t i=0;i<source->args.len;i++)line(g,"%s arg_%zu;",ctype(g,source->args.data[i]->type),i);
        if(!source->receiver&&!source->args.len)line(g,"unsigned char unit;");
        g->indent--;line(g,"} %s_data;",node->cname);
        line(g,"void %s_call(void *opaque) {",node->cname);g->indent++;
        line(g,"%s_data *captured = opaque;",node->cname);line(g,"(void)captured;");
        Node copy=*source;copy.args=(Nodes){0};
        if(source->receiver)copy.receiver=captured_name(g,source->function->receiver->type,"captured->receiver",source->tok);
        for(size_t i=0;i<source->args.len;i++)km_nodes_push(g->ctx,&copy.args,captured_name(g,source->args.data[i]->type,km_sprintf(g->ctx,"captured->arg_%zu",i),source->tok));
        (void)call(g,&copy);g->indent--;line(g,"}");
    }
    Node *children[]={node->left,node->right,node->value,node->base,node->index,node->callee,node->condition,node->body,node->otherwise,node->init,node->post,node->receiver};
    for(size_t i=0;i<sizeof children/sizeof children[0];i++)defer_callbacks(g,children[i]);
    Nodes vectors[]={node->items,node->params,node->args,node->fields,node->values};
    for(size_t i=0;i<sizeof vectors/sizeof vectors[0];i++)for(size_t j=0;j<vectors[i].len;j++)defer_callbacks(g,vectors[i].data[j]);
}
static void register_defer(Generator *g,Node *node) {
    Node *source=node->value;char *data=fresh(g,"defer_data");line(g,"%s_data %s = {0};",node->cname,data);
    if(source->receiver){
        Type *expected=source->function->receiver->type;char *receiver;
        if(expected->kind==TY_REF&&source->receiver->type->kind!=TY_REF)receiver=temporary(g,expected,km_sprintf(g->ctx,"&(%s)",lvalue(g,source->receiver,NULL)));
        else receiver=expression(g,source->receiver);
        line(g,"%s.receiver = %s;",data,receiver);
    }
    for(size_t i=0;i<source->args.len;i++){char *value=expression(g,source->args.data[i]);line(g,"%s.arg_%zu = %s;",data,i,value);}
    char *entry=fresh(g,"defer_entry");line(g,"kr_defer_node %s = {0};",entry);line(g,"kr_defer_push(&%s, %s_call, &%s);",entry,node->cname,data);
}
static void block(Generator *g, Node *node, bool braces)
{
    if (braces) {
        line(g, "{");
        ++g->indent;
    }
    bool cleanup=false;for(size_t i=0;i<node->items.len;i++)if(node->items.data[i]->kind==N_DEFER)cleanup=true;
    if(cleanup)open_cleanup(g);
    for (size_t i = 0; i < node->items.len; ++i) statement(g, node->items.data[i]);
    if(cleanup)close_cleanup(g);
    if (braces) {
        --g->indent;
        line(g, "}");
    }
}

static void push_loop(Generator *g, char *label)
{
    if (g->loop_count == g->loop_cap) {
        size_t capacity = g->loop_cap ? g->loop_cap * 2 : 8;
        char **grown = km_alloc(g->ctx, capacity * sizeof *grown);
        size_t *scopes=km_alloc(g->ctx,capacity*sizeof *scopes);
        if (g->loop_count) memcpy(grown, g->loops, g->loop_count * sizeof *grown);
        if(g->loop_count)memcpy(scopes,g->loop_scopes,g->loop_count*sizeof *scopes);
        g->loops = grown;
        g->loop_scopes=scopes;
        g->loop_cap = capacity;
    }
    g->loop_scopes[g->loop_count]=g->scope_count;g->loops[g->loop_count++] = label;
}

static void statement(Generator *g, Node *node)
{
    switch (node->kind) {
    case N_BLOCK: block(g, node, true); break;
    case N_DEFER:register_defer(g,node);break;
    case N_ARENA: {
        line(g,"{");g->indent++;open_cleanup(g);
        line(g,"kr_arena *%s = kr_arena_new(%d);",node->symbol->cname,node->tok.line);
        char *entry=fresh(g,"arena_cleanup");line(g,"kr_defer_node %s = {0};",entry);
        line(g,"kr_defer_push(&%s, kr_arena_defer, &%s);",entry,node->symbol->cname);
        block(g,node->body,false);close_cleanup(g);g->indent--;line(g,"}");break;
    }
    case N_DESTRUCT: {
        char *tuple=expression(g,node->value);
        line(g,"(void)%s;",tuple);
        for(size_t i=0;i<node->params.len;i++){Node *b=node->params.data[i];if(b->symbol){line(g,"%s %s = %s.kr_%zu;",ctype(g,b->type),b->symbol->cname,tuple,i);line(g,"(void)%s;",b->symbol->cname);}}
        if(!node->params.len)line(g,"(void)%s;",tuple);
        break;
    }
    case N_MATCH: {
        char *subject=expression(g,node->value);bool fallback=false;
        line(g,"(void)%s;",subject);
        for(size_t i=0;i<node->items.len;i++){
            Node *arm=node->items.data[i];bool any=arm->integer==UINT64_MAX;
            if(any){line(g,"%s{",i?"else ":"");fallback=true;}
            else if(node->type->kind==TY_RESULT)line(g,"%sif (%s%s.ok) {",i?"else ":"",arm->integer?"!":"",subject);
            else line(g,"%sif (%s.tag == %" PRIu64 ") {",i?"else ":"",subject,arm->integer);
            g->indent++;
            for(size_t j=0;j<arm->params.len;j++){Node *b=arm->params.data[j];if(!b->symbol)continue;
                char *value=node->type->kind==TY_RESULT?km_sprintf(g->ctx,"%s.%s",subject,arm->integer?"code":"value"):km_sprintf(g->ctx,"%s.payload.kr_v_%" PRIu64 ".kr_%zu",subject,arm->integer,j);
                line(g,"%s %s = %s;",ctype(g,b->type),b->symbol->cname,value);line(g,"(void)%s;",b->symbol->cname);
            }
            block(g,arm->body,false);g->indent--;line(g,"}");
        }
        if(!fallback)line(g,"else { kr_panic(\"invalid variant tag\", %d); }",node->tok.line);
        break;
    }
    case N_VAR: {
        char *value = node->value ? expression(g, node->value) : "{0}";
        line(g, "%s %s = %s;", ctype(g, node->symbol->type), node->symbol->cname, value);
        line(g, "(void)%s;", node->symbol->cname);
        break;
    }
    case N_ASSIGN: {
        Node *target = node->left;
        Node *source = node->value ? node->value : node->right;
        if (target->kind == N_NAME && !strcmp(target->name, "_")) {
            char *value = expression(g, source);
            if (*value) line(g, "(void)%s;", value);
            break;
        }
        Buffer guards = {0};
        char *target_value = lvalue(g, target, &guards);
        char *address = fresh(g, "addr");
        line(g, "%s *%s = &(%s);", ctype(g, target->type), address, target_value);
        char *previous = NULL;
        if (strcmp(node->op, "=")) previous = temporary(g, target->type, km_sprintf(g->ctx, "*%s", address));
        char *value = expression(g, source);
        if (previous) {
            size_t length = strlen(node->op);
            char *op = km_strndup(g->ctx, node->op, length - 1);
            const char *operation = arithmetic(op);
            if (operation) value = km_sprintf(g->ctx, "kr_%s_%s(%s, %s, %d)", operation, int_suffix(g, target->type), previous, value, node->tok.line);
            else value = km_sprintf(g->ctx, "(%s)(%s %s %s)", ctype(g, target->type), previous, op, value);
        }
        if (guards.len) {
            /* Revalidation must precede even reading the saved C pointer: its
             * allocation may have been freed while evaluating the right side. */
            for (unsigned i = 0; i < g->indent; ++i) km_buf_puts(&g->out, "    ");
            km_buf_append(&g->out, guards.data, guards.len);
            free(guards.data);
        }
        line(g, "*%s = %s;", address, value);
        break;
    }
    case N_EXPRSTMT: {
        char *value = expression(g, node->value);
        if (*value) line(g, "(void)%s;", value);
        break;
    }
    case N_IF: {
        char *condition = expression(g, node->condition);
        line(g, "if (%s)", condition);
        block(g, node->body, true);
        if (node->otherwise) {
            line(g, "else");
            if (node->otherwise->kind == N_IF) {
                line(g, "{");
                ++g->indent;
                statement(g, node->otherwise);
                --g->indent;
                line(g, "}");
            } else block(g, node->otherwise, true);
        }
        break;
    }
    case N_FOR: {
        line(g, "{");
        ++g->indent;
        if (node->init) statement(g, node->init);
        line(g, "for (;;) {");
        ++g->indent;
        if (node->condition) {
            char *condition = expression(g, node->condition);
            line(g, "if (!%s) break;", condition);
        }
        char *label = fresh(g, "continue");
        push_loop(g, label);
        block(g, node->body, true);
        --g->loop_count;
        line(g, "goto %s;", label);
        line(g, "%s: ;", label);
        if (node->post) statement(g, node->post);
        --g->indent;
        line(g, "}");
        --g->indent;
        line(g, "}");
        break;
    }
    case N_RETURN: {
        char *value = node->value ? expression(g, node->value) : "";
        cleanup_to(g,0);
        line(g, "return%s%s;", *value ? " " : "", value);
        break;
    }
    case N_UNSAFE: block(g, node->body, true); break;
    case N_BREAK: cleanup_to(g,g->loop_scopes[g->loop_count-1]);line(g, "break;"); break;
    case N_CONTINUE: cleanup_to(g,g->loop_scopes[g->loop_count-1]);line(g, "goto %s;", g->loops[g->loop_count - 1]); break;
    default: km_error(g->ctx, node->tok, "E900", "statement is unsupported by the C backend");
    }
}

static char *signature(Generator *g, Node *function)
{
    Buffer params = {0};
    if (function->receiver) {
        Node *receiver = function->receiver;
        km_buf_printf(&params, "%s %s", ctype(g, receiver->type), receiver->symbol ? receiver->symbol->cname : "kr_receiver");
    }
    for (size_t i = 0; i < function->params.len; ++i) {
        Node *param = function->params.data[i];
        if (params.len) km_buf_puts(&params, ", ");
        km_buf_printf(&params, "%s %s", ctype(g, param->type), param->symbol ? param->symbol->cname : km_sprintf(g->ctx, "kr_arg_%zu", i));
    }
    char *result = km_sprintf(g->ctx, "%s %s(%s)", ctype(g, function->result), function->cname, params.len ? params.data : "void");
    free(params.data);
    return result;
}

char *km_codegen(Context *ctx)
{
    Generator generator = {.ctx = ctx};
    Generator *g = &generator;
    g->scope_marks=km_alloc(ctx,257*sizeof *g->scope_marks);
    Source *runtime = km_read_source(ctx, ctx->runtime_path, false);
    km_buf_puts(&g->out, "/* Generated by the native Kremen compiler. */\n");
    km_buf_append(&g->out, runtime->data, runtime->len);
    km_buf_puts(&g->out, "\n");
    for (size_t i = 0; i < ctx->structs.len; ++i) scan_node(g, ctx->structs.data[i]);
    for (size_t i = 0; i < ctx->functions.len; ++i) scan_node(g, ctx->functions.data[i]);
    for (size_t i = 0; i < g->type_count; ++i) {
        TypeKind kind = g->types[i].type->kind;
        if (kind == TY_STRUCT || kind == TY_ARRAY || kind == TY_RESULT || kind == TY_TUPLE || kind == TY_ENUM) {
            line(g, "typedef struct %s %s;", g->types[i].name, g->types[i].name);
        }
    }
    for (size_t i = 0; i < g->type_count; ++i) define_type(g, g->types[i].type);
    for (size_t i = 0; i < ctx->functions.len; ++i) line(g, "%s;", signature(g, ctx->functions.data[i]));
    for(size_t i=0;i<ctx->functions.len;i++)defer_callbacks(g,ctx->functions.data[i]->body);
    Node *entry = NULL;
    for (size_t i = 0; i < ctx->functions.len; ++i) {
        Node *function = ctx->functions.data[i];
        if (!function->external && !function->receiver && !strcmp(function->name, "main") && function->module == ctx->modules.data[0]) entry = function;
        if (function->external || !function->body) continue;
        g->function = function;
        line(g, "%s", signature(g, function));
        line(g, "{");
        ++g->indent;
        if (function->receiver) line(g, "(void)%s;", function->receiver->symbol->cname);
        for (size_t j = 0; j < function->params.len; ++j) line(g, "(void)%s;", function->params.data[j]->symbol->cname);
        block(g, function->body, false);
        if (function->result->kind != TY_VOID) line(g, "kr_panic(\"unreachable end of function\", %d);", function->tok.line);
        --g->indent;
        line(g, "}");
    }
    if (!entry) km_error(ctx, (Token){0}, "E900", "entry point is missing during code generation");
    line(g, "int main(void)");
    line(g, "{");
    ++g->indent;
    line(g, "%s();", entry->cname);
    line(g, "return 0;");
    --g->indent;
    line(g, "}");
    char *result = km_strndup(ctx, g->out.data, g->out.len);
    free(g->out.data);
    return result;
}
