/*
 * ast.c — AST constructors, free, and pretty-print
 */
#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Helpers ──────────────────────────────────────────────────────── */

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static AstNode *new_node(AstType type, int line, int col) {
    AstNode *n = (AstNode *)calloc(1, sizeof(AstNode));
    if (!n) { perror("calloc"); exit(1); }
    n->type = type;
    n->line = line;
    n->col  = col;
    return n;
}

static AstNode **dup_ptr_array(AstNode **src, int n) {
    if (n <= 0) return NULL;
    AstNode **dst = (AstNode **)malloc(sizeof(AstNode *) * n);
    if (!dst) { perror("malloc"); exit(1); }
    memcpy(dst, src, sizeof(AstNode *) * n);
    return dst;
}

/* ── Constructors ─────────────────────────────────────────────────── */

AstNode *ast_new_program(void) {
    AstNode *n = new_node(AST_PROGRAM, 0, 0);
    n->u.program.stmts = NULL;
    n->u.program.num_stmts = 0;
    return n;
}

AstNode *ast_new_int(long val, int line, int col) {
    AstNode *n = new_node(AST_INT_LIT, line, col);
    n->u.ival = val;
    return n;
}

AstNode *ast_new_float(double val, int line, int col) {
    AstNode *n = new_node(AST_FLOAT_LIT, line, col);
    n->u.fval = val;
    return n;
}

AstNode *ast_new_string(const char *text, int line, int col) {
    AstNode *n = new_node(AST_STRING_LIT, line, col);
    n->u.text = dup_str(text);
    return n;
}

AstNode *ast_new_ident(const char *text, int line, int col) {
    AstNode *n = new_node(AST_IDENT, line, col);
    n->u.text = dup_str(text);
    return n;
}

AstNode *ast_new_binary(TokenType op, AstNode *left, AstNode *right,
                        int line, int col) {
    AstNode *n = new_node(AST_BINARY_OP, line, col);
    n->u.binary.op    = op;
    n->u.binary.left  = left;
    n->u.binary.right = right;
    return n;
}

AstNode *ast_new_unary(TokenType op, AstNode *operand, int line, int col) {
    AstNode *n = new_node(AST_UNARY_OP, line, col);
    n->u.unary.op      = op;
    n->u.unary.operand = operand;
    return n;
}

AstNode *ast_new_call(const char *name, AstNode **args, int num_args,
                      int line, int col) {
    AstNode *n = new_node(AST_CALL, line, col);
    n->u.call.name     = dup_str(name);
    n->u.call.args     = dup_ptr_array(args, num_args);
    n->u.call.num_args = num_args;
    return n;
}

AstNode *ast_new_index(const char *name, AstNode **indices, int num_indices,
                       int line, int col) {
    AstNode *n = new_node(AST_INDEX, line, col);
    n->u.index.name        = dup_str(name);
    n->u.index.indices     = dup_ptr_array(indices, num_indices);
    n->u.index.num_indices = num_indices;
    return n;
}

AstNode *ast_new_member(const char *module, const char *member,
                        int line, int col) {
    AstNode *n = new_node(AST_MEMBER, line, col);
    n->u.member.module = dup_str(module);
    n->u.member.member = dup_str(member);
    return n;
}

AstNode *ast_new_print(AstNode **parts, char *seps, int num_parts,
                       int line, int col) {
    AstNode *n = new_node(AST_PRINT, line, col);
    n->u.print.parts     = dup_ptr_array(parts, num_parts);
    /* seps is heap-allocated by caller; take ownership */
    n->u.print.seps      = seps;
    n->u.print.num_parts = num_parts;
    return n;
}

AstNode *ast_new_assign(AstNode *target, AstNode *value, int line, int col) {
    AstNode *n = new_node(AST_ASSIGN, line, col);
    n->u.assign.target = target;
    n->u.assign.value  = value;
    return n;
}

AstNode *ast_new_if(AstBranch *branches, int num_branches, int line, int col) {
    AstNode *n = new_node(AST_IF, line, col);
    n->u.if_stmt.branches    = branches;
    n->u.if_stmt.num_branches = num_branches;
    return n;
}

AstNode *ast_new_while(AstNode *cond, AstNode **body, int num_body,
                       int line, int col) {
    AstNode *n = new_node(AST_WHILE, line, col);
    n->u.while_stmt.cond     = cond;
    n->u.while_stmt.body     = dup_ptr_array(body, num_body);
    n->u.while_stmt.num_body = num_body;
    return n;
}

