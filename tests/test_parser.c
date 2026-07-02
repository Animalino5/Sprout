/*
 * test_parser.c — driver that reads a .bas file and prints its AST
 *
 * Usage: ./test_parser <file.bas>
 */
#include "../src/parser.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.bas>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
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

    Parser pr;
    parser_init(&pr, &lx);

    AstNode *prog = parser_parse_program(&pr);

    if (parser_had_error(&pr)) {
        fprintf(stderr, "\n*** Parser reported errors. AST may be partial. ***\n\n");
    }

    printf("──────────── AST for %s ────────────\n\n", argv[1]);
    ast_print(prog);
    printf("\n────────────────────────────────────\n");
    fprintf(stderr, "Parser %s.\n",
            parser_had_error(&pr) ? "FAILED" : "OK");

    ast_free(prog);
    parser_free(&pr);
    free(src);
    return parser_had_error(&pr) ? 1 : 0;
}
