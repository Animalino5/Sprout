/*
 * ast.h — Abstract Syntax Tree node types for Sprout
 *
 * Every node is heap-allocated. Use ast_free() to recursively free a tree.
 * Use ast_print() to dump a tree as indented text (for debugging).
 */
#ifndef NB_AST_H
#define NB_AST_H

#include "token.h"

typedef struct AstNode AstNode;

/* ── Branch helper (used by IF) ───────────────────────────────────── */

typedef struct AstBranch {
    AstNode *cond;       /* NULL for ELSE branch */
    AstNode **body;
    int      num_body;
} AstBranch;

/* ── Node types ───────────────────────────────────────────────────── */

typedef enum {
    /* Top level */
    AST_PROGRAM,

    /* Expressions */
    AST_INT_LIT,
    AST_FLOAT_LIT,
    AST_STRING_LIT,
    AST_IDENT,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_CALL,            /* name(args) or name arg1, arg2 */
    AST_INDEX,           /* arr(i) used as L-value */
    AST_MEMBER,          /* module.var */

    /* Statements */
    AST_PRINT,
    AST_ASSIGN,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_DO_LOOP,
    AST_FUNCTION_DEF,
    AST_RETURN,
    AST_DIM,
    AST_ROOM,
    AST_GOTOROOM,
    AST_IMPORT,
    AST_EXPR_STMT,       /* bare call used as statement */
} AstType;

/* ── Node struct ──────────────────────────────────────────────────── */

struct AstNode {
    AstType type;
    int     line;
    int     col;

    union {
        /* PROGRAM — list of top-level statements */
        struct {
            AstNode **stmts;
            int       num_stmts;
        } program;

        /* INT_LIT */
        long ival;

        /* FLOAT_LIT */
        double fval;

        /* STRING_LIT, IDENT — text is heap-allocated */
        char *text;

        /* BINARY_OP, UNARY_OP */
        struct {
            TokenType op;
            AstNode *left;
            AstNode *right;
        } binary;
        struct {
            TokenType op;
            AstNode *operand;
        } unary;

        /* CALL — function/sub call */
        struct {
            char    *name;       /* heap-allocated */
            AstNode **args;
            int      num_args;
        } call;

        /* INDEX — array index on LHS of assignment */
        struct {
            char    *name;       /* heap-allocated */
            AstNode **indices;
            int      num_indices;
        } index;

        /* MEMBER — module.var */
        struct {
            char *module;        /* heap-allocated */
            char *member;        /* heap-allocated */
        } member;

        /* PRINT — list of expressions with separators */
        struct {
            AstNode **parts;
            char     *seps;      /* sep before each part: ';' or ',' or ' ' (single) */
            int       num_parts;
        } print;

        /* ASSIGN — target = value */
        struct {
            AstNode *target;     /* IDENT, INDEX, or MEMBER */
            AstNode *value;
        } assign;

        /* IF — list of branches (THEN, ELSEIF..., [ELSE]) */
        struct {
            AstBranch *branches;
            int        num_branches;
        } if_stmt;

        /* WHILE */
        struct {
            AstNode  *cond;
            AstNode **body;
            int       num_body;
        } while_stmt;

        /* FOR */
        struct {
            char    *var_name;   /* heap-allocated */
            AstNode *start;
            AstNode *end;
            AstNode *step;       /* NULL if no STEP */
            AstNode **body;
            int       num_body;
        } for_stmt;

        /* DO/LOOP — pre or post condition */
        struct {
            int       pre_cond;  /* 1 = DO WHILE/UNTIL, 0 = DO ... LOOP WHILE/UNTIL */
            int       is_until;  /* 1 = UNTIL, 0 = WHILE */
            AstNode  *cond;      /* NULL = infinite loop */
            AstNode **body;
            int       num_body;
        } do_loop;

        /* FUNCTION_DEF */
        struct {
            char    *name;       /* heap-allocated */
            char   **params;     /* array of heap-allocated strings */
            int     *param_is_array;  /* 1 if param is an array (long*), 0 if scalar */
            int      num_params;
            AstNode **body;
            int       num_body;
        } func_def;

        /* RETURN — expr may be NULL for bare RETURN */
        struct {
            AstNode *expr;
        } return_stmt;

        /* DIM — array declaration */
        struct {
            char    *var_name;   /* heap-allocated */
            AstNode **sizes;     /* dimension sizes; NULL/0 for scalar DIM */
            int      num_sizes;
        } dim;

        /* ROOM */
        struct {
            char    *name;       /* heap-allocated */
            AstNode **init_body;
            int       num_init;
            AstNode **update_body;
            int       num_update;
        } room;

        /* GOTOROOM */
        struct {
            char *name;          /* heap-allocated */
        } gotoroom;

        /* IMPORT */
        struct {
            char *filename;      /* heap-allocated */
        } import;
    } u;
};

/* ── Constructors ─────────────────────────────────────────────────── */

AstNode *ast_new_program(void);
AstNode *ast_new_int(long val, int line, int col);
AstNode *ast_new_float(double val, int line, int col);
AstNode *ast_new_string(const char *text, int line, int col);
AstNode *ast_new_ident(const char *text, int line, int col);
AstNode *ast_new_binary(TokenType op, AstNode *left, AstNode *right,
                        int line, int col);
AstNode *ast_new_unary(TokenType op, AstNode *operand, int line, int col);
AstNode *ast_new_call(const char *name, AstNode **args, int num_args,
                      int line, int col);
AstNode *ast_new_index(const char *name, AstNode **indices, int num_indices,
                       int line, int col);
AstNode *ast_new_member(const char *module, const char *member,
                        int line, int col);
AstNode *ast_new_print(AstNode **parts, char *seps, int num_parts,
                       int line, int col);
AstNode *ast_new_assign(AstNode *target, AstNode *value, int line, int col);
AstNode *ast_new_if(AstBranch *branches, int num_branches, int line, int col);
AstNode *ast_new_while(AstNode *cond, AstNode **body, int num_body,
                       int line, int col);
AstNode *ast_new_for(const char *var_name, AstNode *start, AstNode *end,
                     AstNode *step, AstNode **body, int num_body,
                     int line, int col);
AstNode *ast_new_do_loop(int pre_cond, int is_until, AstNode *cond,
                         AstNode **body, int num_body, int line, int col);
AstNode *ast_new_func(const char *name, char **params, int *param_is_array,
                      int num_params,
                      AstNode **body, int num_body, int line, int col);
AstNode *ast_new_return(AstNode *expr, int line, int col);
AstNode *ast_new_dim(const char *var_name, AstNode **sizes, int num_sizes,
                     int line, int col);
AstNode *ast_new_room(const char *name, AstNode **init_body, int num_init,
                      AstNode **update_body, int num_update, int line, int col);
AstNode *ast_new_gotoroom(const char *name, int line, int col);
AstNode *ast_new_import(const char *filename, int line, int col);
AstNode *ast_new_expr_stmt(AstNode *expr, int line, int col);

/* Append a statement to a program's top-level statement list. */
void ast_program_append(AstNode *prog, AstNode *stmt);

/* Recursively free a node and all its children. */
void ast_free(AstNode *node);

/* Pretty-print a tree to stdout (for debugging). */
void ast_print(const AstNode *node);

#endif /* NB_AST_H */
