/* Editor file I/O — see fileio.h.
 *
 * Buffers that hold a whole line are static (BSS), never on the C stack:
 * the editor's cc65 software stack is only 1 KB, and a couple of
 * line-sized arrays would blow it. */
#include "fileio.h"

#include "textnav.h"
#include "../platform/apple2/input.h"
#include "../runtime/prodos.h"   /* raw-MLI/stdio file I/O */

#include <stdint.h>

/* //e holds canonical bytes already; //+ (and the host tests) canonicalise
 * each line on save. The //e disk's launcher build is tagged LITE_IIE, which
 * means "native-case //e, write verbatim". */
#ifdef LITE_IIE
#define EFIO_CANON 0
#else
#define EFIO_CANON 1
#endif

#define READ_CHUNK 256
#define LINE_MAX   511  /* longest source line save canonicalises in one go */

/* A path names a Swift source iff its name ends in ".swift" (case-insensitive).
 * The pre-IIe digraph entry (input_translate) and the editor's digraph display
 * expansion (screen.c) are a Swift-syntax convenience, so they apply only to
 * .swift files; a plain text file such as README.TXT edits and saves its
 * `<% | { }` bytes verbatim. An empty path (untitled scratch) defaults to
 * Swift, the editor's primary use. */
int editor_path_is_swift(const char *path) {
  uint16_t n = 0;
  const char *e;
  if (path == 0 || path[0] == '\0') return 1;
  while (path[n]) ++n;
  if (n < 6) return 0;
  e = path + (uint16_t)(n - 6);
  return e[0] == '.'
      && (e[1] == 's' || e[1] == 'S')
      && (e[2] == 'w' || e[2] == 'W')
      && (e[3] == 'i' || e[3] == 'I')
      && (e[4] == 'f' || e[4] == 'F')
      && (e[5] == 't' || e[5] == 'T');
}

static uint8_t s_chunk[READ_CHUNK];
static char s_line[LINE_MAX + 1];
#if EFIO_CANON
static char s_canon[LINE_MAX + 1];
#endif

/* File handles over prodos.c — raw ProDOS MLI on target (a fixed $1C00 I/O
 * buffer), stdio on host. NOT cc65's open()/read()/write(): that pulls ~4 KB
 * of POSIX-I/O code AND malloc's a 1 KB ProDOS buffer per open, which starved
 * the launcher's tiny heap once the editor grew, so opening a file failed.
 * prodos.c's fixed buffer needs no heap. The editor and the launcher's own asm
 * MLI are time-disjoint, so sharing $1C00 is safe. */
typedef pf_handle efio_handle;
#define EFIO_BAD PF_BAD
static efio_handle h_open_read(const char *path) { return pf_open_read(path); }
static efio_handle h_open_write(const char *path) {
  /* TXT so the launcher treats a saved file as a runnable .swift. */
  return pf_open_write(path, PF_TYPE_TXT, 0x0000);
}
static int h_read(efio_handle h, void *buf, unsigned n) {
  return pf_read(h, (unsigned char *)buf, (uint16_t)n);
}
static int h_write(efio_handle h, const void *buf, unsigned n) {
  return (pf_write(h, (const unsigned char *)buf, (uint16_t)n) == 0)
             ? (int)n : -1;
}
static void h_close(efio_handle h) { pf_close(h); }

/* Stream the file bytes straight into the gap buffer, no translation. Used for
 * the //e build, and on //+ for a plain text file (a non-.swift file is taken
 * verbatim so an all-caps README round-trips + displays natively). 0 ok, -1 if
 * the buffer fills. */
static int load_verbatim(GapBuf *gb, efio_handle h) {
  int n;
  uint16_t i;
  for (;;) {
    n = h_read(h, s_chunk, READ_CHUNK);
    if (n <= 0) break;
    for (i = 0; i < (uint16_t)n; ++i)
      if (!gapbuf_insert(gb, s_chunk[i])) return -1;
  }
  return 0;
}

#if EFIO_CANON
/* //+ load: insert one canonical line (s_line[0..len)) as input form so the
 * save-time input_translate round-trips it. Returns 0 ok, -1 if buffer full. */
