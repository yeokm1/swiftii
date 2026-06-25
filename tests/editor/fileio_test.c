/* Editor file I/O unit tests (host).
 *
 * Drives editor_file_save / editor_file_load against temp files: the //+
 * save-time canonicalisation (input_translate per line) and the load/save
 * round trip. The low-level layer is stdio on the host, so these exercise
 * the same control flow the cc65 build runs with MLI.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "editor/gapbuf.h"
#include "editor/fileio.h"

#define EXPECT(cond, code) do { if (!(cond)) return (code); } while (0)

#define TMP "/tmp/swiftii_editor_fileio_test.swift"
#define TMP_TXT "/tmp/swiftii_editor_fileio_test.txt"

static void put(GapBuf *gb, const char *s) {
  while (*s) gapbuf_insert(gb, (uint8_t)*s++);
}

/* Read a whole file into out (NUL-terminated); returns byte count, or -1. */
static int slurp(const char *path, char *out, int cap) {
  FILE *f = fopen(path, "rb");
  int n;
  if (!f) return -1;
  n = (int)fread(out, 1, (size_t)(cap - 1), f);
  fclose(f);
  if (n < 0) return -1;
  out[n] = '\0';
  return n;
}

static int serial_eq(const GapBuf *gb, const char *expect) {
  uint8_t out[128];
  uint16_t n = gapbuf_serialize(gb, out, (uint16_t)sizeof out);
  uint16_t want = (uint16_t)strlen(expect);
  return n == want && memcmp(out, expect, n) == 0;
}

int test_fileio_save_canonicalizes(void) {
  uint8_t store[128];
  GapBuf gb;
  char disk[128];
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  /* Cooked //+ typed form: uppercase. Save must lowercase it on disk. */
  put(&gb, "LET X = 1\nLET Y = 2");
  EXPECT(editor_file_save(&gb, TMP, 1) == EFIO_OK, 1);
  EXPECT(slurp(TMP, disk, (int)sizeof disk) == 19, 2);
  EXPECT(strcmp(disk, "let x = 1\nlet y = 2") == 0, 3);
  return 0;
}

int test_fileio_load_verbatim(void) {
  uint8_t store[128];
  GapBuf gb;
  FILE *f;
  f = fopen(TMP, "wb");
  EXPECT(f != NULL, 1);
  fputs("let x = 1\nprint(x)", f);
  fclose(f);
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  EXPECT(editor_file_load(&gb, TMP, 1) == EFIO_OK, 2);
  EXPECT(serial_eq(&gb, "let x = 1\nprint(x)"), 3);
  EXPECT(gapbuf_pos(&gb) == 0, 4); /* cursor at the top after load */
  return 0;
}

int test_fileio_round_trip(void) {
  uint8_t store[128];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  put(&gb, "FUNC SQ(X) <%\nRETURN X\n%>");
  EXPECT(editor_file_save(&gb, TMP, 1) == EFIO_OK, 1);
  /* Reload into a fresh buffer; the saved (canonical) bytes come back. */
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  EXPECT(editor_file_load(&gb, TMP, 1) == EFIO_OK, 2);
  /* `<%`/`%>` collapsed to `{`/`}`; keywords lowercased. */
  EXPECT(serial_eq(&gb, "func sq(x) {\nreturn x\n}"), 3);
  return 0;
}

int test_fileio_text_verbatim(void) {
  uint8_t store[128];
  GapBuf gb;
  char disk[128];
  /* A non-.swift path (README.TXT etc.) saves VERBATIM: no auto-lowercase, no
   * `'` case markers, no `<: :> ??!` digraph collapse — so an all-caps file and
   * any literal digraph bytes survive unchanged (the native II+ help form). */
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  put(&gb, "SEE <:1:> AND ?\?!");
  EXPECT(editor_file_save(&gb, TMP_TXT, 0) == EFIO_OK, 1);
  EXPECT(slurp(TMP_TXT, disk, (int)sizeof disk) >= 0, 2);
  EXPECT(strcmp(disk, "SEE <:1:> AND ?\?!") == 0, 3);
  /* The same bytes saved cooked WOULD canonicalise (lowercase + digraphs). */
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  put(&gb, "SEE <:1:> AND ?\?!");
  EXPECT(editor_file_save(&gb, TMP, 1) == EFIO_OK, 4);
  EXPECT(slurp(TMP, disk, (int)sizeof disk) >= 0, 5);
  EXPECT(strcmp(disk, "see [1] and |") == 0, 6);
  return 0;
}

int test_fileio_text_load_verbatim(void) {
  uint8_t store[128];
  GapBuf gb;
  FILE *f;
  /* An all-caps .txt loads verbatim (no input-form rewrite), so the buffer
   * holds the original bytes and re-saves byte-for-byte. */
  f = fopen(TMP_TXT, "wb");
  EXPECT(f != NULL, 1);
  fputs("README\nALL CAPS HELP", f);
  fclose(f);
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  EXPECT(editor_file_load(&gb, TMP_TXT, 0) == EFIO_OK, 2);
  EXPECT(serial_eq(&gb, "README\nALL CAPS HELP"), 3);
  return 0;
}

/* Raw mode on a .swift path: the cooked flag — not the extension — drives
 * canonicalisation now, so a .swift file saved/loaded raw (cooked == 0) is
 * byte-for-byte verbatim. This is the editor's Ctrl-G raw toggle: an all-caps,
 * literal-digraph buffer survives a raw save + raw load unchanged. */
int test_fileio_raw_swift_verbatim(void) {
  uint8_t store[128];
  GapBuf gb;
  char disk[128];
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  put(&gb, "LET X = <:1:>");
  EXPECT(editor_file_save(&gb, TMP, 0) == EFIO_OK, 1);   /* raw save: verbatim */
  EXPECT(slurp(TMP, disk, (int)sizeof disk) >= 0, 2);
  EXPECT(strcmp(disk, "LET X = <:1:>") == 0, 3);
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  EXPECT(editor_file_load(&gb, TMP, 0) == EFIO_OK, 4);   /* raw load: verbatim */
  EXPECT(serial_eq(&gb, "LET X = <:1:>"), 5);
  return 0;
}

int test_fileio_path_is_swift(void) {
  EXPECT(editor_path_is_swift("greet.swift") == 1, 1);
  EXPECT(editor_path_is_swift("/VOL/SAMPLES/GREET.SWIFT") == 1, 2);
  EXPECT(editor_path_is_swift("README.TXT") == 0, 3);
  EXPECT(editor_path_is_swift("notes") == 0, 4);
  EXPECT(editor_path_is_swift("") == 1, 5);    /* untitled scratch -> Swift */
  EXPECT(editor_path_is_swift(".swift") == 1, 6);
  return 0;
}

int test_fileio_load_notfound(void) {
  uint8_t store[16];
  GapBuf gb;
  gapbuf_init(&gb, store, (uint16_t)sizeof store);
  EXPECT(editor_file_load(&gb, "/tmp/swiftii_no_such_file_zzz.swift", 1) ==
         EFIO_NOTFOUND, 1);
  return 0;
}
