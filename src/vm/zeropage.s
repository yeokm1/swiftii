; zeropage.s — ZP claim ($D0-$DF, 16 bytes).
;
; Reserves zero-page storage for the hot VM variables that the ca65
; dispatch loop (sub-commit 4c) and asm opcode handlers (commit 5)
; will use. cc65's runtime ZP sits at $0080-$0099; ProDOS uses
; $00BB-$00BF plus a scattering of low slots; the $00D0-$00DF range
; is safe per docs/contributing/CONSTRAINTS.md and docs/contributing/MEMORY_MAP.md § Zero page.
;
; This file is built for the apple2 target only. On the host the
; same symbol names appear in src/common/zeropage_host.c as ordinary
; globals.
;
; Layout matches docs/contributing/MEMORY_MAP.md § "Zero page (target)"
; and the planned breakdown in docs/contributing/design/008-phase6-optimization.md
; § "Commit 4 — ZP claim". Each slot keeps its name as it appeared in
; the design doc; the `_` prefix is cc65's C-linkage convention.

.exportzp       _vm_pc, _vm_sp, _vm_fp, _vm_tmp1, _vm_tmp2
.exportzp       _heap_ptr, _vm_op, _vm_flags, _vm_tmp3

.segment        "SWIFTIIZP": zeropage

_vm_pc:         .res 2          ; $D0-$D1   VM program counter (16-bit)
_vm_sp:         .res 1          ; $D2       VM software stack pointer (8-bit; 32-slot stack)
_vm_fp:         .res 1          ; $D3       VM frame pointer (8-bit; same range)
_vm_tmp1:       .res 2          ; $D4-$D5   handler scratch ptr 1
_vm_tmp2:       .res 2          ; $D6-$D7   handler scratch ptr 2
_heap_ptr:      .res 2          ; $D8-$D9   heap bump pointer mirror
_vm_op:         .res 1          ; $DA       current opcode
_vm_flags:      .res 1          ; $DB       dispatch flags (halt, error)
_vm_tmp3:       .res 2          ; $DC-$DD   call-frame scratch
                .res 2          ; $DE-$DF   reserved (2 bytes spare)
