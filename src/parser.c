/*
 * parser.c — recursive descent parser for Sprout
 *
 * Grammar (informal):
 *
 *   program      := stmt*
 *   stmt         := print_stmt | if_stmt | while_stmt | for_stmt
 *                 | do_loop_stmt | func_def | return_stmt | dim_stmt
 *                 | room_def | gotoroom_stmt | import_stmt
 *                 | assign_or_call
 *
 *   print_stmt   := PRINT print_part (sep print_part)*
 *   print_part   := expr
 *   sep          := ";" | ","
 *
 *   if_stmt      := IF expr THEN newline stmt* (ELSEIF expr THEN newline stmt*)*
 *                   (ELSE newline stmt*)? END IF | ENDIF
 *   while_stmt   := WHILE expr newline stmt* WEND
 *   for_stmt     := FOR IDENT "=" expr TO expr (STEP expr)? newline stmt* NEXT (IDENT)?
 *   do_loop_stmt := DO (WHILE|UNTIL expr)? newline stmt* LOOP (WHILE|UNTIL expr)?
 *   func_def     := FUNCTION IDENT "(" params? ")" newline stmt* END FUNCTION
 *   params       := IDENT ("," IDENT)*
 *   return_stmt  := RETURN expr?
 *   dim_stmt     := DIM IDENT ("(" sizes ")")?
 *   sizes        := expr ("," expr)*
 *   room_def     := ROOM STRING newline stmt* (UPDATE newline stmt* END UPDATE)?
 *                   END ROOM
 *   gotoroom_stmt := GOTOROOM STRING
 *   import_stmt  := IMPORT STRING
 *
 *   assign_or_call := IDENT ("=" expr | "(" args ")" "=" expr | "(" args ")" | .* )
 *
 * Expressions (precedence low → high):
 *   or_expr      := and_expr (OR and_expr)*
 *   and_expr     := not_expr (AND not_expr)*
 *   not_expr     := NOT not_expr | compare_expr
 *   compare_expr := add_expr (comp_op add_expr)?    (non-associative)
 *   add_expr     := mul_expr (("+"|"-") mul_expr)*
 *   mul_expr     := pow_expr (("*"|"/"|"\"|"MOD") pow_expr)*
 *   pow_expr     := unary ("^" pow_expr)?            (right-assoc)
 *   unary        := ("-"|"NOT") unary | postfix
 *   postfix      := primary ("(" args ")")?          (call or index)
 *   primary      := INT | FLOAT | STRING | TRUE | FALSE
 *                 | IDENT ("." IDENT)?                (var or member)
 *                 | "(" expr ")"
 */
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  /* strcasecmp */
#include <ctype.h>

/* ── Parser state helpers ─────────────────────────────────────────── */

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void advance(Parser *pr);
static void consume_newlines(Parser *pr);

void parser_init(Parser *pr, Lexer *lx) {
    pr->lx        = lx;
    pr->had_error = 0;
    pr->panic     = 0;
    pr->had_next  = 0;
    /* Prime the pump: read first token into cur */
    pr->cur = lexer_next(pr->lx);
    pr->next.type = TOK_EOF;
}

void parser_free(Parser *pr) {
    token_free(&pr->cur);
    if (pr->had_next) token_free(&pr->next);
}

static void advance(Parser *pr) {
    /* Free old current token */
    token_free(&pr->cur);
    /* If we have a lookahead, use it */
    if (pr->had_next) {
        pr->cur = pr->next;
        pr->had_next = 0;
    } else {
        pr->cur = lexer_next(pr->lx);
    }
}

static int check(Parser *pr, TokenType type) {
    return pr->cur.type == type;
}

static int match(Parser *pr, TokenType type) {
    if (pr->cur.type == type) {
        advance(pr);
        return 1;
    }
    return 0;
}

static void error_at(Parser *pr, const Token *t, const char *msg) {
    if (pr->panic) return;
    fprintf(stderr, "Parse error %d:%d: %s (got %s)\n",
            t->line, t->col, msg, token_type_name(t->type));
    pr->had_error = 1;
    pr->panic = 1;
}

static void error(Parser *pr, const char *msg) {
    error_at(pr, &pr->cur, msg);
}

static int expect(Parser *pr, TokenType type, const char *msg) {
    if (pr->cur.type == type) {
        advance(pr);
        return 1;
    }
    error(pr, msg);
    return 0;
}

/* Skip newlines, used to enter multi-line blocks */
static void consume_newlines(Parser *pr) {
    while (check(pr, TOK_NEWLINE)) advance(pr);
}

/* ── Statement list parsing ───────────────────────────────────────── */

/* Forward declarations */
static AstNode *parse_statement(Parser *pr);
static AstNode *parse_expression(Parser *pr);

