# PPL for the fx-CG50

An interpreter for HP Prime programs (HP PPL) meant to run as a gint add-in on the Casio
fx-CG50. This repository holds the language core: a compiler to bytecode, a virtual machine,
and a decimal number type that reproduces the Prime's 12-digit Home arithmetic. It is
developed and tested on a desktop first.

- Portable C11: no floating point, no endianness or alignment assumptions, 32-bit friendly
  (the target is an SH-4 without FPU).
- `make` builds `build/ppl`, the desktop runner:
  - `build/ppl run prog.hppl "F(3)"` loads a program and evaluates expressions;
  - `build/ppl eval "2^0.5"` evaluates on its own;
  - `--ascii` prints `-`, `E` and `i` instead of the Prime's glyphs.
- `make test` runs the decimal unit tests and the PPL regression programs in `tests/lang/`.
- `make conform` scores the engine against the answers recorded in
  [hp-prime-kit](https://github.com/JordiRigau/hp-prime-kit) (MIT). It needs `ref/hp-prime-kit`
  only to regenerate the table, with `python3 -I tools/mkconform.py ref/hp-prime-kit`.
- `make sh4size` compiles the core for the SH-4 and prints its code size.

Design notes, the number semantics with their sources, and the current state are in
[`notes/ppl.md`](notes/ppl.md).
