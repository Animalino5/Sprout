/*
 * typecheck.c — Type inference and checking
 *
 * Strategy: single-pass, top-down. When we see the first assignment to
 * a variable, we record its type in the symbol table. Subsequent uses
 * look up the type. If a use appears before any assignment, we error
 * (or treat as UNKNOWN).
 *
 * Builtins have hardcoded return types in a static table.
 */
#include "typecheck.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  /* strcasecmp */

/* strdup isn't in C11 strictly, define our own */
static char *nb_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* ── Builtin function return types ────────────────────────────────── */

typedef struct {
    const char *name;
    NbType      ret;
} BuiltinRet;

static const BuiltinRet BUILTINS[] = {
    /* Math */
    {"ABS",       TY_INT},
    {"SGN",       TY_INT},
    {"INT",       TY_INT},
    {"FIX",       TY_INT},
    {"SQR",       TY_FLOAT},
    {"SIN",       TY_FLOAT},
    {"COS",       TY_FLOAT},
    {"TAN",       TY_FLOAT},
    {"ATN",       TY_FLOAT},
    {"ATAN2",     TY_FLOAT},
    {"EXP",       TY_FLOAT},
    {"LOG",       TY_FLOAT},
    {"RND",       TY_INT},
    {"RNDF",      TY_FLOAT},
    {"MIN",       TY_INT},     /* actually same as arg, simplified */
    {"MAX",       TY_INT},
    {"CLAMP",     TY_INT},
    {"LERP",      TY_FLOAT},
    {"DIST",      TY_FLOAT},
    {"DEG",       TY_FLOAT},
    {"RAD",       TY_FLOAT},

    /* String */
    {"LEN",       TY_INT},
    {"LEFT$",     TY_STRING},
    {"RIGHT$",    TY_STRING},
    {"MID$",      TY_STRING},
    {"UCASE$",    TY_STRING},
    {"LCASE$",    TY_STRING},
    {"INSTR",     TY_INT},
    {"VAL",       TY_INT},
    {"STR$",      TY_STRING},
    {"CHR$",      TY_STRING},
    {"ASC",       TY_INT},
    {"INKEY$",    TY_STRING},

    /* Console */
    {"CSRLIN",    TY_INT},
    {"POS",       TY_INT},

    /* Input */
    {"BUTTON",    TY_INT},
    {"BUTTONDOWN",TY_INT},
    {"BUTTONUP",  TY_INT},
    {"INPUT_TOUCH_X",   TY_INT},
    {"INPUT_TOUCH_Y",   TY_INT},
    {"INPUT_TOUCH_STATE", TY_INT},
    {"TOUCHDOWN", TY_INT},
    {"TOUCHPRESSED",  TY_INT},
    {"TOUCHRELEASED", TY_INT},

    /* Graphics */
    {"RGB",       TY_INT},
    {"POINT",     TY_INT},
    {"LOADIMAGE", TY_IMAGE},
    {"IMAGEW",    TY_INT},
    {"IMAGEH",    TY_INT},
    {"LOADSHEET", TY_IMAGE},

    /* Audio */
    {"LOADSOUND", TY_SOUND},
    {"LOADMUSIC", TY_SOUND},
    {"LOADFONT",  TY_INT},

    /* Files */
    {"EXISTS",    TY_INT},
    {"EOF",       TY_INT},
    {"LOF",       TY_INT},
    {"READNUM",   TY_INT},
    {"READLINE$", TY_STRING},

    /* Timing */
    {"MSECONDS",  TY_INT},
    {"FRAMECOUNT", TY_INT},
};

static const int NUM_BUILTINS = (int)(sizeof(BUILTINS) / sizeof(BUILTINS[0]));

NbType nb_tc_builtin_return_type(const char *name) {
    for (int i = 0; i < NUM_BUILTINS; i++) {
        if (strcasecmp(name, BUILTINS[i].name) == 0) {
            return BUILTINS[i].ret;
        }
    }
    return TY_UNKNOWN;
}