AstNode *ast_new_for(const char *var_name, AstNode *start, AstNode *end,
                     AstNode *step, AstNode **body, int num_body,
                     int line, int col) {
    AstNode *n = new_node(AST_FOR, line, col);
    n->u.for_stmt.var_name = dup_str(var_name);
    n->u.for_stmt.start    = start;
    n->u.for_stmt.end      = end;
    n->u.for_stmt.step     = step;
    n->u.for_stmt.body     = dup_ptr_array(body, num_body);
    n->u.for_stmt.num_body = num_body;
    return n;
}

AstNode *ast_new_do_loop(int pre_cond, int is_until, AstNode *cond,
                         AstNode **body, int num_body, int line, int col) {
    AstNode *n = new_node(AST_DO_LOOP, line, col);
    n->u.do_loop.pre_cond = pre_cond;
    n->u.do_loop.is_until = is_until;
    n->u.do_loop.cond     = cond;
    n->u.do_loop.body     = dup_ptr_array(body, num_body);
    n->u.do_loop.num_body = num_body;
    return n;
}

AstNode *ast_new_func(const char *name, char **params, int *param_is_array,
                      int num_params,
                      AstNode **body, int num_body, int line, int col) {
    AstNode *n = new_node(AST_FUNCTION_DEF, line, col);
    n->u.func_def.name       = dup_str(name);
    n->u.func_def.num_params = num_params;
    if (num_params > 0) {
        n->u.func_def.params = (char **)malloc(sizeof(char *) * num_params);
        n->u.func_def.param_is_array = (int *)malloc(sizeof(int) * num_params);
        for (int i = 0; i < num_params; i++) {
            n->u.func_def.params[i] = dup_str(params[i]);
            n->u.func_def.param_is_array[i] = param_is_array ? param_is_array[i] : 0;
        }
    } else {
        n->u.func_def.param_is_array = NULL;
    }
    n->u.func_def.body     = dup_ptr_array(body, num_body);
    n->u.func_def.num_body = num_body;
    return n;
}

AstNode *ast_new_return(AstNode *expr, int line, int col) {
    AstNode *n = new_node(AST_RETURN, line, col);
    n->u.return_stmt.expr = expr;
    return n;
}

AstNode *ast_new_dim(const char *var_name, AstNode **sizes, int num_sizes,
                     int line, int col) {
    AstNode *n = new_node(AST_DIM, line, col);
    n->u.dim.var_name  = dup_str(var_name);
    n->u.dim.sizes     = dup_ptr_array(sizes, num_sizes);
    n->u.dim.num_sizes = num_sizes;
    return n;
}

AstNode *ast_new_room(const char *name, AstNode **init_body, int num_init,
                      AstNode **update_body, int num_update, int line, int col) {
    AstNode *n = new_node(AST_ROOM, line, col);
    n->u.room.name         = dup_str(name);
    n->u.room.init_body    = dup_ptr_array(init_body, num_init);
    n->u.room.num_init     = num_init;
    n->u.room.update_body  = dup_ptr_array(update_body, num_update);
    n->u.room.num_update   = num_update;
    return n;
}

AstNode *ast_new_gotoroom(const char *name, int line, int col) {
    AstNode *n = new_node(AST_GOTOROOM, line, col);
    n->u.gotoroom.name = dup_str(name);
    return n;
}

AstNode *ast_new_import(const char *filename, int line, int col) {
    AstNode *n = new_node(AST_IMPORT, line, col);
    n->u.import.filename = dup_str(filename);
    return n;
}

AstNode *ast_new_expr_stmt(AstNode *expr, int line, int col) {
    AstNode *n = new_node(AST_EXPR_STMT, line, col);
    /* expr is the call node — take ownership, don't dup */
    n = new_node(AST_EXPR_STMT, line, col);
    n->u.assign.target = expr;  /* reuse assign.target field to hold the expr */
    return n;
}

/* ── Program append ───────────────────────────────────────────────── */

void ast_program_append(AstNode *prog, AstNode *stmt) {
    int n = prog->u.program.num_stmts;
    prog->u.program.stmts = (AstNode **)realloc(
        prog->u.program.stmts, sizeof(AstNode *) * (n + 1));
    prog->u.program.stmts[n] = stmt;
    prog->u.program.num_stmts = n + 1;
}

/* ── Free ─────────────────────────────────────────────────────────── */

static void free_stmt_array(AstNode **stmts, int n);
static void free_branches(AstBranch *branches, int n);

