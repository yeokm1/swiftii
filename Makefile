# SwiftII top-level Makefile.
#
# Targets:
#   test       host unit tests under clang
#   sim        bytecode tests on the py65 6502 simulator
#   apple2     cross-compile via cc65 (produces SWIFTIIP.SYSTEM, II+ lite)
#   apple2-swiftsat  Saturn 128K extras (SWIFTSAT.SYSTEM)
#   apple2-swiftaux  //e aux extras (SWIFTAUX.SYSTEM)
#   apple2-all     lite + SWIFTSAT + SWIFTAUX
#   boot-launcher  cc65 + ca65 boot selector (produces build/boot_launcher/
#              SWIFTII; the .po installs it as SWIFTII.SYSTEM so ProDOS
#              auto-launches it — see docs/contributing/LESSONS.md 2026-05-25)
#   disks      build the 9-disk set: four REPL program disks,
#              four Family B compiler disks, and disk-data
#   run        launch the emulator on the II+ lite image (broadest compat)
#   run-sat/iie/aux  launch the Saturn / //e-lite / //e-aux image
#   run-aux    launch the emulator on the //e-aux image (SWIFTAUX)
#   run-configs print the full hardware test matrix (which target = which machine)
#   run-mari-* Mariani per-machine (mounts disk + prints the GUI setting to pick)
#   run-iz-*   izapple2 profiles: iip/sat/iie/iienh + edge cases
#              (embedded ROMs)
#   size       report binary sizes from the linker map
#   ci         clean + all tests + all ship builds + size + disks + README check
#   clean      nuke build/

BUILD          := build
HOST_DIR       := $(BUILD)/host
HOST_OBJ       := $(HOST_DIR)/obj
A2_DIR         := $(BUILD)/apple2
A2_OBJ         := $(A2_DIR)/obj
DISK_DIR       := $(BUILD)/disk
BOOT_LAUNCHER_DIR  := $(BUILD)/boot_launcher

# ---------------------------------------------------------------------------
# Tool auto-detection
#
# AppleCommander: default to the jar that setup.sh downloads into tools/.
# The user never needs to export APPLECOMMANDER_JAR manually.
#
# Java: Homebrew installs openjdk keg-only, so `java` isn't on PATH by
# default on a fresh machine. If java isn't visible, prepend the known
# Homebrew path. This makes `make disk` work in a fresh shell after
# setup.sh without any manual export.
# ---------------------------------------------------------------------------

APPLECOMMANDER_JAR ?= $(CURDIR)/tools/host/AppleCommander-ac.jar
export APPLECOMMANDER_JAR

# macOS ships a /usr/bin/java stub that prompts the user to install a
# JDK — so `command -v java` succeeds, but invoking it fails with
# "Unable to locate a Java Runtime". Prefer the Homebrew openjdk@21
# whenever it exists, so AppleCommander runs reliably regardless of
# whether the user added it to their shell PATH. See docs/contributing/LESSONS.md
# section "Java is in /opt/homebrew/opt/openjdk@21/bin".
_HOMEBREW_JAVA_DIR := /opt/homebrew/opt/openjdk@21/bin
ifneq ($(wildcard $(_HOMEBREW_JAVA_DIR)/java),)
  export PATH := $(_HOMEBREW_JAVA_DIR):$(PATH)
endif

# ---------------------------------------------------------------------------
# Source lists.
# ---------------------------------------------------------------------------

CORE_SRC := \
  src/main/main.c \
  src/vm/vm.c \
  src/vm/profile.c \
  src/vm/zeropage_host.c \
  src/vm/builtins_xlc.c \
  src/vm/ops/arith.c \
  src/lexer/lexer.c \
  src/lexer/keywords.c \
  src/compiler/compiler.c \
  src/compiler/pratt.c \
  src/compiler/statements.c \
  src/compiler/strings.c \
  src/compiler/emit.c \
  src/compiler/globals.c \
  src/compiler/funcs.c \
  src/compiler/locals.c \
  src/compiler/loops.c \
  src/compiler/bcbuf.c \
  src/compiler/types.c \
  src/compiler/builtin_calls.c \
  src/file_runner/file_runner.c \
  src/repl/repl.c \
  src/repl/metacmds.c \
  src/runtime/builtins.c \
  src/runtime/string_pool.c \
  src/runtime/value.c \
  src/runtime/heap.c \
  src/runtime/array.c

HOST_PLATFORM_SRC := \
  src/platform/host/io.c \
  src/platform/host/keyboard.c \
  src/platform/host/osdetect.c

# Editor. Portable, platform-free sources that compile on host and target alike.
# Linked into the boot launcher (where the editor lives in-process) and the host
# test binary — NOT into the interpreters' CORE_SRC (they don't edit text).
EDITOR_PORTABLE_SRC := \
  src/editor/gapbuf.c \
  src/editor/textnav.c \
  src/editor/screen.c \
  src/editor/keymap.c \
  src/editor/fileio.c

# The full editor (portable modules + the platform loop + GET_PREFIX asm),
# folded into the II+ boot launcher. The recipe also links input.c
# ($(A2_SHARED_SRC), input_translate for the //+ save canonicaliser). The rest
# of the platform layer (screen.c/keyboard.c/osdetect.c) is NOT needed in 40-col
# mode (the editor uses cc65 conio + direct video RAM).
BOOT_LAUNCHER_EDITOR_SRC := \
  $(EDITOR_PORTABLE_SRC) \
  src/editor/editor.c \
  src/editor/editor_asm.s

# The editor's fileio.c does file I/O through prodos.c (raw ProDOS MLI, fixed
# $1C00 buffer) instead of cc65's open()/read()/write() — the latter's ~4 KB +
# malloc'd 1 KB ProDOS buffer starved the launcher's heap once the editor grew,
# so opening a file failed. mli.s is the MLI trampoline prodos.c calls. Built
# with -DWITH_SWB (which gates prodos.{c,h}).
BOOT_LAUNCHER_IO_SRC := \
  src/runtime/prodos.c \
  src/platform/apple2/mli.s

A2_PLATFORM_SRC := \
  src/platform/apple2/screen.c \
  src/platform/apple2/keyboard.c \
  src/platform/apple2/osdetect.c

# SWIFTSAT-only platform sources.
# xlc_table.s MUST come first in the cl65 input order so its XLC
# segment content (the JMP dispatch table) lands at offset 0
# within XLC ($D000 in Saturn bank 1) — the generic trampoline in
# xlc.s unconditionally JSRs into `$D000 + slot * 3`. Anything
# else contributing to XLC (currently just builtins_xlc.c's
# dispatchers/workers) gets pushed past the table by ld65's
# input-order layout. Verify with `grep "xlc_table" *.map` →
# should be at $D000. xlc.s itself contributes only to CODE/BSS,
# not XLC, so its position is flexible.
A2SAT_PLATFORM_ASM_FIRST := \
  src/platform/apple2/xlc_table.s
A2SAT_PLATFORM_ASM := \
  src/platform/apple2/xlc.s

# Files that live under platform/apple2/ but compile cleanly on the host
# too (portable C, no cc65-specific calls). They link into the host
# binary and the test binary so unit tests can exercise them directly.
# input.c = the typing-model input layer (design doc 003 rev 3); histring.c =
# the //e REPL up/down recall ring (design doc 019), which WITH_LINE_HISTORY
# collapses to an empty TU on every build except the //e REPL interpreters and
# the host test (see its target-specific -DWITH_LINE_HISTORY below).
A2_SHARED_SRC := \
  src/platform/apple2/input.c \
  src/platform/apple2/histring.c

# src/vm/zeropage.s — ZP storage for the hot VM variables.
A2_PLATFORM_ASM := \
  src/vm/zeropage.s

# `:quit` -> boot selector relaunch (cold reboot) is inlined directly in
# platform_shutdown (screen.c) — four instructions, no shared routine and no
# `jsr`, so the cost is identical on every binary (notably SWIFTSAT, whose
# ~11 B MAIN headroom can't fit a routine + a call to it). No build plumbing
# needed; it rides screen.c via A2_REST_SRC.

# ---------------------------------------------------------------------------
# Host build (clang) — produces $(HOST_DIR)/swiftii_host
# ---------------------------------------------------------------------------

CC          := clang
# -DWITH_SWB: the host build always includes the `.swb` accessors
# (heap_const_image/heap_load_const/funcs_add_runtime) so the swb round-trip
# tests link. On cc65 the flag is passed only to the Family B Compiler/Runner
# targets — never the Family A interpreters, which are at the MAIN ceiling.
# -DWITH_BIGLANG: "big language" features (for-in over an array, switch). Host
# (tests) + Family B Compiler; never the at-ceiling Family A interpreters (a
# deliberate per-feature dialect fork). The Compiler funds it by dropping
# array.c (-DNO_ARRAY_RUNTIME below).
# -DWITH_RANDOM: the random(in:) feature. It costs budget on BOTH Family B
# binaries (Compiler parser + Runner xorshift/dispatch), so it stays separate
# from WITH_BIGLANG even though both ship to Family B + host.
HOST_CFLAGS := -std=c17 -Wall -Wextra -Werror -Isrc -DWITH_SWB -DWITH_FILE_CRUD \
               -DWITH_BIGLANG -DWITH_RANDOM -DWITH_EDITOR -fsanitize=address,undefined
HOST_LDFLAGS := -fsanitize=address,undefined

# file_io.c + prodos.c ride every host build because vm.c references
# userfile_* under WITH_SWB (HOST_CFLAGS) and file_io.c calls prodos.c. swb.c
# is host-tested separately (TEST_OBJS).
HOST_SRC := $(CORE_SRC) $(HOST_PLATFORM_SRC) $(A2_SHARED_SRC) \
            src/runtime/file_io.c src/runtime/prodos.c
HOST_OBJS := $(patsubst src/%.c,$(HOST_OBJ)/%.o,$(HOST_SRC))

HOST_BIN := $(HOST_DIR)/swiftii_host

$(HOST_OBJ)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) -c $< -o $@

$(HOST_BIN): $(HOST_OBJS)
	@mkdir -p $(HOST_DIR)
	$(CC) $(HOST_LDFLAGS) $(HOST_OBJS) -o $@

# Host REPL built like the //e lite interpreter (SWIFTIIE): -DWITH_IIE and,
# crucially, NO -DWITH_SWB, so the //e-only REPL features gated on
# `WITH_IIE && !WITH_SWB` (function redefinition — config.h SWIFTII_FUNC_REDEF)
# are compiled in and can be regression-tested on the host. It drops file_io.c
# /prodos.c (those are WITH_SWB/FILE_CRUD-gated) and lands in its own object
# tree so it never clashes with the default host objects. Used by repl-test-iie.
HOST_IIE_DIR  := $(BUILD)/host-iie
HOST_IIE_OBJ  := $(HOST_IIE_DIR)/obj
HOST_IIE_CFLAGS := -std=c17 -Wall -Wextra -Werror -Isrc -DWITH_IIE \
                   -fsanitize=address,undefined
HOST_IIE_SRC  := $(CORE_SRC) $(HOST_PLATFORM_SRC) $(A2_SHARED_SRC)
HOST_IIE_OBJS := $(patsubst src/%.c,$(HOST_IIE_OBJ)/%.o,$(HOST_IIE_SRC))
HOST_IIE_BIN  := $(HOST_IIE_DIR)/swiftii_host_iie

$(HOST_IIE_OBJ)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(HOST_IIE_CFLAGS) -c $< -o $@

$(HOST_IIE_BIN): $(HOST_IIE_OBJS)
	@mkdir -p $(HOST_IIE_DIR)
	$(CC) $(HOST_LDFLAGS) $(HOST_IIE_OBJS) -o $@

# ---------------------------------------------------------------------------
# Host unit tests
# ---------------------------------------------------------------------------

TEST_SRC := \
  tests/unit/runner.c \
  tests/unit/vm_test.c \
  tests/unit/lexer_test.c \
  tests/unit/compiler_test.c \
  tests/unit/error_paths_test.c \
  tests/unit/heap_test.c \
  tests/unit/input_translate_test.c \
  tests/unit/osdetect_test.c \
  tests/editor/gapbuf_test.c \
  tests/editor/textnav_test.c \
  tests/editor/screen_test.c \
  tests/editor/keymap_test.c \
  tests/editor/fileio_test.c \
  tests/editor/session_test.c \
  tests/platform/histring_test.c \
  tests/unit/swb_test.c \
  tests/unit/file_io_test.c \
  tests/unit/srcwin_test.c

# .swb — the on-disk bytecode serialise/deserialise pair shared by the Family B
# Compiler and Runner. NOT in CORE_SRC: it's Family-B-only, so it must not bloat
# the Family-A interpreters. Linked into the host test binary (swb_test.c
# round-trips compiler->.swb->runner) and the cc65 Compiler/Runner builds.
# srcwin.c (the Tier 2 streaming source window) has the same gating: Compiler +
# host tests only.
SWB_SRC := src/swb/swb.c src/swb/swb_read.c src/compiler/srcwin.c

# Family B file I/O: the readFile/writeFile builtin backing (file_io.c) over
# the raw-MLI/stdio layer (prodos.c). Same gating as SWB_SRC; host-tested
# directly (file_io_test.c) and linked into the Compiler + Runner.
FILEIO_SRC := src/runtime/file_io.c src/runtime/prodos.c

# Tests link against the same core sources but with a redefined main(),
# so we exclude src/main/main.c from this set.
TEST_CORE_SRC := $(filter-out src/main/main.c,$(CORE_SRC))
TEST_OBJS := $(patsubst src/%.c,$(HOST_OBJ)/%.o,$(TEST_CORE_SRC)) \
             $(patsubst src/%.c,$(HOST_OBJ)/%.o,$(HOST_PLATFORM_SRC)) \
             $(patsubst src/%.c,$(HOST_OBJ)/%.o,$(A2_SHARED_SRC)) \
             $(patsubst src/%.c,$(HOST_OBJ)/%.o,$(EDITOR_PORTABLE_SRC)) \
             $(patsubst src/%.c,$(HOST_OBJ)/%.o,$(SWB_SRC)) \
             $(patsubst src/%.c,$(HOST_OBJ)/%.o,$(FILEIO_SRC)) \
             $(patsubst tests/%.c,$(HOST_OBJ)/tests/%.o,$(TEST_SRC))

TEST_BIN := $(HOST_DIR)/unit_tests

$(HOST_OBJ)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) -c $< -o $@

# histring.c is gated to the //e REPL interpreters on target (WITH_LINE_HISTORY
# = WITH_IIE && !WITH_SWB); the default host CFLAGS have neither, so force the
# gate on for just these two objects so histring_test.c exercises the real ring
# rather than the empty-TU stub (design doc 019).
$(HOST_OBJ)/platform/apple2/histring.o \
$(HOST_OBJ)/tests/platform/histring_test.o: HOST_CFLAGS += -DWITH_LINE_HISTORY

$(TEST_BIN): $(TEST_OBJS)
	@mkdir -p $(HOST_DIR)
	$(CC) $(HOST_LDFLAGS) $(TEST_OBJS) -o $@

# Paged-Runner test binary. vm.c + bcwin.c here are compiled with
# -DWITH_AUX_BC so the VM reads bytecode through the aux window — incompatible
# with the main test binary's non-paged vm.o, so it links separately into its
# own object tree. A tiny BC_WINDOW forces constant repaging (every loop and
# call), and FILE_BC_SIZE is raised so the "large" case can compile a program
# whose bytecode exceeds the old in-MAIN image cap. Same source set as the unit
# tests (proven to link the VM+compiler+runtime) plus bcwin.c, minus the other
# test files (this file brings its own main()).
PAGED_DEFS := $(HOST_CFLAGS) -DWITH_AUX_BC -DBC_WINDOW=16 -DFILE_BC_SIZE=8192
PAGED_NONTEST_SRC := $(TEST_CORE_SRC) $(HOST_PLATFORM_SRC) $(A2_SHARED_SRC) \
                     $(EDITOR_PORTABLE_SRC) $(SWB_SRC) $(FILEIO_SRC) \
                     src/vm/bcwin.c src/common/aux_store.c
PAGED_OBJ := $(HOST_OBJ)/paged
PAGED_OBJS := $(patsubst src/%.c,$(PAGED_OBJ)/%.o,$(PAGED_NONTEST_SRC)) \
              $(PAGED_OBJ)/tests/unit/paged_runner_test.o
PAGED_TEST_BIN := $(HOST_DIR)/paged_runner_tests

$(PAGED_OBJ)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PAGED_DEFS) -c $< -o $@

$(PAGED_OBJ)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PAGED_DEFS) -c $< -o $@

$(PAGED_TEST_BIN): $(PAGED_OBJS)
	@mkdir -p $(HOST_DIR)
	$(CC) $(HOST_LDFLAGS) $(PAGED_OBJS) -o $@

# Compiler-paged test binary. bcbuf.c/swb.c/emit.c here are the
# WITH_AUX_COMPILE (append-flush) variants with a TINY FILE_BC_SIZE window so a
# function-heavy program forces real flushes to the aux store; WITH_AUX_BC is
# NOT set, so vm.c/swb_read.c stay flat and the test can read back + run the
# paged-produced .swb in one process. MAX_FUNCS raised for the many-function
# stress case. Reuses the paged source set (bcwin.c compiles to nothing here).
COMPGED_DEFS := $(HOST_CFLAGS) -DWITH_AUX_COMPILE -DFILE_BC_SIZE=256 -DMAX_FUNCS=64
COMPGED_OBJ := $(HOST_OBJ)/compged
COMPGED_OBJS := $(patsubst src/%.c,$(COMPGED_OBJ)/%.o,$(PAGED_NONTEST_SRC)) \
                $(COMPGED_OBJ)/tests/unit/compiler_paged_test.o
COMPGED_TEST_BIN := $(HOST_DIR)/compiler_paged_tests

$(COMPGED_OBJ)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMPGED_DEFS) -c $< -o $@

$(COMPGED_OBJ)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(COMPGED_DEFS) -c $< -o $@

$(COMPGED_TEST_BIN): $(COMPGED_OBJS)
	@mkdir -p $(HOST_DIR)
	$(CC) $(HOST_LDFLAGS) $(COMPGED_OBJS) -o $@

.PHONY: test
test: $(TEST_BIN) $(PAGED_TEST_BIN) $(COMPGED_TEST_BIN)
	@$(TEST_BIN)
	@$(PAGED_TEST_BIN)
	@$(COMPGED_TEST_BIN)

.PHONY: host
host: $(HOST_BIN)

# ---------------------------------------------------------------------------
# Disassembler (host tool)
# ---------------------------------------------------------------------------

DISASM_BIN := $(HOST_DIR)/disasm

$(DISASM_BIN): tools/host/disasm/disasm.c
	@mkdir -p $(HOST_DIR)
	$(CC) $(HOST_CFLAGS) tools/host/disasm/disasm.c -o $@

.PHONY: disasm
disasm: $(DISASM_BIN)
ifndef FILE
	@echo "usage: make disasm FILE=path/to/bytecode.swb"
	@false
else
	@$(DISASM_BIN) $(FILE)
endif

# ---------------------------------------------------------------------------
# swbc — host `.swift` -> `.swb` compiler (tools/host/swbc). Links the same
# compiler + .swb writer as the target, but with a large FILE_BC_SIZE so it can
# emit a program bigger than the on-disk Compiler's 1,834 B cap — used to build
# an oversized `.swb` for emulator-verifying the //e Runner's aux paging.
# Reuses the unit-test source set (proven to link compiler+swb+runtime).
# ---------------------------------------------------------------------------
SWBC_DEFS := $(HOST_CFLAGS) -DFILE_BC_SIZE=16384 -DHEAP_SIZE=4096 -DMAX_FUNCS=24 -DMAX_GLOBALS=48
SWBC_NONTEST_SRC := $(TEST_CORE_SRC) $(HOST_PLATFORM_SRC) $(A2_SHARED_SRC) \
                    $(EDITOR_PORTABLE_SRC) $(SWB_SRC) $(FILEIO_SRC)
