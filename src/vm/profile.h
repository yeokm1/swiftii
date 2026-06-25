/* Per-opcode dispatch counter.
 *
 * Gated behind the SWIFTII_PROFILE compile-time macro. When the macro
 * is not defined, every PROFILE_* macro expands to a no-op so the
 * production build pays zero — no counter table, no per-dispatch
 * increment, no atexit hook. The cc65 target build never defines
 * SWIFTII_PROFILE; profiling lives entirely on the host side under
 * `make bench`.
 */
#ifndef SWIFTII_PROFILE_H
#define SWIFTII_PROFILE_H

#ifdef SWIFTII_PROFILE

void profile_init(void);                 /* idempotent; safe to call repeatedly */
void profile_tick(unsigned char op);     /* one tick per dispatched opcode */
void profile_dump(void);                 /* writes table to stderr */

#define PROFILE_INIT()   profile_init()
#define PROFILE_TICK(op) profile_tick((unsigned char)(op))
#define PROFILE_DUMP()   profile_dump()

#else

#define PROFILE_INIT()   ((void)0)
#define PROFILE_TICK(op) ((void)0)
#define PROFILE_DUMP()   ((void)0)

#endif /* SWIFTII_PROFILE */

#endif /* SWIFTII_PROFILE_H */