static void ast_free_impl(AstNode *n) {
    if (!n) return;
    switch (n->type) {
        case AST_PROGRAM:
            free_stmt_array(n->u.program.stmts, n->u.program.num_stmts);
            break;
        case AST_INT_LIT:
        case AST_FLOAT_LIT:
            break;
        case AST_STRING_LIT:
        case AST_IDENT:
            free(n->u.text);
            break;
        case AST_BINARY_OP:
            ast_free_impl(n->u.binary.left);
            ast_free_impl(n->u.binary.right);
            break;
        case AST_UNARY_OP:
            ast_free_impl(n->u.unary.operand);
            break;
        case AST_CALL:
            free(n->u.call.name);
            free_stmt_array(n->u.call.args, n->u.call.num_args);
            free(n->u.call.args);
            break;
        case AST_INDEX:
            free(n->u.index.name);
            free_stmt_array(n->u.index.indices, n->u.index.num_indices);
            free(n->u.index.indices);
            break;
        case AST_MEMBER:
            free(n->u.member.module);
            free(n->u.member.member);
            break;
        case AST_PRINT:
            free_stmt_array(n->u.print.parts, n->u.print.num_parts);
            free(n->u.print.parts);
            free(n->u.print.seps);
            break;
        case AST_ASSIGN:
            ast_free_impl(n->u.assign.target);
            ast_free_impl(n->u.assign.value);
            break;
        case AST_IF:
            free_branches(n->u.if_stmt.branches, n->u.if_stmt.num_branches);
            break;
        case AST_WHILE:
            ast_free_impl(n->u.while_stmt.cond);
            free_stmt_array(n->u.while_stmt.body, n->u.while_stmt.num_body);
            free(n->u.while_stmt.body);
            break;
        case AST_FOR:
            free(n->u.for_stmt.var_name);
            ast_free_impl(n->u.for_stmt.start);
            ast_free_impl(n->u.for_stmt.end);
            ast_free_impl(n->u.for_stmt.step);
            free_stmt_array(n->u.for_stmt.body, n->u.for_stmt.num_body);
            free(n->u.for_stmt.body);
            break;
        case AST_DO_LOOP:
            ast_free_impl(n->u.do_loop.cond);
            free_stmt_array(n->u.do_loop.body, n->u.do_loop.num_body);
            free(n->u.do_loop.body);
            break;
        case AST_FUNCTION_DEF:
            free(n->u.func_def.name);
            for (int i = 0; i < n->u.func_def.num_params; i++) {
                free(n->u.func_def.params[i]);
            }
            free(n->u.func_def.params);
            free(n->u.func_def.param_is_array);
            free_stmt_array(n->u.func_def.body, n->u.func_def.num_body);
            free(n->u.func_def.body);
            break;
        case AST_RETURN:
            ast_free_impl(n->u.return_stmt.expr);
            break;
        case AST_DIM:
            free(n->u.dim.var_name);
            free_stmt_array(n->u.dim.sizes, n->u.dim.num_sizes);
            free(n->u.dim.sizes);
            break;
        case AST_ROOM:
            free(n->u.room.name);
            free_stmt_array(n->u.room.init_body, n->u.room.num_init);
            free(n->u.room.init_body);
            free_stmt_array(n->u.room.update_body, n->u.room.num_update);
            free(n->u.room.update_body);
            break;
        case AST_GOTOROOM:
            free(n->u.gotoroom.name);
            break;
        case AST_IMPORT:
            free(n->u.import.filename);
            break;
        case AST_EXPR_STMT:
            /* target field reused to hold the call expression */
            ast_free_impl(n->u.assign.target);
            break;
    }
    free(n);
}

static void free_stmt_array(AstNode **stmts, int n) {
    if (!stmts) return;
    for (int i = 0; i < n; i++) {
        ast_free_impl(stmts[i]);
    }
}

static void free_branches(AstBranch *branches, int n) {
    if (!branches) return;
    for (int i = 0; i < n; i++) {
        ast_free_impl(branches[i].cond);
        free_stmt_array(branches[i].body, branches[i].num_body);
        free(branches[i].body);
    }
    free(branches);
}

void ast_free(AstNode *node) {
    ast_free_impl(node);
}

/* ── Pretty-print ─────────────────────────────────────────────────── */

static void print_indent(int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
}

static void print_op(TokenType op) {
    printf("%s", token_type_name(op));
}