SWBC_OBJ := $(HOST_OBJ)/swbc
SWBC_OBJS := $(patsubst src/%.c,$(SWBC_OBJ)/%.o,$(SWBC_NONTEST_SRC)) \
             $(SWBC_OBJ)/tools/host/swbc/swbc.o
SWBC_BIN := $(HOST_DIR)/swbc

$(SWBC_OBJ)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SWBC_DEFS) -c $< -o $@

$(SWBC_OBJ)/tools/%.o: tools/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SWBC_DEFS) -c $< -o $@

$(SWBC_BIN): $(SWBC_OBJS)
	@mkdir -p $(HOST_DIR)
	$(CC) $(HOST_LDFLAGS) $(SWBC_OBJS) -o $@

.PHONY: swbc
swbc: $(SWBC_BIN)

# swbc-aux — swbc built like the //e Compiler (WITH_AUX_COMPILE + the 896 B
# scratch window) so we can check on the host which programs the Tier 3 //e
# Compiler can compile (it errors "program too big" if top-level scratch alone
# exceeds the window). Adds the shared aux store; backing buffer is in swbc.c.
SWBCAUX_DEFS := $(HOST_CFLAGS) -DWITH_AUX_COMPILE -DFILE_BC_SIZE=896 -DHEAP_SIZE=4096 -DMAX_FUNCS=24 -DMAX_GLOBALS=48
SWBCAUX_OBJ := $(HOST_OBJ)/swbcaux
SWBCAUX_OBJS := $(patsubst src/%.c,$(SWBCAUX_OBJ)/%.o,$(SWBC_NONTEST_SRC) src/common/aux_store.c) \
                $(SWBCAUX_OBJ)/tools/host/swbc/swbc.o
SWBCAUX_BIN := $(HOST_DIR)/swbc_aux

$(SWBCAUX_OBJ)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SWBCAUX_DEFS) -c $< -o $@

$(SWBCAUX_OBJ)/tools/%.o: tools/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SWBCAUX_DEFS) -c $< -o $@

$(SWBCAUX_BIN): $(SWBCAUX_OBJS)
	@mkdir -p $(HOST_DIR)
	$(CC) $(HOST_LDFLAGS) $(SWBCAUX_OBJS) -o $@

.PHONY: swbc-aux
swbc-aux: $(SWBCAUX_BIN)

# A generated oversized program: `var s=0` then N× `s = s + 7`, then print(s).
# Straight-line (not a loop) so the bytecode is large — forces the Runner to
# page across many windows. Output is the single line 7*N (= 4900 for N=700),
# well past the old 2,944 B image cap.
BIGPROG_N      := 700
BIGPROG_SWIFT  := $(DISK_DIR)/bigprog.swift
BIGPROG_SWB    := $(DISK_DIR)/bigprog.swb

$(BIGPROG_SWIFT):
	@mkdir -p $(DISK_DIR)
	@{ echo 'var s = 0'; \
	   i=1; while [ $$i -le $(BIGPROG_N) ]; do echo 's = s + 7'; i=$$((i+1)); done; \
	   echo 'print(s)'; } > $@
	@echo "bigprog: generated $@ ($(BIGPROG_N) statements; expect output $$(( $(BIGPROG_N) * 7 )))"

$(BIGPROG_SWB): $(SWBC_BIN) $(BIGPROG_SWIFT)
	$(SWBC_BIN) $(BIGPROG_SWIFT) $@

.PHONY: bigswb
bigswb: $(BIGPROG_SWB)
	@echo ">>> Built $(BIGPROG_SWB) — expected output when run: $$(( $(BIGPROG_N) * 7 ))"

# ---------------------------------------------------------------------------
# Apple II cross-compile (cc65). Interpreter binaries:
#
#   apple2          -> build/apple2/SWIFTIIP.SYSTEM         (II+ lite, II+ disk)
#   apple2-iie      -> build/apple2/iie/SWIFTIIE.SYSTEM     (//e lite, //e disk)
#   apple2-swiftsat -> build/apple2/swiftsat/SWIFTSAT.SYSTEM (Saturn 128K extras)
#   apple2-swiftaux -> build/apple2/swiftaux/SWIFTAUX.SYSTEM (//e aux extras)
#
# There is no unified extras binary: SWIFTSAT + SWIFTAUX are separate builds.
# The WITH_EXTRAS umbrella survives as SWIFTSAT's feature set (EXTRAS_CC65_DEFS).
#
# cc65 emits .o files next to source. To stop the builds from trampling
# each other (and to keep them off the host build's clang .o paths), each
# binary's rule wipes stray src/**/*.o after linking, and each extras
# binary order-depends on the previous (lite ← swiftsat ← swiftaux) so the
# rules are never live concurrently under `make -jN`. See design doc 010.
# ---------------------------------------------------------------------------

CL65       := cl65
# -O   global optimisation
# -Cl  static locals (no per-call stack alloc for non-reentrant code)
# -Or  enable register-variable allocation. cc65
#      has historical bugs in -Or codegen for nested loops with
#      static-locals; on the first sign of a miscompilation, drop the
#      flag and rely on error-string dedup alone for the size sweep.
CL65FLAGS  := -t apple2 -O -Cl -Or

# ------ Feature flags ------
# WITH_EXTRAS selects the extras feature set carried by the SWIFTSAT build
# (EXTRAS_CC65_DEFS). Reintroduce a per-feature flag only together with the code
# that consumes it (earlier WITH_CLOSURES / WITH_DICT / WITH_HISTORY umbrellas
# were removed once it was clear no source consumed them). WITH_80COL below is
# real (consumed by screen.c).
WITH_80COL    ?= 1
VIDEX_80COL   ?= 1

EXTRAS_CC65_DEFS := -DWITH_EXTRAS

# 80-column text is the firmware-COUT path in screen.c, gated
# `WITH_80COL` with two arms selected by WITH_IIE:
#   - WITH_80COL && WITH_IIE   -> //e built-in 80-col card ("track A")
#   - WITH_80COL && !WITH_IIE  -> II+ Videx Videoterm ("track B")
# Because both arms share the single `WITH_80COL` flag, it is passed to a
# binary via a *machine-specific* def so each disk picks the right arm:
#   IIE_80COL_DEF    -> the //e binaries (SWIFTAUX, SWIFTIIE lite, //e Runner)
#   VIDEX_80COL_DEF  -> the II+ Videx-capable binaries (SWIFTSAT, II+ Runner);
#                       SWIFTIIP lite deliberately stays 40-col.
# Toggling WITH_80COL=0 / VIDEX_80COL=0 compiles the respective path out for a
# byte-identical-when-off A/B.
IIE_80COL_DEF   := $(if $(filter 1,$(WITH_80COL)),-DWITH_80COL,)
VIDEX_80COL_DEF := $(if $(filter 1,$(VIDEX_80COL)),-DWITH_80COL,)

A2_SRC := $(CORE_SRC) $(A2_PLATFORM_SRC) $(A2_SHARED_SRC) $(A2_PLATFORM_ASM)

# Custom cc65 apple2 startup. Byte-identical to the stock crt0 except its LC-
# segment copy calls cc65's own _memcpy instead of `jsr $D39A` (Applesoft BLTU2),
# which is absent on the original Integer-BASIC Apple ][. _memcpy is already
# linked and the call is a few bytes smaller than stock, so this fits every binary
# (no budget cost) and is listed on every apple2 cl65 line — launcher + all four
# interpreters — so ld65 uses it in place of apple2.lib's crt0.o, making all
# SwiftII binaries boot on every Apple II generation. See the file's header.
A2_CRT0 := src/platform/apple2/crt0_ibasic.s

# ------ Lite build paths ------
# Named SWIFTIIP.SYSTEM, not SWIFTII.SYSTEM: SWIFTII.SYSTEM is the boot launcher
# on disk (ProDOS 2.4.3 auto-launches the first `*.SYSTEM` file in directory
# order, and we need that to be the selector). See docs/contributing/LESSONS.md.
A2_SYSTEM_BIN := $(A2_DIR)/SWIFTIIP.SYSTEM
A2_MAP        := $(A2_DIR)/swiftiip.map
A2_LBL        := $(A2_DIR)/swiftiip.lbl

# ------ //e lite build paths ------
# Same source as the II+ lite, built with -DWITH_IIE: the //e-disk lite,
# SWIFTIIE.SYSTEM. WITH_IIE swaps the doc-003 //+ typing model for native
# //e case input + lowercase display (keyboard.c / screen.c). It ships on
# the //e disk (`make disk-aux`); the II+ disk (`make disk`) ships the
# plain $(A2_SYSTEM_BIN) = SWIFTIIP.SYSTEM. We don't probe $FBB3 at
# runtime (emulator presets misreport it) — the disk picks the machine.
# See design doc 003 rev 4.
A2IIE_DIR        := $(A2_DIR)/iie
A2IIE_SYSTEM_BIN := $(A2IIE_DIR)/SWIFTIIE.SYSTEM
A2IIE_MAP        := $(A2IIE_DIR)/swiftiie.map
A2IIE_LBL        := $(A2IIE_DIR)/swiftiie.lbl

# Sources whose RODATA (string literals included) belongs in the
# language card. cc65's `#pragma rodata-name` is ignored for string
# literals — only the `--rodata-name` command-line flag redirects them.
# These files are pre-compiled to .o with the override, then handed to
# the main cl65 invocation as objects.
A2_LC_RODATA_SRC  := src/compiler/pratt.c
A2_LC_RODATA_OBJ  := $(patsubst src/%.c,$(A2_DIR)/lcobj/%.o,$(A2_LC_RODATA_SRC))
A2_REST_SRC       := $(filter-out $(A2_LC_RODATA_SRC),$(A2_SRC))

$(A2_DIR)/lcobj/%.o: src/%.c
	@mkdir -p $(dir $@)
	cc65 -t apple2 -O -Cl -Or --rodata-name LC $< -o $(@:.o=.s)
	ca65 -t apple2 $(@:.o=.s) -o $@

# Refresh the REPL banner / boot-launcher `__DATE__` and `__TIME__`
# macros on the first build of each calendar day, without forcing
# a rebuild on every make invocation (which would defeat
# `make run`'s incremental fast-path).
#
# Mechanism: a sentinel file at build/apple2/.last_build_date
# stores today's YYYYMMDD. A phony `check-build-date` rule runs on
# every make invocation; it only *writes* to the sentinel when
# today's date differs from the stored value. So the sentinel's
# mtime bumps only at day boundaries.
#
# $(A2_SYSTEM_BIN) / extras / boot launcher then declare $(A2_DATE_STAMP)
# as a real prereq — Make's normal timestamp comparison rebuilds
# them iff the sentinel was just bumped (= a new day), and cc65's
# recompile picks up the fresh `__DATE__` / `__TIME__`.
#
# To force a rebuild without changing source, delete the sentinel
# (rm build/apple2/.last_build_date) and re-run make.
A2_DATE_STAMP := $(A2_DIR)/.last_build_date

.PHONY: check-build-date
check-build-date:
	@mkdir -p $(A2_DIR)
	@TODAY=$$(date +%Y%m%d); \
	OLD=$$(cat $(A2_DATE_STAMP) 2>/dev/null || echo ""); \
	if [ "$$TODAY" != "$$OLD" ]; then \
	  echo "$$TODAY" > $(A2_DATE_STAMP); \
	fi

$(A2_DATE_STAMP): check-build-date

# cc65 build is one-shot — cl65 invokes cc65/ca65/ld65 in sequence.
# $(A2_DATE_STAMP) is a normal prereq so Make's timestamp comparison
# triggers a rebuild when the day changes (refreshing __DATE__) but
# stays incrementally-fast within the same day.
$(A2_SYSTEM_BIN): $(A2_CRT0) $(A2_REST_SRC) $(A2_LC_RODATA_OBJ) src/platform/apple2/swiftii-system.cfg $(A2_DATE_STAMP)
	@mkdir -p $(A2_DIR)
	@# II+ lite stays 40-col (no Videx 80-col path). It can't host the
	@# text80()/text() builtins (~130-210 B over the 64K code ceiling), and a
	@# silent auto-jump to 80-col was unwanted — so the lite REPL is 40-col on
	@# every II+. For 80-col on the II+, use SWIFTSAT (opt-in via text80()).
	$(CL65) $(CL65FLAGS) \
	  -C src/platform/apple2/swiftii-system.cfg \
	  -o $@ \
	  -m $(A2_MAP) \
	  -Ln $(A2_LBL) \
	  $(A2_CRT0) $(A2_REST_SRC) $(A2_LC_RODATA_OBJ)
	@# cc65 leaves .o files next to source; clean them up so the next
	@# host build (which also compiles screen.c with clang to the host
	@# obj dir) doesn't see stale targets.
	@find src -name '*.o' -delete

.PHONY: apple2
apple2: $(A2_SYSTEM_BIN)

# //e-disk lite: identical sources to $(A2_SYSTEM_BIN) but -DWITH_IIE.
# The order-only `| $(A2_SYSTEM_BIN)` serialises the cl65 invocation
# (both compile A2_REST_SRC to src/*.o scratch — see the `find ... -o
# -delete` below), keeping the lite -> iie -> swiftsat -> swiftaux
# chain race-free under `make -j`.
$(A2IIE_SYSTEM_BIN): $(A2_CRT0) $(A2_REST_SRC) $(A2_LC_RODATA_OBJ) src/platform/apple2/swiftii-system.cfg $(A2_DATE_STAMP) | $(A2_SYSTEM_BIN)
	@mkdir -p $(A2IIE_DIR)
	$(CL65) $(CL65FLAGS) -DWITH_IIE $(IIE_80COL_DEF) \
	  -C src/platform/apple2/swiftii-system.cfg \
	  -o $@ \
	  -m $(A2IIE_MAP) \
	  -Ln $(A2IIE_LBL) \
	  $(A2_CRT0) $(A2_REST_SRC) $(A2_LC_RODATA_OBJ)
	@find src -name '*.o' -delete

.PHONY: apple2-iie
apple2-iie: $(A2IIE_SYSTEM_BIN)

# ------ Family B: Compiler + Runner (MAIN-only, doc 015) ------
#
# Two MAIN-only tools (empty LC so ProDOS's MLI survives for file I/O) that
# hand off through a `.swb` disk file: the Compiler reads SOURCE.swift and
# writes SWIFTII.SWB; the Runner reads it and executes. They use
# swiftii-compiler.cfg / swiftii-runner.cfg (LC code-name -> run = MAIN; see
# those files). -DWITH_SWB pulls in the swb accessors (gated out of the
# Family A interpreters). Bigger buffers via -D: the Compiler loads the
# whole source + a roomy bytecode arena; the Runner runs the bytecode in
# place inside the image and gives the rest of the window to the heap.
#
# pratt.c is compiled normally here (NOT via the lcobj --rodata-name LC
# rule the interpreters use) because LC runs in MAIN — its rodata belongs
# in MAIN's RODATA, not a $D000 bank.
A2COMP_DIR   := $(A2_DIR)/compiler
A2COMP_BIN   := $(A2COMP_DIR)/COMPILER.SYSTEM
A2COMP_MAP   := $(A2COMP_DIR)/compiler.map
# //e Tier-1 (non-aux): flat Compiler/Runner with the //e-native render path
# (WITH_IIE) + firmware 80-col Runner, but NO aux paging — runs on any //e,
# including one with just the 1K 80-Column Text Card (no extended aux required).
A2IIECOMP_DIR := $(A2COMP_DIR)/iie
A2IIECOMP_BIN := $(A2IIECOMP_DIR)/COMPILER.SYSTEM
A2IIECOMP_MAP := $(A2IIECOMP_DIR)/compiler.map
# //e Tier-3 (aux): WITH_AUX_COMPILE / WITH_AUX_BC, bytecode paged into the 64K
# extended aux card.
A2IIEAUXCOMP_DIR := $(A2COMP_DIR)/iie-aux
A2IIEAUXCOMP_BIN := $(A2IIEAUXCOMP_DIR)/COMPILER.SYSTEM
A2IIEAUXCOMP_MAP := $(A2IIEAUXCOMP_DIR)/compiler.map
A2RUN_DIR    := $(A2_DIR)/runner
A2RUN_BIN    := $(A2RUN_DIR)/RUNNER.SYSTEM
A2RUN_MAP    := $(A2RUN_DIR)/runner.map
A2IIERUN_DIR := $(A2RUN_DIR)/iie
A2IIERUN_BIN := $(A2IIERUN_DIR)/RUNNER.SYSTEM
A2IIERUN_MAP := $(A2IIERUN_DIR)/runner.map
A2IIEAUXRUN_DIR := $(A2RUN_DIR)/iie-aux
A2IIEAUXRUN_BIN := $(A2IIEAUXRUN_DIR)/RUNNER.SYSTEM
A2IIEAUXRUN_MAP := $(A2IIEAUXRUN_DIR)/runner.map
# Tier 2 (II+ Saturn 128K): paged Compiler/Runner that park bytecode in Saturn
# banks instead of aux ($D000 window via saturn_bc.s).
A2SATCOMP_DIR := $(A2COMP_DIR)/sat
A2SATCOMP_BIN := $(A2SATCOMP_DIR)/COMPILER.SYSTEM
A2SATCOMP_MAP := $(A2SATCOMP_DIR)/compiler.map
A2SATRUN_DIR := $(A2RUN_DIR)/sat
A2SATRUN_BIN := $(A2SATRUN_DIR)/RUNNER.SYSTEM
A2SATRUN_MAP := $(A2SATRUN_DIR)/runner.map

# Compiler: lexer + full compiler + constant-pool runtime + swb; no
# vm/ops/repl/file_runner (it only emits .swb). Machine-independent -> one
# shared binary for both Family B disks.
# NB: array.c is deliberately NOT linked — the Compiler emits array opcodes
# but never executes them, so the only reference was value.c's array-element
# release path, gated out by -DNO_ARRAY_RUNTIME (COMPILER_DEFS). Dropping the
# whole object reclaims ~1.5 KB of MAIN code = BSS headroom for the
# big-language features (for-in-over-array, switch, ...).
COMPILER_SRC := \
  src/main/compiler_main.c \
  src/lexer/lexer.c src/lexer/keywords.c \
  src/compiler/compiler.c src/compiler/pratt.c src/compiler/statements.c \
  src/compiler/strings.c src/compiler/emit.c src/compiler/globals.c \
  src/compiler/funcs.c src/compiler/locals.c src/compiler/loops.c \
  src/compiler/bcbuf.c src/compiler/types.c src/compiler/builtin_calls.c \
  src/compiler/srcwin.c \
  src/runtime/heap.c src/runtime/string_pool.c src/runtime/value.c \
  src/runtime/string.c src/runtime/refcount.c \
  src/swb/swb.c src/runtime/prodos.c \
  src/platform/apple2/screen.c src/platform/apple2/osdetect.c \
  src/platform/apple2/mli.s src/platform/apple2/chain.s src/vm/zeropage.s

# Runner: VM + runtime + all builtins + swb; no lexer/compiler except the
# funcs + bcbuf the VM's OP_CALL needs. Per-machine (-DWITH_IIE keyboard).
RUNNER_SRC := \
  src/main/runner_main.c \
  src/vm/vm.c src/vm/profile.c src/vm/zeropage_host.c src/vm/ops/arith.c \
  src/vm/builtins_xlc.c \
  src/compiler/funcs.c src/compiler/bcbuf.c \
  src/runtime/builtins.c src/runtime/heap.c src/runtime/string_pool.c \
  src/runtime/value.c src/runtime/array.c src/runtime/string.c \
  src/runtime/refcount.c \
  src/swb/swb_read.c src/runtime/file_io.c src/runtime/prodos.c \
  src/platform/apple2/screen.c src/platform/apple2/keyboard.c \
  src/platform/apple2/osdetect.c src/platform/apple2/input.c \
  src/platform/apple2/mli.s src/platform/apple2/chain.s src/vm/zeropage.s

