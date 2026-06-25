/* Host keyboard input.
 *
 * Reads a line from stdin via fgets so REPL pipelines in tests/repl
 * stream cleanly. Never compiled by cc65 — the Apple II backend lives
 * in src/platform/apple2/keyboard.c.
 */
#include "../platform.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

int16_t platform_read_line(char *buf, uint16_t max_len) {
  size_t n;
  if (max_len < 2) return 0;
  if (fgets(buf, (int)max_len, stdin) == NULL) {
    buf[0] = '\0';
    return 0;
  }
  n = strlen(buf);
  return (int16_t)n;
}

/* No keypress timing on the host — return 0 so random() stays deterministic
 * and tests/integration/025_random reproduces its fixed sequence. */
uint16_t platform_entropy(void) { return 0; }
