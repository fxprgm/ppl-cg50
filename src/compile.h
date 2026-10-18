/* compile.h -- AST to bytecode. */
#ifndef PPL_COMPILE_H
#define PPL_COMPILE_H

#include "parse.h"

int compile_function(ppl_vm *V, pprog *prog, pfunc *fn, char **params, int np, node *body,
                     char *msg, size_t msgn, int *errline);
int compile_inits(ppl_vm *V, pprog *prog, pfunc *fn, topdecl *d, int nd,
                  char *msg, size_t msgn, int *errline);
int sysvar_id(const char *name);
const builtin *bi_lookup_any(const char *name);

#endif