# The //e Runner additionally pages bytecode out to aux RAM (-DWITH_AUX_BC):
# bcwin.c is the MAIN window + backing-store glue, runner_aux.s is
# the ROM-AUXMOVE driver. Both are aux-only — linking them into the II+ Runner
# would pull unresolved aux symbols, so they go in the //e list alone.
RUNNER_IIEAUX_SRC := $(RUNNER_SRC) src/vm/bcwin.c src/common/aux_store.c \
                  src/platform/apple2/aux_bc.s

# The Compiler's source window lives in low RAM $0C00-$1BFF (4 KB, not BSS —
# see compiler_main.c), so the freed BSS goes to the bytecode arena + const
# pool + bigger symbol tables. MAX_GLOBALS/MAX_FUNCS must match between Compiler
# and Runner (.swb indices are table positions).
# -DNO_ARRAY_RUNTIME: the Compiler never executes array opcodes, so array.c is
# unlinked (see COMPILER_SRC) and value.c's array-release path is gated out;
# the ~1.5 KB freed pays for -DWITH_BIGLANG (for-in-over-array + switch codegen).
# -DWITH_RANDOM adds the random(in:) parser.
# FILE_BC_SIZE / HEAP_SIZE are sized to xbig.swift, the largest flat-tier sample.
# xbig is now function-wrapped + terse-labelled (1,818 B bytecode / 644 B const
# pool + 16 STRING_POOL_SLOTS = 660): the baseline max program is ~xbig exactly,
# and bigger programs use the Saturn/aux tiers. These are max-program-size
# (capacity) trades, NOT C-stack/safety ones — every byte trimmed from the
# arena/const pool is BSS reclaimed for compiler features. HEAP_SIZE stays 768
# (now ~108 B over xbig's 660, since the slimmer xbig freed const-pool room).
# The Runner's SWB_IMAGE_SIZE tracks this max .swb, so raising a cap here means
# raising it there too (lowering it is always safe).
COMPILER_DEFS := -DWITH_SWB -DWITH_BIGLANG -DWITH_RANDOM -DNO_ARRAY_RUNTIME -DFILE_SRC_SIZE=4096 -DFILE_BC_SIZE=1834 -DHEAP_SIZE=768 \
  -DMAX_GLOBALS=48 -DMAX_FUNCS=24 -DWITH_INVERSE_JM
# Runner runs bytecode in place from the image, so bcbuf's buffer is unused
# -> FILE_BC_SIZE=1, and the freed window goes to the runtime heap.
# SWB_IMAGE_SIZE tracks the LARGEST .swb the Compiler can emit (header 12 + bc
# 1,834 + consts 768 + 24*4 funcs, + slack): raising a Compiler cap means
# raising the image here too.
# WITH_FILE_CRUD: the file/dir builtins (delete/rename/exists/append/mkdir/list,
# doc 017). Runner-only — the Compiler links prodos.c but never calls them, and
# cc65's -Cl makes each function's locals static BSS, so leaving them in would
# cost the at-budget Compiler ~800 B of dead BSS (ld65 links whole objects).
# Host defines it too (file_io_test.c).
# USERFILE_READ_CAP=512 is nearly free: the readFile buffer doubles as the
# listDirectory block buffer and a ProDOS directory block is exactly 512 B.
# WITH_RANDOM adds random(in:)'s worker + dispatch — a multiply-free xorshift
# that reuses the already-linked signed divide, so it pulls in NO cc65 math
# library. (for-in-array + switch desugar to existing opcodes, so the Runner
# needs no code for them.)
# WITH_TESTLOG adds the FAIL-token watcher (screen.c emit()) + the TESTLOG
# append (runner_main, doc 018).
# HEAP_SIZE is the lever that pays for all of the above: each builtin's dispatch
# adds -Cl static BSS, so the heap is trimmed to keep a ~50 B BSS margin under
# the ceiling (see swiftii-runner.cfg for the matching stack trim). These are
# runtime-data capacity trades, NOT stack/safety ones — program SIZE
# (FILE_BC_SIZE/SWB_IMAGE_SIZE) is untouched — and the heap stays well above the
# Family A interpreters' 2 KB. The II+/Saturn variants below derive their own
# (smaller) HEAP_SIZE from this baseline for the same reason.
# -DWITH_INVERSE_JM: pre-IIe (II+/Saturn) render fix for the inverse-video 'J'
# ($0A) and 'M' ($0D) screen codes, whose cputc LF/CR collision garbled
# uppercase output. The Runner needs it for uppercase program output AND its
# "Running:" file-name echo; the pre-IIe Compilers need it for their
# "Compiling:" / "Wrote:" file-path echo, which is uppercase ProDOS paths (a
# path through a SAMPLES dir, or any name with J/M, came out garbled — e.g. an
# 'M' acting as a CR wrapped the rest of the line back over itself). So it is on
# for the Runner (RUNNER_DEFS) AND the pre-IIe Compilers (COMPILER_DEFS /
# COMPILER_SAT_DEFS); NOT the //e Compiler, whose WITH_IIE build never compiles
# the branch (full ASCII char ROM, no inverse-letter rendering). The ~80 B of
# CODE+BSS it adds fits the at-budget pre-IIe Compilers (II+ ~8 B / Saturn
# ~29 B BSS margin remaining); on the Runner the BSS is paid by the HEAP_SIZE
# trims in RUNNER_IIP_DEFS / RUNNER_SAT_DEFS below. Harmless on the //e Runner
# (WITH_IIE never compiles the branch).
# -DWITH_GR_TEXTWIN: confine print() scrolling to gr()'s 4-line text window so a
# multi-scene GR program (e.g. xgrdemo) doesn't scroll the 40x40 picture up into
# garbage. Defined on every binary that runs GR programs: the Family B Runner
# (here + RUNNER_SAT_DEFS) and the extras REPLs (SWIFTSAT/SWIFTAUX recipes). NOT
# the Compiler (also WITH_SWB but never runs programs) nor the lite REPLs — they
# leave it off and stay byte-identical (screen.c's SCROLL_TOP is then a literal
# 0). Cheap: a 1 B global + a 2 B read in scroll_up_one; the gr()/text() writes
# ride builtins_xlc.c (the XLC cold bank on SWIFTSAT, so they don't touch its
# tight MAIN — a setter function there overflowed SWIFTSAT's MAIN by 5 B).
RUNNER_DEFS   := -DWITH_SWB -DWITH_RANDOM -DWITH_FILE_CRUD -DWITH_TESTLOG -DUSERFILE_READ_CAP=512 \
  -DWITH_INVERSE_JM -DWITH_GR_TEXTWIN -DFILE_BC_SIZE=1 -DHEAP_SIZE=2560 -DSWB_IMAGE_SIZE=2944 \
  -DMAX_GLOBALS=48 -DMAX_FUNCS=24

# II+ Runner only: the Videx (track-B) 80-col code adds ~137 B of BSS (cc65 -Cl
# makes each new screen.c function's locals static; the Videx arm adds
# platform_text80/text40/cout_char/has_videx), so this variant trims the heap
# harder than the baseline (still well above the Family A interpreters' 2 KB).
# The //e Runner keeps the higher base HEAP_SIZE: it has no Videx code (its
# 80-col is the //e firmware arm) AND pages bytecode to aux, so its RAM is not
# the constraint. Derived from RUNNER_DEFS so the two never drift.
#
# 2136 (not the earlier 2112): xbig — the largest shipped program, a Tier-1
# top-level-heavy demo — peaks at ~2124 B of runtime heap, so the 2112 trim that
# funded Videx silently regressed it (runtime OOM in section 7 on real II+; it
# ran before the Videx work dropped this from the 2560 base). 2136 clears xbig
# with ~12 B margin and still links with Videx (~15 B BSS headroom under the
# $BF00 ceiling). The //e aux Runner (2560) and host (4096) were always fine.
RUNNER_IIP_DEFS := $(patsubst -DHEAP_SIZE=%,-DHEAP_SIZE=2136,$(RUNNER_DEFS))

$(A2COMP_BIN): $(A2_CRT0) $(COMPILER_SRC) src/platform/apple2/swiftii-compiler.cfg $(A2_DATE_STAMP) | $(A2IIE_SYSTEM_BIN)
	@mkdir -p $(A2COMP_DIR)
	$(CL65) $(CL65FLAGS) $(COMPILER_DEFS) \
	  -C src/platform/apple2/swiftii-compiler.cfg \
	  -o $@ -m $(A2COMP_MAP) \
	  $(A2_CRT0) $(COMPILER_SRC)
	@find src -name '*.o' -delete

# //e Compiler (Tier 3): same compiler, but -DWITH_AUX_COMPILE makes bcbuf a
# MAIN window that flushes completed (immutable) function bodies to the aux
# bytecode store, so the *code* a program can compile rises from the flat
# FILE_BC_SIZE (1,834 B window = max top-level scratch + largest single
# function) to AUX_BC_MAX. Adds the shared aux store + ROM-AUXMOVE driver.
# Links the same compiler cfg; the II+ Compiler stays the flat baseline.
COMPILER_IIEAUX_SRC := $(COMPILER_SRC) src/common/aux_store.c \
                    src/platform/apple2/aux_bc.s
# WITH_AUX_COMPILE adds ~807 B of CODE (windowing + flush + paged swb write),
# which the at-budget Compiler can't absorb at the flat 1,834 B window. But the
# //e window now only has to hold the top-level SCRATCH + the in-progress
# function (completed functions flush to the aux store), so it can be smaller
# than the baseline's whole-program buffer: FILE_BC_SIZE 1,834 -> 896 reclaims
# 938 B of BSS for the new code (~74 B margin). Net effect: per-program
# top-level-scratch ceiling drops to 896 B, but total compiled CODE rises to
# AUX_BC_MAX (~36 KB) for function-heavy programs — the Tier 3 trade. (A
# top-level-heavy program >896 B uses Tier 1.)
COMPILER_IIEAUX_DEFS := -DWITH_SWB -DWITH_BIGLANG -DWITH_RANDOM -DNO_ARRAY_RUNTIME \
  -DFILE_SRC_SIZE=4096 -DFILE_BC_SIZE=896 -DHEAP_SIZE=744 \
  -DMAX_GLOBALS=48 -DMAX_FUNCS=24 -DWITH_AUX_COMPILE
# -DWITH_IIE selects screen.c's //e render path (emit_native_high + cputc, full
# ASCII char ROM) instead of the pre-IIe path's runtime $FBB3 machine probe +
# inverse-letter case rendering. Without it the //e Compiler fell into the pre-IIe
# path on the //e and garbled its uppercase "Compiling:" / "Wrote:" path echo (the
# J->$0A / M->$0D cputc CR/LF collision — and it has no WITH_INVERSE_JM to fix
# that), shortening the visible path. WITH_IIE also DROPS emit_inverse_letter + the
# digraph branches, netting MORE BSS margin (~146 B vs ~71 B). Unlike the //e
# launcher / Runner / SWIFTAUX it does NOT take IIE_80COL_DEF: the WITH_80COL
# firmware arm overflows this at-budget binary by ~194 B, and the Compiler — a
# transient tool that comes up in 40-col text after the chain — never needed
# 80-col (it ran 40-col before this change too; only its render path moves).
$(A2IIEAUXCOMP_BIN): $(A2_CRT0) $(COMPILER_IIEAUX_SRC) src/platform/apple2/swiftii-compiler.cfg $(A2_DATE_STAMP) | $(A2IIECOMP_BIN)
	@mkdir -p $(A2IIEAUXCOMP_DIR)
	$(CL65) $(CL65FLAGS) $(COMPILER_IIEAUX_DEFS) -DWITH_IIE \
	  -C src/platform/apple2/swiftii-compiler.cfg \
	  -o $@ -m $(A2IIEAUXCOMP_MAP) \
	  $(A2_CRT0) $(COMPILER_IIEAUX_SRC)
	@find src -name '*.o' -delete

# //e Tier-1 (non-aux) Compiler: the flat baseline Compiler (same COMPILER_SRC,
# flat FILE_BC_SIZE=1834 — no aux store / aux_bc.s), but built for the //e with
# -DWITH_IIE so it renders its "Compiling:" / "Wrote:" echo via the //e native
# char ROM instead of the pre-IIe inverse-letter path. WITH_IIE DROPS
# emit_inverse_letter + the digraph branches, so this is actually SMALLER than
# the II+ Compiler (which carries WITH_INVERSE_JM) — it fits the flat budget with
# room to spare. Filter WITH_INVERSE_JM out of COMPILER_DEFS: the cputc J/M
# collision it patches only exists on the pre-IIe render path WITH_IIE replaces.
# Like the //e aux Compiler it stays 40-col (no IIE_80COL_DEF) — a transient tool.
COMPILER_IIE_DEFS := $(filter-out -DWITH_INVERSE_JM,$(COMPILER_DEFS))
$(A2IIECOMP_BIN): $(A2_CRT0) $(COMPILER_SRC) src/platform/apple2/swiftii-compiler.cfg $(A2_DATE_STAMP) | $(A2COMP_BIN)
	@mkdir -p $(A2IIECOMP_DIR)
	$(CL65) $(CL65FLAGS) $(COMPILER_IIE_DEFS) -DWITH_IIE \
	  -C src/platform/apple2/swiftii-compiler.cfg \
	  -o $@ -m $(A2IIECOMP_MAP) \
	  $(A2_CRT0) $(COMPILER_SRC)
	@find src -name '*.o' -delete

.PHONY: apple2-compiler
apple2-compiler: $(A2COMP_BIN) $(A2IIECOMP_BIN) $(A2IIEAUXCOMP_BIN)

$(A2RUN_BIN): $(A2_CRT0) $(RUNNER_SRC) src/platform/apple2/swiftii-runner.cfg $(A2_DATE_STAMP) | $(A2COMP_BIN)
	@mkdir -p $(A2RUN_DIR)
	@# II+ Runner gets the Videx (track-B) 80-col arm via VIDEX_80COL_DEF
	@# (!WITH_IIE -> Videoterm). text80()/text() are program opt-in (vm.c ->
	@# builtins_xlc.c); there is no auto-enter, so non-Videx II+ stays 40-col.
	@# RUNNER_IIP_DEFS trims the heap to pay for the Videx code's static BSS.
	$(CL65) $(CL65FLAGS) $(RUNNER_IIP_DEFS) $(VIDEX_80COL_DEF) \
	  -C src/platform/apple2/swiftii-runner.cfg \
	  -o $@ -m $(A2RUN_MAP) \
	  $(A2_CRT0) $(RUNNER_SRC)
	@find src -name '*.o' -delete

# //e Tier-1 (non-aux) Runner: the flat Runner (same RUNNER_SRC, whole .swb
# loaded into the 2,944 B MAIN buffer — no aux window / bcwin.c / aux_bc.s), but
# built for the //e: -DWITH_IIE for native case rendering and IIE_80COL_DEF for
# the //e FIRMWARE 80-col arm (so a program's text80() works on a //e with the
# 1K 80-Column Text Card; harmless on a bare //e if unused). This is the //e
# counterpart of the II+ flat Runner ($A2RUN_BIN) — it has NO Videx code and NO
# aux paging. RUNNER_IIE_DEFS keeps the full RUNNER_DEFS heap (2560); if the
# firmware 80-col arm pushes BSS over the ceiling, trim it here with the same
# patsubst lever RUNNER_IIP_DEFS uses (stays well above the 2 KB heap floor).
RUNNER_IIE_DEFS := $(RUNNER_DEFS)
$(A2IIERUN_BIN): $(A2_CRT0) $(RUNNER_SRC) src/platform/apple2/swiftii-runner.cfg $(A2_DATE_STAMP) | $(A2RUN_BIN)
	@mkdir -p $(A2IIERUN_DIR)
	$(CL65) $(CL65FLAGS) $(RUNNER_IIE_DEFS) -DWITH_IIE $(IIE_80COL_DEF) \
	  -C src/platform/apple2/swiftii-runner.cfg \
	  -o $@ -m $(A2IIERUN_MAP) \
	  $(A2_CRT0) $(RUNNER_SRC)
	@find src -name '*.o' -delete

# AUX_BC_DEFS: page the program bytecode into aux RAM so the
# program-size ceiling is the ~40 KB aux park, not the MAIN .swb image. The
# 1 KB window + 512 B stage chunk + 1 KB tail replace the (now removed)
# ~2.9 KB s_image, so MAIN BSS nets out roughly even — confirm via `make size`.
AUX_BC_DEFS := -DWITH_AUX_BC -DBC_WINDOW=1024 -DSWB_TAIL_CAP=1024
$(A2IIEAUXRUN_BIN): $(A2_CRT0) $(RUNNER_IIEAUX_SRC) src/platform/apple2/swiftii-runner.cfg $(A2_DATE_STAMP) | $(A2IIERUN_BIN)
	@mkdir -p $(A2IIEAUXRUN_DIR)
	$(CL65) $(CL65FLAGS) $(RUNNER_DEFS) $(AUX_BC_DEFS) -DWITH_IIE $(IIE_80COL_DEF) \
	  -C src/platform/apple2/swiftii-runner.cfg \
	  -o $@ -m $(A2IIEAUXRUN_MAP) \
	  $(A2_CRT0) $(RUNNER_IIEAUX_SRC)
	@find src -name '*.o' -delete

.PHONY: apple2-runner
apple2-runner: $(A2RUN_BIN) $(A2IIERUN_BIN) $(A2IIEAUXRUN_BIN)

# ------ Tier 2: II+ Saturn paged Compiler + Runner ------
#
# Same paging machinery as the //e Tier-3 binaries (WITH_AUX_COMPILE windowed
# bcbuf / WITH_AUX_BC windowed VM), but the backing store is Saturn banks, not
# aux: -DBC_STORE_SATURN swaps aux_store.c's driver to saturn_bc.s (which banks
# the $D000 LC region to a Saturn bank around each copy and restores MLI's bank
# afterward). These are II+ builds (no -DWITH_IIE) — they run the //+ typing /
# 40-col model, and on a II+ with a Saturn 128K card lift the program ceiling to
# the Saturn park. The launcher parks the detected Saturn slot at SX_SAT_SLOT;
# saturn_bc_init() reads it at startup. aux_bc.s is replaced by saturn_bc.s.
COMPILER_SAT_SRC := $(COMPILER_SRC) src/common/aux_store.c \
                    src/platform/apple2/saturn_bc.s
# saturn_bc.s's bank-walking driver is ~325 B larger than the //e's compact
# ROM-AUXMOVE driver (aux_bc.s), overflowing the at-budget Compiler BSS by 251 B.
# Reclaimed with two capacity trades (NOT safety trades, like Tier 3's): the
# window FILE_BC_SIZE 896 -> 640 (per-program top-level-scratch + largest single
# function ceiling) and the const pool HEAP_SIZE 744 -> 704. ~45 B BSS margin;
# function-heavy showcases (the Saturn tier's point) keep tiny top-level scratch.
COMPILER_SAT_DEFS := -DWITH_SWB -DWITH_BIGLANG -DWITH_RANDOM -DNO_ARRAY_RUNTIME \
  -DFILE_SRC_SIZE=4096 -DFILE_BC_SIZE=640 -DHEAP_SIZE=704 \
  -DMAX_GLOBALS=48 -DMAX_FUNCS=24 -DWITH_AUX_COMPILE -DBC_STORE_SATURN -DWITH_INVERSE_JM
$(A2SATCOMP_BIN): $(A2_CRT0) $(COMPILER_SAT_SRC) src/platform/apple2/swiftii-compiler.cfg $(A2_DATE_STAMP) | $(A2IIEAUXCOMP_BIN)
	@mkdir -p $(A2SATCOMP_DIR)
	$(CL65) $(CL65FLAGS) $(COMPILER_SAT_DEFS) \
	  -C src/platform/apple2/swiftii-compiler.cfg \
	  -o $@ -m $(A2SATCOMP_MAP) \
	  $(A2_CRT0) $(COMPILER_SAT_SRC)
	@find src -name '*.o' -delete

