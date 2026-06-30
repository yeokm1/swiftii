# Maybe / probably never

Deferred or declined backlog items. Keep this list short: each entry should say
what the item is and, when known, why it is not in the active roadmap.

Ordered by priority: items most worth building should more resources (code
space, time) become available come first, declining to long-shot work at the
end. Settled negative-result investigations are not kept here - their
rationale lives in `LESSONS.md` and the design docs.

1. **High-resolution graphics (//e aux only)** - HGR would expose the Apple II
   high-resolution framebuffer. It is not done because the screen memory uses
   fixed main-RAM ranges that the interpreter currently occupies; only a //e
   aux build might be able to run the interpreter elsewhere, and that needs a
   risky fit and performance spike.

2. **Floating-point `Double` and math** - Would use Applesoft-compatible
   floating point and math builtins. It is not done because both compiler and
   runner size are too tight, the ROM bridge is risky, and fitting it in Family
   B would require unacceptable heap or program-size cuts. The ROM bridge
   mechanism is already solved; the only blocker is code size, so this is a
   prime candidate when more space is available.

3. **`struct` (stored properties and methods)** - Would add a distinctive
   Swift feature for grouped data. It is not done because even a stored-property
   subset measured as multi-KB code growth that fits no binary.

4. **Closures (non-escaping only)** - Would enable `array.map`,
   `array.filter`, `array.sorted(by:)`, and inline callbacks. It is not done
   because upvalue capture, closure heap objects, and call-convention changes
   are expected to cost several KB of code; escaping closures remain out of
   scope.

5. **Dictionary (`[K: V]`)** - Would add keyed lookup, especially string-keyed
   tables. It is not done because a correct hash table costs substantial code
   and heap, and only the //e builds might have enough space after a fit spike.
   Use parallel arrays or linear scans for now.

6. **`guard let`** - Would add Swift-style early-exit optional binding. It is
   not done because `if let ... else { ... }` already covers the use cases, and
   the compiler implementation measured far larger than expected.

7. **Sound beyond Family B `tone()`** - Would add melodies or richer speaker
   synthesis. It is not done because accurate sound timing conflicts with the
   current C VM dispatch loop; it likely needs a rewritten hot path.

8. **Shape-table drawing (`DRAW` / `XDRAW`)** - Would add Applesoft-style
   sprite-like HGR shape tables. It is not done because it depends on HGR
   landing first, and the implementation would still be large.

9. **Multi-line REPL input** - Would let the REPL accept continued blocks such
   as multi-line functions. It is not done because the editor already handles
   block-level authoring for file mode, so this is mostly convenience.

10. **Scrollback buffer** - Would let users page back through output that has
    left the 24-row screen. It is not done because it consumes BSS for stored
    rows and is less useful than command history or simply rerunning a statement.

11. **Paged disk-backed editor** - Would let the on-target editor handle files
    larger than the resident gap buffer. It is not done because it requires a
    major editor redesign, while large programs are already runnable through the
    streaming Family B compiler when authored off-target.

12. **Implicit return** - Would allow single-expression function bodies such as
    `func f() -> Int { x * x }`. It is not done because explicit `return`
    works, samples already use it, and the parser support costs more than this
    sugar is worth.

13. **Argument labels at call sites** - Would accept or validate
    `f(label: x)`. It is not done because Apple II calls are positional-only
    now, and label parsing or validation is a cosmetic compile-time feature on
    the wrong side of the size ceiling.

14. **`normal()` / `inverse()` / `flash()`** - Would expose text display
    attributes. It is not done because pre-IIe output already uses inverse
    video to distinguish canonical case, so display attributes would touch a
    risky screen hot path for a cosmetic feature. A future //e-only
    `normal` / `inverse` subset is the plausible version.

15. **Hex / binary / octal integer literals** - Would add forms such as
    `0xFF`, `0b1010`, and `0o17`. It is not done because demos do not need
    them, the signed 16-bit `Int` range keeps decimal literals manageable, and
    the lexer bytes are better spent elsewhere.

16. **Underscore digit separators** - Would allow literals such as `1_000`.
    It is not done because SwiftII integers are small enough that digit
    grouping has little practical value.

17. **`:quit` warm return to the boot selector** - Would return through the boot
    device instead of cold-resetting through ProDOS. It is not done because the
    robust version needs boot-slot handling and emulator verification across
    slot and machine variants; the faster resident-menu version needs extra
    RAM.

18. **Custom ProDOS commands** - Would make `.SWIFT` files launchable by name
    without the SYS prefix. It is not done because it is only convenience and
    needs a command-dispatch shim.

19. **`assert(_:)`** - Would add a test-style assertion builtin. It is not done
    because there is no demo use case and the language is staying demo-oriented.

20. **Bridging to Applesoft** - Would let SwiftII call Applesoft BASIC
    routines. It is not done because it requires careful preservation of
    Applesoft zero-page state and floating-point state.

21. **`Float` (32-bit IEEE 754)** - Would add a separate 32-bit floating-point
    type. It is not done because the hardware and toolchain have no native
    float support, and Applesoft-compatible `Double` would cover the real use
    cases better.

22. **Tier 3 language features** - Classes, protocols, generics, and error
    handling would make SwiftII feel much more like Swift. They are not done
    because each needs major compiler, VM, metadata, and runtime support.

23. **Tier-3 reserved keywords** - Would reserve `class`, `protocol`, `enum`,
    `throws`, `try`, and `catch`. It is not done because those features are out
    of scope, and reserving unused keywords costs table space while blocking
    otherwise valid identifiers.

24. **Tracing GC** - Would solve reference cycles left by refcounting. It is not
    done because it requires replacing the heap allocator with a collector and
    adds hard-to-test runtime complexity.

25. **Compiling SwiftII straight to 6502** - Would produce faster programs. It
    is not done because generated 6502 code is much larger than bytecode and
    does not fit the 64 KB target model for meaningful programs.

26. **Swift-LLDB-style REPL polish** - Would restore numbered prompts,
    type-formatted echoes, and `$R<n>` auto-result bindings. It is not done
    because the feature consumed too much REPL and symbol-table code; the
    current plain prompt plus implicit printing is much smaller.

27. **Family B Tier 3 write-behind `.swb` bytecode streaming** - Would stream
    compiled bytecode to disk while compiling. It is not done because the Runner
    still needs the `.swb` in RAM, the editor cannot author huge files on
    target, and the current bytecode buffer layout would need a redesign for
    disk backpatching.

28. **Move RODATA from main to LC** - Would migrate constant tables out of main
    RAM. It is not done because BSS headroom is currently more useful than the
    tiny remaining language-card slack.

29. **Hand-tuned 6502 dispatch loop and hot opcode handlers** - Would rewrite
    VM dispatch and selected opcode handlers in assembly. It is not done
    because the parity bridge costs bytes before handler rewrites pay off; the
    measured benefit was cycles, not binary size. Reopen only for a real
    latency or demo-performance problem.

30. **Disable the ProDOS `/RAM` disk on //e aux builds** - Would suppress the
    ProDOS-created `/RAM` volume on //e systems, since the aux extras and
    compiler/runner tiers already claim the auxiliary RAM (`$0200-$BFFF` plus
    the aux LC bank) for cold XLC bodies and paged bytecode - the same region
    that backs `/RAM`. It is not done because the overlap has not been audited:
    the builds appear to coexist today, and reclaiming the space cleanly means
    unhooking the ProDOS RAM-disk driver at boot, which needs emulator
    verification across //e variants. A potential prerequisite for the //e-aux
    HGR spike in item 1, since it frees the aux RAM that interpreter relocation
    would need.
