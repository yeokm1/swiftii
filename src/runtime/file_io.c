/* Family B — user-program file I/O. See file_io.h. */
#include "file_io.h"

#ifdef WITH_SWB

#include "prodos.h"

static unsigned char s_read_buf[USERFILE_READ_CAP];

int16_t userfile_read(const char *path, const unsigned char **out) {
  pf_handle h;
  uint16_t len = 0;
  int n;
  h = pf_open_read(path);
  if (h == PF_BAD) return -1;
  for (;;) {
    if (len >= (uint16_t)USERFILE_READ_CAP) break;
    n = pf_read(h, s_read_buf + len, (uint16_t)(USERFILE_READ_CAP - len));
    if (n <= 0) break;
    len = (uint16_t)(len + n);
  }
  pf_close(h);
  *out = s_read_buf;
  return (int16_t)len;
}

int userfile_write(const char *path, const unsigned char *buf, uint16_t len) {
  pf_handle h = pf_open_write(path, PF_TYPE_TXT, 0x0000);
  if (h == PF_BAD) return -1;
  if (len > 0 && pf_write(h, buf, len) != 0) { pf_close(h); return -1; }
  pf_close(h);
  return 0;
}

int userfile_append(const char *path, const unsigned char *buf, uint16_t len) {
  pf_handle h = pf_open_append(path, PF_TYPE_TXT);
  if (h == PF_BAD) return -1;
  if (len > 0 && pf_write(h, buf, len) != 0) { pf_close(h); return -1; }
  pf_close(h);
  return 0;
}

/* ---- Directory enumeration (doc 017) ---- */

#ifdef __CC65__

/* ProDOS directory layout (ProDOS 8 TRM ch. 4): a directory is a chain of
 * 512-byte blocks. Each block starts with a 4-byte prev/next link, then 13
 * fixed 39-byte entries. The very first entry of the first block is the
 * volume/subdirectory header. An entry's byte 0 is (storage_type << 4) |
 * name_length; storage_type 0 marks a deleted/empty slot. Reading the
 * directory as a file (pf_read) yields these raw blocks, links included. */
#define PF_DIR_BLOCK       512
#define PF_DIR_ENTRY_LEN   0x27
#define PF_DIR_PER_BLOCK   13
#define PF_DIR_FIRST_OFF   4

static pf_handle s_dir_h = PF_BAD;
static unsigned char s_dir_entry;  /* next entry index within the block */
static unsigned char s_dir_header; /* 1 until the header entry is skipped */

int userdir_open(const char *path) {
  s_dir_h = pf_open_read(path);
  if (s_dir_h == PF_BAD) return -1;
  s_dir_entry = PF_DIR_PER_BLOCK;  /* force a block read on first next() */
  s_dir_header = 1;
  return 0;
}

int userdir_next(char *name, unsigned char *len) {
  if (s_dir_h == PF_BAD) return 0;
  for (;;) {
    unsigned int off;
    unsigned char tb, st, nl, i;
    if (s_dir_entry >= PF_DIR_PER_BLOCK) {
      /* Directories are whole 512-byte blocks; a short read is the end. */
      if (pf_read(s_dir_h, s_read_buf, PF_DIR_BLOCK) < PF_DIR_BLOCK) return 0;
      s_dir_entry = 0;
    }
    off = PF_DIR_FIRST_OFF + (unsigned int)s_dir_entry * PF_DIR_ENTRY_LEN;
    ++s_dir_entry;
    if (s_dir_header) { s_dir_header = 0; continue; }  /* dir header entry */
    tb = s_read_buf[off];
    st = (unsigned char)(tb >> 4);
    nl = (unsigned char)(tb & 0x0F);
    if (st == 0 || nl == 0) continue;                  /* deleted / empty */
    for (i = 0; i < nl; ++i) name[i] = (char)s_read_buf[off + 1 + i];
    name[nl] = '\0';
    *len = nl;
    return 1;
  }
}

void userdir_close(void) {
  if (s_dir_h != PF_BAD) { pf_close(s_dir_h); s_dir_h = PF_BAD; }
}

#else  /* host: POSIX dirent */

#include <dirent.h>
#include <string.h>

static DIR *s_dir;

int userdir_open(const char *path) {
  s_dir = opendir(path);
  return s_dir ? 0 : -1;
}

int userdir_next(char *name, unsigned char *len) {
  struct dirent *e;
  if (!s_dir) return 0;
  for (;;) {
    size_t n;
    e = readdir(s_dir);
    if (!e) return 0;
    if (e->d_name[0] == '.') continue;   /* skip ., .. and dotfiles */
    n = strlen(e->d_name);
    if (n > 15) n = 15;                   /* ProDOS name cap */
    memcpy(name, e->d_name, n);
    name[n] = '\0';
    *len = (unsigned char)n;
    return 1;
  }
}

void userdir_close(void) {
  if (s_dir) { closedir(s_dir); s_dir = NULL; }
}

#endif /* __CC65__ */

#endif /* WITH_SWB */
