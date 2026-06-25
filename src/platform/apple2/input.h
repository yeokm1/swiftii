/* Apple II //+ input layer. Design doc 003 revision 3.
 *
 * Translates typed bytes from the //+ keyboard (uppercase-only; no
 * brace, bracket, backslash, underscore, or pipe keys) into canonical
 * lowercase ASCII before they reach the lexer or disk. Transforms:
 *
 *   - Letter keys auto-lowercase. LET X = 5 becomes let x = 5.
 *   - Apostrophe is a one-letter case marker. 'INT becomes Int.
 *   - Double apostrophe is a run case marker. ''MAX + becomes MAX +.
 *   - Apostrophe inside a string literal: literal between letters
 *     ("DON'T" stores as "don't"), case marker after a non-letter
 *     ("'HELLO" stores as "Hello").
 *   - C-standard digraphs <% %> <: :> for { } [ ]; trigraph ??/ for
 *     backslash; ??! reserved for the pipe character.
 *   - Ctrl-W (0x17) emits underscore (the II+ underscore key, shared with the editor).
 *   - Inside line and block comments, auto-lowercase still applies
 *     but apostrophes and digraph sequences pass through literally.
 *
 * The result is canonical lowercase Swift regardless of which machine
 * authored the input. On host the function still links in so the unit
 * tests can drive it directly, but the host's platform_read_line does
 * not call it — host keyboards already produce canonical bytes. On
 * the Apple II target, keyboard.c calls input_translate once per
 * Return-terminated line.
 *
 * Budget sweep (2026-05-23) removed the `:raw` REPL meta-
 * command toggle (and the `input_raw_mode` session flag) — the
 * input layer is now always on; nothing in tree drives it off.
 */
#ifndef SWIFTII_APPLE2_INPUT_H
#define SWIFTII_APPLE2_INPUT_H

#include <stdint.h>

/* Translate `in_len` typed bytes from `in` into canonical bytes in
 * `out`. The translation never grows the byte count (each rule shrinks
 * or preserves length) so `out_max == in_len` is always sufficient;
 * the parameter exists to bound writes when called with a smaller
 * destination. Returns the canonical byte count.
 *
 * `in` and `out` may alias (an in-place rewrite is well-defined since
 * the write index never overtakes the read index). */
uint16_t input_translate(const char *in, uint16_t in_len,
                         char *out, uint16_t out_max);

/* Inverse of input_translate: canonical source -> //+ input form, used by the
 * editor's loader so a loaded file's capitals survive the save-time
 * input_translate. Call per line. See input.c for the (//+-inherent) limits.
 * `in` and `out` must NOT alias (a capital expands to two output bytes). */
uint16_t input_untranslate(const char *in, uint16_t in_len,
                           char *out, uint16_t out_max);

#endif /* SWIFTII_APPLE2_INPUT_H */
