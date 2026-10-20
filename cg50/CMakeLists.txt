# HP PPL for the fx-CG50: the PPL core (../src) in a gint add-in.  GPL-3.0-or-later.
#   cd cg50 && fxsdk build-cg      -> PPL.g3a
# (with the toolchain PATH of ~/main/casio/CLAUDE.md; Pillow only for make_icons.py)
cmake_minimum_required(VERSION 3.15)
project(PPL C)

include(GenerateG3A)
find_package(Gint 2.9 REQUIRED)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(CORE dec decfn decfmt value arith parse compile vm builtins bi_math bi_misc bi_list bi_mat
  bi_prob bi_cas hpprgm units gfx bi_gfx font/gfx_font)
list(TRANSFORM CORE PREPEND ${CORE_DIR}/)
list(TRANSFORM CORE APPEND .c)

set(SOURCES src/main.c src/mem.c src/clock.c src/files.c src/text.c src/font_data.c src/ui.c
  src/screen.c src/io.c src/term.c src/dialogs.c src/progs.c src/home.c src/host.c
  ${CORE})

add_executable(ppl ${SOURCES})
target_include_directories(ppl PRIVATE src ${CORE_DIR})
# every file allocates from the 6 MB DRAM arena (src/ppl_alloc.h, src/mem.c)
target_compile_options(ppl PRIVATE -O2 -g -std=gnu11 -Wall -Wno-unused-parameter
  -include ${CMAKE_CURRENT_SOURCE_DIR}/src/ppl_alloc.h)
set_source_files_properties(${CORE} PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare")
target_link_options(ppl PRIVATE -Wl,-Map=ppl.map)
target_link_libraries(ppl Gint::Gint)

generate_g3a(TARGET ppl OUTPUT "PPL.g3a" NAME "HP PPL"
  ICONS assets-cg/icon-uns.png assets-cg/icon-sel.png)
