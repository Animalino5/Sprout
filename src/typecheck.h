/*
 * typecheck.h — Type inference pass
 *
 * Walks the AST, assigns types to every variable based on first
 * assignment, and checks that operations are type-correct. Reports
 * errors but does not abort — the emitter can still produce C from
 * a partially-typed AST (using TY_UNKNOWN → long fallback).
 */
#ifndef NB_TYPECHECK_H
#define NB_TYPECHECK_H

#include "ast.h"
#include "types.h"

/* Symbol table entry. Maps a variable name to its inferred type. */
typedef struct {
    char   *name;       /* heap-allocated */
    NbType  type;
    int     is_array;
    int     array_size; /* valid if is_array */
    int     line;       /* line where first declared/assigned */
} NbSymbol;

/* Type-checker state. Owns the symbol table. */
typedef struct {
    NbSymbol *symbols;
    int       num_symbols;
    int       cap_symbols;
    int       had_error;
} NbTypeChecker;

/* Initialize a type checker. */
void nb_tc_init(NbTypeChecker *tc);

/* Free type-checker-owned memory. */
void nb_tc_free(NbTypeChecker *tc);

/* Type-check an entire program. Fills the symbol table. */
void nb_tc_check_program(NbTypeChecker *tc, AstNode *prog);

/* Look up a symbol by name. Returns NULL if not found. */
const NbSymbol *nb_tc_lookup(const NbTypeChecker *tc, const char *name);

/* Look up the return type of a builtin function by name. */
NbType nb_tc_builtin_return_type(const char *name);

#endif /* NB_TYPECHECK_H */