/* ── Symbol table ─────────────────────────────────────────────────── */

void nb_tc_init(NbTypeChecker *tc) {
    tc->symbols = NULL;
    tc->num_symbols = 0;
    tc->cap_symbols = 0;
    tc->had_error = 0;
}

void nb_tc_free(NbTypeChecker *tc) {
    for (int i = 0; i < tc->num_symbols; i++) {
        free(tc->symbols[i].name);
    }
    free(tc->symbols);
}

const NbSymbol *nb_tc_lookup(const NbTypeChecker *tc, const char *name) {
    for (int i = 0; i < tc->num_symbols; i++) {
        if (strcasecmp(tc->symbols[i].name, name) == 0) {
            return &tc->symbols[i];
        }
    }
    return NULL;
}

/* Add or update a symbol. Returns the symbol entry. */
static NbSymbol *tc_add_or_update(NbTypeChecker *tc, const char *name,
                                    NbType type, int is_array,
                                    int array_size, int line) {
    /* Look for existing */
    for (int i = 0; i < tc->num_symbols; i++) {
        if (strcasecmp(tc->symbols[i].name, name) == 0) {
            if (type != TY_UNKNOWN) {
                tc->symbols[i].type = type;
            }
            if (is_array) {
                tc->symbols[i].is_array = 1;
                tc->symbols[i].array_size = array_size;
            }
            return &tc->symbols[i];
        }
    }
    /* Add new */
    if (tc->num_symbols >= tc->cap_symbols) {
        tc->cap_symbols = tc->cap_symbols ? tc->cap_symbols * 2 : 16;
        tc->symbols = (NbSymbol *)realloc(tc->symbols,
                                          sizeof(NbSymbol) * tc->cap_symbols);
    }
    NbSymbol *s = &tc->symbols[tc->num_symbols++];
    s->name = nb_strdup(name);
    s->type = type;
    s->is_array = is_array;
    s->array_size = array_size;
    s->line = line;
    return s;
}

static void tc_error(NbTypeChecker *tc, int line, const char *msg) {
    fprintf(stderr, "Type error %d: %s\n", line, msg);
    tc->had_error = 1;
}

/* ── Forward declarations ─────────────────────────────────────────── */

static NbType check_expr(NbTypeChecker *tc, AstNode *expr);
static void check_stmt(NbTypeChecker *tc, AstNode *stmt);
static void check_stmt_list(NbTypeChecker *tc, AstNode **stmts, int n);

/* ── Expression type checking ─────────────────────────────────────── */

static NbType check_expr(NbTypeChecker *tc, AstNode *expr) {
    if (!expr) return TY_UNKNOWN;
    switch (expr->type) {
        case AST_INT_LIT:
            return TY_INT;
        case AST_FLOAT_LIT:
            return TY_FLOAT;
        case AST_STRING_LIT:
            return TY_STRING;
        case AST_IDENT: {
            const NbSymbol *s = nb_tc_lookup(tc, expr->u.text);
            if (!s) {
                /* Not yet declared. Don't error here — might be a
                 * builtin used as identifier, or might be assigned later.
                 * Leave as UNKNOWN. */
                return TY_UNKNOWN;
            }
            return s->type;
        }
        case AST_BINARY_OP: {
            NbType lt = check_expr(tc, expr->u.binary.left);
            NbType rt = check_expr(tc, expr->u.binary.right);
            /* String concatenation: + or & with at least one string */
            int is_concat = (expr->u.binary.op == TOK_PLUS || expr->u.binary.op == TOK_EQ) &&
                            (lt == TY_STRING || rt == TY_STRING);
            if (is_concat) return TY_STRING;
            if (lt == TY_FLOAT || rt == TY_FLOAT) return TY_FLOAT;
            if (lt == TY_INT && rt == TY_INT) return TY_INT;
            if (lt == TY_UNKNOWN || rt == TY_UNKNOWN) return TY_UNKNOWN;
            /* Mismatch */
            return TY_UNKNOWN;
        }
        case AST_UNARY_OP: {
            return check_expr(tc, expr->u.unary.operand);
        }
        case AST_CALL: {
            /* User function or builtin */
            /* First, check if it's a user function — we'd need a function
             * table. For v1, assume builtin. */
            return nb_tc_builtin_return_type(expr->u.call.name);
        }
        case AST_INDEX: {
            /* Array element — type is the array's element type.
             * For v1, arrays are always INT. */
            return TY_INT;
        }
        case AST_MEMBER: {
            /* Module variable — we don't have module symbol tables yet.
             * Default to UNKNOWN, let emitter handle. */
            return TY_UNKNOWN;
        }
        default:
            return TY_UNKNOWN;
    }
}