RUNNER_SAT_SRC := $(RUNNER_SRC) src/vm/bcwin.c src/common/aux_store.c \
                  src/platform/apple2/saturn_bc.s
# Own defs (not RUNNER_DEFS + extra) to avoid -D redefinition. The II+ Saturn
# Runner is the tightest of the three: without -DWITH_IIE it keeps the pre-IIe
# input-translation path, and saturn_bc.s is ~325 B bigger than the //e's
# aux_bc.s. Reclaimed by halving the window (BC_WINDOW 1024 -> 512;
# correctness-neutral, just more repaging) and trimming the runtime heap harder
# than the other two variants (still > the Family A interpreters' 2 KB); the same
# per-builtin -Cl static BSS that drives RUNNER_DEFS' heap is paid here too, plus
# random(in:)'s keypress-timing seed (~117 B). SWB_IMAGE_SIZE is unused on the
# WITH_AUX_BC path (the .swb streams through the window, never the whole-image
# buffer), so it's dropped.
RUNNER_SAT_DEFS := -DWITH_SWB -DWITH_RANDOM -DWITH_FILE_CRUD -DWITH_TESTLOG -DUSERFILE_READ_CAP=512 \
  -DWITH_INVERSE_JM -DWITH_GR_TEXTWIN -DFILE_BC_SIZE=1 -DHEAP_SIZE=1792 -DMAX_GLOBALS=48 -DMAX_FUNCS=24 \
  -DWITH_AUX_BC -DBC_STORE_SATURN -DBC_WINDOW=512 -DSWB_TAIL_CAP=1024
$(A2SATRUN_BIN): $(A2_CRT0) $(RUNNER_SAT_SRC) src/platform/apple2/swiftii-runner.cfg $(A2_DATE_STAMP) | $(A2IIEAUXRUN_BIN)
	@mkdir -p $(A2SATRUN_DIR)
	@# II+ Saturn Runner also gets the Videx (track-B) 80-col arm. Its windowed
	@# bytecode path (BC_WINDOW=512) leaves enough BSS that the Videx code fits
	@# at the existing heap (1792) — no further trim needed. text80()/text() are
	@# program opt-in; the Videoterm is independent of the Saturn program store.
	$(CL65) $(CL65FLAGS) $(RUNNER_SAT_DEFS) $(VIDEX_80COL_DEF) \
	  -C src/platform/apple2/swiftii-runner.cfg \
	  -o $@ -m $(A2SATRUN_MAP) \
	  $(A2_CRT0) $(RUNNER_SAT_SRC)
	@find src -name '*.o' -delete

.PHONY: apple2-saturn-familyb
apple2-saturn-familyb: $(A2SATCOMP_BIN) $(A2SATRUN_BIN)

.PHONY: apple2-familyb
apple2-familyb: $(A2COMP_BIN) $(A2IIECOMP_BIN) $(A2IIEAUXCOMP_BIN) \
                $(A2RUN_BIN) $(A2IIERUN_BIN) $(A2IIEAUXRUN_BIN) \
                $(A2SATCOMP_BIN) $(A2SATRUN_BIN)

# ------ SWIFTSAT build paths ------
#
# SWIFTSAT.SYSTEM is the Saturn 128K extras binary. Uses
# swiftsat-system.cfg's three-segment layout (MAIN + LC + XLC) to
# emit three output files (`.main`, `.lc`, `.xlc`), then packs them
# into one .SYSTEM file with a 6-byte header via
# tools/host/diskimg/pack_swiftsat.py. The chunked-staging boot launcher loads
# main → $2000 and stages the XLC image into Saturn bank 1; the disk
# (`make disk`) ships SWIFTSAT for Saturn machines.
#
# The XLC segment holds the extras (asc/chr/Int(s)/array
# methods/home/peek/poke/gr/…) via per-file `--code-name XLC` placement.
#
# Design doc: docs/contributing/design/011-extras-lc-in-saturn-aux.md.
#
# LC is in-MAIN (cc65 standard `load = MAIN, run = LC`) — see
# swiftsat-system.cfg's comment block for why we don't use the
# dormant doc-011 `load = LC, run = LC` trick (cc65's crt0 always
# does the LC copy, clobbers LC with BSS-area garbage if LC isn't
# staged in MAIN). The packed file has only two chunks: main + XLC.
A2SAT_DIR        := $(A2_DIR)/swiftsat
A2SAT_MAIN_BIN   := $(A2SAT_DIR)/SWIFTSAT.main
A2SAT_XLC_BIN    := $(A2SAT_DIR)/SWIFTSAT.xlc
A2SAT_SYSTEM_BIN := $(A2SAT_DIR)/SWIFTSAT.SYSTEM
A2SAT_MAP        := $(A2SAT_DIR)/swiftsat.map
A2SAT_LBL        := $(A2SAT_DIR)/swiftsat.lbl
A2SAT_LC_RODATA_OBJ := $(patsubst src/%.c,$(A2SAT_DIR)/lcobj/%.o,$(A2_LC_RODATA_SRC))

$(A2SAT_DIR)/lcobj/%.o: src/%.c
	@mkdir -p $(dir $@)
	cc65 -t apple2 -O -Cl -Or $(EXTRAS_CC65_DEFS) -DWITH_SWIFTSAT --rodata-name LC $< -o $(@:.o=.s)
	ca65 -t apple2 $(@:.o=.s) -o $@

# Order-only dep on the lite binary serialises cc65 invocations so they
# don't race on `find src -name '*.o' -delete` under `make -jN`.
# -DWITH_SWIFTSAT gates SWIFTSAT-only C code (xlc_init call in main(),
# generic XLC dispatch in vm.c, etc.) on top of the existing
# EXTRAS_CC65_DEFS. A2SAT_PLATFORM_ASM_FIRST is the JMP table; it
# leads the cl65 input so its XLC content lands at $D000 ahead of
# any other XLC-contributing source. A2SAT_PLATFORM_ASM contains
# the trampoline (CODE-segment, position-independent).
$(A2SAT_MAIN_BIN): $(A2SAT_PLATFORM_ASM_FIRST) $(A2_CRT0) $(A2_REST_SRC) $(A2SAT_LC_RODATA_OBJ) $(A2SAT_PLATFORM_ASM) src/platform/apple2/swiftsat-system.cfg $(A2_DATE_STAMP) | $(A2IIE_SYSTEM_BIN)
	@mkdir -p $(A2SAT_DIR)
	$(CL65) $(CL65FLAGS) $(EXTRAS_CC65_DEFS) -DWITH_SWIFTSAT -DWITH_GR_TEXTWIN $(VIDEX_80COL_DEF) \
	  -C src/platform/apple2/swiftsat-system.cfg \
	  -o $(A2SAT_DIR)/SWIFTSAT \
	  -m $(A2SAT_MAP) \
	  -Ln $(A2SAT_LBL) \
	  $(A2SAT_PLATFORM_ASM_FIRST) $(A2_CRT0) $(A2_REST_SRC) $(A2SAT_LC_RODATA_OBJ) $(A2SAT_PLATFORM_ASM)
	@# ld65 only emits .xlc if at least one segment goes there.
	@# Defence in depth — xlc_table.s alone is enough to populate
	@# it, but if everything ever gates out, create a 0-byte file
	@# so the pack step downstream still finds an input.
	@test -f $(A2SAT_XLC_BIN) || : > $(A2SAT_XLC_BIN)
	@find src -name '*.o' -delete

# ld65 emits SWIFTSAT.main and (when non-empty) SWIFTSAT.xlc as a
# side effect of the rule above (per `file =` directives in
# swiftsat-system.cfg). They share the same rule because Make
# treats one recipe with multiple outputs as
# "produced together."
$(A2SAT_XLC_BIN): $(A2SAT_MAIN_BIN)

$(A2SAT_SYSTEM_BIN): $(A2SAT_MAIN_BIN) $(A2SAT_XLC_BIN) tools/host/diskimg/pack_swiftsat.py
	python3 tools/host/diskimg/pack_swiftsat.py $(A2SAT_MAIN_BIN) $(A2SAT_XLC_BIN) $@

.PHONY: apple2-swiftsat
apple2-swiftsat: $(A2SAT_SYSTEM_BIN)

# ------ SWIFTAUX build paths ------
# SWIFTAUX.SYSTEM is the Apple //e aux extras binary (64K extended
# 80-col card, no Saturn). Built -DWITH_SWIFTAUX ONLY — NOT WITH_SWIFTSAT
# and NOT the WITH_EXTRAS umbrella: it's literally "lite + aux extras".
# Core ops (str concat/interp, arrays, print) run INLINE in MAIN (the
# lite path) so they keep lite's MAIN budget AND avoid a per-call AUXMOVE
# copy-down on hot paths; only the cold XLC-only extras (asc/chr, Int,
# array methods, platform builtins) are copied down from the aux park
# into the $B000 STAGING buffer per call and JSR'd there, via the aux_xlc.s
# trampoline (ROM AUXMOVE). See swiftaux-system.cfg + docs/contributing/design/012
# section "Stage 2 refresh". (Matching SWIFTSAT's WITH_EXTRAS + core-in-XLC
# layout would overflow MAIN here, since aux keeps core ops inline.)
#
# Outputs: SWIFTAUX.main + SWIFTAUX.ovl.asc + SWIFTAUX.ovl.chr (the
# copy-down overlays) + SWIFTAUX.xlc (the 21 not-yet-ported dispatchers,
# DEAD — not packed). pack_swiftaux.py packs main + the padded-stride
# park into SWIFTAUX.SYSTEM. Links aux_xlc.s, NOT xlc.s/xlc_table.s (those
# are Saturn). Order-only dep on the SWIFTSAT SYSTEM serialises the cl65
# invocations so they don't race on `find src -name '*.o' -delete`.
# aux_table.s leads the cl65 input so its per-overlay `jmp <dispatcher>`
# stubs land at offset 0 of each OVL* file (like xlc_table.s for SWIFTSAT).
A2AUX_PLATFORM_ASM_FIRST := src/platform/apple2/aux_table.s
A2AUX_PLATFORM_ASM := src/platform/apple2/aux_xlc.s
A2AUX_DIR        := $(A2_DIR)/swiftaux
A2AUX_MAIN_BIN   := $(A2AUX_DIR)/SWIFTAUX.main
A2AUX_XLC_BIN    := $(A2AUX_DIR)/SWIFTAUX.xlc
A2AUX_OVLASC_BIN := $(A2AUX_DIR)/SWIFTAUX.ovl.asc
A2AUX_OVLCHR_BIN := $(A2AUX_DIR)/SWIFTAUX.ovl.chr
A2AUX_OVLCALL_BIN := $(A2AUX_DIR)/SWIFTAUX.ovl.call
# Overlays: str_interp (cold, evicted from MAIN) + Int(s) + the three array
# methods.
A2AUX_OVLSIP_BIN := $(A2AUX_DIR)/SWIFTAUX.ovl.sip
A2AUX_OVLINT_BIN := $(A2AUX_DIR)/SWIFTAUX.ovl.int
A2AUX_OVLRML_BIN := $(A2AUX_DIR)/SWIFTAUX.ovl.rml
A2AUX_OVLRMA_BIN := $(A2AUX_DIR)/SWIFTAUX.ovl.rma
# NB: extension is ".cnc", not ".con" — "con" is a Windows reserved device
# name (like aux/nul/prn) and OneDrive refuses to sync such files.
A2AUX_OVLCON_BIN := $(A2AUX_DIR)/SWIFTAUX.ovl.cnc
# The two grouped platform overlays + the three cold opcode bodies
# (str_concat/new_array/arr_len) evicted from inline-MAIN to fund the
# platform-builtin parser table.
A2AUX_OVLPMEM_BIN := $(A2AUX_DIR)/SWIFTAUX.ovl.pmem
A2AUX_OVLPGR_BIN := $(A2AUX_DIR)/SWIFTAUX.ovl.pgr
A2AUX_OVLSCC_BIN := $(A2AUX_DIR)/SWIFTAUX.ovl.scc
A2AUX_OVLNAR_BIN := $(A2AUX_DIR)/SWIFTAUX.ovl.nar
A2AUX_OVLALN_BIN := $(A2AUX_DIR)/SWIFTAUX.ovl.aln
A2AUX_SYSTEM_BIN := $(A2AUX_DIR)/SWIFTAUX.SYSTEM
A2AUX_MAP        := $(A2AUX_DIR)/swiftaux.map
A2AUX_LBL        := $(A2AUX_DIR)/swiftaux.lbl
A2AUX_LC_RODATA_OBJ := $(patsubst src/%.c,$(A2AUX_DIR)/lcobj/%.o,$(A2_LC_RODATA_SRC))

$(A2AUX_DIR)/lcobj/%.o: src/%.c
	@mkdir -p $(dir $@)
	cc65 -t apple2 -O -Cl -Or -DWITH_SWIFTAUX --rodata-name LC $< -o $(@:.o=.s)
	ca65 -t apple2 $(@:.o=.s) -o $@

$(A2AUX_MAIN_BIN): $(A2AUX_PLATFORM_ASM_FIRST) $(A2_CRT0) $(A2_REST_SRC) $(A2AUX_LC_RODATA_OBJ) $(A2AUX_PLATFORM_ASM) src/platform/apple2/swiftaux-system.cfg $(A2_DATE_STAMP) | $(A2SAT_SYSTEM_BIN)
	@mkdir -p $(A2AUX_DIR)
	$(CL65) $(CL65FLAGS) -DWITH_SWIFTAUX -DWITH_IIE -DWITH_GR_TEXTWIN $(IIE_80COL_DEF) \
	  -C src/platform/apple2/swiftaux-system.cfg \
	  -o $(A2AUX_DIR)/SWIFTAUX \
	  -m $(A2AUX_MAP) \
	  -Ln $(A2AUX_LBL) \
	  $(A2AUX_PLATFORM_ASM_FIRST) $(A2_CRT0) $(A2_REST_SRC) $(A2AUX_LC_RODATA_OBJ) $(A2AUX_PLATFORM_ASM)
	@# Defence in depth: ensure the discarded/empty side outputs exist
	@# so downstream rules don't choke if a segment ever gates to empty.
	@test -f $(A2AUX_XLC_BIN)     || : > $(A2AUX_XLC_BIN)
	@test -f $(A2AUX_OVLASC_BIN)  || : > $(A2AUX_OVLASC_BIN)
	@test -f $(A2AUX_OVLCHR_BIN)  || : > $(A2AUX_OVLCHR_BIN)
	@test -f $(A2AUX_OVLCALL_BIN) || : > $(A2AUX_OVLCALL_BIN)
	@test -f $(A2AUX_OVLSIP_BIN)  || : > $(A2AUX_OVLSIP_BIN)
	@test -f $(A2AUX_OVLINT_BIN)  || : > $(A2AUX_OVLINT_BIN)
	@test -f $(A2AUX_OVLRML_BIN)  || : > $(A2AUX_OVLRML_BIN)
	@test -f $(A2AUX_OVLRMA_BIN)  || : > $(A2AUX_OVLRMA_BIN)
	@test -f $(A2AUX_OVLCON_BIN)  || : > $(A2AUX_OVLCON_BIN)
	@test -f $(A2AUX_OVLPMEM_BIN) || : > $(A2AUX_OVLPMEM_BIN)
	@test -f $(A2AUX_OVLPGR_BIN)  || : > $(A2AUX_OVLPGR_BIN)
	@test -f $(A2AUX_OVLSCC_BIN)  || : > $(A2AUX_OVLSCC_BIN)
	@test -f $(A2AUX_OVLNAR_BIN)  || : > $(A2AUX_OVLNAR_BIN)
	@test -f $(A2AUX_OVLALN_BIN)  || : > $(A2AUX_OVLALN_BIN)
	@find src -name '*.o' -delete

# ld65 emits these as side effects of the rule above (per `file =` in
# swiftaux-system.cfg); declare the dependency so Make knows.
$(A2AUX_OVLASC_BIN): $(A2AUX_MAIN_BIN)
$(A2AUX_OVLCHR_BIN): $(A2AUX_MAIN_BIN)
$(A2AUX_OVLCALL_BIN): $(A2AUX_MAIN_BIN)
$(A2AUX_OVLSIP_BIN): $(A2AUX_MAIN_BIN)
$(A2AUX_OVLINT_BIN): $(A2AUX_MAIN_BIN)
$(A2AUX_OVLRML_BIN): $(A2AUX_MAIN_BIN)
$(A2AUX_OVLRMA_BIN): $(A2AUX_MAIN_BIN)
$(A2AUX_OVLCON_BIN): $(A2AUX_MAIN_BIN)
$(A2AUX_OVLPMEM_BIN): $(A2AUX_MAIN_BIN)
$(A2AUX_OVLPGR_BIN): $(A2AUX_MAIN_BIN)
$(A2AUX_OVLSCC_BIN): $(A2AUX_MAIN_BIN)
$(A2AUX_OVLNAR_BIN): $(A2AUX_MAIN_BIN)
$(A2AUX_OVLALN_BIN): $(A2AUX_MAIN_BIN)

# pack argv order MUST match pack_swiftaux.py's BODY_ORDER.
$(A2AUX_SYSTEM_BIN): $(A2AUX_MAIN_BIN) $(A2AUX_OVLASC_BIN) $(A2AUX_OVLCHR_BIN) $(A2AUX_OVLCALL_BIN) $(A2AUX_OVLSIP_BIN) $(A2AUX_OVLINT_BIN) $(A2AUX_OVLRML_BIN) $(A2AUX_OVLRMA_BIN) $(A2AUX_OVLCON_BIN) $(A2AUX_OVLSCC_BIN) $(A2AUX_OVLNAR_BIN) $(A2AUX_OVLALN_BIN) $(A2AUX_OVLPMEM_BIN) $(A2AUX_OVLPGR_BIN) tools/host/diskimg/pack_swiftaux.py
	python3 tools/host/diskimg/pack_swiftaux.py $(A2AUX_MAIN_BIN) $(A2AUX_OVLASC_BIN) $(A2AUX_OVLCHR_BIN) $(A2AUX_OVLCALL_BIN) $(A2AUX_OVLSIP_BIN) $(A2AUX_OVLINT_BIN) $(A2AUX_OVLRML_BIN) $(A2AUX_OVLRMA_BIN) $(A2AUX_OVLCON_BIN) $(A2AUX_OVLSCC_BIN) $(A2AUX_OVLNAR_BIN) $(A2AUX_OVLALN_BIN) $(A2AUX_OVLPMEM_BIN) $(A2AUX_OVLPGR_BIN) $@

.PHONY: apple2-swiftaux
apple2-swiftaux: $(A2AUX_SYSTEM_BIN)

.PHONY: apple2-all
apple2-all: apple2 apple2-iie apple2-swiftsat apple2-swiftaux

# ---------------------------------------------------------------------------
# Boot launcher
#
# A standalone ProDOS SYS file (build/boot_launcher/SWIFTII, no .SYSTEM
# suffix) that ProDOS auto-launches first on boot. The launcher detects
# Saturn 128K + IIe aux RAM presence and chains via MLI OPEN/READ/
# CLOSE to SWIFTSAT (Saturn), SWIFTAUX (//e aux), or the lite binary
# (SWIFTIIP on the II+ disk, SWIFTIIE on the //e disk).
# Written in C
# (boot_launcher.c) with low-level helpers in ca65 (boot_launcher_asm.s);
# linked via cl65 with a custom cfg that omits the EXEHDR cc65's
# stock apple2-system would prepend. The budget is the full ProDOS-SYS
# load ceiling, 40,704 B (see BOOT_LAUNCHER_BUDGET below).
# ---------------------------------------------------------------------------

BOOT_LAUNCHER_BIN  := $(BOOT_LAUNCHER_DIR)/SWIFTII
BOOT_LAUNCHER_MAP  := $(BOOT_LAUNCHER_DIR)/boot_launcher.map
BOOT_LAUNCHER_LBL  := $(BOOT_LAUNCHER_DIR)/boot_launcher.lbl