static void ast_print_impl(const AstNode *n, int depth) {
    if (!n) { print_indent(depth); printf("(null)\n"); return; }
    print_indent(depth);
    switch (n->type) {
        case AST_PROGRAM:
            printf("[Program] (%d top-level stmts)\n", n->u.program.num_stmts);
            for (int i = 0; i < n->u.program.num_stmts; i++) {
                ast_print_impl(n->u.program.stmts[i], depth + 1);
            }
            break;
        case AST_INT_LIT:
            printf("[Int] %ld\n", n->u.ival);
            break;
        case AST_FLOAT_LIT:
            printf("[Float] %g\n", n->u.fval);
            break;
        case AST_STRING_LIT:
            printf("[String] \"%s\"\n", n->u.text ? n->u.text : "");
            break;
        case AST_IDENT:
            printf("[Ident] %s\n", n->u.text ? n->u.text : "");
            break;
        case AST_BINARY_OP:
            printf("[BinOp ");
            print_op(n->u.binary.op);
            printf("]\n");
            ast_print_impl(n->u.binary.left,  depth + 1);
            ast_print_impl(n->u.binary.right, depth + 1);
            break;
        case AST_UNARY_OP:
            printf("[Unary ");
            print_op(n->u.unary.op);
            printf("]\n");
            ast_print_impl(n->u.unary.operand, depth + 1);
            break;
        case AST_CALL:
            printf("[Call] %s (%d arg%s)\n",
                   n->u.call.name ? n->u.call.name : "?",
                   n->u.call.num_args,
                   n->u.call.num_args == 1 ? "" : "s");
            for (int i = 0; i < n->u.call.num_args; i++) {
                ast_print_impl(n->u.call.args[i], depth + 1);
            }
            break;
        case AST_INDEX:
            printf("[Index] %s (%d idx)\n",
                   n->u.index.name ? n->u.index.name : "?",
                   n->u.index.num_indices);
            for (int i = 0; i < n->u.index.num_indices; i++) {
                ast_print_impl(n->u.index.indices[i], depth + 1);
            }
            break;
        case AST_MEMBER:
            printf("[Member] %s.%s\n",
                   n->u.member.module ? n->u.member.module : "?",
                   n->u.member.member ? n->u.member.member : "?");
            break;
        case AST_PRINT:
            printf("[Print] (%d part%s)\n",
                   n->u.print.num_parts,
                   n->u.print.num_parts == 1 ? "" : "s");
            for (int i = 0; i < n->u.print.num_parts; i++) {
                print_indent(depth + 1);
                printf("sep='%c' ->\n",
                       n->u.print.seps ? n->u.print.seps[i] : ' ');
                ast_print_impl(n->u.print.parts[i], depth + 2);
            }
            break;
        case AST_ASSIGN:
            printf("[Assign]\n");
            print_indent(depth + 1); printf("target:\n");
            ast_print_impl(n->u.assign.target, depth + 2);
            print_indent(depth + 1); printf("value:\n");
            ast_print_impl(n->u.assign.value, depth + 2);
            break;
        case AST_IF:
            printf("[If] (%d branch%s)\n",
                   n->u.if_stmt.num_branches,
                   n->u.if_stmt.num_branches == 1 ? "" : "es");
            for (int i = 0; i < n->u.if_stmt.num_branches; i++) {
                print_indent(depth + 1);
                if (n->u.if_stmt.branches[i].cond) {
                    printf("branch %d (%s):\n", i,
                           i == 0 ? "THEN" : "ELSEIF");
                    print_indent(depth + 2); printf("cond:\n");
                    ast_print_impl(n->u.if_stmt.branches[i].cond, depth + 3);
                } else {
                    printf("branch %d (ELSE):\n", i);
                }
                print_indent(depth + 2); printf("body (%d):\n",
                       n->u.if_stmt.branches[i].num_body);
                for (int j = 0; j < n->u.if_stmt.branches[i].num_body; j++) {
                    ast_print_impl(n->u.if_stmt.branches[i].body[j],
                                   depth + 3);
                }
            }
            break;
        case AST_WHILE:
            printf("[While]\n");
            print_indent(depth + 1); printf("cond:\n");
            ast_print_impl(n->u.while_stmt.cond, depth + 2);
            print_indent(depth + 1); printf("body (%d):\n",
                   n->u.while_stmt.num_body);
            for (int i = 0; i < n->u.while_stmt.num_body; i++) {
                ast_print_impl(n->u.while_stmt.body[i], depth + 2);
            }
            break;
        case AST_FOR:
            printf("[For] var=%s\n",
                   n->u.for_stmt.var_name ? n->u.for_stmt.var_name : "?");
            print_indent(depth + 1); printf("start:\n");
            ast_print_impl(n->u.for_stmt.start, depth + 2);
            print_indent(depth + 1); printf("end:\n");
            ast_print_impl(n->u.for_stmt.end, depth + 2);
            if (n->u.for_stmt.step) {
                print_indent(depth + 1); printf("step:\n");
                ast_print_impl(n->u.for_stmt.step, depth + 2);
            }
            print_indent(depth + 1); printf("body (%d):\n",
                   n->u.for_stmt.num_body);
            for (int i = 0; i < n->u.for_stmt.num_body; i++) {
                ast_print_impl(n->u.for_stmt.body[i], depth + 2);
            }
            break;
        case AST_DO_LOOP:
            printf("[DoLoop] pre=%s %s%s\n",
                   n->u.do_loop.pre_cond ? "yes" : "no",
                   n->u.do_loop.cond ? (n->u.do_loop.is_until ? "UNTIL " : "WHILE ") : "infinite",
                   n->u.do_loop.cond ? "" : "");
            if (n->u.do_loop.cond) {
                print_indent(depth + 1); printf("cond:\n");
                ast_print_impl(n->u.do_loop.cond, depth + 2);
            }
            print_indent(depth + 1); printf("body (%d):\n",
                   n->u.do_loop.num_body);
            for (int i = 0; i < n->u.do_loop.num_body; i++) {
                ast_print_impl(n->u.do_loop.body[i], depth + 2);
            }
            break;
        case AST_FUNCTION_DEF:
            printf("[Function] %s(%d param%s)\n",
                   n->u.func_def.name ? n->u.func_def.name : "?",
                   n->u.func_def.num_params,
                   n->u.func_def.num_params == 1 ? "" : "s");
            for (int i = 0; i < n->u.func_def.num_params; i++) {
                print_indent(depth + 1);
                printf("param %d: %s%s\n", i, n->u.func_def.params[i],
                       n->u.func_def.param_is_array[i] ? " ()" : "");
            }
            print_indent(depth + 1); printf("body (%d):\n",
                   n->u.func_def.num_body);
            for (int i = 0; i < n->u.func_def.num_body; i++) {
                ast_print_impl(n->u.func_def.body[i], depth + 2);
            }
            break;
        case AST_RETURN:
            printf("[Return]\n");
            if (n->u.return_stmt.expr) {
                ast_print_impl(n->u.return_stmt.expr, depth + 1);
            }
            break;
        case AST_DIM:
            printf("[Dim] %s", n->u.dim.var_name ? n->u.dim.var_name : "?");
            if (n->u.dim.num_sizes > 0) {
                printf(" (%d dim%s:", n->u.dim.num_sizes,
                       n->u.dim.num_sizes == 1 ? "" : "s");
                for (int i = 0; i < n->u.dim.num_sizes; i++) {
                    printf(" ");
                    /* Print size inline — small expressions only */
                    if (n->u.dim.sizes[i]->type == AST_INT_LIT) {
                        printf("%ld", n->u.dim.sizes[i]->u.ival);
                    } else {
                        printf("?");
                    }
                }
                printf(")");
            }
            printf("\n");
            break;
        case AST_ROOM:
            printf("[Room] \"%s\"\n",
                   n->u.room.name ? n->u.room.name : "?");
            print_indent(depth + 1); printf("init (%d):\n",
                   n->u.room.num_init);
            for (int i = 0; i < n->u.room.num_init; i++) {
                ast_print_impl(n->u.room.init_body[i], depth + 2);
            }
            print_indent(depth + 1); printf("update (%d):\n",
                   n->u.room.num_update);
            for (int i = 0; i < n->u.room.num_update; i++) {
                ast_print_impl(n->u.room.update_body[i], depth + 2);
            }
            break;
        case AST_GOTOROOM:
            printf("[GotoRoom] \"%s\"\n",
                   n->u.gotoroom.name ? n->u.gotoroom.name : "?");
            break;
        case AST_IMPORT:
            printf("[Import] \"%s\"\n",
                   n->u.import.filename ? n->u.import.filename : "?");
            break;
        case AST_EXPR_STMT:
            printf("[ExprStmt]\n");
            ast_print_impl(n->u.assign.target, depth + 1);
            break;
    }
}

void ast_print(const AstNode *node) {
    ast_print_impl(node, 0);
}
