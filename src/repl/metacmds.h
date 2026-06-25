/* REPL meta-commands: `:help`, `:quit`, `:list`, `:mem`, `:home`,
 * `:reset`.
 *
 * A meta-command is any non-blank input line whose first non-space
 * character is ':'. The REPL dispatches to `metacmds_handle` before
 * trying to compile the line.
 */
#ifndef SWIFTII_METACMDS_H
#define SWIFTII_METACMDS_H

#include <stdint.h>

typedef enum metacmd_result {
  METACMD_NOT_META = 0, /* line does not start with ':' */
  METACMD_HANDLED  = 1, /* a meta-command ran; keep the REPL going */
  METACMD_QUIT     = 2  /* user asked to exit the REPL */
} metacmd_result_t;

/* Returns whether `line` was a meta-command and, if so, what the REPL
 * should do next. `len` is the byte length not counting the trailing
 * NUL; a trailing '\n' is tolerated. */
metacmd_result_t metacmds_handle(const char *line, uint16_t len);

#endif /* SWIFTII_METACMDS_H */
