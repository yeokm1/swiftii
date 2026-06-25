/* File-mode driver: open, stream, compile, execute.
 *
 * Implementation: loads the entire source file into a single
 * fixed-size buffer (FILE_SRC_SIZE), compiles, runs. Block-streaming
 * for large programs arrives.
 */
#ifndef SWIFTII_FILE_RUNNER_H
#define SWIFTII_FILE_RUNNER_H

/* Run the program at `path` in file mode. Returns 0 on success;
 * 1 if the file cannot be opened or exceeds FILE_SRC_SIZE; 2 on a
 * compile error; 3 on a runtime error. Error messages are written to
 * the platform console with a `prefix: line N: msg` format. */
int file_runner_run(const char *path);

#endif /* SWIFTII_FILE_RUNNER_H */
