/*
 * lexer.h — Sprout lexer
 *
 * The lexer turns source text into a stream of Token structs. It is a
 * single-pass scanner: no preprocessor, no macro expansion.
 *
 * Usage:
 *     Lexer lx;
 *     lexer_init(&lx, src, src_len);
 *     for (;;) {
 *         Token t = lexer_next(&lx);
 *         ... use t ...
 *         token_free(&t);
 *         if (t.type == TOK_EOF) break;
 *     }
 *
 * Tokens are owned by the caller — call token_free() on each one before
 * it goes out of scope, or you'll leak the text buffer.
 */
#ifndef NB_LEXER_H
#define NB_LEXER_H

#include "token.h"

typedef struct {
    const char *src;        /* source text (not owned by lexer) */
    size_t      src_len;    /* byte length of src */
    size_t      pos;        /* current byte offset into src */
    int         line;       /* 1-indexed, advances on '\n' */
    int         col;        /* 1-indexed, resets to 1 on '\n' */
} Lexer;

/* Initialize a lexer over the given source text. Does not copy src —
 * caller must keep it alive for the lifetime of the lexer.
 */
void lexer_init(Lexer *lx, const char *src, size_t src_len);

/* Return the next token and advance the lexer position. The returned
 * token's text (if any) is heap-allocated; caller must call token_free().
 *
 * On error (e.g. unknown character), prints to stderr and returns
 * TOK_EOF. The lexer does not abort — caller decides whether to continue.
 */
Token lexer_next(Lexer *lx);

#endif /* NB_LEXER_H */
