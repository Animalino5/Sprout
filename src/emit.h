/*
 * emit.h — C code emitter
 *
 * Walks a typed AST + symbol table and produces C99 source code that
 * links against libnewbasic.
 */
#ifndef NB_EMIT_H
#define NB_EMIT_H

#include <stdio.h>
#include "ast.h"
#include "typecheck.h"

/* Emit a complete .c file for the given program.
 * Returns 0 on success, non-zero on error.
 * Writes to the FILE* (caller opens/closes). */
int nb_emit_program(FILE *out, AstNode *prog, const NbTypeChecker *tc);

#endif /* NB_EMIT_H */