/* ── Statement type checking ──────────────────────────────────────── */

static void check_assign(NbTypeChecker *tc, AstNode *stmt) {
    AstNode *target = stmt->u.assign.target;
    AstNode *value  = stmt->u.assign.value;
    NbType vt = check_expr(tc, value);

    if (target->type == AST_IDENT) {
        const NbSymbol *existing = nb_tc_lookup(tc, target->u.text);
        if (!existing) {
            /* First assignment — declare with inferred type */
            if (vt == TY_UNKNOWN) {
                /* Can't infer — default to INT */
                vt = TY_INT;
            }
            tc_add_or_update(tc, target->u.text, vt, 0, 0, stmt->line);
        } else {
            /* Already declared — check compatibility.
             * Allow implicit float→int (truncation) and int→float (promotion).
             * This is the #1 pain point for beginners: writing x = 0 then
             * later x = x + 0.5 should just work, not error. */
            if (existing->type != TY_UNKNOWN && vt != TY_UNKNOWN) {
                /* Both numeric types are compatible — allow it */
                if ((existing->type == TY_INT || existing->type == TY_FLOAT) &&
                    (vt == TY_INT || vt == TY_FLOAT)) {
                    /* OK — implicit coercion allowed */
                } else if (!nb_type_compatible(vt, existing->type)) {
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                             "cannot assign %s to %s variable '%s'",
                             nb_type_name(vt), nb_type_name(existing->type),
                             target->u.text);
                    tc_error(tc, stmt->line, buf);
                }
            }
        }
    } else if (target->type == AST_INDEX) {
        /* Array element assignment — ensure array is declared */
        const NbSymbol *s = nb_tc_lookup(tc, target->u.index.name);
        if (!s) {
            /* Declare as INT array */
            tc_add_or_update(tc, target->u.index.name, TY_INT, 1, 0,
                             stmt->line);
        }
    } else if (target->type == AST_MEMBER) {
        /* Module variable — skip for v1 */
    }
}

static void check_print(NbTypeChecker *tc, AstNode *stmt) {
    /* Just check each part — no constraint */
    for (int i = 0; i < stmt->u.print.num_parts; i++) {
        check_expr(tc, stmt->u.print.parts[i]);
    }
}

static void check_dim(NbTypeChecker *tc, AstNode *stmt) {
    int size = 0;
    if (stmt->u.dim.num_sizes > 0 &&
        stmt->u.dim.sizes[0]->type == AST_INT_LIT) {
        size = (int)stmt->u.dim.sizes[0]->u.ival;
    }
    tc_add_or_update(tc, stmt->u.dim.var_name, TY_INT, 1, size, stmt->line);
}

