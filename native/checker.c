#include "kremen.h"
#include <limits.h>

/* Lexical, whole-owner loans are intentionally conservative. A raw conversion
 * keeps its source loan; unsafe removes pointer checks, never lifetime checks. */
typedef struct Binding Binding;
typedef struct Loan Loan;
typedef struct Scope Scope;
typedef struct {
    const char *name; Module *module; Node *receiver, *node;
    size_t index; uint64_t hash; bool repeated;
} Declaration;
struct Binding {
    Symbol *sym; bool alive, constant, neg; uint64_t value;
    Binding *next, *hash_next, *scope_next;
};
struct Scope { Scope *parent; Binding *bindings; Loan *entry_loans; };
struct Loan { Symbol *root, *owner; bool mut; unsigned depth; Token tok; Loan *next; };
typedef struct {
    Context *ctx; Node *fn; unsigned depth, loops, unsafe_depth;
    Binding *bindings; Loan *loans; bool types_ready;
    Declaration *struct_names, *function_names; size_t declaration_cap;
    Binding **local_names; size_t local_cap, local_count; Scope *scope; bool deferred_call;
} Checker;
typedef struct { Type *type; Symbol *root, *via; } Place;
typedef struct { uint64_t value; bool neg; } Number;

static Type *expr(Checker *, Node *, Type *, bool);
static bool statement(Checker *, Node *);
static bool block(Checker *, Node *, bool);
static Type *valid_type(Checker *, Type *, Node *, bool);
static Type *hint(Checker *, Node *);
static Place place(Checker *, Node *, bool, bool);
static Type *builtin_type(Checker *, TypeKind);
static char *type_expression_name(Checker *, Node *);
static size_t tuple_field(Checker *c,Type *t,const char *name,Node *n) {
    size_t value=0;
    if(!name||!*name)km_error(c->ctx,n->tok,"E140","tuple field must be a numeric index");
    for(const char *s=name;*s;s++){if(*s<'0'||*s>'9'||value>32)km_error(c->ctx,n->tok,"E140","tuple field must be a numeric index");value=value*10+(size_t)(*s-'0');}
    if(value>=t->members.len)km_error(c->ctx,n->tok,"E140","tuple index is outside its member list");
    n->integer=value;return value;
}
static bool eq(const char *a, const char *b) { return a && b && strcmp(a,b)==0; }
static Context *cx(Checker *c) { return c->ctx; }
static Module *module_of(Node *n) { return n->module ? n->module : n->tok.src ? n->tok.src->module : NULL; }
typedef struct { const char *name; Token tok; unsigned distance, examined; bool tied; } Suggestion;
static void suggest_candidate(Suggestion *best,const char *wanted,const char *candidate,Token tok) {
    /* Suggestions are an error-path convenience, never an unbounded pass over
     * a hostile symbol table. Very short names tend to produce noisy guesses. */
    if(!candidate||!wanted[0]||!wanted[1]||!wanted[2]||best->examined++>=1024)return;
    unsigned distance=km_name_distance(wanted,candidate);
    if(!distance||distance>2)return;
    if(distance<best->distance){best->name=candidate;best->tok=tok;best->distance=distance;best->tied=false;}
    else if(distance==best->distance&&!eq(best->name,candidate))best->tied=true;
}
static void suggest_finish(Checker *c,Suggestion *best) {
    if(!best->name||best->tied)return;
    cx(c)->diagnostic_hint=km_sprintf(cx(c),"Did you mean '%s'?",best->name);
    if(best->tok.src){cx(c)->related_token=best->tok;cx(c)->related_message="suggested declaration is here";}
}
/* Tables are private to one semantic pass. Canonical struct declarations make
 * receiver identity stable across imports and header/implementation merging. */