/* Parse statements until we hit a terminator keyword (END, WEND, NEXT,
 * LOOP, ELSE, ELSEIF, UPDATE, EOF). Returns a malloc'd array of AstNode*
 * and writes the count to *count_out. Caller owns the array (but not the
 * nodes — they're owned by the array).
 */
static AstNode **parse_statement_list(Parser *pr, int *count_out,
                                       const TokenType *terminators,
                                       int num_terminators) {
    AstNode **stmts = NULL;
    int count = 0;
    int cap = 0;

    consume_newlines(pr);
    for (;;) {
        /* End of input? */
        if (check(pr, TOK_EOF)) break;
        /* Newline? Skip. */
        if (check(pr, TOK_NEWLINE)) { advance(pr); continue; }
        /* Terminator? */
        int is_term = 0;
        for (int i = 0; i < num_terminators; i++) {
            if (check(pr, terminators[i])) { is_term = 1; break; }
        }
        if (is_term) break;

        /* Parse a statement */
        AstNode *s = parse_statement(pr);
        if (s) {
            if (count >= cap) {
                cap = cap ? cap * 2 : 8;
                stmts = (AstNode **)realloc(stmts, sizeof(AstNode *) * cap);
            }
            stmts[count++] = s;
        }

        /* Recovery: if we panicked, skip until newline or EOF */
        if (pr->panic) {
            pr->panic = 0;
            while (!check(pr, TOK_NEWLINE) && !check(pr, TOK_EOF)) {
                advance(pr);
            }
            if (check(pr, TOK_NEWLINE)) advance(pr);
        } else {
            /* Statements end with newline or colon (or EOF) */
            if (check(pr, TOK_COLON)) {
                advance(pr);
            } else if (check(pr, TOK_NEWLINE)) {
                advance(pr);
            } else if (!check(pr, TOK_EOF)) {
                /* Some keyword or token we didn't expect. Error + skip. */
                error(pr, "expected newline or ':' after statement");
            }
        }
    }

    *count_out = count;
    return stmts;
}

/* ── Expression parsing (precedence climbing) ─────────────────────── */

static AstNode *parse_or(Parser *pr);
static AstNode *parse_and(Parser *pr);
static AstNode *parse_not(Parser *pr);
static AstNode *parse_compare(Parser *pr);
static AstNode *parse_add(Parser *pr);
static AstNode *parse_mul(Parser *pr);
static AstNode *parse_pow(Parser *pr);
static AstNode *parse_unary(Parser *pr);
static AstNode *parse_postfix(Parser *pr);
static AstNode *parse_primary(Parser *pr);

static AstNode *parse_expression(Parser *pr) {
    return parse_or(pr);
}

static AstNode *parse_or(Parser *pr) {
    AstNode *left = parse_and(pr);
    while (check(pr, TOK_KW_OR)) {
        Token op = pr->cur;
        advance(pr);
        AstNode *right = parse_and(pr);
        left = ast_new_binary(op.type, left, right, op.line, op.col);
    }
    return left;
}

static AstNode *parse_and(Parser *pr) {
    AstNode *left = parse_not(pr);
    while (check(pr, TOK_KW_AND)) {
        Token op = pr->cur;
        advance(pr);
        AstNode *right = parse_not(pr);
        left = ast_new_binary(op.type, left, right, op.line, op.col);
    }
    return left;
}

static AstNode *parse_not(Parser *pr) {
    if (check(pr, TOK_KW_NOT)) {
        Token op = pr->cur;
        advance(pr);
        AstNode *operand = parse_not(pr);
        return ast_new_unary(op.type, operand, op.line, op.col);
    }
    return parse_compare(pr);
}

static AstNode *parse_compare(Parser *pr) {
    AstNode *left = parse_add(pr);
    /* Non-associative: only one comparison allowed */
    TokenType t = pr->cur.type;
    if (t == TOK_EQ || t == TOK_NE || t == TOK_LT ||
        t == TOK_GT || t == TOK_LE || t == TOK_GE) {
        Token op = pr->cur;
        advance(pr);
        AstNode *right = parse_add(pr);
        left = ast_new_binary(op.type, left, right, op.line, op.col);
    }
    return left;
}

static AstNode *parse_add(Parser *pr) {
    AstNode *left = parse_mul(pr);
    while (check(pr, TOK_PLUS) || check(pr, TOK_MINUS)) {
        Token op = pr->cur;
        advance(pr);
        AstNode *right = parse_mul(pr);
        left = ast_new_binary(op.type, left, right, op.line, op.col);
    }
    return left;
}

static AstNode *parse_mul(Parser *pr) {
    AstNode *left = parse_pow(pr);
    for (;;) {
        TokenType t = pr->cur.type;
        if (t == TOK_STAR || t == TOK_SLASH || t == TOK_BACKSLASH ||
            t == TOK_KW_MOD) {
            Token op = pr->cur;
            advance(pr);
            AstNode *right = parse_pow(pr);
            left = ast_new_binary(op.type, left, right, op.line, op.col);
        } else {
            break;
        }
    }
    return left;
}

