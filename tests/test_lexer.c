/*
 * test_lexer.c — driver that reads a .bas file and prints its token stream
 *
 * Usage: ./test_lexer <file.bas>
 *
 * Each line of output is one token, formatted as:
 *     LINE:COL  TYPE  extra
 * where extra is ival / fval / text as appropriate.
 *
 * Used to verify the lexer works correctly on real BASIC source.
 */
#include "../src/lexer.h"

#include <stdio.h>
#include <stdlib.h>

static void print_token(const Token *t) {
    printf("%5d:%-4d  %-14s", t->line, t->col, token_type_name(t->type));
    switch (t->type) {
        case TOK_INT:
            printf("  ival=%ld", t->ival);
            break;
        case TOK_FLOAT:
            printf("  fval=%g", t->fval);
            break;
        case TOK_STRING:
            printf("  str=\"%s\"", t->text ? t->text : "");
            break;
        case TOK_IDENT:
            printf("  ident=\"%s\"", t->text ? t->text : "");
            break;
        default:
            break;
    }
    printf("\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.bas>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) { perror("ftell"); fclose(f); return 1; }
    fseek(f, 0, SEEK_SET);

    char *src = (char *)malloc((size_t)size + 1);
    if (!src) { fprintf(stderr, "oom\n"); fclose(f); return 1; }
    if (fread(src, 1, (size_t)size, f) != (size_t)size) {
        perror("fread"); free(src); fclose(f); return 1;
    }
    src[size] = '\0';
    fclose(f);

    Lexer lx;
    lexer_init(&lx, src, (size_t)size);

    int token_count = 0;
    for (;;) {
        Token t = lexer_next(&lx);
        print_token(&t);
        token_count++;
        TokenType tt = t.type;
        token_free(&t);
        if (tt == TOK_EOF) break;
    }

    fprintf(stderr, "\n%d tokens emitted.\n", token_count);
    free(src);
    return 0;
}
