/*
 * token.c — Token helpers
 */
#include "token.h"
#include <stdlib.h>

const char *token_type_name(TokenType t) {
    switch (t) {
        case TOK_EOF:           return "EOF";
        case TOK_NEWLINE:       return "NEWLINE";

        case TOK_INT:           return "INT";
        case TOK_FLOAT:         return "FLOAT";
        case TOK_STRING:        return "STRING";
        case TOK_IDENT:         return "IDENT";

        case TOK_KW_PRINT:      return "KW_PRINT";
        case TOK_KW_IF:         return "KW_IF";
        case TOK_KW_THEN:       return "KW_THEN";
        case TOK_KW_ELSE:       return "KW_ELSE";
        case TOK_KW_ELSEIF:     return "KW_ELSEIF";
        case TOK_KW_END:        return "KW_END";
        case TOK_KW_ENDIF:      return "KW_ENDIF";
        case TOK_KW_WHILE:      return "KW_WHILE";
        case TOK_KW_WEND:       return "KW_WEND";
        case TOK_KW_FOR:        return "KW_FOR";
        case TOK_KW_TO:         return "KW_TO";
        case TOK_KW_STEP:       return "KW_STEP";
        case TOK_KW_NEXT:       return "KW_NEXT";
        case TOK_KW_DO:         return "KW_DO";
        case TOK_KW_LOOP:       return "KW_LOOP";
        case TOK_KW_UNTIL:      return "KW_UNTIL";
        case TOK_KW_FUNCTION:   return "KW_FUNCTION";
        case TOK_KW_RETURN:     return "KW_RETURN";
        case TOK_KW_DIM:        return "KW_DIM";
        case TOK_KW_ROOM:       return "KW_ROOM";
        case TOK_KW_UPDATE:     return "KW_UPDATE";
        case TOK_KW_GOTOROOM:   return "KW_GOTOROOM";
        case TOK_KW_IMPORT:     return "KW_IMPORT";
        case TOK_KW_TRUE:       return "KW_TRUE";
        case TOK_KW_FALSE:      return "KW_FALSE";
        case TOK_KW_AND:        return "KW_AND";
        case TOK_KW_OR:         return "KW_OR";
        case TOK_KW_NOT:        return "KW_NOT";
        case TOK_KW_MOD:        return "KW_MOD";
        case TOK_KW_REM:        return "KW_REM";

        case TOK_PLUS:          return "OP_PLUS";
        case TOK_MINUS:         return "OP_MINUS";
        case TOK_STAR:          return "OP_STAR";
        case TOK_SLASH:         return "OP_SLASH";
        case TOK_BACKSLASH:     return "OP_BACKSLASH";
        case TOK_CARET:         return "OP_CARET";
        case TOK_EQ:            return "OP_EQ";
        case TOK_NE:            return "OP_NE";
        case TOK_LT:            return "OP_LT";
        case TOK_GT:            return "OP_GT";
        case TOK_LE:            return "OP_LE";
        case TOK_GE:            return "OP_GE";

        case TOK_LPAREN:        return "LPAREN";
        case TOK_RPAREN:        return "RPAREN";
        case TOK_COMMA:         return "COMMA";
        case TOK_SEMICOLON:     return "SEMICOLON";
        case TOK_COLON:         return "COLON";
        case TOK_DOT:           return "DOT";

        case TOK_COUNT:         return "COUNT";
    }
    return "???";
}

void token_free(Token *t) {
    if (t->text) {
        free(t->text);
        t->text = NULL;
        t->text_len = 0;
    }
}
