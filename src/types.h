/*
 * types.h — Sprout type system
 *
 * Simple types only — no records, no generics. The type checker assigns
 * one of these to every variable and every expression.
 */
#ifndef NB_TYPES_H
#define NB_TYPES_H

typedef enum {
    TY_UNKNOWN = 0,  /* not yet inferred */
    TY_INT,          /* 32-bit signed integer (long) */
    TY_FLOAT,        /* 64-bit floating point (double) */
    TY_STRING,       /* fixed-size char[256] buffer */
    TY_IMAGE,        /* image handle (int) */
    TY_SOUND,        /* sound handle (int) */
    TY_VOID,         /* function returns nothing */
} NbType;

/* C type name for a variable of this type, e.g. "long", "double", "char[256]". */
const char *nb_type_c_name(NbType t);

/* Short name for error messages, e.g. "INT", "STRING". */
const char *nb_type_name(NbType t);

/* True if a value of type `from` can be implicitly converted to `to`.
 * INT → FLOAT is allowed (promotion). FLOAT → INT is NOT (lossy).
 * Same-type is always allowed. */
int nb_type_compatible(NbType from, NbType to);

/* Result type of a binary operation. Returns TY_UNKNOWN if the
 * combination is invalid. */
NbType nb_type_binary_op(NbType left, NbType right, int is_string_concat);

#endif /* NB_TYPES_H */