static uint64_t name_hash(const char *name) {
    uint64_t h=UINT64_C(14695981039346656037);
    for(const unsigned char *p=(const unsigned char *)name;*p;p++) {h^=*p;h*=UINT64_C(1099511628211);}
    return h;
}
static Declaration *declaration_slot(Declaration *slots,size_t cap,Module *module,Node *receiver,const char *name) {
    uint64_t hash=name_hash(name);
    uintptr_t identity=(uintptr_t)(receiver?(void *)receiver:(void *)module);
    hash^=(uint64_t)(identity>>4)*UINT64_C(11400714819323198485);
    if(receiver)hash^=UINT64_C(0xd1b54a32d192ed03);
    size_t slot=(size_t)hash&(cap-1);
    while(slots[slot].name) {
        Declaration *d=&slots[slot];
        if(d->hash==hash&&d->module==module&&d->receiver==receiver&&eq(d->name,name))return d;
        slot=(slot+1)&(cap-1);
    }
    return &slots[slot];
}
static Declaration *struct_slot(Checker *c,Module *module,const char *name) {
    return declaration_slot(c->struct_names,c->declaration_cap,module,NULL,name);
}
static Declaration *function_slot(Checker *c,Module *module,const char *name,Type *receiver) {
    return declaration_slot(c->function_names,c->declaration_cap,receiver?NULL:module,receiver?receiver->decl:NULL,name);
}
static void register_declaration(Declaration *d,Module *module,Node *receiver,Node *n,size_t index) {
    d->name=n->name;d->module=module;d->receiver=receiver;d->node=n;d->index=index;
    d->hash=name_hash(n->name);
    uintptr_t identity=(uintptr_t)(receiver?(void *)receiver:(void *)module);
    d->hash^=(uint64_t)(identity>>4)*UINT64_C(11400714819323198485);
    if(receiver)d->hash^=UINT64_C(0xd1b54a32d192ed03);
}
static Binding *local_binding(Checker *c,const char *name) {
    if(!c->local_cap)return NULL;
    size_t slot=(size_t)name_hash(name)&(c->local_cap-1);
    for(Binding *b=c->local_names[slot];b;b=b->hash_next)
        if(eq(b->sym->name,name))return b;
    return NULL;
}
static void grow_locals(Checker *c) {
    size_t cap=c->local_cap?c->local_cap*2:16;
    Binding **slots=km_alloc(cx(c),cap*sizeof(*slots));
    for(Binding *b=c->bindings;b;b=b->next)if(b->alive) {
        size_t slot=(size_t)name_hash(b->sym->name)&(cap-1);
        b->hash_next=slots[slot];slots[slot]=b;
    }
    c->local_names=slots;c->local_cap=cap;
}
static size_t module_id(Context *c, Module *m) {
    for(size_t i=0;i<c->modules.len;i++) if(c->modules.data[i]==m) return i;
    return 0;
}
static bool primitive_name(const char *s) {
    static const char *names[]={"void","bool","string","arena","file","thread","i8","i16","i32","i64","u8","u16","u32","u64"};
    for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++) if(eq(s,names[i])) return true;
    return false;
}
static Type *int_named(Context *c,const char *s) {
    for(size_t i=0;i<8;i++) if(eq(c->ints[i]->name,s)) return c->ints[i];
    return NULL;
}
typedef struct { const char *name; TypeKind result; size_t count; TypeKind args[2]; bool fallible, unsafe_call; } BuiltinSpec;
static const BuiltinSpec resource_specs[] = {
    {"fsRead",TY_STRING,1,{TY_STRING,TY_VOID},true,false},
    {"fsWrite",TY_BOOL,2,{TY_STRING,TY_STRING},true,false},
    {"fsAppend",TY_BOOL,2,{TY_STRING,TY_STRING},true,false},
    {"fsExists",TY_BOOL,1,{TY_STRING,TY_VOID},true,false},
    {"fsIsDir",TY_BOOL,1,{TY_STRING,TY_VOID},true,false},
    {"fsSize",TY_INT,1,{TY_STRING,TY_VOID},true,false},
    {"fsMkdir",TY_BOOL,1,{TY_STRING,TY_VOID},true,false},
    {"fsRemove",TY_BOOL,1,{TY_STRING,TY_VOID},true,false},
    {"fsRename",TY_BOOL,2,{TY_STRING,TY_STRING},true,false},
    {"fsList",TY_SLICE,1,{TY_STRING,TY_VOID},true,false},
    {"fsCwd",TY_STRING,0,{TY_VOID,TY_VOID},true,false},
    {"fsOpen",TY_FILE,2,{TY_STRING,TY_STRING},true,false},
    {"fsClose",TY_BOOL,1,{TY_FILE,TY_VOID},true,false},
    {"fsReadAll",TY_STRING,1,{TY_FILE,TY_VOID},true,false},
    {"fsWriteAll",TY_INT,2,{TY_FILE,TY_STRING},true,false},
    {"fileClose",TY_VOID,1,{TY_FILE,TY_VOID},false,false},
    {"threadJoin",TY_INT,1,{TY_THREAD,TY_VOID},true,false},
    {"threadClose",TY_VOID,1,{TY_THREAD,TY_VOID},false,false},
    {"errorMessage",TY_STRING,1,{TY_INT,TY_VOID},false,false},
    {"arenaNew",TY_ARENA,0,{TY_VOID,TY_VOID},false,true},
    {"arenaReset",TY_VOID,1,{TY_ARENA,TY_VOID},false,true},
    {"arenaFree",TY_VOID,1,{TY_ARENA,TY_VOID},false,true}
};
static bool builtin_name(const char *s) {
    static const char *names[]={"print","println","readInt","readLine","readIntResult","len","assert","panic","raw","alloc","arenaAlloc","free","slice","ok","fail","threadSpawn"};
    if(primitive_name(s)) return true;
    for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++) if(eq(s,names[i])) return true;
    for(size_t i=0;i<sizeof(resource_specs)/sizeof(resource_specs[0]);i++) if(eq(s,resource_specs[i].name)) return true;
    return false;
}
static Module *qualified_module(Checker *c, Module *m, const char *name, const char **short_name, Token tok) {
    const char *dot=strchr(name,'.');
    *short_name=name;
    if(!dot) return m;
    size_t len=(size_t)(dot-name);
    for(Import *i=m?m->imports:NULL;i;i=i->next) {
        if(strlen(i->alias)==len && !memcmp(i->alias,name,len)) { *short_name=dot+1; return i->target; }
    }
    km_error(cx(c),tok,"E105","unknown import qualifier in '%s'",name);
}
static Node *find_struct(Checker *c, Module *m, const char *name, Token tok) {
    const char *local;
    m=qualified_module(c,m,name,&local,tok);
    return struct_slot(c,m,local)->node;
}
static Node *find_fn(Checker *c, Module *m, const char *name, Type *receiver) {
    return function_slot(c,m,name,receiver)->node;
}
static bool contains_address(Type *t) {
    if(!t) return false;
    if(t->kind==TY_REF||t->kind==TY_PTR) return true;
    if(t->kind==TY_TUPLE) {for(size_t i=0;i<t->members.len;i++)if(contains_address(t->members.data[i]))return true;}
    return (t->kind==TY_ARRAY||t->kind==TY_RESULT||t->kind==TY_SLICE) && contains_address(t->elem);
}
static bool contains_thread(Type *t) {
    if(!t)return false;
    if(t->kind==TY_THREAD)return true;
    if(t->kind==TY_TUPLE){for(size_t i=0;i<t->members.len;i++)if(contains_thread(t->members.data[i]))return true;}
    return (t->kind==TY_ARRAY||t->kind==TY_SLICE||t->kind==TY_RESULT||t->kind==TY_REF||t->kind==TY_PTR)&&contains_thread(t->elem);
}
/* A result's failure tag must always carry an explicit nonzero code. */
static bool contains_result_value(Type *t) {
    if(t->kind==TY_RESULT)return true;
    if(t->kind==TY_ARRAY)return contains_result_value(t->elem);
    if(t->kind==TY_TUPLE){for(size_t i=0;i<t->members.len;i++)if(contains_result_value(t->members.data[i]))return true;}
    if(t->kind==TY_ENUM&&t->decl->fields.len){Node *v=t->decl->fields.data[0];for(size_t i=0;i<v->params.len;i++)if(contains_result_value(v->params.data[i]->type))return true;}
    if(t->kind==TY_STRUCT) {
        for(size_t i=0;i<t->decl->fields.len;i++)
            if(contains_result_value(t->decl->fields.data[i]->type))return true;
    }
    return false;
}
static void same(Checker *c, Type *a, Type *b, Node *n) {
    if(!km_type_equal(a,b)) km_error(cx(c),n->tok,"E110","type mismatch: expected %s, got %s",km_type_name(cx(c),b),km_type_name(cx(c),a));
}
static Type *valid_type(Checker *c, Type *t, Node *n, bool allow_void) {
    if(!t) t=cx(c)->t_void;
    if(t->kind==TY_VOID) {
        if(!allow_void) km_error(cx(c),n->tok,"E105","void is not a value type");
        return t;
    }
    if(t->kind==TY_STRUCT||t->kind==TY_ENUM) {
        if(t->decl) return t->decl->type;
        Token location=t->tok.src?t->tok:n->tok;
        const char *local;
        Module *module=qualified_module(c,module_of(n),t->name,&local,location);
        Node *s=struct_slot(c,module,local)->node;
        if(!s) {
            Suggestion best={.distance=3};
            for(size_t i=0;i<cx(c)->structs.len;i++){Node *candidate=cx(c)->structs.data[i];if(module_of(candidate)==module)suggest_candidate(&best,local,candidate->name,candidate->tok);}
            static const char *names[]={"bool","string","arena","file","thread","i8","i16","i32","i64","u8","u16","u32","u64"};
            if(local==t->name)for(size_t i=0;i<sizeof(names)/sizeof(*names);i++)suggest_candidate(&best,local,names[i],(Token){0});
            suggest_finish(c,&best);
            if(best.name&&!best.tied&&local!=t->name)
                cx(c)->diagnostic_hint=km_sprintf(cx(c),"Did you mean '%.*s%s'?",(int)(local-t->name),t->name,best.name);
            km_error(cx(c),location,"E105","unknown type '%s'",t->name);
        }
        return s->type;
    }
    if(t->kind==TY_TUPLE) {
        for(size_t i=0;i<t->members.len;i++){t->members.data[i]=valid_type(c,t->members.data[i],n,false);if(contains_address(t->members.data[i]))km_error(cx(c),n->tok,"E130","tuples cannot contain references or pointers");if(contains_thread(t->members.data[i]))km_error(cx(c),n->tok,"E150","tuples cannot contain thread handles");}
    }
    if(t->kind==TY_ARRAY||t->kind==TY_REF||t->kind==TY_PTR||t->kind==TY_SLICE||t->kind==TY_RESULT) {
        t->elem=valid_type(c,t->elem,n,false);
        if(contains_thread(t->elem)&&(t->kind!=TY_RESULT||t->elem->kind!=TY_THREAD))km_error(cx(c),n->tok,"E150","thread handles cannot be stored in containers, nested results or accessed through addresses");
        if(t->kind==TY_ARRAY) {
            if(t->count==0 || t->count>1000000) km_error(cx(c),n->tok,"E105","array size must be between 1 and 1000000");
        }
        if((t->kind==TY_ARRAY||t->kind==TY_SLICE||t->kind==TY_RESULT) && contains_address(t->elem))
            km_error(cx(c),n->tok,"E130","arrays, slices and results cannot contain references or raw pointers");
        if(t->kind==TY_REF && (t->elem->kind==TY_REF||t->elem->kind==TY_PTR))
            km_error(cx(c),n->tok,"E130","references to references or pointers are not supported");
    }
    return t;
}
static size_t layout(Checker *c,Type *t,size_t *align,unsigned depth,Node *where) {
    if(depth>128) km_error(cx(c),where->tok,"E106","recursive value type has infinite size");
    if(t->layout_state==2){*align=t->layout_align;return t->layout_size;}
    if(t->layout_state==1)km_error(cx(c),where->tok,"E106","recursive value type has infinite size");
    t->layout_state=1;
    size_t size=0,a=1;
    if(t->kind==TY_INT) {
        size=(size_t)t->bits/8;
        switch(t->bits){case 8:a=_Alignof(int8_t);break;case 16:a=_Alignof(int16_t);break;case 32:a=_Alignof(int32_t);break;default:a=_Alignof(int64_t);break;}
    }
    else if(t->kind==TY_BOOL) {size=sizeof(bool);a=_Alignof(bool);}
    else if(t->kind==TY_STRING) { size=sizeof(struct {const char *data;size_t len;});a=_Alignof(struct {const char *data;size_t len;}); }
    else if(t->kind==TY_REF||t->kind==TY_PTR||t->kind==TY_ARENA||t->kind==TY_FILE||t->kind==TY_THREAD) { size=sizeof(void *);a=_Alignof(void *); }
    else if(t->kind==TY_SLICE) { size=sizeof(struct {void *owner;size_t offset,len;});a=_Alignof(struct {void *owner;size_t offset,len;}); }
    else if(t->kind==TY_ARRAY) {
        size_t es=layout(c,t->elem,&a,depth+1,where);
        if(es && t->count>1048576/es) km_error(cx(c),where->tok,"E105","type exceeds the 1 MiB maximum value size");
        size=es*t->count;
    } else if(t->kind==TY_RESULT) {
        size_t es=layout(c,t->elem,&a,depth+1,where);
        size=(sizeof(bool)+a-1)/a*a+es;
        if(a<_Alignof(bool))a=_Alignof(bool);
        size_t code_align=_Alignof(int64_t);
        size=(size+code_align-1)/code_align*code_align+sizeof(int64_t);
        if(a<code_align)a=code_align;
        size=(size+a-1)/a*a;
    } else if(t->kind==TY_TUPLE) {
        for(size_t i=0;i<t->members.len;i++){size_t fa,fs=layout(c,t->members.data[i],&fa,depth+1,where);if(fa>a)a=fa;size=(size+fa-1)/fa*fa+fs;if(size>1048576)km_error(cx(c),where->tok,"E105","tuple exceeds 1 MiB");}
        size=size?(size+a-1)/a*a:1;
    } else if(t->kind==TY_ENUM) {
        size_t largest=1;
        for(size_t i=0;i<t->decl->fields.len;i++){Node *v=t->decl->fields.data[i];size_t vs=0,va=1;
            for(size_t j=0;j<v->params.len;j++){size_t fa,fs=layout(c,v->params.data[j]->type,&fa,depth+1,where);if(fa>va)va=fa;vs=(vs+fa-1)/fa*fa+fs;if(vs>1048576)km_error(cx(c),where->tok,"E105","variant exceeds 1 MiB");}
            vs=vs?(vs+va-1)/va*va:1;if(vs>largest)largest=vs;if(va>a)a=va;
        }
        if(a<_Alignof(uint32_t))a=_Alignof(uint32_t);
        size=(sizeof(uint32_t)+a-1)/a*a+largest;size=(size+a-1)/a*a;
    } else if(t->kind==TY_STRUCT) {
        Node *s=t->decl;
        for(size_t i=0;i<s->fields.len;i++) {
            size_t fa,fs=layout(c,s->fields.data[i]->type,&fa,depth+1,where);
            if(fa>a) a=fa;
            size=(size+fa-1)/fa*fa+fs;
            if(size>1048576) km_error(cx(c),where->tok,"E105","type exceeds the 1 MiB maximum value size");
        }
        size=size?(size+a-1)/a*a:1;
    }
    if(size>1048576) km_error(cx(c),where->tok,"E105","type exceeds the 1 MiB maximum value size");
    t->layout_size=size;t->layout_align=a;t->layout_state=2;
    *align=a;return size;
}
static void check_size(Checker *c,Type *t,Node *n) { size_t a; (void)layout(c,t,&a,0,n); }
static void push(Checker *c) {
    Scope *scope=km_alloc(cx(c),sizeof(*scope));scope->parent=c->scope;scope->entry_loans=c->loans;c->scope=scope;c->depth++;
}
static void remove_temporary(Checker *c) {
    Loan **p=&c->loans;
    while(*p) { if((*p)->depth==UINT_MAX) *p=(*p)->next; else p=&(*p)->next; }
}
static void release_since(Checker *c,Loan *baseline) {
    Loan **p=&c->loans;
    while(*p && *p!=baseline) { if((*p)->depth==UINT_MAX) *p=(*p)->next; else p=&(*p)->next; }
}
static void pop(Checker *c) {
    Loan *baseline=c->scope->entry_loans;
    for(Binding *b=c->scope->bindings;b;b=b->scope_next) {
        Symbol *s=b->sym;
        if(!s->used&&s->name[0]!='_') km_error(cx(c),s->tok,"W001","unused variable or parameter '%s'; use it or prefix its name with '_'",s->name);
        size_t slot=(size_t)name_hash(s->name)&(c->local_cap-1);
        Binding **entry=&c->local_names[slot];
        while(*entry&&*entry!=b)entry=&(*entry)->hash_next;
        if(*entry)*entry=b->hash_next;
        b->alive=false;c->local_count--;
    }
    c->scope=c->scope->parent;
    Loan **p=&c->loans;
    bool outer=false;
    while(*p) {
        if(*p==baseline)outer=true;
        if((*p)->depth==c->depth||((*p)->depth==UINT_MAX&&!outer)) *p=(*p)->next;
        else p=&(*p)->next;
    }
    c->depth--;
}
static Binding *binding_for(Checker *c,Symbol *s) {
    Binding *b=local_binding(c,s->name);return b&&b->sym==s?b:NULL;
}
static Symbol *lookup(Checker *c,const char *name,Node *n,bool use) {
    Binding *b=local_binding(c,name);
    if(b) {if(use)b->sym->used=true;return b->sym;}
    Suggestion best={.distance=3};
    for(Binding *candidate=c->bindings;candidate;candidate=candidate->next)if(candidate->alive)suggest_candidate(&best,name,candidate->sym->name,candidate->sym->tok);
    suggest_finish(c,&best);
    km_error(cx(c),n->tok,"E107","unknown variable '%s'",name);
}
static bool has_binding(Checker *c,const char *name) { return local_binding(c,name)!=NULL; }
static Symbol *bind(Checker *c,Node *n,Type *t,bool mutable) {
    bool imported=false; Module *m=module_of(c->fn);
    for(Import *i=m?m->imports:NULL;i;i=i->next) if(eq(i->alias,n->name)) imported=true;
    if(eq(n->name,"_")||has_binding(c,n->name)||builtin_name(n->name)||find_fn(c,m,n->name,NULL)||find_struct(c,m,n->name,n->tok)||imported) {
        Binding *previous=local_binding(c,n->name);
        if(previous) {cx(c)->related_token=previous->sym->tok;cx(c)->related_message="previous declaration is here";}
        km_error(cx(c),n->tok,"E108","declaration '%s' shadows an existing or reserved name",n->name);
    }
    Symbol *s=km_alloc(cx(c),sizeof(*s));
    s->name=n->name;s->type=t;s->mutable=mutable;s->tok=n->tok;s->depth=c->depth;
    s->cname=km_sprintf(cx(c),"kr_v_%u_%s",++cx(c)->next_symbol,n->name);
    if(c->local_count>=c->local_cap/2)grow_locals(c);
    Binding *b=km_alloc(cx(c),sizeof(*b)); b->sym=s;b->alive=true;b->next=c->bindings;c->bindings=b;
    size_t slot=(size_t)name_hash(s->name)&(c->local_cap-1);
    b->hash_next=c->local_names[slot];c->local_names[slot]=b;c->local_count++;
    b->scope_next=c->scope->bindings;c->scope->bindings=b;
    return s;
}
static bool via_owns(Symbol *via,Symbol *owner) {
    for(Symbol *v=via;v;v=v->via) if(v==owner) return true;
    return false;
}
static void access_owner(Checker *c,Symbol *root,bool write,Node *n,Symbol *via) {
    if(!root) return;
    for(Loan *l=c->loans;l;l=l->next) if(l->root==root && (!l->owner||!via_owns(via,l->owner)) && (write||l->mut)) {
        cx(c)->related_token=l->tok;cx(c)->related_message="conflicting borrow starts here";
        km_error(cx(c),n->tok,"E131","cannot %s '%s' while it is borrowed; the loan ends with its lexical block",write?"write":"read",root->name);
    }
}
static Loan *borrow(Checker *c,Symbol *root,bool mut,Node *n,bool persistent,Symbol *via) {
    access_owner(c,root,mut,n,via);
    Loan *l=km_alloc(cx(c),sizeof(*l));l->root=root;l->mut=mut;l->tok=n->tok;l->depth=persistent?c->depth:UINT_MAX;l->next=c->loans;c->loans=l;
    return l;
}
static bool is_place(Node *n) { return n&&(n->kind==N_NAME||n->kind==N_FIELD||n->kind==N_INDEX||(n->kind==N_UNARY&&eq(n->op,"*"))); }
/* Raw pointers keep the mutability of the reference that produced them. A
 * *T parameter is writable; a local raw(shared) pointer is read-only. */
