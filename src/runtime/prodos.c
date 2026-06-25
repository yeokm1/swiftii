/* Family B file I/O. See prodos.h. */
#include "prodos.h"

#ifdef WITH_SWB

#ifdef __CC65__

/* ---- Apple II: direct ProDOS MLI (no cc65 stdio / malloc) ---- */

extern unsigned char __fastcall__ mli(void *params, unsigned char cmd);

#define MLI_CREATE   0xC0
#define MLI_DESTROY  0xC1
#define MLI_RENAME   0xC2
#define MLI_GET_INFO 0xC4
#define MLI_OPEN     0xC8
#define MLI_READ     0xCA
#define MLI_WRITE    0xCB
#define MLI_CLOSE    0xCC
#define MLI_SET_MARK 0xCE
#define MLI_GET_EOF  0xD1

/* MLI's OPEN needs a 1 KB page-aligned I/O buffer in main RAM. $1C00-$1FFF
 * is free below the $2000 load area (the launcher used it during the chain,
 * but it has handed off by the time we run) and dodges the staged-path page
 * ($0C00) + staged-len ($1B06). One open at a time, so one buffer. */
#define MLI_IO_BUFFER ((void *)0x1C00)

/* ProDOS classic length-prefixed pathname (byte 0 = length). Partial paths
 * resolve against the system prefix. Uppercased — ProDOS is case-folding,
 * and the //+ typing model produces lowercase. */
static unsigned char s_path[66];

/* `dst` must hold 1 + 64 bytes (ProDOS caps paths at 64 chars). RENAME
 * needs a second pathname, built into a caller-stack buffer — the Runner's
 * BSS slack (doc 016) doesn't fund a second static. */
static void to_prodos_path_buf(const char *path, unsigned char *dst) {
  unsigned char n = 0;
  while (path[n] && n < 64) {
    char c = path[n];
    if (c >= 'a' && c <= 'z') c = (char)(c - 0x20);
    dst[1 + n] = (unsigned char)c;
    ++n;
  }
  dst[0] = n;
}

static void to_prodos_path(const char *path) {
  to_prodos_path_buf(path, s_path);
}

/* Parameter blocks — exact ProDOS MLI layouts (cc65 packs tightly). */
typedef struct {
  unsigned char count;
  void *path;
  void *io_buffer;
  unsigned char ref_num;
} open_parm;

typedef struct {
  unsigned char count;
  unsigned char ref_num;
  void *data;
  uint16_t request;
  uint16_t transfer;
} rw_parm;

typedef struct {
  unsigned char count;
  unsigned char ref_num;
} close_parm;

typedef struct {
  unsigned char count;
  void *path;
} path_parm;

typedef struct {
  unsigned char count;
  void *path;
  unsigned char access;
  unsigned char file_type;
  uint16_t aux_type;
  unsigned char storage_type;
  uint16_t create_date;
  uint16_t create_time;
} create_parm;

typedef struct {
  unsigned char count;
  unsigned char ref_num;
  unsigned char eof[3];      /* 24-bit little-endian EOF / mark */
} eof_parm;

#ifdef WITH_FILE_CRUD
typedef struct {
  unsigned char count;
  void *path;
  void *new_path;
} rename_parm;

/* GET_FILE_INFO ($C4) — full 10-parameter block; only the error code is
 * consulted (existence probe). */
typedef struct {
  unsigned char count;
  void *path;
  unsigned char access;
  unsigned char file_type;
  uint16_t aux_type;
  unsigned char storage_type;
  uint16_t blocks_used;
  uint16_t mod_date;
  uint16_t mod_time;
  uint16_t create_date;
  uint16_t create_time;
} info_parm;
#endif /* WITH_FILE_CRUD */

/* Last MLI error code from a failed pf_open_* / pf_write (for
 * diagnostics — e.g. $48 = volume full when a .swb doesn't fit). */
unsigned char pf_errno;

pf_handle pf_open_read(const char *path) {
  open_parm p;
  unsigned char e;
  to_prodos_path(path);
  p.count = 3;
  p.path = s_path;
  p.io_buffer = MLI_IO_BUFFER;
  p.ref_num = 0;
  e = mli(&p, MLI_OPEN);
  if (e != 0) { pf_errno = e; return PF_BAD; }
  return (pf_handle)p.ref_num;
}

