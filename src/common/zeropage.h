/* Zero-page symbol declarations.
 *
 * Claims $D0-$DF for the VM's hot variables. On the
 * cc65 target build, storage is provisioned by src/vm/zeropage.s and
 * the symbols are flagged ZP-addressable via #pragma zpsym below. On
 * the host build (clang), no actual zero page exists — the same names
 * resolve to ordinary globals defined in src/vm/zeropage_host.c when
 * commit 4b+ starts reading or writing them.
 *
 * The 4a checkpoint reserves the slots; nothing in the C code uses
 * them yet. Commit 4c wires them into the asm dispatch loop; commits
 * 5+ migrate individual opcode handlers.
 *
 * See:
 * - docs/contributing/MEMORY_MAP.md § Zero page (target)
 *   - docs/contributing/design/008-phase6-optimization.md § Commit 4
 *   - docs/contributing/CONSTRAINTS.md § Required practices § Zero-page variables
 */
#ifndef SWIFTII_ZEROPAGE_H
#define SWIFTII_ZEROPAGE_H

#include <stdint.h>

#ifdef __CC65__

/* Symbols defined in src/vm/zeropage.s. cc65 only treats a global as
 * ZP-addressable if you both #pragma zpsym it and the linker places
 * the actual storage in a zeropage-type segment. */

extern uint16_t vm_pc;       /* $D0-$D1   program counter */
extern uint8_t  vm_sp;       /* $D2       stack pointer */
extern uint8_t  vm_fp;       /* $D3       frame pointer */
extern uint16_t vm_tmp1;     /* $D4-$D5   handler scratch 1 */
extern uint16_t vm_tmp2;     /* $D6-$D7   handler scratch 2 */
extern uint16_t heap_ptr;    /* $D8-$D9   heap bump pointer mirror */
extern uint8_t  vm_op;       /* $DA       current opcode */
extern uint8_t  vm_flags;    /* $DB       dispatch flags */
extern uint16_t vm_tmp3;     /* $DC-$DD   call-frame scratch */

#pragma zpsym ("vm_pc")
#pragma zpsym ("vm_sp")
#pragma zpsym ("vm_fp")
#pragma zpsym ("vm_tmp1")
#pragma zpsym ("vm_tmp2")
#pragma zpsym ("heap_ptr")
#pragma zpsym ("vm_op")
#pragma zpsym ("vm_flags")
#pragma zpsym ("vm_tmp3")

#else

/* Host build: ordinary globals. Storage is defined wherever the host
 * counterpart .c file lives (added in commit 4b when something first
 * reads/writes these). The 4a checkpoint declares without defining;
 * the linker has no complaint until a referencing TU appears. */

extern uint16_t vm_pc;
extern uint8_t  vm_sp;
extern uint8_t  vm_fp;
extern uint16_t vm_tmp1;
extern uint16_t vm_tmp2;
extern uint16_t heap_ptr;
extern uint8_t  vm_op;
extern uint8_t  vm_flags;
extern uint16_t vm_tmp3;

#endif /* __CC65__ */

#endif /* SWIFTII_ZEROPAGE_H */
