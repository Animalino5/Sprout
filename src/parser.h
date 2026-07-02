/*
 * parser.h — Sprout recursive descent parser
 *
 * Consumes a token stream from a Lexer and produces an AST.
 *
 * Usage:
 *     Lexer lx; lexer_init(&lx, src, src_len);
 *     Parser pr; parser_init(&pr, &lx);
 *     AstNode *prog = parser_parse_program(&pr);
 *     if (parser_had_error(&pr)) { ... }
 *     ...
 *     ast_free(prog);
 */
#ifndef NB_PARSER_H
#define NB_PARSER_H

#include "ast.h"
#include "lexer.h"

typedef struct {
    Lexer  *lx;
    Token   cur;        /* current token (owned) */
    Token   next;       /* one-token lookahead (owned) */
    int     had_next;   /* 1 if next is valid */
    int     had_error;  /* 1 if any error was reported */
    int     panic;      /* 1 if in error-recovery mode */
} Parser;

/* Initialize a parser over the given lexer. */
void parser_init(Parser *pr, Lexer *lx);

/* Free parser-owned token buffers. Does NOT free the AST. */
void parser_free(Parser *pr);

/* Parse a complete program. Returns a Program AST node (always non-NULL,
 * even on error — partial trees are useful for IDE integration).
 * Caller must call ast_free() on the result.
 */
AstNode *parser_parse_program(Parser *pr);

/* True if any errors were reported during parsing. */
int parser_had_error(const Parser *pr);

#endif /* NB_PARSER_H */