static int load_flush_line(GapBuf *gb, uint16_t len) {
  uint16_t clen = input_untranslate(s_line, len, s_canon, LINE_MAX);
  uint16_t j;
  for (j = 0; j < clen; ++j)
    if (!gapbuf_insert(gb, (unsigned char)s_canon[j])) return -1;
  return 0;
}
#endif

EditorFileResult editor_file_load(GapBuf *gb, const char *path, int cooked) {
  efio_handle h;
  int over = 0;
#if !EFIO_CANON
  (void)cooked;                         /* //e: always verbatim */
#endif
  h = h_open_read(path);
  if (h == EFIO_BAD) return EFIO_NOTFOUND;
  gapbuf_init(gb, gb->buf, gb->cap); /* empty, reusing the same storage */
#if EFIO_CANON
  /* //+ cooked: a file is rewritten canonical -> input form (lowercase + '
   * case markers) per line, mirroring the per-line save canonicalize, so
   * capitals like `readLine` survive a load+save instead of lowercasing to an
   * undeclared name. Raw (cooked == 0, the default for a non-.swift file, or
   * any file the user toggled to raw) is taken verbatim (no Swift typing
   * model), so an all-caps README loads + displays + saves unchanged. */
  if (cooked) {
    int n;
    uint16_t i;
    uint16_t ll = 0;
    uint8_t last_cr = 0;
    for (;;) {
      n = h_read(h, s_chunk, READ_CHUNK);
      if (n <= 0) break;
      for (i = 0; i < (uint16_t)n; ++i) {
        unsigned char c = s_chunk[i];
        if (c == '\n' && last_cr) { last_cr = 0; continue; } /* CRLF -> one */
        last_cr = (uint8_t)(c == '\r');
        if (c == '\n' || c == '\r') {
          if (load_flush_line(gb, ll) != 0 || !gapbuf_insert(gb, '\n')) {
            over = 1; break;
          }
          ll = 0;
        } else if (ll < LINE_MAX) {
          s_line[ll++] = (char)c;
        }
        /* else: over-long line, drop overflow (matches save's clamp) */
      }
      if (over) break;
    }
    if (!over && ll > 0 && load_flush_line(gb, ll) != 0) over = 1;
  } else
#endif
  {
    over = (load_verbatim(gb, h) != 0);
  }
  if (over) {
    h_close(h); gapbuf_init(gb, gb->buf, gb->cap); return EFIO_TOOBIG;
  }
  h_close(h);
  gapbuf_move_to(gb, 0);
  return EFIO_OK;
}

EditorFileResult editor_file_save(const GapBuf *gb, const char *path,
                                  int cooked) {
  efio_handle h;
  uint16_t count;
  uint16_t i;
  uint16_t start;
  uint16_t end;
  uint16_t len;
  uint16_t j;
  static const char nl = '\n';
#if !EFIO_CANON
  (void)cooked;                         /* //e: always verbatim */
#endif
  /* h_open_write stamps the new file TXT ($04) via pf_open_write, so the
   * launcher treats a saved file as a runnable .swift. */
  h = h_open_write(path);
  if (h == EFIO_BAD) return EFIO_IOERR;
  count = textnav_line_count(gb);
  for (i = 0; i < count; ++i) {
    start = textnav_line_at(gb, i);
    end = textnav_line_end(gb, start);
    len = (uint16_t)(end - start);
    if (len > LINE_MAX) len = LINE_MAX; /* over-long line clamps (rare) */
    for (j = 0; j < len; ++j) s_line[j] = (char)gapbuf_at(gb, (uint16_t)(start + j));
#if EFIO_CANON
    if (cooked) {
      uint16_t clen = input_translate(s_line, len, s_canon, LINE_MAX);
      if (h_write(h, s_canon, clen) != (int)clen) { h_close(h); return EFIO_IOERR; }
    } else {
      if (h_write(h, s_line, len) != (int)len) { h_close(h); return EFIO_IOERR; }
    }
#else
    if (h_write(h, s_line, len) != (int)len) { h_close(h); return EFIO_IOERR; }
#endif
    if ((uint16_t)(i + 1) < count) {
      if (h_write(h, &nl, 1) != 1) { h_close(h); return EFIO_IOERR; }
    }
  }
  h_close(h);
  return EFIO_OK;
}
