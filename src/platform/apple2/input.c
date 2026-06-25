/* Apple II //+ input layer.
 *
 * Batch translator: takes one Return-terminated line of typed bytes
 * and rewrites it into canonical lowercase ASCII. Used by the apple2
 * keyboard backend after a line has been read; on the host the same
 * code links in for the unit tests but is not called from the
 * keyboard backend (host stdin already delivers canonical bytes).
 *
 * Rules in priority order at each step (see input.h header for the
 * spec):
 *
 *   1. Inside a `//` line comment: auto-lowercase letters, copy
 *      everything else verbatim; exit on `\n`/`\r`.
 *   2. Inside a `/`*`*`/` block comment: auto-lowercase letters,
 *      copy verbatim; exit when `*` is followed by `/`.
 *   3. Apostrophe handling — depends on context:
 *      - Inside a string AND the previously emitted byte is a letter:
 *        literal `'` (covers contractions like `"don't"`).
 *      - Otherwise: case marker. `''` (doubled) stages a run marker
 *        that uppercases letters until the first non-letter.
 *   4. Ctrl-W ($17) at top level emits `_`.
 *   5. Letter: emit lowercase, with case_pending consumed (single)
 *      or kept (run) to produce an uppercase letter.
 *   6. Non-letter: clears any pending case marker (so a stray `'X`
 *      then space doesn't carry the marker forward).
 *   7. `"` toggles in/out of string state.
 *   8. `\` inside a string consumes the next byte literally (so the
 *      escape sequence passes through untouched).
 *   9. `//` and `/`*` enter the respective comment states.
 *  10. Digraphs: `<%` `%>` `<:` `:>` translate to `{ } [ ]`;
 *      `??/` to `\`; `??!` to `|`. Translation runs inside strings
 *      too — required so `??/` delivers `\` for `\n`, `\(...)`, etc.
 *  11. Anything else: passthrough.
 *
 * This whole model is Swift-specific. The editor only runs it for a
 * `.swift` file; a plain text file (e.g. README.TXT) is loaded and
 * saved verbatim instead (see editor_path_is_swift in fileio.c), so it
 * never reaches input_translate / input_untranslate.
 */
#include "input.h"

#include <stdint.h>

#define ST_NORMAL    0
#define ST_STRING    1
#define ST_LCOMMENT  2
#define ST_BCOMMENT  3

#define CP_NONE   0
#define CP_SINGLE 1
#define CP_RUN    2

/* The //+ batch translator is only used by the pre-IIe keyboard backend
 * (keyboard.c, `#ifndef WITH_IIE`). On the //e-target binaries (SWIFTIIE
 * lite + SWIFTAUX, built `-DWITH_IIE`) the keyboard is already canonical,
 * so `input_translate` is never called — but input.c is force-linked into
 * every apple2 build, so without this gate the whole ~2 KB translator
 * rides along as dead code (it did).
 * Compile it out on WITH_IIE; input.o then contributes nothing there. The
 * host build keeps it (no WITH_IIE) for input_translate_test. */
#ifndef WITH_IIE

