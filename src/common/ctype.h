/* Compile-time type codes for SwiftII's static type tracker.
 *
 * See `docs/contributing/design/009-type-tracker.md` for the encoding rationale.
 *
 * One byte per ctype:
 *   bits 0-3 base type (Int / Bool / String / Void / Unknown)
 *   bit 4    set on the bare-nil literal sentinel (unifies with any T?)
 *   bit 6    array bit  — low nibble is element type
 *   bit 7    optional bit — low nibble is wrapped type
 *
 * Optional-of-array and array-of-optional are deliberately not
 * representable; the parser rejects them. If a future demo needs
 * either, extend to a two-byte encoding (see design doc 009 §
 * Alternatives).
 *
 * Local-include only — quote-include keeps the name out of the libc
 * <ctype.h> namespace.
 */
#ifndef SWIFTII_CTYPE_H
#define SWIFTII_CTYPE_H

typedef unsigned char ctype_t;

#define CT_UNKNOWN     0x00  /* not yet inferred / error sentinel    */
#define CT_VOID        0x01  /* () return                            */
#define CT_INT         0x02
#define CT_BOOL        0x03
#define CT_STRING      0x04
/* 0x05 free */

#define CT_NIL_LIT     0x10  /* bare `nil`; unifies with any T?      */

#define CT_ARR_BIT     0x40
#define CT_ARR_INT     (CT_ARR_BIT | CT_INT)
#define CT_ARR_BOOL    (CT_ARR_BIT | CT_BOOL)
#define CT_ARR_STRING  (CT_ARR_BIT | CT_STRING)
/* Element-type placeholder for OP_NEW_ARRAY 0 (empty literal); unifies
 * with any CT_ARR_*. */
#define CT_ARR_UNKNOWN (CT_ARR_BIT | CT_UNKNOWN)

#define CT_OPT_BIT     0x80
#define CT_OPT_INT     (CT_OPT_BIT | CT_INT)
#define CT_OPT_BOOL    (CT_OPT_BIT | CT_BOOL)
#define CT_OPT_STRING  (CT_OPT_BIT | CT_STRING)

#endif /* SWIFTII_CTYPE_H */