static bool writable_pointer(Node *n) {
    if(n->kind==N_NAME&&n->symbol)return n->symbol->borrow_mut;
    return n->kind==N_CALL&&eq(n->builtin,"raw")&&n->args.len&&n->args.data[0]->type->mut;
}
static void ensure_mutable(Checker *c,Node *n) {
    if(n->kind==N_NAME) {
        Symbol *s=lookup(c,n->name,n,false);
        if(s->type->kind==TY_REF) {
            if(!s->type->mut) km_error(cx(c),n->tok,"E131","cannot write through a shared reference");
        } else if(!s->mutable) km_error(cx(c),n->tok,"E111","cannot modify immutable binding '%s'",s->name);
    } else if(n->kind==N_FIELD||n->kind==N_INDEX) ensure_mutable(c,n->base);
    else if(n->kind==N_UNARY&&eq(n->op,"*")) {
        Type *t=n->value->type;
        if(t&&t->kind==TY_REF&&!t->mut) km_error(cx(c),n->tok,"E131","cannot write through a shared reference");
        if(t&&t->kind==TY_PTR&&!writable_pointer(n->value))km_error(cx(c),n->tok,"E131","cannot write through a raw pointer derived from a shared reference");
    }
}
static Place place(Checker *c,Node *n,bool write,bool read) {
    Place p={0};
    if(n->kind==N_NAME) {
        Symbol *s=lookup(c,n->name,n,read); n->symbol=s;n->type=s->type;
        if(write&&!s->mutable) km_error(cx(c),n->tok,"E111","cannot modify immutable binding '%s'",s->name);
        if(read||write) access_owner(c,s,write,n,NULL);
        Binding *b=binding_for(c,s);
        if(read&&b&&b->constant) {n->constant=true;n->constant_value=b->value;n->constant_negative=b->neg;}
        p.type=s->type;p.root=s;return p;
    }
    if(n->kind==N_UNARY&&eq(n->op,"*")) {
        Type *t=expr(c,n->value,NULL,false);
        if(t->kind!=TY_REF&&t->kind!=TY_PTR) km_error(cx(c),n->tok,"E112","dereference requires a reference or raw pointer");
        if(t->kind==TY_PTR&&!c->unsafe_depth) km_error(cx(c),n->tok,"E132","raw pointer dereference requires an unsafe block");
        if(write&&t->kind==TY_REF&&!t->mut) km_error(cx(c),n->tok,"E131","cannot write through a shared reference");
        if(write&&t->kind==TY_PTR&&!writable_pointer(n->value)) km_error(cx(c),n->tok,"E131","cannot write through a raw pointer derived from a shared reference");
        Symbol *s=n->value->symbol;
        p.root=s?(s->root?s->root:s):n->value->borrow_root;
        p.via=s?s:n->value->borrow_via;
        /* A fresh explicit borrow has already checked conflicts itself. */
        if(t->kind==TY_REF&&!(n->value->kind==N_UNARY&&(eq(n->value->op,"&")||eq(n->value->op,"&mut")))) access_owner(c,p.root,write,n,p.via);
        n->type=p.type=t->elem;return p;
    }
    if(n->kind!=N_FIELD&&n->kind!=N_INDEX) km_error(cx(c),n->tok,"E111","expression is not an assignable storage location");
    Type *t;
    if(is_place(n->base)) {
        p=place(c,n->base,false,false);t=p.type;
        if(t->kind==TY_REF) {
            if(write&&!t->mut) km_error(cx(c),n->tok,"E131","cannot write through a shared reference");
            p.via=n->base->symbol?n->base->symbol:p.via;
            p.root=p.via?(p.via->root?p.via->root:p.via):p.root;
            t=t->elem;
        } else if(write) ensure_mutable(c,n->base);
        if((read||write)&&p.root) {
            if(read) p.root->used=true;
            if(p.via) p.via->used=true;
            access_owner(c,p.root,write,n,p.via);
        }
    } else {
        if(write) km_error(cx(c),n->tok,"E111","assignment requires a mutable storage location");
        t=expr(c,n->base,NULL,false);if(t->kind==TY_REF) t=t->elem;
    }
    if(n->kind==N_FIELD) {
        if(t->kind==TY_TUPLE) n->type=t->members.data[tuple_field(c,t,n->name,n)];
        else if(t->kind==TY_RESULT) {
            if(write) km_error(cx(c),n->tok,"E111","result fields are read-only");
            if(eq(n->name,"ok")) n->type=cx(c)->t_bool;
            else if(eq(n->name,"value")) n->type=t->elem;
            else if(eq(n->name,"code")) n->type=cx(c)->ints[3];
            else km_error(cx(c),n->tok,"E113","result has no field '%s'",n->name);
        } else {
            if(t->kind!=TY_STRUCT) km_error(cx(c),n->tok,"E113","type %s has no fields",km_type_name(cx(c),t));
            Node *field=NULL;
            for(size_t i=0;i<t->decl->fields.len;i++) if(eq(t->decl->fields.data[i]->name,n->name)) field=t->decl->fields.data[i];
            if(!field) {
                Suggestion best={.distance=3};
                for(size_t i=0;i<t->decl->fields.len;i++){Node *candidate=t->decl->fields.data[i];suggest_candidate(&best,n->name,candidate->name,candidate->tok);}
                suggest_finish(c,&best);
                km_error(cx(c),n->tok,"E113","struct %s has no field '%s'",km_type_name(cx(c),t),n->name);
            }
            n->type=field->type;
        }
    } else {
        if(t->kind!=TY_ARRAY&&t->kind!=TY_SLICE&&t->kind!=TY_TUPLE) km_error(cx(c),n->tok,"E114","indexing requires an array, slice or tuple");
        Type *it=expr(c,n->index,NULL,false);
        if(!km_is_int(it)) km_error(cx(c),n->index->tok,"E114","array index must be an integer");
        if(t->kind==TY_TUPLE){if(!n->index->constant||n->index->constant_negative||n->index->constant_value>=t->members.len)km_error(cx(c),n->index->tok,"E140","tuple index must be a valid constant");n->integer=n->index->constant_value;n->type=t->members.data[n->integer];p.type=n->type;return p;}
        if(n->index->constant&&(n->index->constant_negative||(t->kind==TY_ARRAY&&n->index->constant_value>=t->count))) km_error(cx(c),n->index->tok,"E115","index is outside array bounds");
        n->type=t->elem;
    }
    p.type=n->type;return p;
}
static uint64_t max_positive(Type *t) {
    if(t->sign) return t->bits==64?INT64_MAX:((UINT64_C(1)<<(t->bits-1))-1);
    return t->bits==64?UINT64_MAX:((UINT64_C(1)<<t->bits)-1);
}
static uint64_t max_negative(Type *t) { return t->sign?(UINT64_C(1)<<(t->bits-1)):0; }
static void range(Checker *c,Number v,Type *t,Node *n) {
    if(v.value==0) v.neg=false;
    if((v.neg&&(!t->sign||v.value>max_negative(t)))||(!v.neg&&v.value>max_positive(t)))
        km_error(cx(c),n->tok,"E123","integer constant is outside %s range",km_type_name(cx(c),t));
}
static Number number(Node *n) {Number r={n->constant_value,n->constant_negative};return r;}
static void set_number(Node *n,Number v) { n->constant=true;n->constant_value=v.value;n->constant_negative=v.neg&&v.value!=0; }
static int compare_number(Number a,Number b) {
    if(a.value==0) a.neg=false;
    if(b.value==0) b.neg=false;
    if(a.neg!=b.neg) return a.neg?-1:1;
    int r=a.value<b.value?-1:a.value>b.value?1:0;
    return a.neg?-r:r;
}
static Number from_bits(uint64_t v,Type *t) {
    uint64_t mask=max_positive(t);
    if(t->sign) mask= t->bits==64?UINT64_MAX:((UINT64_C(1)<<t->bits)-1);
    v&=mask;
    Number r={v,false};
    if(t->sign&&(v&(UINT64_C(1)<<(t->bits-1)))) {r.neg=true;r.value=(~v+1)&mask;}
    return r;
}
static uint64_t to_bits(Number a) {return a.neg?UINT64_C(0)-a.value:a.value;}
static void binary_constants(Checker *c,const char *op,Node *left,Node *right,Type *t,Node *out) {
    bool shift=eq(op,"<<")||eq(op,">>");
    if((eq(op,"/")||eq(op,"%"))&&right->constant&&right->constant_value==0) km_error(cx(c),right->tok,"E122","division by zero");
    if(shift) {
        if(right->constant&&(right->constant_negative||right->constant_value>=(uint64_t)t->bits)) km_error(cx(c),right->tok,"E122","shift count must be in [0, %d)",t->bits);
        if(left->constant&&left->constant_negative) km_error(cx(c),left->tok,"E122","shift operand must be nonnegative");
    }
    if(!left->constant||!right->constant) return;
    Number a=number(left),b=number(right),r={0,false}; bool comparison=false;
    if(eq(op,"+")||eq(op,"-")) {
        if(eq(op,"-")) b.neg=!b.neg&&b.value!=0;
        if(a.neg==b.neg) {if(UINT64_MAX-a.value<b.value) km_error(cx(c),out->tok,"E123","integer constant overflow");r.value=a.value+b.value;r.neg=a.neg;}
        else if(a.value>=b.value) {r.value=a.value-b.value;r.neg=a.neg;}
        else {r.value=b.value-a.value;r.neg=b.neg;}
    } else if(eq(op,"*")) {
        if(b.value&&a.value>UINT64_MAX/b.value) km_error(cx(c),out->tok,"E123","integer constant overflow");
        r.value=a.value*b.value;r.neg=a.neg!=b.neg;
    } else if(eq(op,"/")||eq(op,"%")) {
        if(t->sign&&a.neg&&a.value==max_negative(t)&&b.neg&&b.value==1) km_error(cx(c),out->tok,"E123","integer division or remainder overflows its operand type");
        r.value=eq(op,"/")?a.value/b.value:a.value%b.value;r.neg=eq(op,"/")?(a.neg!=b.neg):a.neg;
    } else if(eq(op,"<<")) {
        unsigned s=(unsigned)b.value;
        if(s&&a.value>(UINT64_MAX>>s)) km_error(cx(c),out->tok,"E123","integer constant overflow");
        r.value=a.value<<s;
    } else if(eq(op,">>")) r.value=a.value>>(unsigned)b.value;
    else if(eq(op,"&")) r=from_bits(to_bits(a)&to_bits(b),t);
    else if(eq(op,"|")) r=from_bits(to_bits(a)|to_bits(b),t);
    else if(eq(op,"^")) r=from_bits(to_bits(a)^to_bits(b),t);
    else {
        int cmp=compare_number(a,b);comparison=true;
        if(eq(op,"==")) r.value=cmp==0;
        else if(eq(op,"!=")) r.value=cmp!=0;
        else if(eq(op,"<")) r.value=cmp<0;
        else if(eq(op,"<=")) r.value=cmp<=0;
        else if(eq(op,">")) r.value=cmp>0;
        else if(eq(op,">=")) r.value=cmp>=0;
        else return;
    }
    if(km_is_int(t)&&!comparison) range(c,r,t,out);
    set_number(out,r);
}
static bool literal_expression(Node *n) {
    if(n->kind==N_INT) return true;
    if(n->kind==N_UNARY&&(eq(n->op,"+")||eq(n->op,"-")||eq(n->op,"~")||eq(n->op,"^"))) return literal_expression(n->value);
    return n->kind==N_BINARY&&literal_expression(n->left)&&literal_expression(n->right);
}
static Type *hint(Checker *c,Node *n) {
    if(n->type) return n->type;
    if(n->kind==N_NAME) return lookup(c,n->name,n,false)->type;
    if(n->kind==N_BOOL) return cx(c)->t_bool;
    if(n->kind==N_STRING) return cx(c)->t_string;
    if(n->kind==N_STRUCTLIT) {Node *s=find_struct(c,module_of(c->fn),n->name,n->tok);return s?s->type:NULL;}
    if(n->kind==N_ARRAYLIT) return valid_type(c,n->annotation,n,false);
    if(n->kind==N_BINARY) {
        if(eq(n->op,"==")||eq(n->op,"!=")||eq(n->op,"<")||eq(n->op,"<=")||eq(n->op,">")||eq(n->op,">=")||eq(n->op,"&&")||eq(n->op,"||")) return cx(c)->t_bool;
        Type *t=hint(c,n->left);return t?t:hint(c,n->right);
    }
    if(n->kind==N_UNARY) {
        Type *t=hint(c,n->value);
        if(eq(n->op,"&")||eq(n->op,"&mut")) {if(!t)return NULL;Type *r=km_type(cx(c),TY_REF,t);r->mut=eq(n->op,"&mut");return r;}
        if(eq(n->op,"*")&&t&&(t->kind==TY_REF||t->kind==TY_PTR)) return t->elem;
        return t;
    }
    if(n->kind==N_CALL&&n->callee->kind==N_NAME) {
        Type *t=int_named(cx(c),n->callee->name);if(t)return t;
        Node *f=find_fn(c,module_of(c->fn),n->callee->name,NULL);if(f)return f->result;
        if(eq(n->callee->name,"readInt")||eq(n->callee->name,"len"))return cx(c)->ints[3];
        if(eq(n->callee->name,"readLine"))return cx(c)->t_string;
        if(eq(n->callee->name,"readIntResult"))return km_type(cx(c),TY_RESULT,cx(c)->ints[3]);
        for(size_t i=0;i<sizeof(resource_specs)/sizeof(*resource_specs);i++)if(eq(n->callee->name,resource_specs[i].name)) {
            Type *result=builtin_type(c,resource_specs[i].result);
            return resource_specs[i].fallible?km_type(cx(c),TY_RESULT,result):result;
        }
    }
    if(n->kind==N_CALL&&n->callee->kind==N_FIELD) {
        Node *callee=n->callee,*receiver=callee->base;
        char *name=type_expression_name(c,receiver);
        Node *owner=name?find_struct(c,module_of(c->fn),name,receiver->tok):NULL;
        if(owner&&owner->kind==N_ENUM)return owner->type;
        if(receiver->kind==N_NAME&&!has_binding(c,receiver->name)) {
            Module *module=module_of(c->fn);
            for(Import *i=module?module->imports:NULL;i;i=i->next)if(eq(i->alias,receiver->name)) {
                Node *f=find_fn(c,i->target,callee->name,NULL);return f?f->result:NULL;
            }
        }
        Type *base=hint(c,receiver);if(base&&base->kind==TY_REF)base=base->elem;
        if(base&&(base->kind==TY_STRUCT||base->kind==TY_ENUM)) {
            Node *f=find_fn(c,NULL,callee->name,base);return f?f->result:NULL;
        }
    }
    if(n->kind==N_FIELD) {
        Type *t=hint(c,n->base);if(t&&t->kind==TY_REF)t=t->elem;
        if(t&&t->kind==TY_TUPLE)return t->members.data[tuple_field(c,t,n->name,n)];
        if(t&&t->kind==TY_STRUCT) for(size_t i=0;i<t->decl->fields.len;i++) if(eq(t->decl->fields.data[i]->name,n->name))return t->decl->fields.data[i]->type;
        if(t&&t->kind==TY_RESULT) {if(eq(n->name,"ok"))return cx(c)->t_bool;if(eq(n->name,"code"))return cx(c)->ints[3];if(eq(n->name,"value"))return t->elem;}
    }
    if(n->kind==N_INDEX) {Type *t=hint(c,n->base);if(t&&t->kind==TY_REF)t=t->elem;if(t&&(t->kind==TY_ARRAY||t->kind==TY_SLICE))return t->elem;}
    if(n->kind==N_TRY) {Type *t=hint(c,n->value);if(t&&t->kind==TY_RESULT)return t->elem;}
    if(n->kind==N_CATCH) {Type *t=hint(c,n->value);if(t&&t->kind==TY_RESULT)return t->elem;}
    return NULL;
}
static Type *ref_argument(Checker *c,Node *a,Type *expected,bool persistent) {
    Type *t=expr(c,a,expected,persistent);
    bool shared=t->kind==TY_REF&&expected->kind==TY_REF&&km_type_equal(t->elem,expected->elem)&&t->mut&&!expected->mut;
    if(!shared) same(c,t,expected,a);
    if(t->kind==TY_PTR&&!writable_pointer(a))km_error(cx(c),a->tok,"E131","a writable pointer parameter requires raw(&mut value), not a shared-reference pointer");
    if((t->kind==TY_REF||t->kind==TY_PTR)&&a->kind==N_NAME) {
        Symbol *s=a->symbol,*root=s->root?s->root:s;
        borrow(c,root,t->kind==TY_REF?expected->mut:s->borrow_mut,a,persistent,s);
        a->borrow_root=root;a->borrow_via=s;
    } else if(shared&&a->borrow_root) {
        for(Loan *l=c->loans;l;l=l->next) if(l->root==a->borrow_root&&l->owner==NULL) {l->mut=false;break;}
    }
    return t;
}
static void arity(Checker *c,Node *n,size_t count) {
    if(n->args.len!=count) km_error(cx(c),n->tok,"E119","'%s' expects %zu argument(s), got %zu",n->callee->name?n->callee->name:"call",count,n->args.len);
}
static Type *builtin_type(Checker *c,TypeKind kind) {
    switch(kind){
    case TY_VOID:return cx(c)->t_void;case TY_INT:return cx(c)->ints[3];case TY_BOOL:return cx(c)->t_bool;
    case TY_STRING:return cx(c)->t_string;case TY_FILE:return cx(c)->t_file;case TY_ARENA:return cx(c)->t_arena;case TY_THREAD:return cx(c)->t_thread;
    case TY_SLICE:return km_type(cx(c),TY_SLICE,cx(c)->t_string);
    default:km_error(cx(c),(Token){0},"E900","invalid builtin signature");
    }
}
static char *type_expression_name(Checker *c,Node *n) {
    if(n->kind==N_NAME)return has_binding(c,n->name)?NULL:n->name;
    if(n->kind!=N_FIELD)return NULL;
    char *base=type_expression_name(c,n->base);return base?km_sprintf(cx(c),"%s.%s",base,n->name):NULL;
}
static Type *call(Checker *c,Node *n,Type *expected,bool persistent) {
    bool delayed=c->deferred_call;c->deferred_call=false;
    Loan *baseline=c->loans;
    Node *callee=n->callee;n->receiver=NULL;
    if(callee->kind==N_NAME&&builtin_name(callee->name)) {
        const char *name=callee->name;Type *cast=int_named(cx(c),name);
        callee->type=km_type(cx(c),TY_FUNCTION,NULL);n->builtin=cast?"cast":callee->name;
        if(n->typearg&&!eq(name,"alloc")&&!eq(name,"arenaAlloc")) km_error(cx(c),n->tok,"E119","type arguments require alloc or arenaAlloc");
        if(eq(name,"threadSpawn")) {
            arity(c,n,2);Node *worker=n->args.data[0],*fn=NULL;
            if(worker->kind==N_NAME&&!has_binding(c,worker->name))fn=find_fn(c,module_of(c->fn),worker->name,NULL);
            else if(worker->kind==N_FIELD&&worker->base->kind==N_NAME&&!has_binding(c,worker->base->name)) {
                Module *m=module_of(c->fn);
                for(Import *i=m?m->imports:NULL;i;i=i->next)if(eq(i->alias,worker->base->name)){fn=find_fn(c,i->target,worker->name,NULL);break;}
            }
            if(!fn||fn->receiver||fn->external||fn->params.len!=1||!km_type_equal(fn->params.data[0]->type,cx(c)->ints[3])||!km_type_equal(fn->result,cx(c)->ints[3]))
                km_error(cx(c),worker->tok,"E150","threadSpawn requires a named Kremen worker with signature func(value i64) i64");
            worker->type=km_type(cx(c),TY_FUNCTION,NULL);worker->function=fn;n->function=fn;
            same(c,expr(c,n->args.data[1],cx(c)->ints[3],false),cx(c)->ints[3],n->args.data[1]);
            release_since(c,baseline);return km_type(cx(c),TY_RESULT,cx(c)->t_thread);
        }
        for(size_t i=0;i<sizeof(resource_specs)/sizeof(resource_specs[0]);i++)if(eq(name,resource_specs[i].name)) {
            const BuiltinSpec *spec=&resource_specs[i];arity(c,n,spec->count);
            if(spec->unsafe_call&&!c->unsafe_depth)km_error(cx(c),n->tok,"E132","%s requires an unsafe block",name);
            for(size_t j=0;j<spec->count;j++){Type *want=builtin_type(c,spec->args[j]);same(c,expr(c,n->args.data[j],want,false),want,n->args.data[j]);}
            Type *result=builtin_type(c,spec->result);return spec->fallible?km_type(cx(c),TY_RESULT,result):result;
        }
        if(eq(name,"print")||eq(name,"println")) {
            for(size_t i=0;i<n->args.len;i++) {Type *t=expr(c,n->args.data[i],NULL,false);if(!km_is_int(t)&&t->kind!=TY_BOOL&&t->kind!=TY_STRING) km_error(cx(c),n->args.data[i]->tok,"E119","output supports integers, bool and string");}
            release_since(c,baseline);return cx(c)->t_void;
        }
        if(eq(name,"readInt")||eq(name,"readLine")||eq(name,"readIntResult")) {
            arity(c,n,0);if(eq(name,"readLine"))return cx(c)->t_string;
            if(eq(name,"readIntResult"))return km_type(cx(c),TY_RESULT,cx(c)->ints[3]);
            return cx(c)->ints[3];
        }
        if(eq(name,"alloc")||eq(name,"arenaAlloc")) {
            bool arena=eq(name,"arenaAlloc");arity(c,n,arena?2:1);
            if(!c->unsafe_depth)km_error(cx(c),n->tok,"E132","alloc requires an unsafe block");
            if(!n->typearg)km_error(cx(c),n->tok,"E119","alloc requires an element type: alloc[T](count)");
            n->typearg=valid_type(c,n->typearg,n,false);check_size(c,n->typearg,n);
            if(contains_thread(n->typearg))km_error(cx(c),n->tok,"E150","slice elements cannot contain thread handles");
            if(contains_address(n->typearg))km_error(cx(c),n->tok,"E130","slice elements cannot contain references or raw pointers");
            if(contains_result_value(n->typearg))km_error(cx(c),n->tok,"E130","zero-filled allocation cannot contain result values; results require explicit initialization");
            if(arena)same(c,expr(c,n->args.data[0],cx(c)->t_arena,false),cx(c)->t_arena,n->args.data[0]);
            Node *count=n->args.data[arena?1:0];same(c,expr(c,count,cx(c)->ints[3],false),cx(c)->ints[3],count);
            if(count->constant_negative)km_error(cx(c),n->tok,"E119","allocation length must be nonnegative");
            return km_type(cx(c),TY_SLICE,n->typearg);
        }
        if(eq(name,"slice")) {
            arity(c,n,3);Type *t=expr(c,n->args.data[0],NULL,false);
            if(t->kind!=TY_SLICE)km_error(cx(c),n->tok,"E119","slice requires a heap slice");
            for(size_t i=1;i<3;i++)same(c,expr(c,n->args.data[i],cx(c)->ints[3],false),cx(c)->ints[3],n->args.data[i]);
            if(n->args.data[1]->constant_negative||n->args.data[2]->constant_negative)km_error(cx(c),n->tok,"E115","slice bounds must be nonnegative");
            return t;
        }
        arity(c,n,1);Node *a=n->args.data[0];
        if(cast) {
            Type *t=expr(c,a,literal_expression(a)?cast:NULL,false);
            if(!km_is_int(t))km_error(cx(c),a->tok,"E119","integer conversion requires an integer");
            if(a->constant){range(c,number(a),cast,a);set_number(n,number(a));}
            return cast;
        }
        if(eq(name,"ok")||eq(name,"fail")) {
            if(!expected||expected->kind!=TY_RESULT)km_error(cx(c),n->tok,"E119","ok/fail requires a contextual result type !T");
            if(contains_address(expected->elem))km_error(cx(c),n->tok,"E130","results cannot contain references or pointers");
            Type *want=eq(name,"ok")?expected->elem:cx(c)->ints[3];
            same(c,expr(c,a,want,false),want,a);
            if(eq(name,"fail")&&a->constant&&(a->constant_negative||a->constant_value==0))km_error(cx(c),a->tok,"E119","failure code must be positive");
            return expected;
        }
        if(eq(name,"raw")) {
            if(!c->unsafe_depth)km_error(cx(c),n->tok,"E132","raw(reference) requires an unsafe block");
            Type *t=expr(c,a,NULL,persistent);
            if(t->kind!=TY_REF)km_error(cx(c),a->tok,"E119","raw requires a reference");
            if(a->kind==N_NAME)ref_argument(c,a,t,persistent);
            n->borrow_root=a->borrow_root;n->borrow_via=a->borrow_via;
            return km_type(cx(c),TY_PTR,t->elem);
        }
        Type *want=eq(name,"assert")?cx(c)->t_bool:eq(name,"panic")?cx(c)->t_string:NULL;
        Type *t=expr(c,a,want,false);
        if(eq(name,"len")) {
            if(t->kind!=TY_ARRAY&&t->kind!=TY_SLICE&&t->kind!=TY_STRING)km_error(cx(c),a->tok,"E119","len requires an array, slice or string");
            if(t->kind==TY_ARRAY){Number v={t->count,false};set_number(n,v);}
            return cx(c)->ints[3];
        }
        if(eq(name,"free")) {
            if(!c->unsafe_depth)km_error(cx(c),n->tok,"E132","free requires an unsafe block");
            if(t->kind!=TY_SLICE)km_error(cx(c),a->tok,"E119","free requires a heap slice");
            return cx(c)->t_void;
        }
        if(!want)km_error(cx(c),n->tok,"E119","'%s' is not callable",name);
        same(c,t,want,a);return cx(c)->t_void;
    }
    Node *fn=NULL;
    if(n->typearg)km_error(cx(c),n->tok,"E119","type arguments are only supported by alloc");
    if(callee->kind==N_NAME) {
        fn=find_fn(c,module_of(c->fn),callee->name,NULL);
        if(!fn){
            Suggestion best={.distance=3};
            for(size_t i=0;i<cx(c)->functions.len;i++){Node *candidate=cx(c)->functions.data[i];if(!candidate->receiver&&module_of(candidate)==module_of(c->fn))suggest_candidate(&best,callee->name,candidate->name,candidate->tok);}
            static const char *names[]={"print","println","readInt","readLine","readIntResult","len","assert","panic","raw","alloc","arenaAlloc","free","slice","ok","fail","threadSpawn"};
            for(size_t i=0;i<sizeof(names)/sizeof(*names);i++)suggest_candidate(&best,callee->name,names[i],(Token){0});
            for(size_t i=0;i<sizeof(resource_specs)/sizeof(*resource_specs);i++)suggest_candidate(&best,callee->name,resource_specs[i].name,(Token){0});
            suggest_finish(c,&best);
            km_error(cx(c),callee->tok,"E107","unknown function '%s'",callee->name);
        }
    } else if(callee->kind==N_FIELD) {
        Node *r=callee->base;
        char *type_name=type_expression_name(c,r);
        Node *algebraic=type_name?find_struct(c,module_of(c->fn),type_name,r->tok):NULL;
        if(algebraic&&algebraic->kind==N_ENUM) {
            Node *variant=NULL;for(size_t i=0;i<algebraic->fields.len;i++)if(eq(algebraic->fields.data[i]->name,callee->name))variant=algebraic->fields.data[i];
            if(!variant){
                Suggestion best={.distance=3};
                for(size_t i=0;i<algebraic->fields.len;i++){Node *candidate=algebraic->fields.data[i];suggest_candidate(&best,callee->name,candidate->name,candidate->tok);}
                suggest_finish(c,&best);km_error(cx(c),callee->tok,"E141","unknown variant '%s'",callee->name);
            }
            if(variant->params.len!=n->args.len)km_error(cx(c),n->tok,"E119","variant '%s' expects %zu arguments",variant->name,variant->params.len);
            for(size_t i=0;i<n->args.len;i++)same(c,expr(c,n->args.data[i],variant->params.data[i]->type,false),variant->params.data[i]->type,n->args.data[i]);
            callee->type=km_type(cx(c),TY_FUNCTION,NULL);n->function=variant;n->builtin="variant";n->integer=variant->integer;return algebraic->type;
        }
        if(r->kind==N_NAME&&!has_binding(c,r->name)) {
            Module *m=module_of(c->fn);
            for(Import *i=m?m->imports:NULL;i;i=i->next) if(eq(i->alias,r->name)) {
                fn=find_fn(c,i->target,callee->name,NULL);
                if(!fn){
                    Suggestion best={.distance=3};
                    for(size_t j=0;j<cx(c)->functions.len;j++){Node *candidate=cx(c)->functions.data[j];if(!candidate->receiver&&module_of(candidate)==i->target)suggest_candidate(&best,callee->name,candidate->name,candidate->tok);}
                    suggest_finish(c,&best);
                    km_error(cx(c),callee->tok,"E107","unknown function '%s' in imported module '%s'",callee->name,r->name);
                }
                break;
            }
        }
        if(!fn) {
            Type *rt=hint(c,r);if(!rt)rt=expr(c,r,NULL,false);
            Type *base=rt->kind==TY_REF?rt->elem:rt;
            if(base->kind!=TY_STRUCT&&base->kind!=TY_ENUM)km_error(cx(c),callee->tok,"E119","method receiver must be a named type or an imported module");
            fn=find_fn(c,NULL,callee->name,base);
            if(!fn){
                Suggestion best={.distance=3};
                for(size_t i=0;i<cx(c)->functions.len;i++){Node *candidate=cx(c)->functions.data[i];if(!candidate->receiver)continue;Type *receiver=candidate->receiver->type;if(receiver->kind==TY_REF)receiver=receiver->elem;if(km_type_equal(receiver,base))suggest_candidate(&best,callee->name,candidate->name,candidate->tok);}
                suggest_finish(c,&best);km_error(cx(c),callee->tok,"E107","unknown method '%s'",callee->name);
            }
            Type *want=fn->receiver->type;
            if(want->kind==TY_REF) {
                if(rt->kind==TY_REF)ref_argument(c,r,want,delayed);
                else {
                    if(r->kind!=N_NAME)km_error(cx(c),r->tok,"E130","borrowed method receiver must be a named local");
                    Place p=place(c,r,false,false);p.root->used=true;
                    if(want->mut&&!p.root->mutable)km_error(cx(c),r->tok,"E131","cannot mutably borrow an immutable receiver");
                    borrow(c,p.root,want->mut,r,delayed,NULL);
                }
            } else {
                if(rt->kind==TY_REF)km_error(cx(c),r->tok,"E119","value method requires an explicit dereference");
                same(c,expr(c,r,want,false),want,r);
            }
            n->receiver=r;
        }
    } else km_error(cx(c),callee->tok,"E119","only named functions and methods can be called");
    if(!fn)km_error(cx(c),callee->tok,"E107","unknown imported function '%s'",callee->name);
    if(fn->external&&!c->unsafe_depth)km_error(cx(c),n->tok,"E132","external function calls require an unsafe block");
    callee->type=km_type(cx(c),TY_FUNCTION,NULL);callee->function=fn;n->function=fn;
    if(n->args.len!=fn->params.len)km_error(cx(c),n->tok,"E119","'%s' expects %zu argument(s), got %zu",fn->name,fn->params.len,n->args.len);
    for(size_t i=0;i<n->args.len;i++) {
        Node *arg=n->args.data[i];Type *want=fn->params.data[i]->type;
        if(want->kind==TY_REF||want->kind==TY_PTR)ref_argument(c,arg,want,delayed);
        else same(c,expr(c,arg,want,false),want,arg);
    }
    release_since(c,baseline);return fn->result;
}
static Type *expr(Checker *c,Node *n,Type *expected,bool persistent) {
    switch(n->kind) {
    case N_INT: {
        n->type=expected&&km_is_int(expected)?expected:cx(c)->ints[3];
        Number v={n->integer,n->negative};range(c,v,n->type,n);set_number(n,v);break;
    }
    case N_BOOL:n->type=cx(c)->t_bool;n->constant=true;n->constant_value=n->flag;break;
    case N_STRING:n->type=cx(c)->t_string;break;
    case N_NAME:case N_FIELD:case N_INDEX:n->type=place(c,n,false,true).type;break;
    case N_UNARY:
        if(eq(n->op,"&")||eq(n->op,"&mut")) {
            if(n->value->kind!=N_NAME)km_error(cx(c),n->tok,"E130","borrow requires a named local; borrow the entire value");
            Place p=place(c,n->value,false,false);
            if(contains_thread(p.type))km_error(cx(c),n->tok,"E150","thread handles cannot be borrowed or converted to addresses");
            if(p.type->kind==TY_REF||p.type->kind==TY_PTR)km_error(cx(c),n->tok,"E130","direct reference/pointer reborrows are forbidden; pass it to a function");
            p.root->used=true;bool mut=eq(n->op,"&mut");
            if(mut&&!p.root->mutable)km_error(cx(c),n->tok,"E131","cannot mutably borrow an immutable binding");
            borrow(c,p.root,mut,n,persistent,NULL);n->borrow_root=p.root;
            n->type=km_type(cx(c),TY_REF,p.type);n->type->mut=mut;
        } else if(eq(n->op,"*")) n->type=place(c,n,false,true).type;
        else if(eq(n->op,"-")&&n->value->kind==N_INT) {
            n->type=expected&&km_is_int(expected)?expected:cx(c)->ints[3];
            Number v={n->value->integer,true};range(c,v,n->type,n);n->value->type=n->type;set_number(n,v);
        } else {
            Type *t=expr(c,n->value,eq(n->op,"!")?cx(c)->t_bool:expected,false);
            if(eq(n->op,"!"))same(c,t,cx(c)->t_bool,n->value);
            else if(!km_is_int(t))km_error(cx(c),n->tok,"E112","unary operator requires an integer");
            n->type=t;
            if(n->value->constant) {
                Number v=number(n->value);
                if(eq(n->op,"!")){v.value=!v.value;v.neg=false;}
                else if(eq(n->op,"-"))v.neg=!v.neg&&v.value!=0;
                else if(eq(n->op,"~")||eq(n->op,"^"))v=from_bits(~to_bits(v),t);
                else if(!eq(n->op,"+"))km_error(cx(c),n->tok,"E112","unsupported unary operator '%s'",n->op);
                if(km_is_int(t))range(c,v,t,n);
                set_number(n,v);
            }
        }
        break;
    case N_BINARY:
        if(eq(n->op,"&&")||eq(n->op,"||")) {
            same(c,expr(c,n->left,cx(c)->t_bool,false),cx(c)->t_bool,n->left);
            same(c,expr(c,n->right,cx(c)->t_bool,false),cx(c)->t_bool,n->right);n->type=cx(c)->t_bool;
            if(n->left->constant&&n->right->constant){n->constant=true;n->constant_value=eq(n->op,"&&")?(n->left->constant_value&&n->right->constant_value):(n->left->constant_value||n->right->constant_value);}
        } else {
            Type *context=expected&&km_is_int(expected)?expected:hint(c,n->left);if(!context)context=hint(c,n->right);
            Type *lt=expr(c,n->left,context,false),*rt=expr(c,n->right,lt,false);same(c,rt,lt,n->right);
            bool cmp=eq(n->op,"==")||eq(n->op,"!=")||eq(n->op,"<")||eq(n->op,"<=")||eq(n->op,">")||eq(n->op,">=");
            if(cmp) {
                if(!km_is_int(lt)&&!((lt->kind==TY_BOOL||lt->kind==TY_STRING)&&(eq(n->op,"==")||eq(n->op,"!="))))km_error(cx(c),n->tok,"E112","comparison is not defined for %s",km_type_name(cx(c),lt));
                n->type=cx(c)->t_bool;
            } else {if(!km_is_int(lt))km_error(cx(c),n->tok,"E112","binary operator requires integers");n->type=lt;}
            binary_constants(c,n->op,n->left,n->right,lt,n);
        }
        break;
    case N_CALL:n->type=call(c,n,expected,persistent);break;
    case N_TUPLE: {
        if(expected&&expected->kind==TY_TUPLE&&expected->members.len!=n->values.len)km_error(cx(c),n->tok,"E140","tuple arity does not match its type");
        Type *t=km_type(cx(c),TY_TUPLE,NULL);
        for(size_t i=0;i<n->values.len;i++){Type *want=expected&&expected->kind==TY_TUPLE?expected->members.data[i]:NULL;Type *member=expr(c,n->values.data[i],want,false);if(want)same(c,member,want,n->values.data[i]);if(member->kind==TY_VOID||contains_address(member))km_error(cx(c),n->tok,"E130","tuple members must be ordinary values");if(contains_thread(member))km_error(cx(c),n->tok,"E150","tuple members cannot contain thread handles");km_types_push(cx(c),&t->members,member);}
        n->type=t;check_size(c,t,n);break;
    }
    case N_STRUCTLIT: {
        Node *s=find_struct(c,module_of(c->fn),n->name,n->tok);if(!s)km_error(cx(c),n->tok,"E105","unknown struct '%s'",n->name);
        if(s->kind==N_ENUM)km_error(cx(c),n->tok,"E141","enum values require a variant constructor");
        n->type=s->type;
        for(size_t i=0;i<n->fields.len;i++) {
            Node *f=n->fields.data[i],*decl=NULL;
            for(size_t j=0;j<i;j++)if(eq(n->fields.data[j]->name,f->name))km_error(cx(c),f->tok,"E113","duplicate field '%s'",f->name);
            for(size_t j=0;j<s->fields.len;j++)if(eq(s->fields.data[j]->name,f->name))decl=s->fields.data[j];
            if(!decl)km_error(cx(c),f->tok,"E113","unknown field '%s'",f->name);
            f->type=decl->type;same(c,expr(c,f->value,decl->type,false),decl->type,f->value);
        }
        if(n->fields.len!=s->fields.len)km_error(cx(c),n->tok,"E116","struct literal must initialize every field");
        break;
    }
    case N_ARRAYLIT:
        n->type=valid_type(c,n->annotation,n,false);check_size(c,n->type,n);
        if(n->type->kind!=TY_ARRAY||n->values.len!=n->type->count)km_error(cx(c),n->tok,"E116","array literal must contain exactly its declared number of elements");
        for(size_t i=0;i<n->values.len;i++)same(c,expr(c,n->values.data[i],n->type->elem,false),n->type->elem,n->values.data[i]);
        break;
    case N_TRY: {
        Type *t=expr(c,n->value,NULL,false);
        if(t->kind!=TY_RESULT)km_error(cx(c),n->tok,"E119","postfix '?' requires a result !T");
        if(c->fn->result->kind!=TY_RESULT)km_error(cx(c),n->tok,"E119","postfix '?' requires an enclosing function returning !T");
        n->type=t->elem;break;
    }
    case N_CATCH: {
        Loan *baseline=c->loans;
        Type *want=expected?km_type(cx(c),TY_RESULT,expected):NULL;
        Type *t=expr(c,n->value,want,false);
        if(t->kind!=TY_RESULT)km_error(cx(c),n->tok,"E142","catch requires a result !T");
        release_since(c,baseline);push(c);
        if(n->name&&!eq(n->name,"_"))n->symbol=bind(c,n,cx(c)->ints[3],false);
        same(c,expr(c,n->right,t->elem,false),t->elem,n->right);pop(c);n->type=t->elem;break;
    }
    default:km_error(cx(c),n->tok,"E100","unsupported expression");
    }
    return n->type;
}
static bool contains_break(Node *n) {
    if(!n)return false;
    if(n->kind==N_BREAK)return true;
    if(n->kind==N_FOR)return false;
    if(n->kind==N_BLOCK){for(size_t i=0;i<n->items.len;i++)if(contains_break(n->items.data[i]))return true;}
    if(n->kind==N_IF)return contains_break(n->body)||contains_break(n->otherwise);
    if(n->kind==N_UNSAFE||n->kind==N_ARENA)return contains_break(n->body);
    if(n->kind==N_MATCH){for(size_t i=0;i<n->items.len;i++)if(contains_break(n->items.data[i]->body))return true;}
    return false;
}
static bool block(Checker *c,Node *n,bool new_scope) {
    if(new_scope)push(c);
    bool terminal=false;
    for(size_t i=0;i<n->items.len;i++) {
        if(terminal)km_error(cx(c),n->items.data[i]->tok,"E121","unreachable statement");
        terminal=statement(c,n->items.data[i]);remove_temporary(c);
    }
    if(new_scope)pop(c);
    return terminal;
}
static bool statement(Checker *c,Node *n) {
    switch(n->kind) {
    case N_BLOCK:return block(c,n,true);
    case N_DEFER:
        if(n->value->kind!=N_CALL)km_error(cx(c),n->tok,"E143","defer requires a function call");
        c->deferred_call=true;
        if(expr(c,n->value,NULL,false)->kind!=TY_VOID)km_error(cx(c),n->tok,"E143","deferred cleanup must return void");
        c->deferred_call=false;return false;
    case N_ARENA: {
        push(c);n->type=cx(c)->t_arena;n->symbol=bind(c,n,n->type,false);
        bool terminal=block(c,n->body,false);pop(c);return terminal;
    }
    case N_DESTRUCT: {
        Type *t=expr(c,n->value,NULL,false);
        if(t->kind!=TY_TUPLE||t->members.len!=n->params.len)km_error(cx(c),n->tok,"E140","destructuring requires a tuple of matching arity");
        n->type=t;
        for(size_t i=0;i<n->params.len;i++){Node *b=n->params.data[i];b->type=t->members.data[i];if(!eq(b->name,"_"))b->symbol=bind(c,b,b->type,!n->flag);}
        return false;
    }
    case N_MATCH: {
        Type *t=expr(c,n->value,NULL,false);remove_temporary(c);n->type=t;
        if(t->kind!=TY_ENUM&&t->kind!=TY_RESULT)km_error(cx(c),n->tok,"E141","match requires an enum or result value");
        size_t count=t->kind==TY_RESULT?2:t->decl->fields.len;
        bool *seen=km_alloc(cx(c),count*sizeof(*seen));bool fallback=false,terminal=true;
        for(size_t i=0;i<n->items.len;i++){
            Node *arm=n->items.data[i];if(fallback)km_error(cx(c),arm->tok,"E141","pattern after '_' is unreachable");
            bool any=eq(arm->name,"_");Node *variant=NULL;size_t tag=0;Nodes payload={0};
            if(any){fallback=true;arm->integer=UINT64_MAX;if(arm->params.len)km_error(cx(c),arm->tok,"E141","wildcard cannot bind payload");}
            else if(t->kind==TY_RESULT){
                if(eq(arm->name,"ok"))tag=0;else if(eq(arm->name,"fail"))tag=1;else km_error(cx(c),arm->tok,"E141","result patterns must be ok(value) and fail(code)");
                if(arm->params.len!=1)km_error(cx(c),arm->tok,"E141","result pattern requires one binding");
                arm->integer=tag;
            }else{
                const char *name=strrchr(arm->name,'.');name=name?name+1:arm->name;
                if(name!=arm->name){char *prefix=km_strndup(cx(c),arm->name,(size_t)(name-arm->name-1));Node *owner=find_struct(c,module_of(c->fn),prefix,arm->tok);if(!owner||!km_type_equal(owner->type,t))km_error(cx(c),arm->tok,"E141","pattern belongs to another enum");}
                for(size_t j=0;j<count;j++)if(eq(t->decl->fields.data[j]->name,name))variant=t->decl->fields.data[j];
                if(!variant)km_error(cx(c),arm->tok,"E141","unknown variant pattern '%s'",name);
                tag=(size_t)variant->integer;payload=variant->params;arm->function=variant;arm->integer=tag;
                if(arm->params.len!=payload.len)km_error(cx(c),arm->tok,"E141","variant pattern has incorrect payload arity");
            }
            if(!any){if(seen[tag])km_error(cx(c),arm->tok,"E141","duplicate variant pattern");seen[tag]=true;}
            push(c);
            for(size_t j=0;j<arm->params.len;j++){
                Node *b=arm->params.data[j];b->type=t->kind==TY_RESULT?(tag==0?t->elem:cx(c)->ints[3]):payload.data[j]->type;
                if(!eq(b->name,"_"))b->symbol=bind(c,b,b->type,false);
            }
            bool exits=block(c,arm->body,false);pop(c);terminal=terminal&&exits;
        }
        if(!fallback)for(size_t i=0;i<count;i++)if(!seen[i])km_error(cx(c),n->tok,"E141","non-exhaustive match; add missing variants or '_'");
        return terminal;
    }
    case N_VAR: {
        Type *want=n->annotation?valid_type(c,n->annotation,n,false):NULL,*t;
        Loan *baseline=c->loans;
        if(!n->value) {
            if(!want)km_error(cx(c),n->tok,"E116","variable requires a type or initializer");
            if(n->flag||contains_address(want)||contains_result_value(want)||want->kind==TY_ARENA||want->kind==TY_FILE||want->kind==TY_THREAD)km_error(cx(c),n->tok,"E116","this value requires an explicit initializer");
            t=want;
        } else {
            t=expr(c,n->value,want,true);
            if(t->kind==TY_VOID)km_error(cx(c),n->tok,"E116","cannot bind a void expression");
            if(want)same(c,t,want,n->value);
            if(t->kind==TY_REF&&!(n->value->kind==N_UNARY&&(eq(n->value->op,"&")||eq(n->value->op,"&mut"))))km_error(cx(c),n->tok,"E130","reference binding requires a fresh explicit borrow");
            if(t->kind==TY_PTR&&!eq(n->value->builtin,"raw"))km_error(cx(c),n->tok,"E130","pointer binding requires raw(reference) in an unsafe block");
        }
        check_size(c,t,n);n->type=t;n->symbol=bind(c,n,t,!n->flag);
        if(n->flag&&n->value&&n->value->constant){Binding *b=binding_for(c,n->symbol);b->constant=true;b->value=n->value->constant_value;b->neg=n->value->constant_negative;}
        if(n->value&&(t->kind==TY_REF||t->kind==TY_PTR)) {
            n->symbol->root=n->value->borrow_root;n->symbol->via=n->value->borrow_via;
            n->symbol->borrow_mut=t->kind==TY_REF?t->mut:false;
            if(t->kind==TY_PTR){Type *rt=n->value->args.data[0]->type;n->symbol->borrow_mut=rt->mut;}
            for(Loan *l=c->loans;l&&l!=baseline;l=l->next)if(!l->owner)l->owner=n->symbol;
        }
        return false;
    }
    case N_ASSIGN: {
        if(n->left->kind==N_NAME&&eq(n->left->name,"_")) {
            if(!eq(n->op,"="))km_error(cx(c),n->tok,"E111","discard only supports plain '='");
            Type *t=expr(c,n->value,NULL,false);if(t->kind==TY_VOID)km_error(cx(c),n->tok,"E116","cannot discard a void expression");n->left->type=t;return false;
        }
        Type *t=place(c,n->left,true,!eq(n->op,"=")).type;
        if(t->kind==TY_REF||t->kind==TY_PTR)km_error(cx(c),n->tok,"E130","references and pointers cannot be reassigned");
        same(c,expr(c,n->value,t,false),t,n->value);
        if(!eq(n->op,"=")) {
            if(!km_is_int(t))km_error(cx(c),n->tok,"E112","compound assignment requires an integer");
            size_t len=strlen(n->op);char *op=km_strndup(cx(c),n->op,len?len-1:0);binary_constants(c,op,n->left,n->value,t,n);
        }
        return false;
    }
    case N_EXPRSTMT: {
        Type *t=expr(c,n->value,NULL,false);
        if(t->kind!=TY_VOID)km_error(cx(c),n->tok,"E117","unused expression result; assign it or use '_ = expression'");
        return n->value->kind==N_CALL&&eq(n->value->builtin,"panic");
    }
    case N_IF: {
        same(c,expr(c,n->condition,cx(c)->t_bool,false),cx(c)->t_bool,n->condition);remove_temporary(c);
        bool yes=block(c,n->body,true),no=false;
        if(n->otherwise)no=statement(c,n->otherwise);
        return yes&&no;
    }
    case N_FOR: {
        push(c);if(n->init){(void)statement(c,n->init);remove_temporary(c);}
        if(n->condition){same(c,expr(c,n->condition,cx(c)->t_bool,false),cx(c)->t_bool,n->condition);remove_temporary(c);}
        c->loops++;(void)block(c,n->body,true);if(n->post)(void)statement(c,n->post);c->loops--;pop(c);
        return (!n->condition||(n->condition->constant&&n->condition->constant_value))&&!contains_break(n->body);
    }
    case N_RETURN:
        if(!n->value)same(c,cx(c)->t_void,c->fn->result,n);
        else {Type *t=expr(c,n->value,c->fn->result,false);same(c,t,c->fn->result,n);if(contains_address(t))km_error(cx(c),n->tok,"E130","reference and raw pointer returns are forbidden");}
        return true;
    case N_UNSAFE:{c->unsafe_depth++;bool r=block(c,n->body,true);c->unsafe_depth--;return r;}
    case N_BREAK:case N_CONTINUE:if(!c->loops)km_error(cx(c),n->tok,"E118","break/continue is only valid inside a loop");return true;
    default:km_error(cx(c),n->tok,"E100","unsupported statement");
    }
}
static bool signature_equal(Node *a,Node *b) {
    if(a->external!=b->external||a->params.len!=b->params.len||!km_type_equal(a->result,b->result)||(a->receiver==NULL)!=(b->receiver==NULL))return false;
    if(a->receiver&&!km_type_equal(a->receiver->type,b->receiver->type))return false;
    for(size_t i=0;i<a->params.len;i++)if(!km_type_equal(a->params.data[i]->type,b->params.data[i]->type))return false;
    return true;
}
static void resolve_fields(Checker *c,Nodes fields) {
    for(size_t i=0;i<fields.len;i++){
        Node *f=fields.data[i];for(size_t j=0;j<i;j++)if(eq(f->name,fields.data[j]->name))km_error(cx(c),f->tok,"E102","duplicate field '%s'",f->name);
        f->type=valid_type(c,f->annotation,f,false);if(contains_address(f->type))km_error(cx(c),f->tok,"E130","aggregate fields cannot contain references or pointers");if(contains_thread(f->type))km_error(cx(c),f->tok,"E150","aggregate fields cannot contain thread handles");
    }
}
bool km_check(Context *ctx) {
    Checker checker={0};Checker *c=&checker;c->ctx=ctx;
    ctx->structs.len=0;ctx->functions.len=0;
    c->declaration_cap=16;
    while(c->declaration_cap/2<ctx->decls.len)c->declaration_cap*=2;
    c->struct_names=km_alloc(ctx,c->declaration_cap*sizeof(*c->struct_names));
    c->function_names=km_alloc(ctx,c->declaration_cap*sizeof(*c->function_names));
    Nodes repeated_structs={0};
    for(size_t i=0;i<ctx->decls.len;i++) {
        Node *n=ctx->decls.data[i];if(n->kind!=N_STRUCT&&n->kind!=N_ENUM&&n->kind!=N_FN)km_error(ctx,n->tok,"E101","only types and functions are allowed at file scope");
        if(n->kind!=N_STRUCT&&n->kind!=N_ENUM)continue;
        Declaration *entry=struct_slot(c,module_of(n),n->name);
        Node *previous=entry->node;
        if(builtin_name(n->name))km_error(ctx,n->tok,"E102","reserved declaration '%s'",n->name);
        if(previous) {
            if(previous->kind!=n->kind)km_error(ctx,n->tok,"E110","type kind does not match its header");
            bool ph=previous->tok.src&&previous->tok.src->header;
            bool nh=n->tok.src&&n->tok.src->header;
            if(ph==nh)km_error(ctx,n->tok,"E102","duplicate struct '%s'",n->name);
            if(entry->repeated)km_error(ctx,n->tok,"E102","duplicate struct '%s'",n->name);
            entry->repeated=true;
            n->type=previous->type;n->cname=previous->cname;km_nodes_push(ctx,&repeated_structs,n);continue;
        }
        n->type=km_type(ctx,n->kind==N_ENUM?TY_ENUM:TY_STRUCT,NULL);n->type->name=km_sprintf(ctx,"m%zu.%s",module_id(ctx,module_of(n)),n->name);n->type->decl=n;
        n->cname=km_sprintf(ctx,"kr_s_%zu_%zu_%s",module_id(ctx,module_of(n)),strlen(n->name),n->name);
        register_declaration(entry,module_of(n),NULL,n,ctx->structs.len);km_nodes_push(ctx,&ctx->structs,n);
    }
    for(size_t i=0;i<ctx->structs.len;i++) {
        Node *n=ctx->structs.data[i];
        if(n->kind==N_ENUM){
            if(!n->fields.len||n->fields.len>256)km_error(ctx,n->tok,"E141","enum must have 1 to 256 variants");
            for(size_t j=0;j<n->fields.len;j++){Node *v=n->fields.data[j];for(size_t k=0;k<j;k++)if(eq(v->name,n->fields.data[k]->name))km_error(ctx,v->tok,"E102","duplicate variant");v->type=n->type;resolve_fields(c,v->params);}
        } else resolve_fields(c,n->fields);
    }
    for(size_t i=0;i<repeated_structs.len;i++) {
        Node *n=repeated_structs.data[i],*original=n->type->decl;
        if(n->fields.len!=original->fields.len)km_error(ctx,n->tok,"E110","struct '%s' does not match its header declaration",n->name);
        for(size_t j=0;j<n->fields.len;j++) {
            Node *f=n->fields.data[j],*other=original->fields.data[j];
            if(n->kind==N_ENUM){
                f->type=n->type;resolve_fields(c,f->params);
                if(!eq(f->name,other->name)||f->params.len!=other->params.len)km_error(ctx,f->tok,"E110","enum header mismatch");
                for(size_t k=0;k<f->params.len;k++)if(!eq(f->params.data[k]->name,other->params.data[k]->name)||!km_type_equal(f->params.data[k]->type,other->params.data[k]->type))km_error(ctx,f->tok,"E110","variant payload header mismatch");
                continue;
            }
            f->type=valid_type(c,f->annotation,f,false);
            if(!eq(f->name,other->name)||!km_type_equal(f->type,other->type))
                km_error(ctx,f->tok,"E110","struct '%s' does not match its header declaration",n->name);
        }
    }
    c->types_ready=true;
    for(size_t i=0;i<ctx->structs.len;i++)check_size(c,ctx->structs.data[i]->type,ctx->structs.data[i]);
    for(size_t i=0;i<ctx->decls.len;i++) {
        Node *n=ctx->decls.data[i];if(n->kind!=N_FN)continue;
        Type *base=NULL;
        if(n->receiver) {
            n->receiver->type=valid_type(c,n->receiver->annotation,n->receiver,false);base=n->receiver->type;if(base->kind==TY_REF)base=base->elem;
            if(base->kind!=TY_STRUCT&&base->kind!=TY_ENUM)km_error(ctx,n->receiver->tok,"E103","method receiver must be a named value type");
            if(module_of(base->decl)!=module_of(n))km_error(ctx,n->receiver->tok,"E103","a method must be declared in its receiver type's module");
        } else if(builtin_name(n->name)||find_struct(c,module_of(n),n->name,n->tok))km_error(ctx,n->tok,"E102","duplicate or reserved declaration '%s'",n->name);
        n->result=valid_type(c,n->result,n,true);if(contains_address(n->result))km_error(ctx,n->tok,"E130","functions cannot return references or raw pointers");if(contains_thread(n->result))km_error(ctx,n->tok,"E150","functions cannot return thread handles; join the worker in its creator thread");check_size(c,n->result,n);
        for(size_t j=0;j<n->params.len;j++){Node *p=n->params.data[j];p->type=valid_type(c,p->annotation,p,false);check_size(c,p->type,p);}
        if(!n->body&&!n->external&&!(n->tok.src&&n->tok.src->header))km_error(ctx,n->tok,"E119","function declarations without a body belong in .krh headers or must be extern");
        if(n->external&&n->body)km_error(ctx,n->tok,"E119","extern functions cannot define a body");
        if(n->external&&n->receiver)km_error(ctx,n->tok,"E119","extern methods are not supported");
        if(n->external)n->cname=n->name;
        else n->cname=km_sprintf(ctx,"kr_fn_%zu_%zu_%s_%zu_%s",module_id(ctx,module_of(n)),base?strlen(base->decl->name):0,base?base->decl->name:"",strlen(n->name),n->name);
        Declaration *entry=function_slot(c,module_of(n),n->name,base);
        Node *previous=entry->node;
        if(previous) {
            if(!signature_equal(previous,n))km_error(ctx,n->tok,"E110","signature does not match previous declaration of '%s'",n->name);
            if(previous->body&&n->body)km_error(ctx,n->tok,"E102","duplicate function '%s'",n->name);
            if(n->body){ctx->functions.data[entry->index]=n;entry->node=n;}
        } else {
            register_declaration(entry,base?NULL:module_of(n),base?base->decl:NULL,n,ctx->functions.len);
            km_nodes_push(ctx,&ctx->functions,n);
        }
    }
    Module *root=ctx->modules.len?ctx->modules.data[0]:NULL;Node *main=find_fn(c,root,"main",NULL);
    Token empty={0};empty.line=empty.col=1;if(ctx->decls.len)empty=ctx->decls.data[0]->tok;
    if(!main)km_error(ctx,empty,"E104","program requires func main()");
    if(main->params.len||main->result->kind!=TY_VOID||main->receiver||!main->body||main->external)km_error(ctx,main->tok,"E104","entry point must be func main() with no parameters or result");
    for(size_t i=0;i<ctx->functions.len;i++) {
        Node *n=ctx->functions.data[i];
        if(!n->body) {if(!n->external)km_error(ctx,n->tok,"E119","header declaration '%s' has no source definition",n->name);continue;}
        c->fn=n;c->bindings=NULL;c->loans=NULL;c->depth=0;c->loops=0;c->unsafe_depth=0;
        c->scope=NULL;c->local_names=NULL;c->local_cap=0;c->local_count=0;push(c);
        if(n->receiver)n->receiver->symbol=bind(c,n->receiver,n->receiver->type,true);
        for(size_t j=0;j<n->params.len;j++){Node *p=n->params.data[j];p->symbol=bind(c,p,p->type,true);p->symbol->borrow_mut=p->type->kind==TY_PTR;}
        bool terminal=block(c,n->body,false);
        if(n->result->kind!=TY_VOID&&!terminal)km_error(ctx,n->tok,"E120","function '%s' can finish without returning %s",n->name,km_type_name(ctx,n->result));
        pop(c);
    }
    if(ctx->mode==KM_MODE_LEARN)km_check_learn(ctx);
    return true;
}
