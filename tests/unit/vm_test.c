/* VM smoke test: feed the hardcoded hello-world bytecode through the
 * VM and check that the captured stdout contains "hello, world".
 *
 * Uses dup/dup2 to redirect fd 1 to a temp file, so the capture survives
 * non-tty test runs (CI, `make test` from the shell). Will swap
 * this out for a proper platform-output capture abstraction.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "vm/vm.h"
#include "vm/opcodes.h"
#include "common/errors.h"
#include "platform/platform.h"

static const unsigned char k_hello_bc[] = {
  OP_STR, 0x00, 0x00,
  OP_CALL_BUILTIN, BUILTIN_PRINT, 0x01,
  OP_POP,
  OP_HALT
};

int test_vm_hello(void) {
  char buf[256];
  ssize_t n;
  swiftii_err_t rc;
  int saved_fd, capture_fd;
  const char *tmp_path = "/tmp/swiftii_test_out.txt";

  /* Snapshot stdout, redirect to a fresh temp file. */
  fflush(stdout);
  saved_fd = dup(1);
  if (saved_fd < 0) return 1;
  capture_fd = open(tmp_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (capture_fd < 0) return 2;
  if (dup2(capture_fd, 1) < 0) return 3;
  close(capture_fd);

  if (platform_init() != 0) return 4;
  rc = vm_run(k_hello_bc, 0, sizeof(k_hello_bc));
  platform_shutdown();
  fflush(stdout);

  /* Restore stdout. */
  dup2(saved_fd, 1);
  close(saved_fd);

  if (rc != SE_OK) return 5;

  /* Read what the VM wrote. */
  capture_fd = open(tmp_path, O_RDONLY);
  if (capture_fd < 0) return 6;
  n = read(capture_fd, buf, sizeof(buf) - 1);
  close(capture_fd);
  if (n < 0) return 7;
  buf[n] = '\0';

  if (strstr(buf, "hello, world") == NULL) {
    fprintf(stderr, "vm output was: %s\n", buf);
    return 8;
  }
  return 0;
}