static AstNode *parse_pow(Parser *pr) {
    AstNode *left = parse_unary(pr);
    if (check(pr, TOK_CARET)) {
        Token op = pr->cur;
        advance(pr);
        /* Right-associative: recurse on parse_pow */
        AstNode *right = parse_pow(pr);
        left = ast_new_binary(op.type, left, right, op.line, op.col);
    }
    return left;
}

static AstNode *parse_unary(Parser *pr) {
    if (check(pr, TOK_MINUS)) {
        Token op = pr->cur;
        advance(pr);
        AstNode *operand = parse_unary(pr);
        return ast_new_unary(op.type, operand, op.line, op.col);
    }
    return parse_postfix(pr);
}

static AstNode *parse_postfix(Parser *pr) {
    AstNode *expr = parse_primary(pr);

    /* Member access: module.var */
    while (check(pr, TOK_DOT)) {
        advance(pr);
        if (!check(pr, TOK_IDENT)) {
            error(pr, "expected member name after '.'");
            break;
        }
        Token member_tok = pr->cur;
        char *member_name = dup_str(member_tok.text);
        advance(pr);

        if (expr->type == AST_IDENT) {
            /* Replace IDENT with MEMBER — steal the module name */
            char *mod_name = expr->u.text;
            AstNode *m = ast_new_member(mod_name, member_name,
                                        expr->line, expr->col);
            free(member_name);
            expr->u.text = NULL;
            ast_free(expr);
            expr = m;
        } else {
            error(pr, "'.' only valid after identifier");
            free(member_name);
        }
    }

    /* Function call: (args) */
    while (check(pr, TOK_LPAREN) && expr->type == AST_IDENT) {
        Token call_tok = pr->cur;
        advance(pr);  /* ( */

        AstNode **args = NULL;
        int num_args = 0;
        int cap = 0;
        if (!check(pr, TOK_RPAREN)) {
            do {
                AstNode *a = parse_expression(pr);
                if (num_args >= cap) {
                    cap = cap ? cap * 2 : 4;
                    args = (AstNode **)realloc(args, sizeof(AstNode *) * cap);
                }
                args[num_args++] = a;
            } while (match(pr, TOK_COMMA));
        }
        expect(pr, TOK_RPAREN, "expected ')' after arguments");

        /* Replace IDENT with CALL — steal the name */
        char *name = expr->u.text;
        AstNode *call = ast_new_call(name, args, num_args,
                                     expr->line, expr->col);
        free(args);
        expr->u.text = NULL;
        ast_free(expr);
        expr = call;
        (void)call_tok;
    }

    return expr;
}

static AstNode *parse_primary(Parser *pr) {
    Token t = pr->cur;
    switch (t.type) {
        case TOK_INT: {
            long v = t.ival;
            advance(pr);
            return ast_new_int(v, t.line, t.col);
        }
        case TOK_FLOAT: {
            double v = t.fval;
            advance(pr);
            return ast_new_float(v, t.line, t.col);
        }
        case TOK_STRING: {
            AstNode *n = ast_new_string(t.text, t.line, t.col);
            advance(pr);
            return n;
        }
        case TOK_KW_TRUE: {
            advance(pr);
            return ast_new_int(1, t.line, t.col);
        }
        case TOK_KW_FALSE: {
            advance(pr);
            return ast_new_int(0, t.line, t.col);
        }
        case TOK_IDENT: {
            AstNode *n = ast_new_ident(t.text, t.line, t.col);
            advance(pr);
            return n;
        }
        case TOK_LPAREN: {
            advance(pr);
            AstNode *e = parse_expression(pr);
            expect(pr, TOK_RPAREN, "expected ')'");
            return e;
        }
        default:
            error(pr, "expected expression");
            advance(pr);
            return ast_new_int(0, t.line, t.col);  /* placeholder */
    }
}

/* ── Statement parsing ────────────────────────────────────────────── */

/* Forward declarations for statement parsers */
static AstNode *parse_print(Parser *pr);
static AstNode *parse_if(Parser *pr);
static AstNode *parse_while(Parser *pr);
static AstNode *parse_for(Parser *pr);
static AstNode *parse_do_loop(Parser *pr);
static AstNode *parse_function(Parser *pr);
static AstNode *parse_return(Parser *pr);
static AstNode *parse_dim(Parser *pr);
static AstNode *parse_room(Parser *pr);
static AstNode *parse_gotoroom(Parser *pr);
static AstNode *parse_import(Parser *pr);
static AstNode *parse_assign_or_call(Parser *pr);

