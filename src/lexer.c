/*
 * lexer.c — Sprout lexer implementation
 *
 * Single-pass scanner. Handles:
 *   - Whitespace (space, tab, CR) — skipped
 *   - Line continuation: "_\n" — skipped, treated as same logical line
 *   - Comments: ' ... (single line), REM ... (single line), /' ... '/ (block, nestable)
 *   - Numbers: decimal, hex (&H), binary (&B), octal (&O), float, exponent, type suffix
 *   - Strings: "..." with "" for embedded quote
 *   - Identifiers: [A-Za-z_][A-Za-z0-9_]* optionally followed by $ % & ! #
 *   - Keywords: case-insensitive match against the keyword table
 *   - Operators and punctuation: + - * / \ ^ = <> < > <= >= ( ) , ; : .
 *
 * Newlines are significant (TOK_NEWLINE). Colons separate statements on
 * the same line.
 */
#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Keyword table ────────────────────────────────────────────────── */

static const struct {
    const char *kw;
    TokenType   tt;
} KEYWORDS[] = {
    {"PRINT",    TOK_KW_PRINT},
    {"IF",       TOK_KW_IF},
    {"THEN",     TOK_KW_THEN},
    {"ELSE",     TOK_KW_ELSE},
    {"ELSEIF",   TOK_KW_ELSEIF},
    {"END",      TOK_KW_END},
    {"ENDIF",    TOK_KW_ENDIF},
    {"WHILE",    TOK_KW_WHILE},
    {"WEND",     TOK_KW_WEND},
    {"FOR",      TOK_KW_FOR},
    {"TO",       TOK_KW_TO},
    {"STEP",     TOK_KW_STEP},
    {"NEXT",     TOK_KW_NEXT},
    {"DO",       TOK_KW_DO},
    {"LOOP",     TOK_KW_LOOP},
    {"UNTIL",    TOK_KW_UNTIL},
    {"FUNCTION", TOK_KW_FUNCTION},
    {"RETURN",   TOK_KW_RETURN},
    {"DIM",      TOK_KW_DIM},
    {"ROOM",     TOK_KW_ROOM},
    {"GOTOROOM", TOK_KW_GOTOROOM},
    {"IMPORT",   TOK_KW_IMPORT},
    {"TRUE",     TOK_KW_TRUE},
    {"FALSE",    TOK_KW_FALSE},
    {"AND",      TOK_KW_AND},
    {"OR",       TOK_KW_OR},
    {"NOT",      TOK_KW_NOT},
    {"MOD",      TOK_KW_MOD},
    {"REM",      TOK_KW_REM},
};
static const int NUM_KEYWORDS = (int)(sizeof(KEYWORDS) / sizeof(KEYWORDS[0]));

/* ── Lexer state helpers ──────────────────────────────────────────── */

void lexer_init(Lexer *lx, const char *src, size_t src_len) {
    lx->src     = src;
    lx->src_len = src_len;
    lx->pos     = 0;
    lx->line    = 1;
    lx->col     = 1;
}

static char peek(const Lexer *lx) {
    if (lx->pos >= lx->src_len) return '\0';
    return lx->src[lx->pos];
}

static char peek_at(const Lexer *lx, size_t offset) {
    if (lx->pos + offset >= lx->src_len) return '\0';
    return lx->src[lx->pos + offset];
}

static char advance(Lexer *lx) {
    if (lx->pos >= lx->src_len) return '\0';
    char c = lx->src[lx->pos++];
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    return c;
}

static int is_ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
static int is_ident_cont(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}
static int is_digit(char c) {
    return c >= '0' && c <= '9';
}
static int is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static int is_type_suffix(char c) {
    return c == '$' || c == '%' || c == '&' || c == '!' || c == '#';
}

static Token make_token(TokenType type, int line, int col) {
    Token t;
    t.type     = type;
    t.line     = line;
    t.col      = col;
    t.ival     = 0;
    t.fval     = 0.0;
    t.text     = NULL;
    t.text_len = 0;
    return t;
}

/* ── Whitespace and comment skipping ──────────────────────────────── */

