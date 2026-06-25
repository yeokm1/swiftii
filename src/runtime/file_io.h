/* Family B — user-program file I/O (doc 015).
 *
 * Backs the Swift-level readFile/writeFile builtins. Only the MAIN-only
 * Runner (and the host) compile this: it uses cc65's POSIX I/O over ProDOS
 * MLI, which the Family A interpreters can't reach (their LC holds the
 * interpreter, not MLI). WITH_SWB-gated so it never links into the
 * at-ceiling interpreters.
 */
#ifndef SWIFTII_FILE_IO_H
#define SWIFTII_FILE_IO_H

#ifdef WITH_SWB

#include <stdint.h>

/* Largest file readFile can return in one call (it lands in a static
 * buffer, then a heap string). Overridable via -D. */
#ifndef USERFILE_READ_CAP
#define USERFILE_READ_CAP 1024
#endif

/* Read the whole file at `path` (NUL-terminated) into an internal buffer;
 * on success points `*out` at the bytes and returns the length (0..
 * USERFILE_READ_CAP). Returns -1 if the file can't be opened. A file longer
 * than the cap is truncated to the cap (still a success). The buffer is
 * valid until the next userfile_read call. */
int16_t userfile_read(const char *path, const unsigned char **out);

/* Create/truncate `path` and write `len` bytes from `buf`. Returns 0 on
 * success, -1 on any open/write error. */
int userfile_write(const char *path, const unsigned char *buf, uint16_t len);

/* Append `len` bytes from `buf` to `path`, creating it (TXT) if absent.
 * Returns 0 on success, -1 on any open/write error. Doc 017. */
int userfile_append(const char *path, const unsigned char *buf, uint16_t len);

/* Directory enumeration (doc 017). One walk at a time; userdir_open reuses
 * the readFile block buffer, so a readFile call between open and close
 * corrupts the walk (the VM builtin runs a walk to completion in one
 * dispatch, so Swift code can't observe this). */

/* Begin listing directory `path`. Returns 0 on success, -1 if it can't be
 * opened. */
int userdir_open(const char *path);

/* Write the next entry's name (NUL-terminated) into `name` (>= 16 bytes)
 * and its length into `*len`. Returns 1 if a name was produced, 0 at the
 * end of the directory. Skips the header and deleted entries. */
int userdir_next(char *name, unsigned char *len);

/* Close the walk started by userdir_open (idempotent). */
void userdir_close(void);

#endif /* WITH_SWB */
#endif /* SWIFTII_FILE_IO_H */
