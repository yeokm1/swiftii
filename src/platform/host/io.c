/* Host platform: stdio-backed console I/O.
 *
 * Compiled only under clang for tests and host-side development. Never
 * compiled by cc65 — `printf`/`stdio.h` are forbidden on the target
 * (docs/contributing/CONSTRAINTS.md rule 3) and would pull in the cc65 formatting runtime.
 */
#include "../platform.h"

#include <stdio.h>
#include <stdint.h>

int platform_init(void) {
  return 0;
}

void platform_write(const char *buf, uint16_t len) {
  fwrite(buf, 1, len, stdout);
}

void platform_write_str(const char *s) {
  fputs(s, stdout);
}

void platform_putc(char c) {
  fputc(c, stdout);
}

void platform_clear_screen(void) {
  /* ANSI: clear screen + cursor to (1,1). Works on most modern
   * terminals; harmless garbage on those without ANSI support. */
  fputs("\x1b[2J\x1b[H", stdout);
  fflush(stdout);
}

void platform_htab(uint8_t col) {
  /* No-op on the host: emitting a cursor-move escape would pollute
   * captured test output. The cc65 build does the real gotoxy. */
  (void)col;
}

void platform_vtab(uint8_t row) {
  (void)row;
}

uint8_t platform_read_file(const char *path, char *buf,
                           uint16_t max, uint16_t *out_len) {
  FILE *fp = fopen(path, "rb");
  size_t n;
  if (!fp) return 1;
  n = fread(buf, 1, max, fp);
  if (n == max && !feof(fp)) { /* more bytes remain → too large */
    fclose(fp);
    return 2;
  }
  fclose(fp);
  *out_len = (uint16_t)n;
  return 0;
}

void platform_shutdown(void) {
  fflush(stdout);
}

void platform_wait_ms(uint16_t ms) {
  /* No-op on the host: a real sleep would make test runs wall-clock
   * dependent for no benefit. The cc65 build does the real delay. */
  (void)ms;
}

void platform_tone(uint16_t half_period, uint16_t cycles) {
  /* No-op on the host: there is no Apple II speaker, and emitting real audio
   * would make tests environment-dependent. The cc65 build toggles $C030. */
  (void)half_period;
  (void)cycles;
}
