/* bc.h -- bytecode of compiled PPL.  Operands follow the opcode byte;
 * 16-bit operands are big-endian and read byte by byte (no alignment). */
#ifndef PPL_BC_H
#define PPL_BC_H

enum {
    BC_NOP = 0,
    BC_CONST,     /* u16 k                  push constant */
    BC_ZERO,      /*                        push 0 */
    BC_SINT,      /* i8                     push small integer */
    BC_LLOAD,     /* u8 slot */
    BC_LSTORE,    /* u8 slot                pop into local */
    BC_GLOAD,     /* u16 idx                program variable */
    BC_GSTORE,    /* u16 idx */
    BC_SLOAD,     /* u8 id                  system variable (A-Z, L0...) */
    BC_SSTORE,    /* u8 id */
    BC_NLOAD,     /* u16 name               resolved at run time */
    BC_NSTORE,    /* u16 name */
    BC_LSETIDX,   /* u8 slot, u8 argc       stack: value, idx1..idxn */
    BC_GSETIDX,   /* u16 idx, u8 argc */
    BC_SSETIDX,   /* u8 id, u8 argc */
    BC_NSETIDX,   /* u16 name, u8 argc */
    BC_INDEX,     /* u8 argc                stack: container, idx1..idxn */
    BC_CALLB,     /* u16 builtin, u8 argc */
    BC_CALLF,     /* u16 function, u8 argc  function of this program */
    BC_CALLN,     /* u16 name, u8 argc      resolved at run time */
    BC_BIN,       /* u8 OP_* */
    BC_CMPKA,     /* u8 OP_*                acc a b -> (acc AND a op b) b */
    BC_NEG, BC_NOT, BC_FACT, BC_SQ, BC_CUBE, BC_INV, BC_SQRT,
    BC_JMP,       /* i16 */
    BC_JF,        /* i16                    pop, jump if false */
    BC_POP, BC_DUP, BC_SETLAST, BC_RET, BC_RETLAST,
    BC_MKLIST,    /* u16 n */
    BC_MKVEC,     /* u16 n */
    BC_MKMAT,     /* u16 rows, u16 cols */
    BC_MKCPLX,
    BC_TRY,       /* i16 handler */
    BC_ENDTRY,
    BC_THUNK,     /* u16 len                push a thunk of the next len bytes, skip them */
    BC_SUBRET,
    BC_VREF,      /* u8 kind, u16 idx */
    BC_LINE,      /* u16 line */
    BC_KILL,
    BC_SC,        /* u8 OP_AND/OP_OR, i16   AND/OR left operand decides: replace it with the result, jump */
    BC_OLOAD,     /* u8 slot                a local of the function running EXPR (fn->outer_frame) */
    BC_OSTORE,    /* u8 slot */
    BC_COUNT
};

/* variable reference kinds (BC_VREF, value.ib of T_VREF) */
enum { VK_LOCAL = 1, VK_PROG, VK_SYS, VK_NAME,
       VK_SLOT };   /* u.vp: a variable's value slot, made at run time (_FNROOTX) */

#endif
