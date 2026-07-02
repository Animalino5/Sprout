/*
 * token.h — token types and Token struct for Sprout lexer
 *
 * Part of the Sprout compiler. This file defines every token type the
 * lexer can produce and the runtime representation of a token.
 */
#ifndef NB_TOKEN_H
#define NB_TOKEN_H

#include <stddef.h>

typedef enum {
    /* ── Special ── */
    TOK_EOF,
    TOK_NEWLINE,

    /* ── Literals ── */
    TOK_INT,            /* 123, &HFF, &B1010, &O777 */
    TOK_FLOAT,          /* 1.5, 1e10, 3.14! */
    TOK_STRING,         /* "..." with text field set */

    /* ── Identifiers (may end in $ % & ! #) ── */
    TOK_IDENT,

    /* ── Keywords ── */
    TOK_KW_PRINT,
    TOK_KW_IF,
    TOK_KW_THEN,
    TOK_KW_ELSE,
    TOK_KW_ELSEIF,
    TOK_KW_END,         /* END — used alone or in "END IF", "END FUNCTION" etc. */
    TOK_KW_ENDIF,       /* ENDIF — single-token form */
    TOK_KW_WHILE,
    TOK_KW_WEND,
    TOK_KW_FOR,
    TOK_KW_TO,
    TOK_KW_STEP,
    TOK_KW_NEXT,
    TOK_KW_DO,
    TOK_KW_LOOP,
    TOK_KW_UNTIL,
    TOK_KW_FUNCTION,
    TOK_KW_RETURN,
    TOK_KW_DIM,
    TOK_KW_ROOM,
    TOK_KW_UPDATE,
    TOK_KW_GOTOROOM,
    TOK_KW_IMPORT,
    TOK_KW_TRUE,
    TOK_KW_FALSE,
    TOK_KW_AND,
    TOK_KW_OR,
    TOK_KW_NOT,
    TOK_KW_MOD,
    TOK_KW_REM,         /* REM comment keyword — rarely produced, lexer skips */

    /* ── Operators ── */
    TOK_PLUS,           /* + */
    TOK_MINUS,          /* - */
    TOK_STAR,           /* * */
    TOK_SLASH,          /* / */
    TOK_BACKSLASH,      /* \ (integer division) */
    TOK_CARET,          /* ^ (power) */
    TOK_EQ,             /* = (assignment OR equality — parser decides) */
    TOK_NE,             /* <> */
    TOK_LT,             /* < */
    TOK_GT,             /* > */
    TOK_LE,             /* <= */
    TOK_GE,             /* >= */

    /* ── Punctuation ── */
    TOK_LPAREN,         /* ( */
    TOK_RPAREN,         /* ) */
    TOK_COMMA,          /* , */
    TOK_SEMICOLON,      /* ; (used in PRINT a; b) */
    TOK_COLON,          /* : (statement separator) */
    TOK_DOT,            /* . (member access for module.var) */

    TOK_COUNT           /* sentinel — not a real token */
} TokenType;

/* Token value. Passed by value; text (if any) is heap-allocated and owned
 * by the token. Call token_free() when done.
 */
typedef struct {
    TokenType type;
    int line;           /* 1-indexed */
    int col;            /* 1-indexed */

    long ival;          /* valid when type == TOK_INT */
    double fval;        /* valid when type == TOK_FLOAT */

    char *text;         /* valid when type == TOK_STRING or TOK_IDENT */
    size_t text_len;    /* length of text, excluding null terminator */
} Token;

/* Human-readable name of a token type, e.g. "KW_PRINT" or "OP_PLUS". */
const char *token_type_name(TokenType t);

/* Free heap-allocated fields of a token. Safe to call on stack-allocated
 * tokens. Does not free the Token struct itself.
 */
void token_free(Token *t);

#endif /* NB_TOKEN_H */