static void check_func_def(NbTypeChecker *tc, AstNode *stmt) {
    /* Add function name to symbol table first (as UNKNOWN — we'll
     * update it after checking the body). */
    tc_add_or_update(tc, stmt->u.func_def.name, TY_UNKNOWN, 0, 0, stmt->line);

    /* Add params to symbol table as INT (default for untyped params).
     * Array params are marked as arrays. */
    for (int i = 0; i < stmt->u.func_def.num_params; i++) {
        int is_arr = stmt->u.func_def.param_is_array ?
                     stmt->u.func_def.param_is_array[i] : 0;
        tc_add_or_update(tc, stmt->u.func_def.params[i], TY_INT,
                         is_arr, 0, stmt->line);
    }

    /* Check the body FIRST — this processes assignments and adds local
     * variables to the symbol table with their inferred types. */
    check_stmt_list(tc, stmt->u.func_def.body, stmt->u.func_def.num_body);

    /* NOW scan for RETURN statements to infer the return type.
     * By this point, all local variables have types. */
    NbType ret_type = TY_UNKNOWN;
    for (int i = 0; i < stmt->u.func_def.num_body; i++) {
        AstNode *s = stmt->u.func_def.body[i];
        if (s->type == AST_RETURN && s->u.return_stmt.expr) {
            NbType t = check_expr(tc, s->u.return_stmt.expr);
            if (ret_type == TY_UNKNOWN) ret_type = t;
        }
    }
    if (ret_type == TY_UNKNOWN) ret_type = TY_VOID;

    /* Update the function's type in the symbol table */
    tc_add_or_update(tc, stmt->u.func_def.name, ret_type, 0, 0, stmt->line);
}

static void check_stmt(NbTypeChecker *tc, AstNode *stmt) {
    if (!stmt) return;
    switch (stmt->type) {
        case AST_PRINT:       check_print(tc, stmt); break;
        case AST_ASSIGN:      check_assign(tc, stmt); break;
        case AST_IF:
            for (int i = 0; i < stmt->u.if_stmt.num_branches; i++) {
                if (stmt->u.if_stmt.branches[i].cond) {
                    check_expr(tc, stmt->u.if_stmt.branches[i].cond);
                }
                check_stmt_list(tc, stmt->u.if_stmt.branches[i].body,
                                stmt->u.if_stmt.branches[i].num_body);
            }
            break;
        case AST_WHILE:
            check_expr(tc, stmt->u.while_stmt.cond);
            check_stmt_list(tc, stmt->u.while_stmt.body,
                            stmt->u.while_stmt.num_body);
            break;
        case AST_FOR:
            check_expr(tc, stmt->u.for_stmt.start);
            check_expr(tc, stmt->u.for_stmt.end);
            if (stmt->u.for_stmt.step) {
                check_expr(tc, stmt->u.for_stmt.step);
            }
            /* Loop variable — declare as INT */
            tc_add_or_update(tc, stmt->u.for_stmt.var_name, TY_INT,
                             0, 0, stmt->line);
            check_stmt_list(tc, stmt->u.for_stmt.body,
                            stmt->u.for_stmt.num_body);
            break;
        case AST_DO_LOOP:
            if (stmt->u.do_loop.cond) check_expr(tc, stmt->u.do_loop.cond);
            check_stmt_list(tc, stmt->u.do_loop.body,
                            stmt->u.do_loop.num_body);
            break;
        case AST_FUNCTION_DEF:
            check_func_def(tc, stmt);
            break;
        case AST_RETURN:
            if (stmt->u.return_stmt.expr) {
                check_expr(tc, stmt->u.return_stmt.expr);
            }
            break;
        case AST_DIM:
            check_dim(tc, stmt);
            break;
        case AST_ROOM:
            check_stmt_list(tc, stmt->u.room.init_body,
                            stmt->u.room.num_init);
            check_stmt_list(tc, stmt->u.room.update_body,
                            stmt->u.room.num_update);
            break;
        case AST_GOTOROOM:
        case AST_IMPORT:
            /* No type implications */
            break;
        case AST_EXPR_STMT:
            check_expr(tc, stmt->u.assign.target);
            break;
        default:
            break;
    }
}

static void check_stmt_list(NbTypeChecker *tc, AstNode **stmts, int n) {
    for (int i = 0; i < n; i++) {
        check_stmt(tc, stmts[i]);
    }
}

void nb_tc_check_program(NbTypeChecker *tc, AstNode *prog) {
    if (!prog || prog->type != AST_PROGRAM) return;
    check_stmt_list(tc, prog->u.program.stmts, prog->u.program.num_stmts);
}