static int is_letter(uint8_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static uint8_t to_lower(uint8_t c) {
  if (c >= 'A' && c <= 'Z') return (uint8_t)(c + 32);
  return c;
}

static uint8_t to_upper(uint8_t c) {
  if (c >= 'a' && c <= 'z') return (uint8_t)(c - 32);
  return c;
}

uint16_t input_translate(const char *in, uint16_t in_len,
                         char *out, uint16_t out_max) {
  uint16_t r;
  uint16_t w;
  uint8_t state;
  uint8_t case_pending;
  uint8_t prev;
  uint8_t k;
  uint8_t n;
  uint8_t n2;
  uint8_t letter;

  r = 0;
  w = 0;
  state = ST_NORMAL;
  case_pending = CP_NONE;
  prev = 0;

  while (r < in_len && w < out_max) {
    k = (uint8_t)in[r];

    if (state == ST_BCOMMENT) {
      /* Auto-lowercase only — markers and digraphs are literal in
       * comments so a comment can document the typing model without
       * itself being mangled. */
      letter = to_lower(k);
      out[w++] = (char)letter;
      prev = letter;
      ++r;
      if (k == '*' && r < in_len && (uint8_t)in[r] == '/' &&
          w < out_max) {
        out[w++] = '/';
        prev = '/';
        ++r;
        state = ST_NORMAL;
      }
      continue;
    }

    if (state == ST_LCOMMENT) {
      if (k == '\n' || k == '\r') {
        out[w++] = (char)k;
        prev = k;
        ++r;
        state = ST_NORMAL;
        continue;
      }
      letter = to_lower(k);
      out[w++] = (char)letter;
      prev = letter;
      ++r;
      continue;
    }

    /* state == ST_NORMAL or ST_STRING */

    if (k == '\'') {
      if (state == ST_STRING && is_letter(prev)) {
        out[w++] = '\'';
        prev = '\'';
        ++r;
        continue;
      }
      if (r + 1 < in_len && (uint8_t)in[r + 1] == '\'') {
        case_pending = CP_RUN;
        r += 2;
      } else {
        case_pending = CP_SINGLE;
        ++r;
      }
      continue;
    }

    if (k == 0x17 && state == ST_NORMAL) {   /* Ctrl-W: input-method underscore */
      out[w++] = '_';
      prev = '_';
      case_pending = CP_NONE;
      ++r;
      continue;
    }

    if (is_letter(k)) {
      letter = to_lower(k);
      if (case_pending == CP_SINGLE) {
        letter = to_upper(letter);
        case_pending = CP_NONE;
      } else if (case_pending == CP_RUN) {
        letter = to_upper(letter);
      }
      out[w++] = (char)letter;
      prev = letter;
      ++r;
      continue;
    }

    /* Non-letter ends a run marker, and any unconsumed single marker
     * is silently discarded (the user typed `'` against something
     * that wasn't a letter). */
    case_pending = CP_NONE;

    if (k == '"') {
      out[w++] = '"';
      prev = '"';
      ++r;
      state = (state == ST_STRING) ? ST_NORMAL : ST_STRING;
      continue;
    }

    if (state == ST_STRING && k == '\\') {
      /* The next byte is the escape's argument (`n`, `t`, `\\`, `"`,
       * `r`, `0`, or `(`). Emit it without re-applying the string
       * state-toggle on `"` and without the apostrophe rule, but DO
       * auto-lowercase letters so the //+-typed `\N` becomes the
       * canonical `\n`. */
      out[w++] = '\\';
      prev = '\\';
      ++r;
      if (r < in_len && w < out_max) {
        n = (uint8_t)in[r];
        if (is_letter(n)) n = to_lower(n);
        out[w++] = (char)n;
        prev = n;
        ++r;
      }
      continue;
    }

    if (state == ST_NORMAL && k == '/' && r + 1 < in_len) {
      n = (uint8_t)in[r + 1];
      if (n == '/' && w + 1 < out_max) {
        out[w++] = '/';
        out[w++] = '/';
        prev = '/';
        r += 2;
        state = ST_LCOMMENT;
        continue;
      }
      if (n == '*' && w + 1 < out_max) {
        out[w++] = '/';
        out[w++] = '*';
        prev = '*';
        r += 2;
        state = ST_BCOMMENT;
        continue;
      }
    }

    /* Digraphs apply in both NORMAL and STRING states. */
    n = (r + 1 < in_len) ? (uint8_t)in[r + 1] : 0;
    if (k == '<' && n == '%') { out[w++] = '{'; prev = '{'; r += 2; continue; }
    if (k == '%' && n == '>') { out[w++] = '}'; prev = '}'; r += 2; continue; }
    if (k == '<' && n == ':') { out[w++] = '['; prev = '['; r += 2; continue; }
    if (k == ':' && n == '>') { out[w++] = ']'; prev = ']'; r += 2; continue; }
    if (k == '?' && n == '?' && r + 2 < in_len) {
      n2 = (uint8_t)in[r + 2];
      if (n2 == '/') { out[w++] = '\\'; prev = '\\'; r += 3; continue; }
      if (n2 == '!') { out[w++] = '|';  prev = '|';  r += 3; continue; }
    }

    out[w++] = (char)k;
    prev = k;
    ++r;
  }

  return w;
}

/* input_untranslate is only used by the editor's //+ loader (fileio.c, gated
 * EFIO_CANON). Gate it to the editor builds (the boot launcher + host) so it
 * doesn't ride along as dead code in the at-ceiling interpreters, which link
 * input.o for input_translate only. LITE_IIE (//e launcher) saves verbatim
 * (EFIO_CANON=0) and never calls it. */
#if defined(WITH_EDITOR) && !defined(LITE_IIE)
/* Inverse of input_translate: canonical source -> //+ input form, so a loaded
 * file edits + saves through input_translate unchanged. Without this the
 * editor loads raw bytes and input_translate at save lowercases real capitals
 * (e.g. `readLine` -> `readline` -> "undeclared name"). Each uppercase letter
 * becomes `'` + its lowercase (a single case marker), so input_translate
 * resolves it back. Caller invokes per line (state resets per call, mirroring
 * save). Limits, all inherent to the //+ typing model and harmless here:
 *   - In a string, a capital immediately after a letter can't take a single
 *     marker (input_translate would read the `'` as a literal apostrophe per
 *     the contraction rule), so it is left raw and lowercases on the next save
 *     — the //+ user can't type that case either.
 *   - Comment capitals are left raw (input_translate lowercases comments
 *     regardless); no stray markers are injected into comments.
 *   - `{ } [ ] \ |` and `??`/escapes pass through (input_translate leaves a
 *     literal one alone; the digraphs are only an input alternative). */
uint16_t input_untranslate(const char *in, uint16_t in_len,
                           char *out, uint16_t out_max) {
  uint16_t r = 0;
  uint16_t w = 0;
  uint8_t state = ST_NORMAL;
  uint8_t prev = 0;
  uint8_t k;

  while (r < in_len && w < out_max) {
    k = (uint8_t)in[r];

    if (state == ST_LCOMMENT) {
      out[w++] = (char)k; prev = k; ++r;
      if (k == '\n' || k == '\r') state = ST_NORMAL;
      continue;
    }
    if (state == ST_BCOMMENT) {
      out[w++] = (char)k; prev = k; ++r;
      if (k == '*' && r < in_len && (uint8_t)in[r] == '/' && w < out_max) {
        out[w++] = '/'; prev = '/'; ++r; state = ST_NORMAL;
      }
      continue;
    }

    /* NORMAL or STRING */
    if (state == ST_NORMAL && k == '/' && r + 1 < in_len && w + 1 < out_max) {
      uint8_t n = (uint8_t)in[r + 1];
      if (n == '/') { out[w++] = '/'; out[w++] = '/'; r += 2; state = ST_LCOMMENT; prev = '/'; continue; }
      if (n == '*') { out[w++] = '/'; out[w++] = '*'; r += 2; state = ST_BCOMMENT; prev = '*'; continue; }
    }
    if (state == ST_STRING && k == '\\') {
      out[w++] = '\\'; prev = '\\'; ++r;
      if (r < in_len && w < out_max) { out[w++] = in[r]; prev = (uint8_t)in[r]; ++r; }
      continue;
    }
    if (k == '"') {
      out[w++] = '"'; prev = '"'; ++r;
      state = (state == ST_STRING) ? ST_NORMAL : ST_STRING;
      continue;
    }
    if (k >= 'A' && k <= 'Z') {
      if (state == ST_STRING && is_letter(prev)) {
        out[w++] = (char)k; prev = k; ++r;        /* can't mark; leave raw */
      } else if (w + 1 < out_max) {
        out[w++] = '\'';
        out[w++] = (char)(k + 32);
        prev = (uint8_t)(k + 32);
        ++r;
      } else {
        break;                                    /* no room for marker pair */
      }
      continue;
    }
    out[w++] = (char)k; prev = k; ++r;
  }
  return w;
}
#endif /* WITH_EDITOR && !LITE_IIE */

#endif /* !WITH_IIE */
