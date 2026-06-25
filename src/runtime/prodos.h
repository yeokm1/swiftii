/* File I/O for the Family B Compiler/Runner.
 *
 * A tiny open/read/write/close API used by compiler_main, runner_main, and
 * the readFile/writeFile builtins (file_io.c). On the Apple II it calls
 * ProDOS MLI directly (via mli.s) with a fixed low-RAM I/O buffer — NOT
 * cc65's POSIX layer, whose ~4 KB of code + malloc'd 1 KB buffer was eating
 * the MAIN-only Compiler's window (capping source size). On the host it is
 * plain stdio so the same callers build and the logic stays testable.
 *
 * One file open at a time: the tools read a file then close it before
 * opening the next, so a single MLI I/O buffer suffices. WITH_SWB-gated
 * (Family B only).
 */
#ifndef SWIFTII_PRODOS_H
#define SWIFTII_PRODOS_H

#ifdef WITH_SWB

#include <stdint.h>

/* Wide enough to hold a ProDOS ref_num (target) or a FILE* (host). On cc65
 * intptr_t is 16-bit; on the host 64-bit, so the host FILE* survives the
 * round-trip. */
typedef intptr_t pf_handle;
#define PF_BAD ((pf_handle)-1)

/* MLI error code from the last failed pf_open_* (target only; 0 on host).
 * For diagnostics — e.g. $46 = file not found, $44 = path not found,
 * $42 = too many files open, $4A = incompatible format. */
extern unsigned char pf_errno;

/* ProDOS file types for pf_open_write. */
#define PF_TYPE_TXT 0x04
#define PF_TYPE_BIN 0x06

/* Open `path` (NUL-terminated, partial paths resolve against the ProDOS
 * prefix) for reading. Returns a handle or PF_BAD. */
pf_handle pf_open_read(const char *path);

/* Create/truncate `path` with ProDOS file type `ftype` + aux `aux`, open
 * for writing. Returns a handle or PF_BAD. */
pf_handle pf_open_write(const char *path, uint8_t ftype, uint16_t aux);

/* Delete the file (or EMPTY directory) at `path`. MLI DESTROY / host
 * remove(). Returns 0 on success, -1 on error (non-empty directory,
 * missing file, locked file...). Ungated: the Compiler calls it to clear a
 * stale .swb as a SEPARATE step before pf_open_write (a back-to-back
 * DESTROY+CREATE in the same call spuriously returned $48 on some disks —
 * compiler_main.c). */
int pf_delete(const char *path);

#ifdef WITH_FILE_CRUD
/* Doc 017 file/dir CRUD — Runner + host only (the Compiler links prodos.c
 * but never calls the rest, and -Cl would turn their locals into dead BSS). */

/* Open `path` for appending (creates it with type `ftype` if absent; the
 * write mark starts at EOF). Returns a handle or PF_BAD. */
pf_handle pf_open_append(const char *path, uint8_t ftype);

/* Rename/move `oldp` to `newp` (same volume on ProDOS). Returns 0 on
 * success, -1 on error. */
int pf_rename(const char *oldp, const char *newp);

/* 1 if `path` names an existing file or directory, else 0. MLI
 * GET_FILE_INFO / host stat(). */
int pf_exists(const char *path);

/* Create a directory at `path`. Returns 0 on success, -1 on error
 * (exists, missing parent...). */
int pf_mkdir(const char *path);
#endif /* WITH_FILE_CRUD */

/* Size of an open file (MLI GET_EOF / host ftell), saturated to
 * 64 KB - 1; 0 on error. Used for compile-progress percentages. */
uint16_t pf_size(pf_handle h);

/* Read up to `n` bytes into `buf`; returns the count (0 at EOF) or -1. */
int pf_read(pf_handle h, unsigned char *buf, uint16_t n);

/* Write `n` bytes from `buf`; returns 0 on success, -1 on error. */
int pf_write(pf_handle h, const unsigned char *buf, uint16_t n);

void pf_close(pf_handle h);

#endif /* WITH_SWB */
#endif /* SWIFTII_PRODOS_H */
