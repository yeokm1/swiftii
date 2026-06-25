/* Shared scalar types and the tagged Value layout.
 *
 * Value is exactly 3 bytes: 1 tag byte + 2 payload bytes (little-endian).
 * See docs/contributing/MEMORY_MAP.md § Tagged value layout for the canonical spec.
 */
#ifndef SWIFTII_TYPES_H
#define SWIFTII_TYPES_H

#include <stdint.h>

/* Value tags. Stable; do not renumber once shipped. */
typedef unsigned char value_tag_t;

#define T_NIL     0x00
#define T_BOOL    0x01
#define T_INT     0x02
#define T_STR     0x10
#define T_ARR     0x11
/* 0x12 free */
#define T_OPT_NIL 0x20
#define T_FN      0x30

typedef struct value {
  unsigned char tag;
  unsigned char lo;
  unsigned char hi;
} Value;

/* Build/inspect helpers. The previous compound-literal constructors
 * (VALUE_INT, VALUE_NIL, etc.) were unused and would have failed under
 * cc65 anyway — cc65 does not support C99 compound literals (see
 * feedback_cc65_quirks.md). Construct Values field-by-field instead. */

#define VALUE_PAYLOAD_U16(v) ((uint16_t)((v).lo) | ((uint16_t)((v).hi) << 8))

#define VALUE_PAYLOAD_I16(v) ((int16_t)VALUE_PAYLOAD_U16(v))

#define IS_NIL_VAL(v) ((v).tag == T_NIL || (v).tag == T_OPT_NIL)

#endif /* SWIFTII_TYPES_H */
