/* Shared error codes returned by the VM and platform layer. */
#ifndef SWIFTII_ERRORS_H
#define SWIFTII_ERRORS_H

typedef unsigned char swiftii_err_t;

#define SE_OK           0
#define SE_HALT         1   /* normal program end (OP_HALT) */
#define SE_STACK_UNDER  2
#define SE_STACK_OVER   3
#define SE_BAD_OPCODE   4
#define SE_TYPE_MISMATCH 5
#define SE_DIV_ZERO     6
#define SE_OOM          7
#define SE_IO           8
/* Language-level runtime errors. Used by OP_UNWRAP on a nil
 * optional and by OP_ARR_GET/SET on out-of-bounds
 * indices. Distinct from SE_TYPE_MISMATCH so the driver can surface
 * a more user-friendly message later. */
#define SE_RUNTIME      9
/* User pressed Ctrl-C while a program ran. Family B Runner only: the VM
 * polls the keyboard on backward jumps (vm.c OP_LOOP, WITH_SWB-gated). */
#define SE_BREAK        10

#endif /* SWIFTII_ERRORS_H */