static void skip_ws_and_comments(Lexer *lx) {
    for (;;) {
        char c = peek(lx);

        /* Spaces, tabs, CR — skip */
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lx);
            continue;
        }

        /* Line continuation "_\n" — skip both, treat as one logical line */
        if (c == '_' && peek_at(lx, 1) == '\n') {
            advance(lx);  /* _ */
            advance(lx);  /* \n */
            continue;
        }

        /* Single-quote comment — to end of line */
        if (c == '\'') {
            while (peek(lx) != '\0' && peek(lx) != '\n') {
                advance(lx);
            }
            continue;
        }

        /* REM comment — must be a word boundary (not "Remember" etc.) */
        if ((c == 'R' || c == 'r') &&
            (toupper((unsigned char)peek_at(lx, 1)) == 'E') &&
            (toupper((unsigned char)peek_at(lx, 2)) == 'M') &&
            !is_ident_cont(peek_at(lx, 3))) {
            while (peek(lx) != '\0' && peek(lx) != '\n') {
                advance(lx);
            }
            continue;
        }

        /* Block comment /' ... '/ — nestable */
        if (c == '/' && peek_at(lx, 1) == '\'') {
            int depth = 1;
            advance(lx);  /* / */
            advance(lx);  /* ' */
            while (peek(lx) != '\0' && depth > 0) {
                if (peek(lx) == '/' && peek_at(lx, 1) == '\'') {
                    depth++;
                    advance(lx); advance(lx);
                } else if (peek(lx) == '\'' && peek_at(lx, 1) == '/') {
                    depth--;
                    advance(lx); advance(lx);
                } else {
                    advance(lx);
                }
            }
            continue;
        }

        /* Anything else — stop. */
        break;
    }
}

/* ── Number lexing ────────────────────────────────────────────────── */

static Token lex_number(Lexer *lx) {
    int line = lx->line, col = lx->col;
    int is_float = 0;

    /* Hex: &H... */
    if (peek(lx) == '&' &&
        (peek_at(lx, 1) == 'H' || peek_at(lx, 1) == 'h')) {
        advance(lx);  /* & */
        advance(lx);  /* H */
        size_t start = lx->pos;
        while (is_hex_digit(peek(lx))) advance(lx);
        size_t len = lx->pos - start;
        char buf[32];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, lx->src + start, len);
        buf[len] = '\0';
        Token t = make_token(TOK_INT, line, col);
        t.ival = (long)strtoll(buf, NULL, 16);
        return t;
    }

    /* Binary: &B... */
    if (peek(lx) == '&' &&
        (peek_at(lx, 1) == 'B' || peek_at(lx, 1) == 'b')) {
        advance(lx);  /* & */
        advance(lx);  /* B */
        size_t start = lx->pos;
        while (peek(lx) == '0' || peek(lx) == '1') advance(lx);
        size_t len = lx->pos - start;
        char buf[64];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, lx->src + start, len);
        buf[len] = '\0';
        Token t = make_token(TOK_INT, line, col);
        t.ival = (long)strtoll(buf, NULL, 2);
        return t;
    }

    /* Octal: &O... */
    if (peek(lx) == '&' &&
        (peek_at(lx, 1) == 'O' || peek_at(lx, 1) == 'o')) {
        advance(lx);  /* & */
        advance(lx);  /* O */
        size_t start = lx->pos;
        while (peek(lx) >= '0' && peek(lx) <= '7') advance(lx);
        size_t len = lx->pos - start;
        char buf[32];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, lx->src + start, len);
        buf[len] = '\0';
        Token t = make_token(TOK_INT, line, col);
        t.ival = (long)strtoll(buf, NULL, 8);
        return t;
    }

    /* Decimal — possibly float */
    size_t start = lx->pos;
    while (is_digit(peek(lx))) advance(lx);

    if (peek(lx) == '.' && is_digit(peek_at(lx, 1))) {
        is_float = 1;
        advance(lx);  /* . */
        while (is_digit(peek(lx))) advance(lx);
    }

    /* Exponent: e[+|-]digits */
    if (peek(lx) == 'e' || peek(lx) == 'E') {
        is_float = 1;
        advance(lx);
        if (peek(lx) == '+' || peek(lx) == '-') advance(lx);
        while (is_digit(peek(lx))) advance(lx);
    }

    /* Trailing type suffix on number literal */
    char c = peek(lx);
    if (c == '!' || c == '#') { is_float = 1; advance(lx); }
    else if (c == '%' || c == '&') { advance(lx); }

    size_t len = lx->pos - start;
    char buf[64];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, lx->src + start, len);
    buf[len] = '\0';

    Token t = make_token(is_float ? TOK_FLOAT : TOK_INT, line, col);
    if (is_float) {
        t.fval = strtod(buf, NULL);
    } else {
        t.ival = (long)strtoll(buf, NULL, 10);
    }
    return t;
}

/* ── String lexing ────────────────────────────────────────────────── */

static Token lex_string(Lexer *lx) {
    int line = lx->line, col = lx->col;
    advance(lx);  /* opening " */

    size_t cap = 64;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { return make_token(TOK_EOF, line, col); }

    for (;;) {
        char c = peek(lx);
        if (c == '\0') {
            fprintf(stderr, "Lexer %d:%d: unterminated string\n", line, col);
            break;
        }
        if (c == '\n') {
            fprintf(stderr, "Lexer %d:%d: unterminated string (newline)\n",
                    line, col);
            break;
        }
        if (c == '"') {
            advance(lx);
            /* Doubled "" = literal quote */
            if (peek(lx) == '"') {
                if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
                buf[len++] = '"';
                advance(lx);
                continue;
            }
            break;
        }
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = c;
        advance(lx);
    }

    buf[len] = '\0';
    Token t = make_token(TOK_STRING, line, col);
    t.text     = buf;
    t.text_len = len;
    return t;
}