static AstNode *parse_statement(Parser *pr) {
    Token t = pr->cur;
    switch (t.type) {
        case TOK_KW_PRINT:    return parse_print(pr);
        case TOK_KW_IF:       return parse_if(pr);
        case TOK_KW_WHILE:    return parse_while(pr);
        case TOK_KW_FOR:      return parse_for(pr);
        case TOK_KW_DO:       return parse_do_loop(pr);
        case TOK_KW_FUNCTION: return parse_function(pr);
        case TOK_KW_RETURN:   return parse_return(pr);
        case TOK_KW_DIM:      return parse_dim(pr);
        case TOK_KW_ROOM:     return parse_room(pr);
        case TOK_KW_GOTOROOM: return parse_gotoroom(pr);
        case TOK_KW_IMPORT:   return parse_import(pr);
        case TOK_KW_END:
            /* END alone (not END IF, END FUNCTION, etc.) = terminate program.
             * Block terminators (END IF, END FUNCTION, END ROOM, END UPDATE)
             * are handled by parse_statement_list and should never reach here.
             * If we do see END here, it's a standalone END statement. */
            advance(pr);
            return ast_new_expr_stmt(
                ast_new_call("END", NULL, 0, t.line, t.col),
                t.line, t.col);
        case TOK_IDENT:       return parse_assign_or_call(pr);
        case TOK_NEWLINE:
            /* Blank line — skip */
            advance(pr);
            return NULL;
        default:
            error(pr, "expected statement");
            advance(pr);
            return NULL;
    }
}

/* PRINT expr (sep expr)* */
static AstNode *parse_print(Parser *pr) {
    Token t = pr->cur;
    advance(pr);  /* PRINT */

    AstNode **parts = NULL;
    char *seps = NULL;
    int num_parts = 0;
    int cap = 0;

    /* If PRINT is followed by newline, it's a bare PRINT (prints nothing) */
    if (check(pr, TOK_NEWLINE) || check(pr, TOK_COLON) || check(pr, TOK_EOF)) {
        /* Empty PRINT — just emit a newline. No parts. */
        return ast_new_print(NULL, NULL, 0, t.line, t.col);
    }

    /* First part, no separator */
    if (num_parts >= cap) {
        cap = 4;
        parts = (AstNode **)malloc(sizeof(AstNode *) * cap);
        seps = (char *)malloc(cap);
    }
    parts[num_parts] = parse_expression(pr);
    seps[num_parts] = ' ';  /* no separator for first part */
    num_parts++;

    /* Subsequent parts separated by ; or , */
    while (check(pr, TOK_SEMICOLON) || check(pr, TOK_COMMA)) {
        char sep = (pr->cur.type == TOK_SEMICOLON) ? ';' : ',';
        advance(pr);
        if (num_parts >= cap) {
            cap *= 2;
            parts = (AstNode **)realloc(parts, sizeof(AstNode *) * cap);
            seps = (char *)realloc(seps, cap);
        }
        parts[num_parts] = parse_expression(pr);
        seps[num_parts] = sep;
        num_parts++;
    }

    return ast_new_print(parts, seps, num_parts, t.line, t.col);
}

