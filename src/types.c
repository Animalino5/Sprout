/*
 * types.c — Sprout type system implementation
 */
#include "types.h"

const char *nb_type_c_name(NbType t) {
    switch (t) {
        case TY_UNKNOWN: return "long";
        case TY_INT:     return "long";
        case TY_FLOAT:   return "double";
        case TY_STRING:  return "char";  /* used as: char name[256] */
        case TY_IMAGE:   return "int";   /* handle */
        case TY_SOUND:   return "int";   /* handle */
        case TY_VOID:    return "void";
    }
    return "long";
}

const char *nb_type_name(NbType t) {
    switch (t) {
        case TY_UNKNOWN: return "UNKNOWN";
        case TY_INT:     return "INT";
        case TY_FLOAT:   return "FLOAT";
        case TY_STRING:  return "STRING";
        case TY_IMAGE:   return "IMAGE";
        case TY_SOUND:   return "SOUND";
        case TY_VOID:    return "VOID";
    }
    return "?";
}

int nb_type_compatible(NbType from, NbType to) {
    if (from == to) return 1;
    if (from == TY_INT && to == TY_FLOAT) return 1;  /* promotion */
    /* IMAGE and SOUND are both int handles, interconvertible */
    if ((from == TY_IMAGE && to == TY_SOUND) ||
        (from == TY_SOUND && to == TY_IMAGE)) return 1;
    return 0;
}

NbType nb_type_binary_op(NbType left, NbType right, int is_string_concat) {
    if (is_string_concat) {
        /* & or + with at least one string → string */
        if (left == TY_STRING || right == TY_STRING) return TY_STRING;
    }
    if (left == TY_FLOAT || right == TY_FLOAT) return TY_FLOAT;
    if (left == TY_INT && right == TY_INT) return TY_INT;
    return TY_UNKNOWN;
}