# cc65 toolchain version (e.g. "2.18"), parsed from `cl65 --version` and baked
# into the launcher About screen via -DCC65_VER_STR so the displayed version
# can never drift from the compiler that actually built the binary.
CC65_VERSION := $(shell cl65 --version 2>&1 | sed -n 's/.*V\([0-9][0-9.]*\).*/\1/p')

# Per-disk boot launcher: the //e disk's launcher opens SWIFTIIE.SYSTEM as its
# lite fallback, the II+ disk's opens SWIFTIIP.SYSTEM (boot_launcher.c's
# -DLITE_IIE). Same source, two builds — one per disk image.
BOOT_LAUNCHER_IIE_BIN := $(BOOT_LAUNCHER_DIR)/iie/SWIFTII
BOOT_LAUNCHER_IIE_MAP := $(BOOT_LAUNCHER_DIR)/iie/boot_launcher.map
BOOT_LAUNCHER_IIE_LBL := $(BOOT_LAUNCHER_DIR)/iie/boot_launcher.lbl

# The //e AUX compiler disk's launcher (-DLITE_IIE -DFAMILYB_AUX). Same //e build
# as BOOT_LAUNCHER_IIE_BIN, but tags the Family B banner "...//e aux" so it
# differs from the non-aux //e compiler disk, whose COMPILER.SYSTEM has the same
# filename and so can't be told apart at run time (the //e analogue of the
# II+/Saturn split below).
BOOT_LAUNCHER_IIE_AUX_BIN := $(BOOT_LAUNCHER_DIR)/iie-aux/SWIFTII
BOOT_LAUNCHER_IIE_AUX_MAP := $(BOOT_LAUNCHER_DIR)/iie-aux/boot_launcher.map
BOOT_LAUNCHER_IIE_AUX_LBL := $(BOOT_LAUNCHER_DIR)/iie-aux/boot_launcher.lbl

# The II+ Saturn compiler disk's launcher (-DFAMILYB_SATURN). Same II+ build as
# BOOT_LAUNCHER_BIN (no LITE_IIE), but tags the Family B banner "...][+ Saturn"
# so it differs from the flat II+ compiler disk, whose COMPILER.SYSTEM has the
# same filename and so can't be told apart at run time.
BOOT_LAUNCHER_SAT_BIN := $(BOOT_LAUNCHER_DIR)/sat/SWIFTII
BOOT_LAUNCHER_SAT_MAP := $(BOOT_LAUNCHER_DIR)/sat/boot_launcher.map
BOOT_LAUNCHER_SAT_LBL := $(BOOT_LAUNCHER_DIR)/sat/boot_launcher.lbl

.PHONY: boot-launcher
boot-launcher: $(BOOT_LAUNCHER_BIN) $(BOOT_LAUNCHER_IIE_BIN) $(BOOT_LAUNCHER_IIE_AUX_BIN) $(BOOT_LAUNCHER_SAT_BIN)

# ---------------------------------------------------------------------------
# DEBUG.SYSTEM — the standalone detection diagnostic the boot
# launcher chains from its Debug menu. A tiny ProDOS SYS binary (load $2000,
# same crt0 + cfg as the interpreters) = debug.c + debug_asm.s; it does NOT
# link the launcher's object. Ships on every program disk; machine-independent
# (reads the same raw bytes), so one build serves both II+ and //e disks.
# ---------------------------------------------------------------------------
DEBUG_SYS_DIR := $(BUILD)/debug_sys
DEBUG_SYS_BIN := $(DEBUG_SYS_DIR)/DEBUG.SYSTEM

$(DEBUG_SYS_BIN): tools/apple2/debug_sys/debug.c tools/apple2/debug_sys/debug_asm.s \
                  $(A2_CRT0) src/platform/apple2/swiftii-system.cfg $(A2_DATE_STAMP)
	@mkdir -p $(DEBUG_SYS_DIR)
	$(CL65) -t apple2 -O -Cl \
	  -C src/platform/apple2/swiftii-system.cfg \
	  -o $@ \
	  -m $(DEBUG_SYS_DIR)/debug.map \
	  $(A2_CRT0) tools/apple2/debug_sys/debug.c tools/apple2/debug_sys/debug_asm.s
	@find tools/apple2/debug_sys -maxdepth 1 -name '*.o' -delete

.PHONY: debug-sys
debug-sys: $(DEBUG_SYS_BIN)

# TESTRUN.SYSTEM (design doc 018) — the on-target auto-test harness
# the boot launcher chains from its [T] command (and on boot-resume when a
# TESTRUN note is present). A standalone ProDOS SYS binary (load $2000) =
# testrun.c + the SHARED boot_launcher_asm.s (reusing its proven MLI verbs,
# hardware probes and chain loaders). Machine-independent — one build serves
# every disk (like DEBUG.SYSTEM); it detects this disk's interpreter at run
# time. The asm is built with no LITE_IIE / editor defines (it only needs the
# chain + MLI verbs); testrun.c supplies the 16 C globals the asm imports.
# ---------------------------------------------------------------------------
TESTRUN_SYS_DIR := $(BUILD)/testrun_sys
TESTRUN_SYS_BIN := $(TESTRUN_SYS_DIR)/TESTRUN.SYSTEM

# Order-only on the launcher bins so the three cl65 runs that compile the
# SHARED tools/apple2/boot_launcher/boot_launcher_asm.s don't race on its .o scratch
# under `make -j` (same guard the //e launcher uses on the II+ launcher).
$(TESTRUN_SYS_BIN): tools/apple2/testrun_sys/testrun.c tools/apple2/boot_launcher/boot_launcher_asm.s \
                    tools/apple2/testrun_sys/testrun.cfg $(A2_CRT0) src/common/config.h $(A2_DATE_STAMP) \
                    | $(BOOT_LAUNCHER_BIN) $(BOOT_LAUNCHER_IIE_BIN) $(BOOT_LAUNCHER_IIE_AUX_BIN) $(BOOT_LAUNCHER_SAT_BIN)
	@mkdir -p $(TESTRUN_SYS_DIR)
	$(CL65) -t apple2 -O -Cl \
	  -C tools/apple2/testrun_sys/testrun.cfg \
	  -o $@ \
	  -m $(TESTRUN_SYS_DIR)/testrun.map \
	  $(A2_CRT0) tools/apple2/testrun_sys/testrun.c tools/apple2/boot_launcher/boot_launcher_asm.s
	@find tools/apple2/testrun_sys -maxdepth 1 -name '*.o' -delete
	@rm -f tools/apple2/boot_launcher/boot_launcher_asm.o

.PHONY: testrun-sys
testrun-sys: $(TESTRUN_SYS_BIN)

# BOTH launchers link the editor in-process (BOOT_LAUNCHER_EDITOR_SRC). The //e
# launcher (below) builds the same sources with -DLITE_IIE, which selects the
# editor's native-case rendering/save path; the browser's RET/[E]/[F] open the
# editor on both disks.
$(BOOT_LAUNCHER_BIN): tools/apple2/boot_launcher/boot_launcher.c \
                  tools/apple2/boot_launcher/boot_launcher_asm.s \
                  $(BOOT_LAUNCHER_EDITOR_SRC) \
                  $(A2_SHARED_SRC) \
                  $(A2_CRT0) \
                  tools/apple2/boot_launcher/boot_launcher.cfg \
                  $(A2_DATE_STAMP)
	@mkdir -p $(BOOT_LAUNCHER_DIR)
	$(CL65) -t apple2 -O -Cl -DWITH_EDITOR -DWITH_SWB -DCC65_VER_STR='"$(CC65_VERSION)"' \
	  -C tools/apple2/boot_launcher/boot_launcher.cfg \
	  -o $@ \
	  -m $(BOOT_LAUNCHER_MAP) \
	  -Ln $(BOOT_LAUNCHER_LBL) \
	  $(A2_CRT0) \
	  tools/apple2/boot_launcher/boot_launcher.c \
	  tools/apple2/boot_launcher/boot_launcher_asm.s \
	  $(BOOT_LAUNCHER_EDITOR_SRC) \
	  $(A2_SHARED_SRC) $(BOOT_LAUNCHER_IO_SRC)
	@find tools/apple2/boot_launcher -maxdepth 1 -name '*.o' -delete
	@find src -name '*.o' -delete

# //e-disk launcher (-DLITE_IIE -> fallback name SWIFTIIE.SYSTEM). Also links
# the editor (LITE_IIE selects the //e native-case rendering/save path).
# Order-only on $(BOOT_LAUNCHER_BIN) so the two cl65 runs don't race on the
# shared src/*.o scratch under `make -j`.
$(BOOT_LAUNCHER_IIE_BIN): tools/apple2/boot_launcher/boot_launcher.c \
                      tools/apple2/boot_launcher/boot_launcher_asm.s \
                      $(BOOT_LAUNCHER_EDITOR_SRC) \
                      $(A2_SHARED_SRC) $(BOOT_LAUNCHER_IO_SRC) \
                      $(A2_CRT0) \
                      tools/apple2/boot_launcher/boot_launcher.cfg \
                      $(A2_DATE_STAMP) | $(BOOT_LAUNCHER_BIN)
	@mkdir -p $(dir $@)
	$(CL65) -t apple2 -O -Cl -DLITE_IIE --asm-define LITE_IIE -DWITH_EDITOR -DWITH_SWB -DCC65_VER_STR='"$(CC65_VERSION)"' \
	  -C tools/apple2/boot_launcher/boot_launcher.cfg \
	  -o $@ \
	  -m $(BOOT_LAUNCHER_IIE_MAP) \
	  -Ln $(BOOT_LAUNCHER_IIE_LBL) \
	  $(A2_CRT0) \
	  tools/apple2/boot_launcher/boot_launcher.c \
	  tools/apple2/boot_launcher/boot_launcher_asm.s \
	  $(BOOT_LAUNCHER_EDITOR_SRC) \
	  $(A2_SHARED_SRC) $(BOOT_LAUNCHER_IO_SRC)
	@find tools/apple2/boot_launcher -maxdepth 1 -name '*.o' -delete
	@find src -name '*.o' -delete

# //e AUX-compiler-disk launcher (-DLITE_IIE -DFAMILYB_AUX -> banner "...//e aux").
# Identical to the //e launcher above plus FAMILYB_AUX, which only swaps a banner
# string. Order-only on the other launcher bins so the shared src/*.o +
# boot_launcher_asm.o scratch don't race under `make -j`.
$(BOOT_LAUNCHER_IIE_AUX_BIN): tools/apple2/boot_launcher/boot_launcher.c \
                      tools/apple2/boot_launcher/boot_launcher_asm.s \
                      $(BOOT_LAUNCHER_EDITOR_SRC) \
                      $(A2_SHARED_SRC) $(BOOT_LAUNCHER_IO_SRC) \
                      $(A2_CRT0) \
                      tools/apple2/boot_launcher/boot_launcher.cfg \
                      $(A2_DATE_STAMP) | $(BOOT_LAUNCHER_BIN) $(BOOT_LAUNCHER_IIE_BIN)
	@mkdir -p $(dir $@)
	$(CL65) -t apple2 -O -Cl -DLITE_IIE --asm-define LITE_IIE -DFAMILYB_AUX -DWITH_EDITOR -DWITH_SWB -DCC65_VER_STR='"$(CC65_VERSION)"' \
	  -C tools/apple2/boot_launcher/boot_launcher.cfg \
	  -o $@ \
	  -m $(BOOT_LAUNCHER_IIE_AUX_MAP) \
	  -Ln $(BOOT_LAUNCHER_IIE_AUX_LBL) \
	  $(A2_CRT0) \
	  tools/apple2/boot_launcher/boot_launcher.c \
	  tools/apple2/boot_launcher/boot_launcher_asm.s \
	  $(BOOT_LAUNCHER_EDITOR_SRC) \
	  $(A2_SHARED_SRC) $(BOOT_LAUNCHER_IO_SRC)
	@find tools/apple2/boot_launcher -maxdepth 1 -name '*.o' -delete
	@find src -name '*.o' -delete

# II+ Saturn-compiler-disk launcher (-DFAMILYB_SATURN -> banner "...][+ Saturn").
# Otherwise identical to the II+ launcher (no LITE_IIE; the flag only swaps a
# banner string, so no --asm-define). Order-only on the other launcher bins so
# the shared src/*.o + boot_launcher_asm.o scratch don't race under `make -j`.
$(BOOT_LAUNCHER_SAT_BIN): tools/apple2/boot_launcher/boot_launcher.c \
                      tools/apple2/boot_launcher/boot_launcher_asm.s \
                      $(BOOT_LAUNCHER_EDITOR_SRC) \
                      $(A2_SHARED_SRC) $(BOOT_LAUNCHER_IO_SRC) \
                      $(A2_CRT0) \
                      tools/apple2/boot_launcher/boot_launcher.cfg \
                      $(A2_DATE_STAMP) | $(BOOT_LAUNCHER_BIN) $(BOOT_LAUNCHER_IIE_BIN) $(BOOT_LAUNCHER_IIE_AUX_BIN)
	@mkdir -p $(dir $@)
	$(CL65) -t apple2 -O -Cl -DFAMILYB_SATURN -DWITH_EDITOR -DWITH_SWB -DCC65_VER_STR='"$(CC65_VERSION)"' \
	  -C tools/apple2/boot_launcher/boot_launcher.cfg \
	  -o $@ \
	  -m $(BOOT_LAUNCHER_SAT_MAP) \
	  -Ln $(BOOT_LAUNCHER_SAT_LBL) \
	  $(A2_CRT0) \
	  tools/apple2/boot_launcher/boot_launcher.c \
	  tools/apple2/boot_launcher/boot_launcher_asm.s \
	  $(BOOT_LAUNCHER_EDITOR_SRC) \
	  $(A2_SHARED_SRC) $(BOOT_LAUNCHER_IO_SRC)
	@find tools/apple2/boot_launcher -maxdepth 1 -name '*.o' -delete
	@find src -name '*.o' -delete

# ---------------------------------------------------------------------------
# Editor — merged into the boot launcher
#
# The editor is no longer a standalone SWIFTED.SYSTEM binary: it links into the
# II+ boot launcher (BOOT_LAUNCHER_EDITOR_SRC, above) and
# the browser enters it in-process. editor.c exports editor_main(), not main(),
# so it no longer links on its own. The launcher's size is covered by the `size`
# target; the editor modules are still unit-tested on the host (EDITOR_PORTABLE_SRC
# in the test build).
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Disk image set (ProDOS 2.4.3 .po)
#
# SwiftII ships a NINE-DISK distribution: four single-interpreter REPL
# program disks, four Family B compiler program disks, and one non-boot DATA
# disk. Each REPL program disk carries the boot launcher (auto-launched as
# SWIFTII.SYSTEM) + exactly ONE interpreter; each compiler disk carries the
# launcher + Compiler + matching Runner. One binary per disk (rather than
# packing several onto one) keeps the II+ boot disk off its 0-bytes-free wall
# and leaves room for the launcher to grow (e.g. the file-browser preview).
#
#   disk-iip-lite-repl  swiftii-iip-lite-repl.po  II+ launcher + SWIFTIIP  (II/II+, //+ typing)
#   disk-iip-sat-repl   swiftii-iip-sat-repl.po   II+ launcher + SWIFTSAT  (Saturn 128K extras)
#   disk-iie-lite-repl  swiftii-iie-lite-repl.po  //e launcher + SWIFTIIE  (//e native case)
#   disk-iie-aux-repl   swiftii-iie-aux-repl.po   //e launcher + SWIFTAUX  (//e 64K-aux extras)
#   disk-iip-compiler      swiftii-iip-compiler.po       II+ launcher + flat Compiler/Runner   (Tier 1, any II+)
#   disk-iie-compiler      swiftii-iie-compiler.po       //e launcher + //e flat Compiler/Runner (Tier 1, any //e, fw 80-col)
#   disk-iie-aux-compiler  swiftii-iie-aux-compiler.po   //e launcher + aux-paged Compiler/Runner (Tier 3, //e 64K aux)
#   disk-iip-sat-compiler  swiftii-iip-sat-compiler.po   II+ launcher + Saturn-paged Compiler/Runner (Tier 2, II+ Saturn)
#   disk-data      swiftii-data.po      samples + on-disk TESTS/ (non-boot, drive 2)
#
# `make disks` builds all nine. The demo SAMPLES/ ALSO ship on each program disk
# (so a single-drive user has runnable examples without the data disk); the
# dev-facing TESTS/ stay on the data disk only (they would fill the tight
# iip-sat image). A user authors new programs with the browser's [F] new-file,
# saving to the program disk or the mounted data disk.
# ---------------------------------------------------------------------------

PO_IIP_LITE := $(DISK_DIR)/swiftii-iip-lite-repl.po
PO_IIP_SAT  := $(DISK_DIR)/swiftii-iip-sat-repl.po
PO_IIE_LITE := $(DISK_DIR)/swiftii-iie-lite-repl.po
PO_IIE_AUX  := $(DISK_DIR)/swiftii-iie-aux-repl.po
PO_IMAGE_DATA := $(DISK_DIR)/swiftii-data.po
# Family B (Pascal-style compile-from-disk) program disks: launcher
# (+ in-process editor) + the shared Compiler + the per-machine Runner, no REPL
# interpreter. doc 015.
PO_IIP_COMP := $(DISK_DIR)/swiftii-iip-compiler.po
# //e Tier-1 (non-aux): the //e-native flat Compiler/Runner — WITH_IIE rendering
# + firmware 80-col Runner, NO aux paging — for a //e without the 64K extended
# aux card (incl. a plain 1K 80-Column Text Card machine).
PO_IIE_COMP := $(DISK_DIR)/swiftii-iie-compiler.po
# //e Tier-3 (aux): the aux-paged Compiler/Runner — needs the //e 64K extended
# 80-col (aux) card; lifts the program ceiling past the flat 1,834 B cap.
PO_IIE_AUXCOMP := $(DISK_DIR)/swiftii-iie-aux-compiler.po
# Tier 2 (II+ Saturn 128K): same II+ launcher, but the SATURN-paged Compiler +
# Runner (Saturn banks back the bytecode store), so big programs compile + run
# past the flat 1,834 B cap on a Saturn machine.
PO_IIP_SATCOMP := $(DISK_DIR)/swiftii-iip-sat-compiler.po