/* IF expr THEN newline stmt* (ELSEIF expr THEN newline stmt*)* (ELSE newline stmt*)? END IF */
static AstNode *parse_if(Parser *pr) {
    Token t = pr->cur;
    advance(pr);  /* IF */

    AstNode *cond = parse_expression(pr);

    expect(pr, TOK_KW_THEN, "expected THEN after IF condition");
    /* THEN can be followed by newline (block IF) or a statement (one-line IF) */
    if (!check(pr, TOK_NEWLINE)) {
        /* One-line IF: THEN statement [ELSE statement]? */
        /* Build a single-branch IF with one statement in the body */
        AstBranch *branches = (AstBranch *)calloc(1, sizeof(AstBranch));
        branches[0].cond = cond;
        branches[0].body = NULL;
        branches[0].num_body = 0;

        AstNode *s = parse_statement(pr);
        if (s) {
            branches[0].body = (AstNode **)malloc(sizeof(AstNode *));
            branches[0].body[0] = s;
            branches[0].num_body = 1;
        }

        /* Optional ELSE on same line */
        if (match(pr, TOK_KW_ELSE)) {
            AstBranch *new_branches = (AstBranch *)calloc(2, sizeof(AstBranch));
            new_branches[0] = branches[0];
            new_branches[1].cond = NULL;
            new_branches[1].body = NULL;
            new_branches[1].num_body = 0;
            AstNode *else_stmt = parse_statement(pr);
            if (else_stmt) {
                new_branches[1].body = (AstNode **)malloc(sizeof(AstNode *));
                new_branches[1].body[0] = else_stmt;
                new_branches[1].num_body = 1;
            }
            free(branches);
            branches = new_branches;
            return ast_new_if(branches, 2, t.line, t.col);
        }
        return ast_new_if(branches, 1, t.line, t.col);
    }

    /* Block IF — multi-line */
    advance(pr);  /* newline after THEN */
    consume_newlines(pr);

    /* Terminators for the THEN body — include ENDIF (single-token form) */
    static const TokenType terms[] = {TOK_KW_ELSEIF, TOK_KW_ELSE, TOK_KW_END, TOK_KW_ENDIF};
    int num_branches = 0;
    int cap_branches = 2;
    AstBranch *branches = (AstBranch *)calloc(cap_branches, sizeof(AstBranch));

    /* THEN branch */
    branches[0].cond = cond;
    branches[0].body = parse_statement_list(pr, &branches[0].num_body,
                                             terms, 4);
    num_branches = 1;

    /* ELSEIF branches */
    while (check(pr, TOK_KW_ELSEIF)) {
        advance(pr);  /* ELSEIF */
        AstNode *elseif_cond = parse_expression(pr);
        expect(pr, TOK_KW_THEN, "expected THEN after ELSEIF condition");
        consume_newlines(pr);

        if (num_branches >= cap_branches) {
            cap_branches *= 2;
            branches = (AstBranch *)realloc(branches,
                                            sizeof(AstBranch) * cap_branches);
        }
        branches[num_branches].cond = elseif_cond;
        branches[num_branches].body = parse_statement_list(
            pr, &branches[num_branches].num_body, terms, 4);
        num_branches++;
    }

    /* ELSE branch */
    if (check(pr, TOK_KW_ELSE)) {
        advance(pr);  /* ELSE */
        consume_newlines(pr);

        if (num_branches >= cap_branches) {
            cap_branches *= 2;
            branches = (AstBranch *)realloc(branches,
                                            sizeof(AstBranch) * cap_branches);
        }
        branches[num_branches].cond = NULL;  /* ELSE */
        branches[num_branches].body = parse_statement_list(
            pr, &branches[num_branches].num_body, terms, 4);
        num_branches++;
    }

    /* Accept either END IF (two tokens) or ENDIF (single token) */
    if (check(pr, TOK_KW_ENDIF)) {
        advance(pr);
    } else {
        expect(pr, TOK_KW_END, "expected END IF or ENDIF");
        expect(pr, TOK_KW_IF, "expected IF after END");
    }

    return ast_new_if(branches, num_branches, t.line, t.col);
}

/* WHILE expr newline stmt* WEND */
static AstNode *parse_while(Parser *pr) {
    Token t = pr->cur;
    advance(pr);  /* WHILE */

    AstNode *cond = parse_expression(pr);
    consume_newlines(pr);

    static const TokenType terms[] = {TOK_KW_WEND};
    int num_body = 0;
    AstNode **body = parse_statement_list(pr, &num_body, terms, 1);

    expect(pr, TOK_KW_WEND, "expected WEND");

    return ast_new_while(cond, body, num_body, t.line, t.col);
}

/* FOR IDENT "=" expr TO expr (STEP expr)? newline stmt* NEXT (IDENT)? */
static AstNode *parse_for(Parser *pr) {
    Token t = pr->cur;
    advance(pr);  /* FOR */

    if (!check(pr, TOK_IDENT)) {
        error(pr, "expected variable name after FOR");
        return NULL;
    }
    Token var_tok = pr->cur;
    char *var_name = dup_str(var_tok.text);
    advance(pr);

    expect(pr, TOK_EQ, "expected '=' after FOR variable");
    AstNode *start = parse_expression(pr);
    expect(pr, TOK_KW_TO, "expected TO after FOR start value");
    AstNode *end = parse_expression(pr);

    AstNode *step = NULL;
    if (match(pr, TOK_KW_STEP)) {
        step = parse_expression(pr);
    }

    consume_newlines(pr);

    static const TokenType terms[] = {TOK_KW_NEXT};
    int num_body = 0;
    AstNode **body = parse_statement_list(pr, &num_body, terms, 1);

    expect(pr, TOK_KW_NEXT, "expected NEXT");
    /* Optional variable name after NEXT — ignore if present */
    if (check(pr, TOK_IDENT)) advance(pr);

    AstNode *result = ast_new_for(var_name, start, end, step, body, num_body,
                                  t.line, t.col);
    free(var_name);
    return result;
}

/* DO (WHILE|UNTIL expr)? newline stmt* LOOP (WHILE|UNTIL expr)? */
static AstNode *parse_do_loop(Parser *pr) {
    Token t = pr->cur;
    advance(pr);  /* DO */

    int pre_cond = 0;
    int is_until = 0;
    AstNode *cond = NULL;

    if (check(pr, TOK_KW_WHILE) || check(pr, TOK_KW_UNTIL)) {
        pre_cond = 1;
        is_until = (pr->cur.type == TOK_KW_UNTIL);
        advance(pr);
        cond = parse_expression(pr);
    }

    consume_newlines(pr);

    static const TokenType terms[] = {TOK_KW_LOOP};
    int num_body = 0;
    AstNode **body = parse_statement_list(pr, &num_body, terms, 1);

    expect(pr, TOK_KW_LOOP, "expected LOOP");

    /* Optional post-condition */
    if (!pre_cond && (check(pr, TOK_KW_WHILE) || check(pr, TOK_KW_UNTIL))) {
        is_until = (pr->cur.type == TOK_KW_UNTIL);
        advance(pr);
        cond = parse_expression(pr);
    }

    return ast_new_do_loop(pre_cond, is_until, cond, body, num_body,
                           t.line, t.col);
}