/* ── Identifier (and keyword) lexing ──────────────────────────────── */

static TokenType match_keyword(const char *upper, size_t len) {
    for (int i = 0; i < NUM_KEYWORDS; i++) {
        if (strlen(KEYWORDS[i].kw) == len &&
            memcmp(KEYWORDS[i].kw, upper, len) == 0) {
            return KEYWORDS[i].tt;
        }
    }
    return TOK_IDENT;
}

static Token lex_identifier(Lexer *lx) {
    int line = lx->line, col = lx->col;
    size_t start = lx->pos;

    while (is_ident_cont(peek(lx))) advance(lx);

    /* Optional type suffix — part of identifier name */
    int has_suffix = 0;
    char c = peek(lx);
    if (is_type_suffix(c)) {
        advance(lx);
        has_suffix = 1;
    }

    size_t len = lx->pos - start;
    char *buf = (char *)malloc(len + 1);
    if (!buf) { return make_token(TOK_EOF, line, col); }
    memcpy(buf, lx->src + start, len);
    buf[len] = '\0';

    /* Try keyword match (only for identifiers without type suffix) */
    if (!has_suffix) {
        char upper[32];
        if (len < sizeof(upper)) {
            for (size_t i = 0; i < len; i++) {
                upper[i] = (char)toupper((unsigned char)buf[i]);
            }
            TokenType kw = match_keyword(upper, len);
            if (kw != TOK_IDENT) {
                free(buf);
                return make_token(kw, line, col);
            }
        }
    }

    Token t = make_token(TOK_IDENT, line, col);
    t.text     = buf;
    t.text_len = len;
    return t;
}

/* ── Main entry: lexer_next ───────────────────────────────────────── */

Token lexer_next(Lexer *lx) {
    skip_ws_and_comments(lx);

    if (lx->pos >= lx->src_len) {
        return make_token(TOK_EOF, lx->line, lx->col);
    }

    int line = lx->line, col = lx->col;
    char c = peek(lx);

    /* Newline */
    if (c == '\n') {
        advance(lx);
        return make_token(TOK_NEWLINE, line, col);
    }

    /* Number (decimal, hex, binary, octal) */
    if (is_digit(c) ||
        (c == '.' && is_digit(peek_at(lx, 1))) ||
        (c == '&' && (peek_at(lx, 1) == 'H' || peek_at(lx, 1) == 'h' ||
                      peek_at(lx, 1) == 'B' || peek_at(lx, 1) == 'b' ||
                      peek_at(lx, 1) == 'O' || peek_at(lx, 1) == 'o'))) {
        return lex_number(lx);
    }

    /* String */
    if (c == '"') {
        return lex_string(lx);
    }

    /* Identifier or keyword */
    if (is_ident_start(c)) {
        return lex_identifier(lx);
    }

    /* Operators and punctuation */
    advance(lx);
    switch (c) {
        case '+': return make_token(TOK_PLUS,       line, col);
        case '-': return make_token(TOK_MINUS,      line, col);
        case '*': return make_token(TOK_STAR,       line, col);
        case '/': return make_token(TOK_SLASH,      line, col);
        case '\\': return make_token(TOK_BACKSLASH, line, col);
        case '^': return make_token(TOK_CARET,      line, col);
        case '(': return make_token(TOK_LPAREN,     line, col);
        case ')': return make_token(TOK_RPAREN,     line, col);
        case ',': return make_token(TOK_COMMA,      line, col);
        case ';': return make_token(TOK_SEMICOLON,  line, col);
        case ':': return make_token(TOK_COLON,      line, col);
        case '.': return make_token(TOK_DOT,        line, col);
        case '=': return make_token(TOK_EQ,         line, col);
        case '<':
            if (peek(lx) == '=') { advance(lx); return make_token(TOK_LE, line, col); }
            if (peek(lx) == '>') { advance(lx); return make_token(TOK_NE, line, col); }
            return make_token(TOK_LT, line, col);
        case '>':
            if (peek(lx) == '=') { advance(lx); return make_token(TOK_GE, line, col); }
            return make_token(TOK_GT, line, col);
        default:
            fprintf(stderr,
                    "Lexer %d:%d: unknown character '%c' (0x%02x)\n",
                    line, col, c, (unsigned char)c);
            return make_token(TOK_EOF, line, col);
    }
}