pf_handle pf_open_write(const char *path, uint8_t ftype, uint16_t aux) {
  create_parm c;
  path_parm d;
  open_parm o;

  to_prodos_path(path);

  /* Truncate by DESTROY (ignore "not found") then CREATE fresh. */
  d.count = 1;
  d.path = s_path;
  (void)mli(&d, MLI_DESTROY);

  c.count = 7;
  c.path = s_path;
  c.access = 0xC3;            /* read/write/rename/destroy */
  c.file_type = ftype;
  c.aux_type = aux;
  c.storage_type = 1;        /* standard (seedling/sapling) file */
  c.create_date = 0;
  c.create_time = 0;
  {
    unsigned char e = mli(&c, MLI_CREATE);
    if (e != 0) { pf_errno = e; return PF_BAD; }
  }

  o.count = 3;
  o.path = s_path;           /* still holds this path */
  o.io_buffer = MLI_IO_BUFFER;
  o.ref_num = 0;
  {
    unsigned char e = mli(&o, MLI_OPEN);
    if (e != 0) { pf_errno = e; return PF_BAD; }
  }
  return (pf_handle)o.ref_num;
}

#ifdef WITH_FILE_CRUD
pf_handle pf_open_append(const char *path, uint8_t ftype) {
  open_parm o;
  eof_parm m;

  to_prodos_path(path);
  o.count = 3;
  o.path = s_path;
  o.io_buffer = MLI_IO_BUFFER;
  o.ref_num = 0;
  if (mli(&o, MLI_OPEN) != 0) {
    /* Not there (or unopenable) — create fresh and retry once. */
    create_parm c;
    unsigned char e;
    c.count = 7;
    c.path = s_path;
    c.access = 0xC3;
    c.file_type = ftype;
    c.aux_type = 0;
    c.storage_type = 1;
    c.create_date = 0;
    c.create_time = 0;
    e = mli(&c, MLI_CREATE);
    if (e != 0) { pf_errno = e; return PF_BAD; }
    e = mli(&o, MLI_OPEN);
    if (e != 0) { pf_errno = e; return PF_BAD; }
  }

  /* Park the mark at EOF: GET_EOF and SET_MARK share the param layout,
   * so the 24-bit position round-trips untouched. */
  m.count = 2;
  m.ref_num = o.ref_num;
  m.eof[0] = m.eof[1] = m.eof[2] = 0;
  if (mli(&m, MLI_GET_EOF) != 0 || mli(&m, MLI_SET_MARK) != 0) {
    pf_close((pf_handle)o.ref_num);
    return PF_BAD;
  }
  return (pf_handle)o.ref_num;
}
#endif /* WITH_FILE_CRUD — pf_open_append */

#if defined(WITH_FILE_CRUD) || defined(WITH_BIGLANG)
/* Runner (WITH_FILE_CRUD) + Compiler (WITH_BIGLANG). The Compiler calls it to
 * clear a stale .swb as a SEPARATE step before pf_open_write (see prodos.h /
 * compiler_main.c). NOT compiled into the launcher / Family A interpreters
 * (neither flag) so cc65 -Cl doesn't park its locals as dead BSS there. */
int pf_delete(const char *path) {
  path_parm p;
  unsigned char e;
  to_prodos_path(path);
  p.count = 1;
  p.path = s_path;
  e = mli(&p, MLI_DESTROY);
  if (e != 0) { pf_errno = e; return -1; }
  return 0;
}
#endif

#ifdef WITH_FILE_CRUD
int pf_rename(const char *oldp, const char *newp) {
  rename_parm p;
  unsigned char new_path[66];
  unsigned char e;
  to_prodos_path(oldp);
  to_prodos_path_buf(newp, new_path);
  p.count = 2;
  p.path = s_path;
  p.new_path = new_path;
  e = mli(&p, MLI_RENAME);
  if (e != 0) { pf_errno = e; return -1; }
  return 0;
}

int pf_exists(const char *path) {
  info_parm p;
  to_prodos_path(path);
  p.count = 0x0A;
  p.path = s_path;
  return mli(&p, MLI_GET_INFO) == 0 ? 1 : 0;
}