/* FUNCTION IDENT "(" params? ")" newline stmt* END FUNCTION */
static AstNode *parse_function(Parser *pr) {
    Token t = pr->cur;
    advance(pr);  /* FUNCTION */

    if (!check(pr, TOK_IDENT)) {
        error(pr, "expected function name after FUNCTION");
        return NULL;
    }
    Token name_tok = pr->cur;
    char *func_name = dup_str(name_tok.text);
    advance(pr);

    expect(pr, TOK_LPAREN, "expected '(' after function name");

    char **params = NULL;
    int  *param_is_array = NULL;
    int num_params = 0;
    int cap_params = 0;
    if (!check(pr, TOK_RPAREN)) {
        do {
            if (!check(pr, TOK_IDENT)) {
                error(pr, "expected parameter name");
                break;
            }
            if (num_params >= cap_params) {
                cap_params = cap_params ? cap_params * 2 : 4;
                params = (char **)realloc(params, sizeof(char *) * cap_params);
                param_is_array = (int *)realloc(param_is_array, sizeof(int) * cap_params);
            }
            params[num_params] = dup_str(pr->cur.text);
            param_is_array[num_params] = 0;  /* default: scalar */
            advance(pr);

            /* Check for () after param name → array parameter */
            if (check(pr, TOK_LPAREN)) {
                advance(pr);  /* ( */
                /* Optional size inside — we ignore it, just note it's an array */
                if (!check(pr, TOK_RPAREN)) {
                    /* Skip any expression inside the parens (size, or empty) */
                    int paren_depth = 1;
                    while (paren_depth > 0 && !check(pr, TOK_EOF)) {
                        if (check(pr, TOK_LPAREN)) paren_depth++;
                        else if (check(pr, TOK_RPAREN)) paren_depth--;
                        if (paren_depth > 0) advance(pr);
                    }
                }
                expect(pr, TOK_RPAREN, "expected ')' after array parameter");
                param_is_array[num_params] = 1;
            }

            num_params++;
        } while (match(pr, TOK_COMMA));
    }
    expect(pr, TOK_RPAREN, "expected ')' after parameter list");

    consume_newlines(pr);

    static const TokenType terms[] = {TOK_KW_END};
    int num_body = 0;
    AstNode **body = parse_statement_list(pr, &num_body, terms, 1);

    expect(pr, TOK_KW_END, "expected END FUNCTION");
    expect(pr, TOK_KW_FUNCTION, "expected FUNCTION after END");

    AstNode *func = ast_new_func(func_name, params, param_is_array, num_params,
                                  body, num_body, t.line, t.col);
    free(func_name);
    /* Free the params array (the strings were dup'd in the constructor) */
    for (int i = 0; i < num_params; i++) free(params[i]);
    free(params);
    free(param_is_array);
    return func;
}

/* RETURN expr? */
static AstNode *parse_return(Parser *pr) {
    Token t = pr->cur;
    advance(pr);  /* RETURN */

    /* Bare RETURN if newline/colon/EOF follows */
    if (check(pr, TOK_NEWLINE) || check(pr, TOK_COLON) || check(pr, TOK_EOF)) {
        return ast_new_return(NULL, t.line, t.col);
    }

    AstNode *expr = parse_expression(pr);
    return ast_new_return(expr, t.line, t.col);
}

/* DIM IDENT ("(" sizes ")")? */
static AstNode *parse_dim(Parser *pr) {
    Token t = pr->cur;
    advance(pr);  /* DIM */

    if (!check(pr, TOK_IDENT)) {
        error(pr, "expected variable name after DIM");
        return NULL;
    }
    Token var_tok = pr->cur;
    char *var_name = dup_str(var_tok.text);
    advance(pr);

    AstNode **sizes = NULL;
    int num_sizes = 0;
    int cap_sizes = 0;
    if (match(pr, TOK_LPAREN)) {
        if (!check(pr, TOK_RPAREN)) {
            do {
                AstNode *s = parse_expression(pr);
                if (num_sizes >= cap_sizes) {
                    cap_sizes = cap_sizes ? cap_sizes * 2 : 4;
                    sizes = (AstNode **)realloc(sizes, sizeof(AstNode *) * cap_sizes);
                }
                sizes[num_sizes++] = s;
            } while (match(pr, TOK_COMMA));
        }
        expect(pr, TOK_RPAREN, "expected ')' after array sizes");
    }

    AstNode *result = ast_new_dim(var_name, sizes, num_sizes, t.line, t.col);
    free(var_name);
    return result;
}

