/* REPL driver.
 *
 * Hosts the read-compile-eval loop on top of the pipeline.
 * Globals and the compiler's symbol table persist across iterations
 * so a session feels continuous; meta-commands (`:help`, `:list`,
 * etc.) are dispatched by `repl/metacmds.c` before each input line is
 * passed to the compiler.
 */
#ifndef SWIFTII_REPL_H
#define SWIFTII_REPL_H

/* Run the REPL until EOF on stdin or `:quit`. Returns 0 on a clean
 * exit; nonzero is reserved for future fatal errors. Resets the
 * symbol table and VM globals on entry. */
int repl_run(void);

#endif /* SWIFTII_REPL_H */