int pf_mkdir(const char *path) {
  create_parm c;
  unsigned char e;
  to_prodos_path(path);
  c.count = 7;
  c.path = s_path;
  c.access = 0xC3;
  c.file_type = 0x0F;        /* DIR */
  c.aux_type = 0;
  c.storage_type = 0x0D;     /* linked directory */
  c.create_date = 0;
  c.create_time = 0;
  e = mli(&c, MLI_CREATE);
  if (e != 0) { pf_errno = e; return -1; }
  return 0;
}
#endif /* WITH_FILE_CRUD */

/* File size of an open file via GET_EOF, saturated to 64 KB - 1 (more
 * than any SwiftII source). 0 on error. */
uint16_t pf_size(pf_handle h) {
  eof_parm p;
  p.count = 2;
  p.ref_num = (unsigned char)h;
  p.eof[0] = p.eof[1] = p.eof[2] = 0;
  if (mli(&p, MLI_GET_EOF) != 0) return 0;
  if (p.eof[2] != 0) return 0xFFFFu;
  return (uint16_t)(p.eof[0] | ((uint16_t)p.eof[1] << 8));
}

int pf_read(pf_handle h, unsigned char *buf, uint16_t n) {
  rw_parm p;
  p.count = 4;
  p.ref_num = (unsigned char)h;
  p.data = buf;
  p.request = n;
  p.transfer = 0;
  /* MLI returns error $4C (EOF) when nothing more to read; treat as 0. */
  if (mli(&p, MLI_READ) != 0) return (int)p.transfer; /* short/EOF read */
  return (int)p.transfer;
}

int pf_write(pf_handle h, const unsigned char *buf, uint16_t n) {
  rw_parm p;
  unsigned char e;
  p.count = 4;
  p.ref_num = (unsigned char)h;
  p.data = (void *)buf;
  p.request = n;
  p.transfer = 0;
  e = mli(&p, MLI_WRITE);
  if (e != 0) { pf_errno = e; return -1; }   /* $48 = volume full */
  return (p.transfer == n) ? 0 : -1;
}

void pf_close(pf_handle h) {
  close_parm p;
  p.count = 1;
  p.ref_num = (unsigned char)h;
  (void)mli(&p, MLI_CLOSE);
}

#else  /* host: plain stdio + POSIX */

#include <stdio.h>
#include <sys/stat.h>

unsigned char pf_errno;

pf_handle pf_open_read(const char *path) {
  FILE *f = fopen(path, "rb");
  return f ? (pf_handle)(intptr_t)f : PF_BAD;
}

pf_handle pf_open_write(const char *path, uint8_t ftype, uint16_t aux) {
  FILE *f;
  (void)ftype; (void)aux;
  f = fopen(path, "wb");
  return f ? (pf_handle)(intptr_t)f : PF_BAD;
}

#ifdef WITH_FILE_CRUD
pf_handle pf_open_append(const char *path, uint8_t ftype) {
  FILE *f;
  (void)ftype;
  f = fopen(path, "ab");
  return f ? (pf_handle)(intptr_t)f : PF_BAD;
}

int pf_delete(const char *path) {
  return remove(path) == 0 ? 0 : -1;
}

int pf_rename(const char *oldp, const char *newp) {
  return rename(oldp, newp) == 0 ? 0 : -1;
}

int pf_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 ? 1 : 0;
}

int pf_mkdir(const char *path) {
  return mkdir(path, 0777) == 0 ? 0 : -1;
}
#endif /* WITH_FILE_CRUD */

uint16_t pf_size(pf_handle h) {
  FILE *f = (FILE *)(intptr_t)h;
  long pos = ftell(f);
  long end;
  if (pos < 0 || fseek(f, 0, SEEK_END) != 0) return 0;
  end = ftell(f);
  fseek(f, pos, SEEK_SET);
  if (end < 0) return 0;
  return (end > 0xFFFFL) ? 0xFFFFu : (uint16_t)end;
}

int pf_read(pf_handle h, unsigned char *buf, uint16_t n) {
  return (int)fread(buf, 1, n, (FILE *)(intptr_t)h);
}

int pf_write(pf_handle h, const unsigned char *buf, uint16_t n) {
  return ((uint16_t)fwrite(buf, 1, n, (FILE *)(intptr_t)h) == n) ? 0 : -1;
}

void pf_close(pf_handle h) {
  fclose((FILE *)(intptr_t)h);
}

#endif  /* __CC65__ */

#endif  /* WITH_SWB */