# Sample sources are split by DESTINATION: files that ship on the
# program/binary disks live under progdisk/; files that are exclusive to the
# data disk live under datadisk/. No source file is duplicated across the two.
#   progdisk/samples   = regular programs that run on ANY system (incl. lite REPL)
#   progdisk/xsamples  = x-prefixed EXTRAS-REPL demos (graphics/sound/games via
#                        peek/poke/gr/text80...) small enough to ship on a program
#                        disk — they run on the SWIFTSAT/SWIFTAUX extras REPL AND
#                        on any Family B Runner
#   progdisk/fbsamples = x-prefixed FAMILY-B-ONLY demos (random(in:)/switch/for-in)
#                        — these REJECT on every Family A REPL (the dialect fork),
#                        so they must NEVER ship on an extras REPL disk; they ride
#                        only on the data disk (the Family B program disks ship a
#                        minimal inline set that excludes them)
#   datadisk/xsamples  = the oversize showcases (xbig, xgrdemo, xfuncs) — too big for the
#                        2 KB Family-A staging cap, so they ship ONLY on the data
#                        disk (drive 2, streamed by the Family B Compiler)
# Lite REPL disks ship only progdisk/samples; the extras REPL disks add
# progdisk/xsamples (NOT fbsamples — those can't run on a REPL). The DATA disk
# image is the canonical FULL set: it assembles SAMPLES/ + XSAMPLES/ from ALL
# trees (program-disk samples are referenced from progdisk/, not copied into
# datadisk/).
SAMPLES_DIR := progdisk/samples
SAMPLES_EXTRAS_DIR := progdisk/xsamples
# Family-B-only extras (random/switch/for-in): data-disk-only, never a REPL disk.
FB_SAMPLES_DIR := progdisk/fbsamples
# Data-disk-only extras (oversize showcases); folded into the data disk's
# XSAMPLES/ alongside the program-disk extras above.
DATA_SAMPLES_EXTRAS_DIR := datadisk/xsamples
# On-disk Help text (was the launcher's Help menu screen, moved out to reclaim
# launcher code space). build_po.sh drops the chosen file into the disk root as
# README.TXT; the About screen points the user there. There is ONE canonical
# (mixed-case) source per family — REPL vs Family B compiler — so the help text
# can't drift between builds. build_po.sh derives each disk's copy from it:
# //e disks ship it as-is (the //e shows lowercase natively); II+ disks get an
# ALL-CAPS fold (README_UPPER=1), exactly as the launcher's cout_str folds the
# About screen on the II+. The `@YEAR@` placeholder is substituted from
# src/common/version.h (README_YEAR) so the copyright matches the About page.
README_FILE_REPL     := progdisk/readme-repl.txt
README_FILE_COMPILER := progdisk/readme-compiler.txt
# Release year, single-sourced from version.h (matches the About screen).
SWIFTII_YEAR := $(shell sed -n 's/.*SWIFTII_YEAR[^"]*"\([0-9]*\)".*/\1/p' src/common/version.h)
# Build timestamp stamped into each disk's README.TXT (@BUILT@). Format and
# timezone match the launcher's About page, which prints C's `__DATE__ __TIME__`
# ("Mmm DD YYYY HH:MM:SS", local time): %e is space-padded like __DATE__'s day,
# and no -u so it stays local. Evaluated once per make invocation so every disk
# in a `make disks` run shares one stamp.
SWIFTII_BUILT := $(shell date '+%b %e %Y %H:%M:%S')
# Program-disk sample sources (prereqs for the program-disk targets).
SAMPLE_SWIFTS := $(wildcard progdisk/samples/*.swift) $(wildcard progdisk/xsamples/*.swift) $(wildcard progdisk/fbsamples/*.swift)
# Data-disk-only sample sources (the oversize showcases).
DATA_SAMPLE_SWIFTS := $(wildcard datadisk/xsamples/*.swift)
# On-disk tests are tiered by capability (docs 017, 018), nested under one
# TESTS/ tree (the harness walks a single root):
#   datadisk/tests/core     -> TESTS/CORE/     general  (any REPL)
#   datadisk/tests/xtests   -> TESTS/XTESTS/   extras   (SWIFTSAT / SWIFTAUX REPL)
#   datadisk/tests/fbtests  -> TESTS/FBTESTS/  Family B (compiler disk + this data
#                                              disk in drive 2; file builtins)
#   datadisk/tests/errtests -> TESTS/ERRTESTS/ error-message DEMOS (deliberately
#                                  fail; run with [X] on a compiler disk — not self-checking)
TESTS_DIR := datadisk/tests/core
EXTRAS_TESTS_DIR := datadisk/tests/xtests
FB_TESTS_DIR := datadisk/tests/fbtests
ERR_TESTS_DIR := datadisk/tests/errtests
TEST_SWIFTS := $(wildcard datadisk/tests/core/*.swift) \
               $(wildcard datadisk/tests/xtests/*.swift) \
               $(wildcard datadisk/tests/fbtests/*.swift) \
               $(wildcard datadisk/tests/errtests/*.swift)

.PHONY: disks disk-iip-lite-repl disk-iip-sat-repl disk-iie-lite-repl disk-iie-aux-repl disk-data \
        disk-iip-compiler disk-iie-compiler disk-iie-aux-compiler disk-iip-sat-compiler disks-familyb
disks: disk-iip-lite-repl disk-iip-sat-repl disk-iie-lite-repl disk-iie-aux-repl disk-data \
       disk-iip-compiler disk-iie-compiler disk-iie-aux-compiler disk-iip-sat-compiler

disk-iip-lite-repl: $(PO_IIP_LITE)
disk-iip-sat-repl:  $(PO_IIP_SAT)
disk-iie-lite-repl: $(PO_IIE_LITE)
disk-iie-aux-repl:  $(PO_IIE_AUX)
disk-iip-compiler: $(PO_IIP_COMP)
disk-iie-compiler: $(PO_IIE_COMP)
disk-iie-aux-compiler: $(PO_IIE_AUXCOMP)
disk-iip-sat-compiler: $(PO_IIP_SATCOMP)
disks-familyb: disk-iip-compiler disk-iie-compiler disk-iie-aux-compiler disk-iip-sat-compiler

# SwiftII version, read from the single source of truth in src/common/version.h.
VERSION := $(shell sed -n 's/.*SWIFTII_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' src/common/version.h)

# Stage the nine distribution images under releases/v<version>/ for tagging +
# upload to GitHub Releases. The per-version folder is exempted from the *.po
# gitignore (see .gitignore !releases/**/*.po), so a tagged image set is
# committed; loose build/disk/*.po stay ignored. Each copy gets a -v<version>
# suffix (e.g. swiftii-iip-lite-repl-v1.0.1.po) so a single .po downloaded on
# its own from GitHub Releases still carries its version out of the folder.
.PHONY: release
release: disks
	@mkdir -p releases/v$(VERSION)
	@for po in build/disk/*.po; do \
		cp "$$po" "releases/v$(VERSION)/$$(basename "$$po" .po)-v$(VERSION).po"; \
	done
	@echo "release: staged $$(ls releases/v$(VERSION)/*.po 2>/dev/null | wc -l | tr -d ' ') images into releases/v$(VERSION)/"

# II+ program disks use the II+ boot launcher (no -DLITE_IIE). The demo SAMPLES/
# ship on every program disk so a single-drive user has runnable examples; the
# dev-facing TESTS/ stay on the data disk only. The LITE disks ship only the
# regular samples ($(SAMPLES_DIR)) — the x-prefixed extras programs can't run on
# a lite REPL, so they're omitted; the EXTRAS disks (SWIFTSAT / SWIFTAUX) add
# $(SAMPLES_EXTRAS_DIR) (the extras-REPL x-programs). They do NOT add
# $(FB_SAMPLES_DIR): those are Family-B-only (random/switch/for-in) and would
# reject on any REPL — they ride the data disk only. The oversize showcases
# (xbig/xgrdemo/xfuncs) are also data-disk-only — they live under datadisk/xsamples, not
# progdisk, so they're never passed to a program disk (the 2 KB staging limit
# would skip them anyway).
$(PO_IIP_LITE): $(BOOT_LAUNCHER_BIN) $(A2_SYSTEM_BIN) $(DEBUG_SYS_BIN) $(SAMPLE_SWIFTS) $(README_FILE_REPL) src/common/version.h
	@mkdir -p $(DISK_DIR)
	SAMPLES_DIR=$(SAMPLES_DIR) DEBUG_BIN=$(DEBUG_SYS_BIN) README_FILE=$(README_FILE_REPL) README_UPPER=1 README_YEAR=$(SWIFTII_YEAR) README_VERSION=$(VERSION) README_BUILT="$(SWIFTII_BUILT)" bash tools/host/diskimg/build_po.sh $(BOOT_LAUNCHER_BIN) $@ SWIFTIIP.SYSTEM $(A2_SYSTEM_BIN)

$(PO_IIP_SAT): $(BOOT_LAUNCHER_BIN) $(A2SAT_SYSTEM_BIN) $(DEBUG_SYS_BIN) $(SAMPLE_SWIFTS) $(README_FILE_REPL) src/common/version.h
	@mkdir -p $(DISK_DIR)
	SAMPLES_DIR=$(SAMPLES_DIR) SAMPLES_EXTRAS_DIR=$(SAMPLES_EXTRAS_DIR) DEBUG_BIN=$(DEBUG_SYS_BIN) README_FILE=$(README_FILE_REPL) README_UPPER=1 README_YEAR=$(SWIFTII_YEAR) README_VERSION=$(VERSION) README_BUILT="$(SWIFTII_BUILT)" bash tools/host/diskimg/build_po.sh $(BOOT_LAUNCHER_BIN) $@ SWIFTSAT.SYSTEM $(A2SAT_SYSTEM_BIN)

# //e program disks use the //e boot launcher (-DLITE_IIE -> native case + the
# 80-col path + SWIFTIIE fallback name).
$(PO_IIE_LITE): $(BOOT_LAUNCHER_IIE_BIN) $(A2IIE_SYSTEM_BIN) $(DEBUG_SYS_BIN) $(SAMPLE_SWIFTS) $(README_FILE_REPL) src/common/version.h
	@mkdir -p $(DISK_DIR)
	SAMPLES_DIR=$(SAMPLES_DIR) DEBUG_BIN=$(DEBUG_SYS_BIN) README_FILE=$(README_FILE_REPL) README_YEAR=$(SWIFTII_YEAR) README_VERSION=$(VERSION) README_BUILT="$(SWIFTII_BUILT)" bash tools/host/diskimg/build_po.sh $(BOOT_LAUNCHER_IIE_BIN) $@ SWIFTIIE.SYSTEM $(A2IIE_SYSTEM_BIN)

$(PO_IIE_AUX): $(BOOT_LAUNCHER_IIE_BIN) $(A2AUX_SYSTEM_BIN) $(DEBUG_SYS_BIN) $(SAMPLE_SWIFTS) $(README_FILE_REPL) src/common/version.h
	@mkdir -p $(DISK_DIR)
	SAMPLES_DIR=$(SAMPLES_DIR) SAMPLES_EXTRAS_DIR=$(SAMPLES_EXTRAS_DIR) DEBUG_BIN=$(DEBUG_SYS_BIN) README_FILE=$(README_FILE_REPL) README_YEAR=$(SWIFTII_YEAR) README_VERSION=$(VERSION) README_BUILT="$(SWIFTII_BUILT)" bash tools/host/diskimg/build_po.sh $(BOOT_LAUNCHER_IIE_BIN) $@ SWIFTAUX.SYSTEM $(A2AUX_SYSTEM_BIN)

# Family B program disks (doc 015): launcher + the machine-independent
# Compiler (COMPILER.SYSTEM) + the per-machine Runner (RUNNER.SYSTEM); no REPL
# interpreter, which is what frees the room for the toolchain. The editor ships
# inside the launcher. SAMPLES/ ride along as source to compile. The
# II+ disk uses the II+ launcher + II+ Runner; the //e disk the //e launcher +
# //e Runner. The shared Compiler binary is identical on both.
# SAMPLE_SRC_LIMIT=0: the Compiler streams source from disk (doc 016), so a
# Family B disk could carry sources past the 2 KB Family-A staging cap.
# SAMPLES_ONLY: a MINIMAL inline set — the canonical
# FULL sample set + the tiered on-disk tests now live on the universal data disk
# (drive 2, `disk-data`), which every Family B disk's *-2disk run target mounts.
# Shipping only three small programs frees ~10 KB of volume slack for the
# Compiler's .swb output (the old 6-sample sets left ~5-6 KB → MLI $48 disk-full
# on bigger compiles). One uniform set across all three tiers — a hello, a
# function-call + arithmetic demo, and a game — all with bytecode well under the
# tightest (Saturn 640 B) window, so each compiles on EVERY tier (measured via
# `make swbc`: greet 53 B, functions 122 B, xsnake 457 B). (TESTRUN.SYSTEM,
# design doc 018, lives on the data disk — not the program disks — so it costs
# the tight compiler disks nothing.) The tier paging showcases are data-disk-only
# now; two-drive users get the full set + the FBTESTS file-CRUD suite.
FAMILYB_SAMPLES := greet.swift functions.swift xsnake.swift
$(PO_IIP_COMP): $(BOOT_LAUNCHER_BIN) $(A2COMP_BIN) $(A2RUN_BIN) $(DEBUG_SYS_BIN) $(SAMPLE_SWIFTS) $(README_FILE_COMPILER) src/common/version.h
	@mkdir -p $(DISK_DIR)
	SAMPLES_DIR=$(SAMPLES_DIR) SAMPLES_EXTRAS_DIR=$(SAMPLES_EXTRAS_DIR) SAMPLE_SRC_LIMIT=0 SAMPLES_ONLY="$(FAMILYB_SAMPLES)" DEBUG_BIN=$(DEBUG_SYS_BIN) README_FILE=$(README_FILE_COMPILER) README_RUNNER="RUNNER   runs the .swb on any\n    II+ (no extra card)." README_UPPER=1 README_YEAR=$(SWIFTII_YEAR) README_VERSION=$(VERSION) README_BUILT="$(SWIFTII_BUILT)" bash tools/host/diskimg/build_po.sh $(BOOT_LAUNCHER_BIN) $@ COMPILER.SYSTEM $(A2COMP_BIN) RUNNER.SYSTEM $(A2RUN_BIN)

# //e (Tier 1, non-aux) disk: the //e-native FLAT Compiler + flat Runner — the
# Runner loads the whole .swb into MAIN (no aux paging) and drives the //e
# FIRMWARE 80-col, the Compiler renders via the //e char ROM (WITH_IIE). Runs on
# any //e, including one with only the 1K 80-Column Text Card — the gap the II+
# fallback left (no //e 80-col, inverse-video case hack). Flat 1,834 B program
# cap; a //e with the 64K extended aux card uses the aux disk below for big
# programs.
$(PO_IIE_COMP): $(BOOT_LAUNCHER_IIE_BIN) $(A2IIECOMP_BIN) $(A2IIERUN_BIN) $(DEBUG_SYS_BIN) $(SAMPLE_SWIFTS) $(README_FILE_COMPILER) src/common/version.h
	@mkdir -p $(DISK_DIR)
	SAMPLES_DIR=$(SAMPLES_DIR) SAMPLES_EXTRAS_DIR=$(SAMPLES_EXTRAS_DIR) SAMPLE_SRC_LIMIT=0 SAMPLES_ONLY="$(FAMILYB_SAMPLES)" DEBUG_BIN=$(DEBUG_SYS_BIN) README_FILE=$(README_FILE_COMPILER) README_RUNNER="RUNNER   runs the .swb on any\n    //e (no extra card)." README_YEAR=$(SWIFTII_YEAR) README_VERSION=$(VERSION) README_BUILT="$(SWIFTII_BUILT)" bash tools/host/diskimg/build_po.sh $(BOOT_LAUNCHER_IIE_BIN) $@ COMPILER.SYSTEM $(A2IIECOMP_BIN) RUNNER.SYSTEM $(A2IIERUN_BIN)

# //e (Tier 3, aux) disk: the AUX-PAGED Compiler + aux-paged Runner — both lift
# the bytecode ceiling via the aux store, so a function-heavy program (e.g.
# xfuncs on the data disk) compiles + runs far past the flat 1,834 B cap. Needs
# the //e 64K extended 80-col (aux) card.
$(PO_IIE_AUXCOMP): $(BOOT_LAUNCHER_IIE_AUX_BIN) $(A2IIEAUXCOMP_BIN) $(A2IIEAUXRUN_BIN) $(DEBUG_SYS_BIN) $(SAMPLE_SWIFTS) $(README_FILE_COMPILER) src/common/version.h
	@mkdir -p $(DISK_DIR)
	SAMPLES_DIR=$(SAMPLES_DIR) SAMPLES_EXTRAS_DIR=$(SAMPLES_EXTRAS_DIR) SAMPLE_SRC_LIMIT=0 SAMPLES_ONLY="$(FAMILYB_SAMPLES)" DEBUG_BIN=$(DEBUG_SYS_BIN) README_FILE=$(README_FILE_COMPILER) README_RUNNER="RUNNER   runs the .swb on a //e\n    with a 64K aux card." README_YEAR=$(SWIFTII_YEAR) README_VERSION=$(VERSION) README_BUILT="$(SWIFTII_BUILT)" bash tools/host/diskimg/build_po.sh $(BOOT_LAUNCHER_IIE_AUX_BIN) $@ COMPILER.SYSTEM $(A2IIEAUXCOMP_BIN) RUNNER.SYSTEM $(A2IIEAUXRUN_BIN)

# Tier 2 (II+ Saturn) disk: the same II+ launcher, but the Saturn-paged Compiler
# + Runner. Like the //e disk it pages bytecode (here into Saturn banks), so a
# function-heavy program (xfuncs on the data disk) compiles + runs past the flat
# 1,834 B cap — on a II+ with a Saturn 128K card. Same minimal inline set as the
# other Family B disks; the paging showcases live on the data disk (drive 2).
$(PO_IIP_SATCOMP): $(BOOT_LAUNCHER_SAT_BIN) $(A2SATCOMP_BIN) $(A2SATRUN_BIN) $(DEBUG_SYS_BIN) $(SAMPLE_SWIFTS) $(README_FILE_COMPILER) src/common/version.h
	@mkdir -p $(DISK_DIR)
	SAMPLES_DIR=$(SAMPLES_DIR) SAMPLES_EXTRAS_DIR=$(SAMPLES_EXTRAS_DIR) SAMPLE_SRC_LIMIT=0 SAMPLES_ONLY="$(FAMILYB_SAMPLES)" DEBUG_BIN=$(DEBUG_SYS_BIN) README_FILE=$(README_FILE_COMPILER) README_RUNNER="RUNNER   runs the .swb on a II+\n    with a Saturn 128K card." README_UPPER=1 README_YEAR=$(SWIFTII_YEAR) README_VERSION=$(VERSION) README_BUILT="$(SWIFTII_BUILT)" bash tools/host/diskimg/build_po.sh $(BOOT_LAUNCHER_SAT_BIN) $@ COMPILER.SYSTEM $(A2SATCOMP_BIN) RUNNER.SYSTEM $(A2SATRUN_BIN)

# Sample/data disk: a non-boot ProDOS volume holding the demo programs under
# SAMPLES/ and the self-checking unit tests under TESTS/, kept off the
# program disks. Mount it as a second drive to browse/run/edit them
# (`make run-mari-iip-2disk`).
disk-data: $(PO_IMAGE_DATA)
$(PO_IMAGE_DATA): $(SAMPLE_SWIFTS) $(DATA_SAMPLE_SWIFTS) $(TEST_SWIFTS) $(TESTRUN_SYS_BIN)
	@mkdir -p $(DISK_DIR)
	SAMPLES_EXTRAS_DIR="$(SAMPLES_EXTRAS_DIR) $(FB_SAMPLES_DIR) $(DATA_SAMPLES_EXTRAS_DIR)" TESTRUN_BIN=$(TESTRUN_SYS_BIN) bash tools/host/diskimg/build_data_po.sh $@ $(SAMPLES_DIR) \
	  $(TESTS_DIR) $(EXTRAS_TESTS_DIR) $(FB_TESTS_DIR) $(ERR_TESTS_DIR)

# ---------------------------------------------------------------------------
# Run / size / sim
# ---------------------------------------------------------------------------

# Default the emulator to the prebuilt binary committed at the repo root
# (./izapple2sdl_mac_arm64) when it's present, so the run-iz-* targets work with
# no setup. Falls back to `izapple2sdl` on PATH if that file isn't there (e.g.
# Intel macs / a system install). Override either way with
# `make run-iz-iip IZAPPLE2=/path/to/it` — `?=` leaves a user value untouched.
IZAPPLE2 ?= $(if $(wildcard ./izapple2sdl_mac_arm64),./izapple2sdl_mac_arm64,izapple2sdl)

# Export the emulator overrides so both `VAR=v make run-…` (env) and
# `make run-… VAR=v` (command-line make var) reach the run scripts. Undefined
# ones export empty, which the scripts' `${VAR:-default}` treats as unset.
export IZAPPLE2 IZAPPLE2_EXTRA_ARGS

# `make run` boots the II+ lite disk (the broadest-compatibility image — runs
# on every Apple II generation). Pick another disk with `make run-sat` /
# `run-iie` / `run-aux`, or the per-machine run-iz-* / run-mari-* below.
.PHONY: run run-sat run-iie run-aux
run: disk-iip-lite-repl
	bash emulator/run.sh $(PO_IIP_LITE)
run-sat: disk-iip-sat-repl
	bash emulator/run.sh $(PO_IIP_SAT)
run-iie: disk-iie-lite-repl
	bash emulator/run.sh $(PO_IIE_LITE)
run-aux: disk-iie-aux-repl
	bash emulator/run.sh $(PO_IIE_AUX)

# Two-drive Mariani targets — program disk in drive 1, the data disk (samples +
# TESTS/ + your saved programs) in drive 2. Mariani's machine is a GUI setting,
# so the disk image is all the target picks; configure the matching model in
# Mariani's GUI. To run the on-disk unit tests: boot, `3` FILES -> the data disk
# -> enter TESTS/ -> RET a file -> read the last line (`fail 0`). These mirror
# the izapple2 run-iz-*-2disk targets with the `-2disk` suffix (and DATA_DISK=
# still works on any run target by hand).
.PHONY: run-mari-iip-2disk run-mari-sat-2disk run-mari-iie-2disk run-mari-aux-2disk
run-mari-iip-2disk: disk-iip-lite-repl disk-data
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run.sh $(PO_IIP_LITE)
run-mari-sat-2disk: disk-iip-sat-repl disk-data
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run.sh $(PO_IIP_SAT)
run-mari-iie-2disk: disk-iie-lite-repl disk-data
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run.sh $(PO_IIE_LITE)
run-mari-aux-2disk: disk-iie-aux-repl disk-data
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run.sh $(PO_IIE_AUX)

# Mariani per-machine targets. Mariani (AppleWin/macOS) has NO command-line
# machine config — model/Saturn/aux are GUI settings it persists — so these
# just mount the matching single-interpreter disk and print the machine you
# must select in Mariani's config first.
.PHONY: run-mari-iip run-mari-sat run-mari-iie run-mari-aux
run-mari-iip: disk-iip-lite-repl
	@echo ">>> In Mariani, configure: Apple ][+ , 64K (16K Language Card), NO Saturn  -> SWIFTIIP"
	bash emulator/run.sh $(PO_IIP_LITE)
run-mari-sat: disk-iip-sat-repl
	@echo ">>> In Mariani, configure: Apple ][+ WITH Saturn 128K  -> SWIFTSAT"
	bash emulator/run.sh $(PO_IIP_SAT)
run-mari-iie: disk-iie-lite-repl
	@echo ">>> In Mariani, configure: Apple //e , 64K, NO aux/80-col card  -> SWIFTIIE"
	bash emulator/run.sh $(PO_IIE_LITE)
run-mari-aux: disk-iie-aux-repl
	@echo ">>> In Mariani, configure: Apple //e WITH Extended 80-Column Card (128K)  -> SWIFTAUX"
	bash emulator/run.sh $(PO_IIE_AUX)

# Mariani Family B compiler-runner targets — the GUI-configured-machine mirror of
# the izapple2 run-iz-compiler* set. Each mounts the matching compiler disk (and,
# for the -2disk variants, the data disk in drive 2 for the on-disk Family B
# tests) and prints the Mariani model to select first. Naming mirrors izapple2:
# run-mari-compiler[-iie|-sat] + a `-2disk` suffix for the data-disk variant.
.PHONY: run-mari-compiler run-mari-compiler-2disk \
        run-mari-compiler-iie run-mari-compiler-iie-2disk \
        run-mari-compiler-iie-aux run-mari-compiler-iie-aux-2disk \
        run-mari-compiler-sat run-mari-compiler-sat-2disk
run-mari-compiler: disk-iip-compiler
	@echo ">>> In Mariani, configure: Apple ][+ , 64K (16K Language Card), NO Saturn  -> COMPILER/RUNNER"
	bash emulator/run.sh $(PO_IIP_COMP)
run-mari-compiler-2disk: disk-iip-compiler disk-data
	@echo ">>> In Mariani, configure: Apple ][+ , 64K (16K Language Card), NO Saturn  -> COMPILER/RUNNER"
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run.sh $(PO_IIP_COMP)
run-mari-compiler-iie: disk-iie-compiler
	@echo ">>> In Mariani, configure: Apple //e , NO Extended 80-Col (64K)  -> COMPILER/RUNNER (Tier 1, fw 80-col)"
	bash emulator/run.sh $(PO_IIE_COMP)
run-mari-compiler-iie-2disk: disk-iie-compiler disk-data
	@echo ">>> In Mariani, configure: Apple //e , NO Extended 80-Col (64K)  -> COMPILER/RUNNER (Tier 1, fw 80-col)"
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run.sh $(PO_IIE_COMP)
run-mari-compiler-iie-aux: disk-iie-aux-compiler
	@echo ">>> In Mariani, configure: Apple //e WITH Extended 80-Column Card (128K)  -> COMPILER/RUNNER (Tier 3, aux-paged)"
	bash emulator/run.sh $(PO_IIE_AUXCOMP)
run-mari-compiler-iie-aux-2disk: disk-iie-aux-compiler disk-data
	@echo ">>> In Mariani, configure: Apple //e WITH Extended 80-Column Card (128K)  -> COMPILER/RUNNER (Tier 3, aux-paged)"
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run.sh $(PO_IIE_AUXCOMP)
run-mari-compiler-sat: disk-iip-sat-compiler
	@echo ">>> In Mariani, configure: Apple ][+ WITH Saturn 128K  -> COMPILER/RUNNER"
	bash emulator/run.sh $(PO_IIP_SATCOMP)
run-mari-compiler-sat-2disk: disk-iip-sat-compiler disk-data
	@echo ">>> In Mariani, configure: Apple ][+ WITH Saturn 128K  -> COMPILER/RUNNER"
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run.sh $(PO_IIP_SATCOMP)

# (No supported emulator models the //e basic-80-col card or a bare-64K //e;
# those configs are real-hardware-only — see ROADMAP "Maybe".)

# izapple2 run targets. It models a Saturn 128K, ships EMBEDDED ROMs (no
# version-locked romset) and is a single binary — the lowest-friction
# emulator. Profiles map in emulator/run_izapple2.sh:
#   run-iz-iip    -model=2plus -s0 language           -> SWIFTIIP  (iip-lite disk)
#   run-iz-sat    -model=2plus -s0 saturn             -> SWIFTSAT  (iip-sat disk)
#   run-iz-videx  -model=2plus -s0 saturn -s3 videx   -> SWIFTSAT  Videx 80-col (iip-sat disk)
#   run-iz-iie    -model=2e                           -> SWIFTIIE  (iie-lite disk)
#   run-iz-iienh  -model=2enh                         -> SWIFTAUX  (iie-aux disk, 65C02)
# Needs the `izapple2sdl` binary on PATH (github.com/ivanizag/izapple2/releases)
# or IZAPPLE2=/path/to/it.
# NOTE: izapple2's //e is always 128K, so `iie`/`iienh` exercise the SWIFTAUX
# (extended-80-col) case only — bare-64K / basic-80-col //e need real hardware
# (no supported emulator models them — see ROADMAP "Maybe"). izapple2 DOES
# run SWIFTSAT on its Saturn (input + output work). One OPEN cosmetic issue:
# typed keys don't echo while typing on SWIFTSAT under izapple2 (not on lite);
# input is still captured — see LESSONS 2026-06-01 / docs/testing/TESTING.md.
# Edge/negative cases (izapple2):
#   run-iz-ii      -model=2 -s0 language                  Original ][ Integer-BASIC ROM. Boots + runs
#                                                         programs on every binary (custom crt0_ibasic.s
#                                                         uses cc65 _memcpy for the LC copy instead of the
#                                                         ][+-only $D39A BLTU2; SWIFTSAT included). `C600G`.
#   run-iz-iip48   -model=2plus (48K, no LC)              NEGATIVE: ProDOS needs 64K
#   run-iz-sat-s4  -model=2plus -s0 language -s4 saturn   Saturn in a non-zero slot
#   run-iz-memexp  -model=2plus -s0 language -s4 memexp   non-Saturn RAM must NOT pick extras
.PHONY: run-iz-ii run-iz-iip run-iz-sat run-iz-videx run-iz-iie run-iz-iienh \
        run-iz-iip48 run-iz-sat-s4 run-iz-memexp \
        run-iz-iip-2disk run-iz-sat-2disk run-iz-videx-2disk run-iz-iie-2disk \
        run-iz-iienh-2disk run-iz-compiler run-iz-compiler-iie run-iz-compiler-iie-aux \
        run-iz-compiler-2disk run-iz-compiler-iie-2disk run-iz-compiler-iie-aux-2disk \
        run-iz-compiler-sat run-iz-compiler-sat-2disk
# Family B compiler disks (doc 015). Boot to the launcher; pick a
# .swift in the file selector and press [X] to compile + run it.
run-iz-compiler: disk-iip-compiler
	bash emulator/run_izapple2.sh iip $(PO_IIP_COMP)
run-iz-compiler-iie: disk-iie-compiler
	bash emulator/run_izapple2.sh iie $(PO_IIE_COMP)
# //e Tier-3 (aux) compiler disk under the `iienh` profile (enhanced //e, 128K
# w/ aux). The aux-paged Compiler/Runner; pick xfuncs.swift + [X] to exercise
# aux_bc.s end-to-end past the flat cap.
run-iz-compiler-iie-aux: disk-iie-aux-compiler
	bash emulator/run_izapple2.sh iienh $(PO_IIE_AUXCOMP)
# Tier 2 (II+ Saturn 128K): boots the Saturn compiler disk under the `sat`
# profile (-model=2plus -s0 saturn). In the file selector pick xfuncs.swift and
# press [X]: it compiles past the flat cap via Saturn-bank paging, then runs ->
# 210. Exercises saturn_bc.s end-to-end (Compiler flush + Runner read window).
run-iz-compiler-sat: disk-iip-sat-compiler
	bash emulator/run_izapple2.sh sat $(PO_IIP_SATCOMP)
run-iz-compiler-sat-2disk: disk-iip-sat-compiler disk-data
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run_izapple2.sh sat $(PO_IIP_SATCOMP)
# Compiler disk + data disk in drive 2 — for the Family B on-disk tests
# (doc 017): in the file selector, switch to the data disk, enter FBTESTS/,
# and run TFILEIO with [X]. The Compiler writes its .swb onto the data disk
# (drive 2), which has ample free space.
run-iz-compiler-2disk: disk-iip-compiler disk-data
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run_izapple2.sh iip $(PO_IIP_COMP)
run-iz-compiler-iie-2disk: disk-iie-compiler disk-data
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run_izapple2.sh iie $(PO_IIE_COMP)
run-iz-compiler-iie-aux-2disk: disk-iie-aux-compiler disk-data
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run_izapple2.sh iienh $(PO_IIE_AUXCOMP)
# Emulator-verify the //e Runner's aux paging end-to-end: builds the //e Family
# B disk + a data disk carrying the oversized BIGPROG.SWB, and boots izapple2
# (//e, 128K w/ aux). In the launcher: switch to the data disk (drive 2), select
# BIGPROG.SWB, press [X] — it runs DIRECTLY on the Runner (no compile). PASS =
# it prints 4900 (700 * 7). bc_len ~4.9 KB > the old 2,944 B cap, so the paged
# Runner reads it across many aux windows via the AUXMOVE driver (runner_aux.s).
.PHONY: run-iz-bigswb-iie
run-iz-bigswb-iie: disk-iie-aux-compiler $(BIGPROG_SWB) $(SAMPLE_SWIFTS) $(DATA_SAMPLE_SWIFTS) $(TEST_SWIFTS)
	BIG_SWB=$(BIGPROG_SWB) SAMPLES_EXTRAS_DIR="$(SAMPLES_EXTRAS_DIR) $(FB_SAMPLES_DIR) $(DATA_SAMPLES_EXTRAS_DIR)" bash tools/host/diskimg/build_data_po.sh $(PO_IMAGE_DATA) \
	  $(SAMPLES_DIR) $(TESTS_DIR) $(EXTRAS_TESTS_DIR) $(FB_TESTS_DIR) $(ERR_TESTS_DIR)
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run_izapple2.sh iienh $(PO_IIE_AUXCOMP)
run-iz-iip: disk-iip-lite-repl
	bash emulator/run_izapple2.sh iip $(PO_IIP_LITE)
run-iz-sat: disk-iip-sat-repl
	bash emulator/run_izapple2.sh sat $(PO_IIP_SAT)
# Videx Videoterm 80-col smoke test (track B). Boots SWIFTSAT on a II+ with a
# Saturn 128K (slot 0) + a Videx Videoterm (slot 3). At the REPL: `text80()`
# enters 80-col, `text()` reverts. ⚠ izapple2's bundled Videoterm ROM diverges
# from real cards (the reason track B was hard to verify in-emulator) — treat
# this as an output-routing smoke test only; REAL II+ + Videoterm hardware is
# the source of truth.
run-iz-videx: disk-iip-sat-repl
	bash emulator/run_izapple2.sh videx $(PO_IIP_SAT)
run-iz-iie: disk-iie-lite-repl
	bash emulator/run_izapple2.sh iie $(PO_IIE_LITE)
run-iz-iienh: disk-iie-aux-repl
	bash emulator/run_izapple2.sh iienh $(PO_IIE_AUX)

# izapple2 with the data disk auto-mounted in drive 2 (run_izapple2.sh reads
# DATA_DISK). Use these to run the on-disk TESTS/ + samples: `3` FILES -> the
# data disk -> enter TESTS/ -> RET a file. -sat for the x-prefixed extras tests
# (SWIFTSAT), -iie for the //e binary (SWIFTAUX).
run-iz-iip-2disk: disk-iip-lite-repl disk-data
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run_izapple2.sh iip $(PO_IIP_LITE)
run-iz-sat-2disk: disk-iip-sat-repl disk-data
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run_izapple2.sh sat $(PO_IIP_SAT)
run-iz-videx-2disk: disk-iip-sat-repl disk-data
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run_izapple2.sh videx $(PO_IIP_SAT)
run-iz-iie-2disk: disk-iie-lite-repl disk-data
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run_izapple2.sh iie $(PO_IIE_LITE)
run-iz-iienh-2disk: disk-iie-aux-repl disk-data
	DATA_DISK=$(PO_IMAGE_DATA) bash emulator/run_izapple2.sh iienh $(PO_IIE_AUX)
run-iz-ii: disk-iip-lite-repl
	bash emulator/run_izapple2.sh ii $(PO_IIP_LITE)
run-iz-iip48: disk-iip-lite-repl
	bash emulator/run_izapple2.sh iip48 $(PO_IIP_LITE)
run-iz-sat-s4: disk-iip-sat-repl
	bash emulator/run_izapple2.sh sat-s4 $(PO_IIP_SAT)
run-iz-memexp: disk-iip-lite-repl
	bash emulator/run_izapple2.sh memexp $(PO_IIP_LITE)

# Print the full hardware test matrix: which `make run-*` target exercises
# which machine config, which SwiftII binary it should select, and the
# per-emulator caveats. (Documentation only — runs nothing.)
.PHONY: run-configs
run-configs:
	@echo "SwiftII hardware test matrix — make <target> [IZAPPLE2=./izapple2sdl_mac_arm64]"
	@echo ""
	@echo "  Standard rows mount the DATA disk (samples + TESTS/) in drive 2 so you"
	@echo "  can run programs; the edge/negative rows boot the program disk alone."
	@echo "  9-disk set: each REPL program disk carries the launcher + ONE interpreter."
	@echo ""
	@echo "  CONFIG                                    TARGET                   SELECTS    NOTES"
	@echo "  ----------------------------------------  -----------------------  ---------  -----"
	@echo "  -- izapple2 (CLI-configured; embedded ROMs; II+ fully verified) --"
	@echo "  II+ 16K LC                                run-iz-iip-2disk         SWIFTIIP   boots swiftii-iip-lite-repl.po"
	@echo "  II+ Saturn 128K                           run-iz-sat-2disk         SWIFTSAT   boots swiftii-iip-sat-repl.po"
	@echo "  II+ Saturn 128K + Videx slot 3            run-iz-videx-2disk       SWIFTSAT   iip-sat.po + data disk; Videx 80-col smoke"
	@echo "  //e (128K)                                run-iz-iie-2disk         SWIFTIIE   boots swiftii-iie-lite-repl.po"
	@echo "  //e enhanced (65C02, 128K)                run-iz-iienh-2disk       SWIFTAUX   boots swiftii-iie-aux-repl.po"
	@echo ""
	@echo "  -- Mariani (GUI-configured machine; mounts both disks; set the model in its GUI) --"
	@echo "  II+ 16K LC                                run-mari-iip-2disk       SWIFTIIP   iip-lite.po; GUI: ][+ , 64K LC, NO Saturn"
	@echo "  II+ Saturn 128K                           run-mari-sat-2disk       SWIFTSAT   iip-sat.po;  GUI: ][+ WITH Saturn 128K"
	@echo "  //e 64K (no aux)                          run-mari-iie-2disk       SWIFTIIE   iie-lite.po; GUI: //e , 64K, NO aux/80-col"
	@echo "  //e + extended 80-col (128K)              run-mari-aux-2disk       SWIFTAUX   iie-aux.po;  GUI: //e WITH Extended 80-Col (128K)"
	@echo "  Family B compiler-runner (II+ / //e / //e aux / Sat) run-mari-compiler[-iie|-iie-aux|-sat]-2disk  boots the matching compiler disk + data disk"
	@echo "  (run-mari-iip/sat/iie/aux + run-mari-compiler[-iie|-iie-aux|-sat] are the single-disk equivalents — print the GUI hint, no data disk)"
	@echo ""
	@echo "  -- edge / negative cases (izapple2; program disk only) --"
	@echo "  Apple ][ (orig) 16K LC                    run-iz-ii                SWIFTIIP   iip-lite.po; boots+runs (crt0_ibasic.s _memcpy LC copy); C600G"
	@echo "  II+ 48K, NO language card                 run-iz-iip48             (none)     NEGATIVE: ProDOS needs 64K -> no boot"
	@echo "  II+ Saturn in a non-zero slot (slot 4)    run-iz-sat-s4            SWIFTSAT   iip-sat.po; launcher slot-scan + slot-conditional trampoline"
	@echo "  II+ + non-Saturn RAM card (memexp/slinky) run-iz-memexp            SWIFTIIP   iip-lite.po; RAM card must NOT false-trigger extras"
	@echo ""
	@echo "  Full written matrix + expected results: docs/testing/TESTING.md"

# ---------------------------------------------------------------------------
# Automated emulator acceptance harness (tools/host/acceptance/). Drives izapple2's
# `headless` frontend — embedded ROMs (nothing to source), a deterministic
# stdin protocol (run <cycles>, key/type/enter, text, png) — so it boots every
# hardware config, injects keystrokes, runs the TESTRUN.SYSTEM [T] sweep (or a
# graphics / Videx scenario), scrapes the screen, and reports pass/fail across
# the whole matrix. Family B verdicts come from the on-disk TESTLOG, read back
# with AppleCommander. Build the binary once with `go install
# github.com/ivanizag/izapple2/frontend/headless@latest` (or `make
# acceptance-build`); point at it with IZAPPLE2_HEADLESS=… if it's not on PATH.
# Pass CONFIGS="iip sat" to scope it; ARGS=--dry-run to print the plan;
# ARGS=--window to watch it in a GUI window (browser, mirrors the rendered
# screen incl. graphics); ARGS=--show to echo the text screen in the terminal.
# Pass RELEASE=releases/v1.0.1 to run against a pre-built (released) image set
# instead of building disks fresh from source — the make build step is skipped.
# Both disks are mounted read/write and write scenarios (editor saves, file
# CRUD) modify them, so the harness always runs off per-config COPIES in
# build/acceptance/<config>/ — the source images (incl. a tagged release) are
# never touched.
.PHONY: acceptance acceptance-list acceptance-build
acceptance:
	@python3 tools/host/acceptance/run_acceptance.py $(CONFIGS) \
		$(if $(RELEASE),--disk-dir $(RELEASE)) $(ARGS)
acceptance-list:
	@python3 tools/host/acceptance/run_acceptance.py --list
acceptance-build:
	go install github.com/ivanizag/izapple2/frontend/headless@latest
	@echo ">>> built $$(go env GOPATH)/bin/headless"

# Authentic doc screenshots — drive the same izapple2 `headless` engine the
# acceptance harness uses, snapshot the framebuffer to docs/screenshots/.
# Needs the `headless` binary (make acceptance-build). `make screenshots
# SHOTS="repl-hero graphics"` for a subset; `make screenshots-list` to list.
.PHONY: screenshots screenshots-list
# disk-iip-compiler + disk-data feed the opt-in `grdemo` shot (the oversize
# XGRDEMO showcase streams off the data disk through the flat Tier-1 compiler).
screenshots: disk-iip-lite-repl disk-iip-sat-repl disk-iie-lite-repl disk-iip-sat-compiler disk-iip-compiler disk-data
	@python3 tools/host/screenshots/capture.py $(SHOTS)
screenshots-list:
	@python3 tools/host/screenshots/capture.py --list

# Demo VIDEO — replay the demo/VIDEO-DEMO.md keystroke script through the same
# izapple2 `headless` engine and encode each act to MP4 (raw footage; captions
# + real-HW cut-ins are added in the edit). Needs the `headless` binary
# (make acceptance-build) and ffmpeg. The script + tuning live in demo/capture/.
# `make demo-video ARGS="--act 1 --fps 20"` to pass flags through.
#
# The disks are REBUILT FRESH first: Act 1's editor beat saves a `demo.swift`
# into the boot disk image, so a stale disk from a previous capture would start
# the file browser in a polluted state. The rebuild keeps every run deterministic.
.PHONY: demo-video demo-video-smoke
demo-video:
	@rm -f $(PO_IIP_SAT) $(PO_IIE_AUX) $(PO_IIP_SATCOMP) $(PO_IMAGE_DATA)
	@$(MAKE) disk-iip-sat-repl disk-iie-aux-repl disk-iip-sat-compiler disk-data
	@python3 demo/capture/capture_demo.py $(ARGS)
demo-video-smoke: disk-iip-sat-repl disk-data
	@python3 demo/capture/capture_demo.py --smoke $(ARGS)

# ProDOS-SYS load ceiling ($BF00 - $2000) for every interpreter MAIN
# image — lite, plus the SWIFTSAT/SWIFTAUX MAIN segments (EXTRAS_SYS_BUDGET
# is the historical name; SWIFTSAT + SWIFTAUX MAIN reuse it). The boot
# launcher has a much tighter budget — it shares the $2000+ load space with
# the interpreters it chains to.
LITE_SYS_BUDGET   := 40704
EXTRAS_SYS_BUDGET := 40704
# XLC ceiling for SWIFTSAT. Must match
# __XLCSIZE__ in src/platform/apple2/swiftsat-system.cfg ($3000 =
# 12,288 B for the Saturn-bank-1 XLC image). SWIFTSAT's LC is
# in-MAIN (cc65 standard), so its bytes count against MAIN, not
# tracked separately.
SAT_XLC_BUDGET    := 12288
# SWIFTAUX park ceiling. The park lives in aux main RAM
# from AUX_PARK ($2000) upward; aux $0200-$BFFF gives ~40 KB of room, so
# reuse the 40,704 B ProDOS-main figure as a generous soft cap (the park
# is staged via AUXMOVE, not READ to $2000, so the real limit is aux RAM
# size). MAIN uses the same 40,704 B ProDOS-load ceiling as the others.
AUX_PARK_BUDGET   := 40704
# The boot launcher has no hardware ceiling below the interpreters': it
# loads at $2000, the I/O buffer + directory/entry tables sit below $2000,
# and ProDOS lives at $BF00 — so the launcher owns the whole $2000-$BEFF
# window until it chains the (also-$2000) interpreter over itself. Now that
# the launcher has grown from a tiny boot selector into a full menu +
# two-pane file browser, the budget is raised to its MAXIMUM: the same
# 40,704 B ProDOS-SYS load ceiling ($BF00 - $2000) the interpreters use.
# The cfg's MAIN/BSS window was opened to match ($2000-$BEFF; BSS rides
# just above the file up to $BF00 - __STACKSIZE__), so the file may grow
# the whole way. Directory/entry tables ($0800 / $1400 / $1680) and the
# MLI I/O buffer ($1C00) stay in free RAM below $2000, not in this window.
BOOT_LAUNCHER_BUDGET  := 40704

.PHONY: size
size: $(A2_SYSTEM_BIN) $(A2IIE_SYSTEM_BIN) $(BOOT_LAUNCHER_BIN) $(BOOT_LAUNCHER_IIE_BIN) $(BOOT_LAUNCHER_IIE_AUX_BIN) $(BOOT_LAUNCHER_SAT_BIN) $(A2SAT_SYSTEM_BIN) $(A2AUX_SYSTEM_BIN) $(A2COMP_BIN) $(A2IIECOMP_BIN) $(A2IIEAUXCOMP_BIN) $(A2RUN_BIN) $(A2IIERUN_BIN) $(A2IIEAUXRUN_BIN) $(A2SATCOMP_BIN) $(A2SATRUN_BIN) $(DEBUG_SYS_BIN) $(TESTRUN_SYS_BIN)
	@# One table over all build artifacts, grouped by target. SWIFTSAT splits
	@# into MAIN + XLC images (cfg `load = MAIN, run = LC`); SWIFTAUX into MAIN +
	@# the park of 13 copy-down overlay bodies staged in aux RAM. `total` rows
	@# include the 4 B header (+ padded park for SWIFTAUX) and have no single
	@# budget. The //e 80-col overhead figures are a hardcoded snapshot (re-measure:
	@# build a //e binary with WITH_80COL=0 — clean rebuild, Make keys off mtime not
	@# -D flags — and diff against the default build).
	@lite=$$(wc -c < $(A2_SYSTEM_BIN)); \
	 iie=$$(wc -c < $(A2IIE_SYSTEM_BIN)); \
	 launcher=$$(wc -c < $(BOOT_LAUNCHER_BIN)); \
	 launcher_iie=$$(wc -c < $(BOOT_LAUNCHER_IIE_BIN)); \
	 launcher_iie_aux=$$(wc -c < $(BOOT_LAUNCHER_IIE_AUX_BIN)); \
	 launcher_sat=$$(wc -c < $(BOOT_LAUNCHER_SAT_BIN)); \
	 smain=$$(wc -c < $(A2SAT_MAIN_BIN)); \
	 sxlc=$$(wc -c < $(A2SAT_XLC_BIN)); \
	 stotal=$$(wc -c < $(A2SAT_SYSTEM_BIN)); \
	 amain=$$(wc -c < $(A2AUX_MAIN_BIN)); \
	 apark=$$(( $$(wc -c < $(A2AUX_OVLASC_BIN)) + $$(wc -c < $(A2AUX_OVLCHR_BIN)) + $$(wc -c < $(A2AUX_OVLCALL_BIN)) + $$(wc -c < $(A2AUX_OVLSIP_BIN)) + $$(wc -c < $(A2AUX_OVLINT_BIN)) + $$(wc -c < $(A2AUX_OVLRML_BIN)) + $$(wc -c < $(A2AUX_OVLRMA_BIN)) + $$(wc -c < $(A2AUX_OVLCON_BIN)) + $$(wc -c < $(A2AUX_OVLSCC_BIN)) + $$(wc -c < $(A2AUX_OVLNAR_BIN)) + $$(wc -c < $(A2AUX_OVLALN_BIN)) + $$(wc -c < $(A2AUX_OVLPMEM_BIN)) + $$(wc -c < $(A2AUX_OVLPGR_BIN)) )); \
	 atotal=$$(wc -c < $(A2AUX_SYSTEM_BIN)); \
	 fbcomp=$$(wc -c < $(A2COMP_BIN)); \
	 fbcompiieflat=$$(wc -c < $(A2IIECOMP_BIN)); \
	 fbcompiie=$$(wc -c < $(A2IIEAUXCOMP_BIN)); \
	 fbcompsat=$$(wc -c < $(A2SATCOMP_BIN)); \
	 fbrun=$$(wc -c < $(A2RUN_BIN)); \
	 fbruniieflat=$$(wc -c < $(A2IIERUN_BIN)); \
	 fbruniie=$$(wc -c < $(A2IIEAUXRUN_BIN)); \
	 fbrunsat=$$(wc -c < $(A2SATRUN_BIN)); \
	 dbg=$$(wc -c < $(DEBUG_SYS_BIN)); \
	 row="  %-16s %-11s %6d %8s %9s\n"; \
	 printf "  %-16s %-11s %6s %8s %9s\n" "image" "target" "bytes" "budget" "headroom"; \
	 printf "  %-16s %-11s %6s %8s %9s\n" "----------------" "-----------" "------" "--------" "---------"; \
	 printf "$$row" "SWIFTIIP.SYSTEM" "II+ lite"   $$lite   "$(LITE_SYS_BUDGET)"   "$$(( $(LITE_SYS_BUDGET) - lite ))"; \
	 printf "$$row" "SWIFTSAT MAIN"   "II+ Saturn" $$smain  "$(EXTRAS_SYS_BUDGET)" "$$(( $(EXTRAS_SYS_BUDGET) - smain ))"; \
	 printf "$$row" "SWIFTSAT XLC"    "II+ Saturn" $$sxlc   "$(SAT_XLC_BUDGET)"    "$$(( $(SAT_XLC_BUDGET) - sxlc ))"; \
	 printf "$$row" "SWIFTSAT total"  "II+ Saturn" $$stotal "-" "-"; \
	 echo ""; \
	 printf "$$row" "SWIFTIIE.SYSTEM" "//e lite"   $$iie    "$(LITE_SYS_BUDGET)"   "$$(( $(LITE_SYS_BUDGET) - iie ))"; \
	 printf "$$row" "SWIFTAUX MAIN"   "//e aux"    $$amain  "$(EXTRAS_SYS_BUDGET)" "$$(( $(EXTRAS_SYS_BUDGET) - amain ))"; \
	 printf "$$row" "SWIFTAUX park"   "//e aux"    $$apark  "-" "-"; \
	 printf "$$row" "SWIFTAUX total"  "//e aux"    $$atotal "-" "-"; \
	 echo ""; \
	 printf "$$row" "SWIFTII.SYSTEM"  "II+ launcher"  $$launcher     "$(BOOT_LAUNCHER_BUDGET)"  "$$(( $(BOOT_LAUNCHER_BUDGET) - launcher ))"; \
	 printf "$$row" "SWIFTII.SYSTEM"  "//e launcher"  $$launcher_iie "$(BOOT_LAUNCHER_BUDGET)"  "$$(( $(BOOT_LAUNCHER_BUDGET) - launcher_iie ))"; \
	 printf "$$row" "SWIFTII.SYSTEM"  "//e aux launch" $$launcher_iie_aux "$(BOOT_LAUNCHER_BUDGET)"  "$$(( $(BOOT_LAUNCHER_BUDGET) - launcher_iie_aux ))"; \
	 printf "$$row" "SWIFTII.SYSTEM"  "Sat launcher"  $$launcher_sat "$(BOOT_LAUNCHER_BUDGET)"  "$$(( $(BOOT_LAUNCHER_BUDGET) - launcher_sat ))"; \
	 printf "$$row" "DEBUG.SYSTEM"    "diagnostic"    $$dbg          "-"                        "-"; \
	 echo ""; \
	 printf "$$row" "COMPILER.SYSTEM" "Family B"     $$fbcomp   "$(LITE_SYS_BUDGET)" "$$(( $(LITE_SYS_BUDGET) - fbcomp ))"; \
	 printf "$$row" "COMPILER.SYSTEM" "B //e"        $$fbcompiieflat "$(LITE_SYS_BUDGET)" "$$(( $(LITE_SYS_BUDGET) - fbcompiieflat ))"; \
	 printf "$$row" "COMPILER.SYSTEM" "B //e aux"    $$fbcompiie "$(LITE_SYS_BUDGET)" "$$(( $(LITE_SYS_BUDGET) - fbcompiie ))"; \
	 printf "$$row" "COMPILER.SYSTEM" "B Saturn"     $$fbcompsat "$(LITE_SYS_BUDGET)" "$$(( $(LITE_SYS_BUDGET) - fbcompsat ))"; \
	 printf "$$row" "RUNNER.SYSTEM"   "Family B II+" $$fbrun    "$(LITE_SYS_BUDGET)" "$$(( $(LITE_SYS_BUDGET) - fbrun ))"; \
	 printf "$$row" "RUNNER.SYSTEM"   "Family B //e" $$fbruniieflat "$(LITE_SYS_BUDGET)" "$$(( $(LITE_SYS_BUDGET) - fbruniieflat ))"; \
	 printf "$$row" "RUNNER.SYSTEM"   "B //e aux"    $$fbruniie "$(LITE_SYS_BUDGET)" "$$(( $(LITE_SYS_BUDGET) - fbruniie ))"; \
	 printf "$$row" "RUNNER.SYSTEM"   "B Saturn"     $$fbrunsat "$(LITE_SYS_BUDGET)" "$$(( $(LITE_SYS_BUDGET) - fbrunsat ))"; \
	 echo ""; \
	 echo "  80-col overhead per build (//e only):"; \
	 printf "    %-16s %s\n" "SWIFTIIE.SYSTEM" "+315 B"; \
	 printf "    %-16s %s\n" "SWIFTAUX MAIN"   "+152 B"; \
	 ok=1; \
	 [ $$lite  -le $(LITE_SYS_BUDGET) ]   || { echo "  ERROR: SWIFTIIP over budget"; ok=0; }; \
	 [ $$iie   -le $(LITE_SYS_BUDGET) ]   || { echo "  ERROR: SWIFTIIE over budget"; ok=0; }; \
	 [ $$launcher  -le $(BOOT_LAUNCHER_BUDGET) ]  || { echo "  ERROR: II+ boot launcher over budget"; ok=0; }; \
	 [ $$launcher_iie -le $(BOOT_LAUNCHER_BUDGET) ] || { echo "  ERROR: //e boot launcher over budget"; ok=0; }; \
	 [ $$launcher_iie_aux -le $(BOOT_LAUNCHER_BUDGET) ] || { echo "  ERROR: //e aux boot launcher over budget"; ok=0; }; \
	 [ $$smain -le $(EXTRAS_SYS_BUDGET) ] || { echo "  ERROR: SWIFTSAT MAIN over budget"; ok=0; }; \
	 [ $$sxlc  -le $(SAT_XLC_BUDGET) ]    || { echo "  ERROR: SWIFTSAT XLC over budget"; ok=0; }; \
	 [ $$amain -le $(EXTRAS_SYS_BUDGET) ] || { echo "  ERROR: SWIFTAUX MAIN over budget"; ok=0; }; \
	 [ $$fbcomp -le $(LITE_SYS_BUDGET) ] || { echo "  ERROR: COMPILER over budget"; ok=0; }; \
	 [ $$fbcompiieflat -le $(LITE_SYS_BUDGET) ] || { echo "  ERROR: COMPILER (//e) over budget"; ok=0; }; \
	 [ $$fbcompiie -le $(LITE_SYS_BUDGET) ] || { echo "  ERROR: COMPILER (//e aux) over budget"; ok=0; }; \
	 [ $$fbcompsat -le $(LITE_SYS_BUDGET) ] || { echo "  ERROR: COMPILER (Saturn) over budget"; ok=0; }; \
	 [ $$fbrun  -le $(LITE_SYS_BUDGET) ] || { echo "  ERROR: RUNNER (II+) over budget"; ok=0; }; \
	 [ $$fbruniieflat -le $(LITE_SYS_BUDGET) ] || { echo "  ERROR: RUNNER (//e) over budget"; ok=0; }; \
	 [ $$fbruniie -le $(LITE_SYS_BUDGET) ] || { echo "  ERROR: RUNNER (//e aux) over budget"; ok=0; }; \
	 [ $$fbrunsat -le $(LITE_SYS_BUDGET) ] || { echo "  ERROR: RUNNER (Saturn) over budget"; ok=0; }; \
	 [ $$ok -eq 1 ]
	@if [ -f $(A2_MAP) ]; then \
	  echo "--- linker map (lite, segments) ---"; \
	  awk '/^Segment list:/,/^$$/' $(A2_MAP); \
	fi

.PHONY: sim
sim:
	@python3 tests/sim/runner.py

# ---------------------------------------------------------------------------
# Integration test and REPL test.
# ---------------------------------------------------------------------------

.PHONY: integration
integration: $(HOST_BIN)
	@bash tests/integration/runner.sh

# Run the self-checking on-disk tests (datadisk/tests/core + fbtests) through
# the host build for fast, emulator-free coverage of the same compiler + VM
# logic the target exercises (the emulator acceptance sweeps remain the target
# system of record). Each must end in "... fail 0".
.PHONY: ondisk-host
ondisk-host: $(HOST_BIN)
	@bash tests/ondisk/runner.sh

# The default host binary is a superset build (WITH_SWB / WITH_BIGLANG /
# WITH_RANDOM) so these sessions cover core REPL behaviour plus host-testable
# extras. Family-A dialect rejection tests live in repl-test-iie below.
.PHONY: repl-test
repl-test: $(HOST_BIN)
	@bash tests/repl/runner.sh

# //e-only REPL features (function redefinition, ...) and Family-A dialect
# rejection tests run against the WITH_IIE / no-WITH_SWB host build.
.PHONY: repl-test-iie
repl-test-iie: $(HOST_IIE_BIN)
	@REPL_BIN=$(HOST_IIE_BIN) REPL_DIR=tests/repl-iie bash tests/repl/runner.sh

# ---------------------------------------------------------------------------
# Benchmark profile build
#
# Builds a parallel host binary with -DSWIFTII_PROFILE=1 so that the VM
# dispatch loop ticks a per-opcode counter and dumps the table to
# stderr at program exit. Used to capture per-opcode baseline numbers before
# an optimisation lands. The cc65 build NEVER sees the macro;
# profiling is host-only.
# ---------------------------------------------------------------------------

BENCH_DIR    := $(HOST_DIR)/bench
BENCH_OBJ    := $(BENCH_DIR)/obj
BENCH_BIN    := $(HOST_DIR)/swiftii_bench
BENCH_CFLAGS := $(HOST_CFLAGS) -DSWIFTII_PROFILE=1
BENCH_OBJS   := $(patsubst src/%.c,$(BENCH_OBJ)/%.o,$(HOST_SRC))

$(BENCH_OBJ)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BENCH_CFLAGS) -c $< -o $@

$(BENCH_BIN): $(BENCH_OBJS)
	@mkdir -p $(HOST_DIR)
	$(CC) $(HOST_LDFLAGS) $(BENCH_OBJS) -o $@

.PHONY: bench
bench: $(BENCH_BIN)
	@mkdir -p $(BENCH_DIR)
	@for swift in tests/bench/*.swift; do \
	  name=$$(basename $$swift .swift); \
	  echo "--- $$name ---"; \
	  $(BENCH_BIN) $$swift > $(BENCH_DIR)/$$name.stdout 2> $(BENCH_DIR)/$$name.profile; \
	  cat $(BENCH_DIR)/$$name.profile; \
	  echo; \
	done

# ---------------------------------------------------------------------------
# CI gate
# ---------------------------------------------------------------------------

# Verify every built disk's README.TXT is the correct case-fold of the single
# canonical source (progdisk/readme-repl.txt / readme-compiler.txt) — keeps the
# on-disk help in sync across all builds. Needs `make disks disks-familyb` first.
.PHONY: check-readme
check-readme:
	@bash tools/host/diskimg/check_readme.sh

.PHONY: ci
ci: clean test sim integration ondisk-host repl-test repl-test-iie apple2-all apple2-familyb boot-launcher size disks disks-familyb check-readme

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(BUILD)
	@find src -name '*.o' -delete

.PHONY: all
all: test sim apple2-all boot-launcher

.DEFAULT_GOAL := all