/* ROOM STRING newline stmt* (UPDATE newline stmt* END UPDATE)? END ROOM */
static AstNode *parse_room(Parser *pr) {
    Token t = pr->cur;
    advance(pr);  /* ROOM */

    if (!check(pr, TOK_STRING)) {
        error(pr, "expected room name string after ROOM");
        return NULL;
    }
    Token name_tok = pr->cur;
    char *room_name = dup_str(name_tok.text);
    advance(pr);

    consume_newlines(pr);

    /* Parse init body until UPDATE (as IDENT) or END */
    /* UPDATE is a contextual keyword — it's an IDENT with text "UPDATE" */
    int num_init = 0;
    AstNode **init_body = NULL;
    int num_update = 0;
    AstNode **update_body = NULL;

    /* Parse init body — stop at END or IDENT "UPDATE" */
    for (;;) {
        consume_newlines(pr);
        if (check(pr, TOK_EOF)) break;
        if (check(pr, TOK_KW_END)) break;
        /* Check for contextual UPDATE keyword */
        if (check(pr, TOK_IDENT) && pr->cur.text &&
            strcasecmp(pr->cur.text, "UPDATE") == 0) {
            /* Look ahead: is this UPDATE followed by a newline (block marker)
             * or by ( / args (function call)? */
            /* For simplicity: if we're inside a ROOM and see UPDATE with no
             * parens, treat it as the block marker. */
            break;
        }
        if (check(pr, TOK_NEWLINE)) { advance(pr); continue; }

        AstNode *s = parse_statement(pr);
        if (s) {
            init_body = (AstNode **)realloc(init_body,
                sizeof(AstNode *) * (num_init + 1));
            init_body[num_init++] = s;
        }
        if (pr->panic) {
            pr->panic = 0;
            while (!check(pr, TOK_NEWLINE) && !check(pr, TOK_EOF)) advance(pr);
            if (check(pr, TOK_NEWLINE)) advance(pr);
        } else {
            if (check(pr, TOK_COLON)) { advance(pr); }
            else if (check(pr, TOK_NEWLINE)) { advance(pr); }
            else if (!check(pr, TOK_EOF)) { error(pr, "expected newline"); }
        }
    }

    /* Check for UPDATE block */
    if (check(pr, TOK_IDENT) && pr->cur.text &&
        strcasecmp(pr->cur.text, "UPDATE") == 0) {
        advance(pr);  /* UPDATE */
        consume_newlines(pr);

        /* Parse update body until END */
        for (;;) {
            consume_newlines(pr);
            if (check(pr, TOK_EOF)) break;
            if (check(pr, TOK_KW_END)) break;
            if (check(pr, TOK_NEWLINE)) { advance(pr); continue; }

            AstNode *s = parse_statement(pr);
            if (s) {
                update_body = (AstNode **)realloc(update_body,
                    sizeof(AstNode *) * (num_update + 1));
                update_body[num_update++] = s;
            }
            if (pr->panic) {
                pr->panic = 0;
                while (!check(pr, TOK_NEWLINE) && !check(pr, TOK_EOF)) advance(pr);
                if (check(pr, TOK_NEWLINE)) advance(pr);
            } else {
                if (check(pr, TOK_COLON)) { advance(pr); }
                else if (check(pr, TOK_NEWLINE)) { advance(pr); }
                else if (!check(pr, TOK_EOF)) { error(pr, "expected newline"); }
            }
        }

        expect(pr, TOK_KW_END, "expected END UPDATE");
        /* Expect IDENT 'UPDATE' */
        if (!check(pr, TOK_IDENT) || !pr->cur.text ||
            strcasecmp(pr->cur.text, "UPDATE") != 0) {
            error(pr, "expected UPDATE after END");
        } else {
            advance(pr);
        }
    }

    consume_newlines(pr);
    expect(pr, TOK_KW_END, "expected END ROOM");
    expect(pr, TOK_KW_ROOM, "expected ROOM after END");

    AstNode *result = ast_new_room(room_name, init_body, num_init,
                                   update_body, num_update, t.line, t.col);
    free(room_name);
    return result;
}

/* GOTOROOM STRING */
static AstNode *parse_gotoroom(Parser *pr) {
    Token t = pr->cur;
    advance(pr);  /* GOTOROOM */

    if (!check(pr, TOK_STRING)) {
        error(pr, "expected room name string after GOTOROOM");
        return NULL;
    }
    Token name_tok = pr->cur;
    char *name = dup_str(name_tok.text);
    advance(pr);

    AstNode *result = ast_new_gotoroom(name, t.line, t.col);
    free(name);
    return result;
}

