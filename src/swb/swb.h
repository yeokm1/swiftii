/* `.swb` on-disk bytecode format (doc 015).
 *
 * The compiler and runner are separate MAIN-only binaries that hand off
 * through a disk file, not RAM (an interpreter maps its code over ProDOS's
 * MLI body and so can't chain — only a MAIN-only tool can). `.swb` is that
 * file: a serialised, re-runnable bytecode artifact (Pascal `.CODE`).
 *
 * Image layout (all multi-byte fields little-endian):
 *
 *   off  size  field
 *   0    3     magic 'S','W','B'
 *   3    1     version (SWB_VERSION)
 *   4    2     program_start   (VM entry PC; skips the function arena)
 *   6    2     bc_len          (total bytecode length)
 *   8    2     heap_len        (constant-pool bytes = bump - STRING_POOL_SLOTS)
 *   10   1     funcs_count     (0..MAX_FUNCS)
 *   11   1     reserved (0)
 *   12        bytecode[bc_len]
 *   ...       heap-const[heap_len]                (reproduced at heap off 16)
 *   ...       funcs[funcs_count]: { bc_start:u16, param_count:u8, has_return:u8 }
 *
 * Globals do NOT travel (filled at runtime by OP_DEFINE_GLOBAL). The funcs
 * table's runtime fields DO travel: the VM's OP_CALL resolves the target
 * via funcs_get_start/funcs_get_param_count, and the runner never compiles,
 * so it must rebuild that table from the file. Heap offsets in OP_STR are
 * array-relative indices into s_heap[] (from STRING_POOL_SLOTS), not RAM
 * addresses, so reproducing the byte image makes them resolve unchanged.
 */
#ifndef SWIFTII_SWB_H
#define SWIFTII_SWB_H

#include <stdint.h>

/* Version policy: bump SWB_VERSION whenever the bytecode opcode set, the
 * builtin id assignments, or the funcs-record layout changes (e.g. a
 * stretch goal adding `switch` opcodes). The version check in
 * parse_header then rejects a mismatched Compiler/Runner pair with "bad
 * .swb image" up front, instead of an old Runner dying mid-program on an
 * opcode it has never heard of. */
#define SWB_VERSION     1u
#define SWB_HEADER_SIZE 12u
#define SWB_FUNC_SIZE   4u   /* bc_start(2) + param_count(1) + has_return(1) */

typedef enum {
  SWB_OK = 0,
  SWB_ERR_OUT_FULL,   /* serialise: output buffer too small */
  SWB_ERR_IO,         /* serialise (stream): a writer callback failed */
  SWB_ERR_MAGIC,      /* deserialise: bad magic or version */
  SWB_ERR_TRUNC,      /* deserialise: input shorter than the header claims */
  SWB_ERR_BC_CAP,     /* deserialise: bytecode exceeds the caller's buffer */
  SWB_ERR_HEAP_CAP,   /* deserialise: constant pool exceeds the heap */
  SWB_ERR_FUNCS_CAP,  /* deserialise: funcs_count exceeds MAX_FUNCS */
  SWB_ERR_BOUNDS      /* deserialise: program_start / a func start PC lies
                         outside the bytecode (corrupt or truncated image) */
} swb_err_t;

/* Sink for swb_write_stream: write `n` bytes from `buf`; return 0 on
 * success, nonzero on I/O error. Lets the Compiler stream a `.swb`
 * straight to disk (MLI WRITE) without building a full image buffer in
 * its tight RAM window. */
typedef int (*swb_writer)(void *ctx, const unsigned char *buf, uint16_t n);

/* Serialise a freshly compiled program into a `.swb` image in `out`.
 * `bc`/`bc_len` are the compiler's bytecode (bcbuf_data() + the
 * CompileResult.bc_len); `program_start` is CompileResult.program_start.
 * The constant heap and funcs table are read from their global singletons
 * (the state left by the compile). On success writes `*out_len`. */
swb_err_t swb_write(const unsigned char *bc, uint16_t bc_len,
                    uint16_t program_start,
                    unsigned char *out, uint16_t out_cap,
                    uint16_t *out_len);

/* Stream a `.swb` image out through `wr` (header, bytecode, constant heap,
 * funcs section in order). Same content as swb_write but with no full-image
 * buffer — the constant heap + funcs are read from their global singletons.
 * Returns SWB_ERR_IO if any `wr` call fails. */
swb_err_t swb_write_stream(const unsigned char *bc, uint16_t bc_len,
                           uint16_t program_start,
                           swb_writer wr, void *ctx);

/* Deserialise a `.swb` image: copy the bytecode into `bc_out` (capacity
 * `bc_cap`) and rebuild the constant heap + funcs table in their global
 * singletons (the heap and funcs are reset first). Returns the VM entry
 * point + bytecode length via `*program_start` / `*bc_len`, ready for
 * vm_run(bc_out, *program_start, *bc_len). */
swb_err_t swb_read(const unsigned char *in, uint16_t in_len,
                   unsigned char *bc_out, uint16_t bc_cap,
                   uint16_t *program_start, uint16_t *bc_len);

/* Like swb_read but runs the bytecode IN PLACE: validates `in`, rebuilds
 * the heap + funcs singletons, and points `*bc` at the bytecode inside the
 * image (no copy). The Runner reads the whole `.swb` into one buffer and
 * passes `*bc` straight to vm_run — saving a second bytecode buffer in its
 * RAM window. `in` must stay valid for the run. */
swb_err_t swb_open_image(const unsigned char *in, uint16_t in_len,
                         const unsigned char **bc,
                         uint16_t *program_start, uint16_t *bc_len);

#if defined(WITH_AUX_BC) || !defined(__CC65__)
/* Paged loading for the //e Runner (-DWITH_AUX_BC), where the bytecode is
 * staged into aux RAM and never held whole in MAIN. Two-step:
 *
 *   swb_header_info(hdr12, &prog_start, &bc_len, &heap_len, &funcs_n)
 *       Validate the 12-byte header and report the section sizes so the
 *       caller can stream `bc_len` bytecode bytes to aux and then read the
 *       (heap_len + funcs_n*SWB_FUNC_SIZE) tail into a MAIN buffer.
 *
 *   swb_load_tail(tail, heap_len, funcs_n, bc_len)
 *       Rebuild the heap + funcs singletons from that tail buffer (the
 *       const-heap followed by the funcs records, exactly as on disk). The
 *       bytecode is NOT needed here — it already lives in aux.
 *
 * Compiled in for the host (tests) and the WITH_AUX_BC Runner only. */
swb_err_t swb_header_info(const unsigned char *in,
                          uint16_t *program_start, uint16_t *bc_len,
                          uint16_t *heap_len, uint8_t *funcs_n);
swb_err_t swb_load_tail(const unsigned char *tail, uint16_t heap_len,
                        uint8_t funcs_n, uint16_t bc_len);
#endif

#endif /* SWIFTII_SWB_H */
