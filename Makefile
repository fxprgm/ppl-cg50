# Host build of the PPL engine and tools.  `make sh4size` compiles the core
# for the fx-CG50's SH-4 (compile only) to track the 2 MiB add-in budget.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
SAN     ?=
CORE    = src/dec.c src/decfn.c src/decfmt.c src/value.c src/arith.c \
          src/parse.c src/compile.c src/vm.c src/builtins.c src/bi_math.c \
          src/bi_misc.c src/bi_list.c src/bi_mat.c src/bi_prob.c src/bi_cas.c src/hpprgm.c \
          src/gfx.c src/bi_gfx.c src/font/gfx_font.c
HDRS    = $(wildcard src/*.h)

SHCC    = $(HOME)/.local/bin/sh-elf-gcc
SHFLAGS = -std=c11 -Os -m4-nofpu -mb -ffunction-sections -fdata-sections -Wall

all: build/ppl build/test_dec

build/ppl: $(CORE) tools/ppl.c $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SAN) -o $@ $(CORE) tools/ppl.c

build/test_dec: src/dec.c src/decfn.c src/decfmt.c tests/test_dec.c src/dec.h src/decx.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SAN) -o $@ src/dec.c src/decfn.c src/decfmt.c tests/test_dec.c

asan:
	$(MAKE) clean
	$(MAKE) SAN="-fsanitize=address,undefined" CFLAGS="-std=c11 -Wall -Wextra -O1 -g"

test: all
	./build/test_dec
	./tests/run_tests.sh

conform: build/ppl
	./build/ppl conform tests/conformance/hpkit_cases.tsv > build/conform.txt
	python3 -I tools/conform_report.py build/conform.txt > tests/conformance/SCORE.md
	@tail -n 3 tests/conformance/SCORE.md

sh4size:
	@mkdir -p build/sh4
	@for f in $(CORE); do \
	  $(SHCC) $(SHFLAGS) -c $$f -o build/sh4/$$(basename $$f .c).o || exit 1; done
	@$(HOME)/.local/bin/sh-elf-size -t build/sh4/*.o

clean:
	rm -rf build

.PHONY: all asan test conform sh4size clean