/* IMPORT STRING */
static AstNode *parse_import(Parser *pr) {
    Token t = pr->cur;
    advance(pr);  /* IMPORT */

    if (!check(pr, TOK_STRING)) {
        error(pr, "expected filename string after IMPORT");
        return NULL;
    }
    Token file_tok = pr->cur;
    char *filename = dup_str(file_tok.text);
    advance(pr);

    AstNode *result = ast_new_import(filename, t.line, t.col);
    free(filename);
    return result;
}

/* Assign-or-call: this is the tricky one. After an IDENT, we can have:
 *
 *   x = ...              → simple assignment
 *   x(i) = ...           → array assignment (INDEX on LHS)
 *   x.y = ...            → member assignment
 *   x(args)              → call statement (no =)
 *   x                    → bare identifier statement (call without parens)
 *   x arg1, arg2, ...    → "command call" — collect args until EOL
 */
static AstNode *parse_assign_or_call(Parser *pr) {
    Token t = pr->cur;
    char *name = dup_str(t.text);  /* save name before advance */
    advance(pr);  /* consume IDENT */

    /* Member access: x.y */
    if (check(pr, TOK_DOT)) {
        advance(pr);
        if (!check(pr, TOK_IDENT)) {
            error(pr, "expected member name after '.'");
            free(name);
            return NULL;
        }
        Token member_tok = pr->cur;
        char *member_name = dup_str(member_tok.text);
        advance(pr);

        AstNode *target = ast_new_member(name, member_name, t.line, t.col);
        free(name);
        free(member_name);

        if (match(pr, TOK_EQ)) {
            AstNode *value = parse_expression(pr);
            return ast_new_assign(target, value, t.line, t.col);
        }
        /* Member access without = — error for now */
        error(pr, "expected '=' after member access");
        ast_free(target);
        return NULL;
    }

    /* Array index or function call: x(...) */
    if (check(pr, TOK_LPAREN)) {
        advance(pr);  /* ( */

        /* Parse args/indices */
        AstNode **args = NULL;
        int num_args = 0;
        int cap = 0;
        if (!check(pr, TOK_RPAREN)) {
            do {
                AstNode *a = parse_expression(pr);
                if (num_args >= cap) {
                    cap = cap ? cap * 2 : 4;
                    args = (AstNode **)realloc(args, sizeof(AstNode *) * cap);
                }
                args[num_args++] = a;
            } while (match(pr, TOK_COMMA));
        }
        expect(pr, TOK_RPAREN, "expected ')'");

        /* Is this an assignment? */
        if (match(pr, TOK_EQ)) {
            /* Array assignment: x(args) = value */
            AstNode *target = ast_new_index(name, args, num_args,
                                             t.line, t.col);
            free(args);
            free(name);
            AstNode *value = parse_expression(pr);
            return ast_new_assign(target, value, t.line, t.col);
        }
        /* Otherwise it's a function call statement */
        AstNode *call = ast_new_call(name, args, num_args, t.line, t.col);
        free(args);
        free(name);
        return ast_new_expr_stmt(call, t.line, t.col);
    }

    /* Simple assignment: x = value */
    if (match(pr, TOK_EQ)) {
        AstNode *target = ast_new_ident(name, t.line, t.col);
        free(name);
        AstNode *value = parse_expression(pr);
        return ast_new_assign(target, value, t.line, t.col);
    }

    /* Command call: x arg1, arg2, ...
     * Collect comma-separated expressions until end of statement.
     */
    AstNode **args = NULL;
    int num_args = 0;
    int cap = 0;

    /* If there's nothing else on the line, it's a zero-arg call */
    if (check(pr, TOK_NEWLINE) || check(pr, TOK_COLON) || check(pr, TOK_EOF)) {
        AstNode *call = ast_new_call(name, NULL, 0, t.line, t.col);
        free(name);
        return ast_new_expr_stmt(call, t.line, t.col);
    }

    /* Otherwise, parse comma-separated args */
    do {
        AstNode *a = parse_expression(pr);
        if (num_args >= cap) {
            cap = cap ? cap * 2 : 4;
            args = (AstNode **)realloc(args, sizeof(AstNode *) * cap);
        }
        args[num_args++] = a;
    } while (match(pr, TOK_COMMA));

    AstNode *call = ast_new_call(name, args, num_args, t.line, t.col);
    free(args);
    free(name);
    return ast_new_expr_stmt(call, t.line, t.col);
}

/* ── Top-level entry point ────────────────────────────────────────── */

AstNode *parser_parse_program(Parser *pr) {
    AstNode *prog = ast_new_program();

    static const TokenType terms[] = {TOK_EOF};
    int num_stmts = 0;
    AstNode **stmts = parse_statement_list(pr, &num_stmts, terms, 1);

    for (int i = 0; i < num_stmts; i++) {
        ast_program_append(prog, stmts[i]);
    }
    free(stmts);

    return prog;
}

int parser_had_error(const Parser *pr) {
    return pr->had_error;
}
