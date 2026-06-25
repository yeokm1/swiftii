# LESSONS.md

A running catalog of things we learned the hard way. Add to this when a bug,
surprise, or gotcha would have saved real time if it had been known earlier.

Entries should be short - usually one paragraph. Split a long incident into
separate lessons when the reusable knowledge is really separate. Keep the
incident detail only when it preserves information that cannot be recovered
from the source, tests, or design docs.

## Format

```
## Topic Area

### Short title

What happened. What we expected. What was actually true. What we did about
it. Mention source files, design docs, or tests when they are the fastest
way to recover the detail.
```

Add new entries under the most relevant topic. If no topic fits, add one.

---

## Measurement and Budgets

### Treat the linker map as the source of truth

`make size` is useful, but its "headroom" column is file-size headroom, not
free RAM. Several binaries had many KB of file headroom while BSS was within
about 100 bytes of the `$BF00 - __STACKSIZE__` ceiling. Before spending bytes,
read the `.map`: compare BSS end against the configured RAM ceiling, and check
segment-specific limits as well as total SYS size.

### When BSS overlays ONCE, code growth spends BSS area

Some launcher configs place `BSS` at `__ONCE_RUN__`, with the ceiling fixed
near `$BF00 - __STACKSIZE__`. In that layout, every byte of new CODE or ONCE
data pushes BSS upward and reduces BSS-area slack one-for-one. A BSS-neutral
refactor can still fail with `BSS overflows` if it adds code. Measure
`__BSS_RUN__ + __BSS_SIZE__` against the ceiling; do not rely on "code
headroom" for these binaries.

### Design-doc byte estimates age quickly

Many accepted design docs were directionally right but wrong on current byte
costs once later work landed. Re-measure against the current tree before
starting an implementation, especially if the plan cites absolute addresses,
headroom numbers, or projected savings. A stale estimate can be off by a factor
of four, or even have the wrong sign.

### Feature audits beat micro-optimisation near the ceiling

When the binary is hard against the ProDOS SYS limit, pruning low-value features
often wins more safely than tiny code tuning. Removing unused reserved syntax,
status strings, and dead recognition paths has repeatedly recovered hundreds of
bytes with less risk than hand-tuning hot code. Confirm removals with tests and
tree greps, then document the user-visible trade.

### Capacity trades are safer than stack-safety trades

For Family B, new features often had to be funded by shrinking bytecode arenas,
constant pools, runner images, or heaps. Measure the actual compiled footprint
of stress samples before cutting safety margins: source size is a poor proxy
because comments and whitespace emit nothing. Prefer reclaiming unused capacity
above the real stress-sample footprint to trimming the C stack.

### A feature that needs rollback state is not small on cc65

Function redefinition looked like a small parser/table update, but the
correct rollback path had to save and restore several fields conditionally.
cc65 generated enough code for that restore logic to exceed available budget.
Any feature that needs atomic replacement, speculative parse state, or
multi-field rollback should be costed pessimistically and measured before it
is treated as polish.

## cc65 Code Generation

### `-Cl` makes locals static and breaks re-entrant parsers

cc65's `-Cl` option promotes function locals to static storage. Recursive or
mutually recursive parser functions then clobber their own locals across nested
calls. The Pratt parser needed `#pragma static-locals (off)`, and statement
parsers needed explicit LIFO storage for locals live across `parse_block`.
Host clang will not catch this class because it uses real stack locals; any
parser re-entrancy change needs emulator or target validation.

### Under `-Cl`, extracting a helper has a BSS cost

A new helper function can add static BSS for locals and parameters in every
binary that links it, even if the helper was meant to reduce duplicate code.
This bit the paged Runner and the at-budget sibling binaries. For tight cc65
builds, keep unchanged paths byte-identical when possible and hide new helpers
behind the exact feature gate that needs them.

### `-Or` only helps when the source uses `register`

Adding `-Or` alone produced byte-identical output. Targeted `register`
declarations in `vm_run` saved real bytes, while a similar hint in
`lexer_next` made code larger. Treat `register` as a measured hint, not a
global style rule.

### `__fastcall__` is not a size lever for single-argument helpers

At `-O -Cl`, cc65 already passes a single pointer argument efficiently in A/X.
Converting hot single-argument functions to `__fastcall__` produced no binary
change. Use the attribute only when it changes a multi-argument call sequence
or a lower-optimisation build, and verify with assembly before broad edits.

### Inline fast paths can cost more than a JSR

Inlining `value_retain` / `value_release` tag checks grew the binary because
the repeated memory loads and branches outweighed a cheap 6502 `JSR`.
On this target, a call can be smaller than an inline guard unless the guard is
only a few bytes. Measure the simplest variant first.

### `uint16_t` arithmetic loops are much larger than they look

A small decimal parser using `acc = acc * 10 + digit` compiled to hundreds of
bytes on cc65. Multiplication, unsigned division, overflow checks, and 16-bit
stack locals all pull helper code or verbose stack shuffling. Sketch the
assembly for any 16-bit arithmetic loop before budgeting it; prefer shifts,
8-bit state, or already-linked signed helpers when possible.

### Avoid non-power-of-two struct strides in indexed tables

Adding one byte to symbol-table structs changed their array stride from a
power of two to an awkward size and cost hundreds of bytes in indexed access
code. Keep frequently indexed structs power-of-two sized, split rarely used
fields into parallel arrays, or flatten storage manually.

### cc65 rejects large struct returns

Returning a 6+ byte struct by value failed in cc65. Use out-parameters for
compile results and any other aggregate larger than a few bytes.

### cc65 enum storage is `int` sized

Enums consume 16-bit storage. Use `unsigned char` typedefs for opcodes, tags,
and compact discriminants that only need 8 bits.

### `char` signedness differs from common host assumptions

cc65 treats plain `char` as unsigned. Use `signed char` or fixed-width types
when negative byte values matter. Host tests can miss this if clang defaults
to signed `char`.

### `int` is 16-bit signed

A cc65 `int` is a 16-bit signed value. Use `long` only when the code really
needs 32 bits, and prefer explicit `uint16_t` / `int16_t` for target-sized
values.

### C trigraphs are still real

The literal sequence `??/` in a C string is a trigraph for backslash and will
trip `-Werror=trigraphs`. Write `?\?/` when the runtime bytes must be `??/`.
This matters for the Apple II Plus digraph help text and tests.

### The 6502 hardware stack is not a safety net

The hardware stack is one 256-byte page and wraps silently. cc65 uses a
separate C software stack for many things, but calls, interrupts, and assembly
paths still must respect the real stack. Avoid recursion in target code, keep
assembly call chains shallow, and treat any stack-reserve trim as a release-risk
trade that needs verification.

## Linker and Segment Layout

### ld65 links explicit objects whole

ld65 does not function-strip explicit object files. A dead function in an
explicitly linked translation unit still costs bytes; a host-only or
platform-deferred file must be gated at the source-file level, not only at the
call site. This is how unused cc65 stdio and dead input translation code cost
kilobytes until the source files or bodies were gated.

### Split shared modules by binary boundary

Putting `.swb` read and write code in one translation unit made the Compiler
carry Runner-only code and the Runner carry Compiler-only code. Split modules
when two binaries use different halves; whole-object linking makes that a real
budget issue.

### cc65 deduplicates string literals only inside one translation unit

Replacing repeated literals with a local `static const` did nothing when cc65
had already pooled identical strings inside the `.c` file. Cross-file dedup is
manual: define one `extern const char[]` and reference it from each file. Count
actual generated assembly, not source occurrences.

### `#pragma rodata-name` does not move C string literals

The pragma affects explicit static const data, but cc65 emits string literals
into `RODATA` directly. Use the `cc65 --rodata-name` path for files whose
literal data must move, as the LC object rules do.

### Moving CODE or RODATA to LC does not reduce SYS file size

With `load = MAIN, run = LC`, language-card bytes still live in the SYS file
and are copied to LC at startup. LC migration can free main-RAM runtime layout,
but it does not reduce the ProDOS file-load ceiling. To reduce file size, remove
or compress bytes.

### `load == run` for LC is not a free ProDOS loader solution

An ld65 config with LC `load == run` can produce clean split output, but ProDOS
loads a SYS file to one address and cc65 crt0 still expects an LC copy source.
MLI also cannot safely write directly into the language-card region because
ProDOS's MLI body lives there. LC payloads must be staged through main RAM or
handled by a custom loader.

### BSS must not overlap late-used loader segments

The boot launcher overlays BSS onto ONCE, but chain loader images are needed
after startup. Place `LOADER` / `LOADER_AUX` before `ONCE` so BSS cannot clobber
the bytes later copied to `$0300`.

### `cl65` object names collide by basename

Compiling `screen.c` and `screen.s` directly in the same module produced
duplicate `screen.o`. Use distinct basenames or explicit per-file object paths
under the Makefile's build directory.

### Production stdio is a kilobyte-scale dependency

cc65 stdio and printf-family formatting are far too large for production
target code. A dead Apple II file-runner path pulled in stdio and cost
kilobytes of code plus BSS before the source file was target-gated. Keep
`printf`, `fopen`, and friends in host-only tests/tools unless a design
explicitly budgets them.

## ProDOS, Boot, and File I/O

### ProDOS auto-launches `*.SYSTEM`

ProDOS 2.4.3's boot path looks for `*.SYSTEM` files. A SYS-type file without
the suffix can be skipped. The boot launcher must be named accordingly and
ordered so it is the file ProDOS launches.

### cc65 EXEHDR breaks ProDOS SYS launch

The stock cc65 Apple II executable header puts a byte before `STARTUP`; ProDOS
loads the whole SYS file at the AUX load address and jumps to the first byte.
For SwiftII SYS binaries, remove the EXEHDR so execution begins at real startup
code.

### ProDOS and ROM banking have different steady states

ProDOS SYS launch leaves ROM visible, while MLI calls need bank-1 LC RAM for
the MLI body. cc65 startup may change the LC state again before `main`. Code
that calls ROM routines and MLI must set the expected bank state explicitly
around each call, and the restore state is binary-specific.

### MLI cannot write directly to language-card RAM

The MLI body itself lives in LC RAM. If MLI is visible, a READ to `$D000+`
overwrites MLI; if another bank is visible, the MLI body is not executable.
Stage LC payloads into main RAM and copy them under code you control.

### MLI READ can overwrite its own caller

The boot launcher reads the selected SYS file into `$2000`, the same address
where the launcher is running. Post-READ code must live in a small zero-page
bouncer, with parameter blocks that survive the overwrite, before jumping to
the loaded program.

### MLI READ destinations cannot be zero page

MLI parameter blocks may live in zero page, but READ data buffers in zero page
silently failed. Use main-RAM scratch for headers and staged data.

### The interpreter cannot MLI-chain after it starts

Interpreter binaries overwrite the LC area that ProDOS uses for MLI. `:quit`
cannot safely OPEN/READ another SYS file or use enhanced QUIT tricks. The
working path is a cold reboot after banking ROM back into `$D000-$FFFF` and
invalidating warm start.

### Delete existing output files as a separate step before create/write

The Compiler saw ProDOS `$48` volume-full errors when `DESTROY`, `CREATE`, and
write happened back-to-back on an existing `.swb`, even when space remained.
Deleting the old `.swb` before compilation, then creating the new file later,
matched the reliable manual workflow.

### Surface MLI error codes

A terse "write error" hid a real disk-full condition on Family B disks. Include
the MLI errno in user-facing failures when the user can act on it.

### Open files consume ProDOS buffers

Each open ProDOS file needs a large file buffer. Do not hold source files open
across phases that need that RAM for bytecode, heaps, or staging.

### Zero page is shared territory

cc65 uses zero page for its software stack pointer and scratch state, while
ProDOS MLI uses its own zero-page scratch during calls. Only claim zero-page
slots through the documented linker/assembly path, and save or avoid any bytes
that might be live across MLI.

## Apple II Hardware and Display

### Validate every new grammar character against the II+ keyboard

The stock Apple II Plus keyboard cannot type lowercase or several Swift grammar
characters. Host tests will happily use characters a target user cannot enter.
Design doc 003's input layer exists to keep on-disk source canonical while
making those characters reachable.

### ESC is not a reliable compose key in the emulator loop

An ESC-based input compose scheme was clean on paper, but Mariani intercepted
ESC before the Apple II keyboard register saw it. Any keyboard design that
depends on modifier or modal keys must be verified in the development emulator
and, when possible, on hardware.

### Ctrl-U cannot mean both right-arrow and underscore

The II+ Swift input method first used Ctrl-U for `_`, but Ctrl-U *is* the
right-arrow byte (`$15`) — so the same keystroke meant "underscore" and
"cursor right", and pressing the arrow surprised users by typing `_`. The fix
(after the editor went Apple-Pascal, making the arrows non-destructive motion)
was to move underscore off `$15` entirely and onto **Ctrl-W (`$17`)** in *both*
the REPL and the editor. Right-arrow then means only cursor-right (editor) or
nothing (REPL). Lesson: never overload a key that is physically an arrow with a
character the language needs.

### Pre-IIe display has no lowercase

High-bit lowercase bytes on the II+ character ROM render as unrelated uppercase
or punctuation glyphs. User-facing boot text printed through ROM COUT must be
uppercase, and SwiftII's display layer substitutes lowercase deliberately.

### cc65 Apple II conio has narrow control-character semantics

`cputc('\r')` returns to column 0 but does not advance the row; `cputc('\n')`
advances the row but does not return to column 0. `cputc($08)` writes a glyph
instead of moving left. Application code must implement CRLF, scrolling, and
destructive backspace explicitly using conio cursor calls or the firmware path.

### Raw COUT is not always the right default

On a bare ProDOS-only disk, ROM COUT can depend on I/O hook state that is not
what a BASIC.SYSTEM-launched program would get. cc65 conio was the safer Phase 0
path; later firmware-COUT uses are deliberate and paired with bank/hook setup.

### cc65 conio wraps instead of scrolling

At the bottom of the screen, cc65's newline/wrap logic returns to the top
instead of scrolling. Detect bottom-row newline and column-wrap cases and scroll
in SwiftII's screen layer.

### Scrolling must respect `gr()`'s mixed-mode text window

Mixed low-res graphics (`gr()`) is a 40x40 picture in text rows 0-19 plus a
4-line text window in rows 20-23 — the GR page *is* the text page reinterpreted.
A `print()` that fills the window then scrolls must scroll only rows 20-23; the
naive whole-screen scroll (from row 0) drags the picture up, and the top text
line lands in the bottom GR rows where it renders as colour blocks (the
"artifacts at the bottom-left of the graphics area" a multi-scene GR program
like `xgrdemo` showed). SwiftII's own scroll (`scroll_up_one`, since cc65 conio
wraps rather than scrolls — see above) takes a scroll-region top: `gr()` mixed
mode sets it to 20, `grFull()`/`text()` reset it to 0. NOT the monitor `WNDTOP`
($22): cc65 `cputc` wraps the cursor to `WNDTOP`, so raising it would break the
"cursor wrapped to top -> scroll" detection on the normal text path. It is a
plain global (`g_gr_scroll_top`), written directly by the GR builtins rather
than through a setter, so on the budget-tight SWIFTSAT the writes stay in the
XLC cold bank and MAIN carries only the byte + the read (a setter body in MAIN
overflowed SWIFTSAT by 5 B). Gated `WITH_GR_TEXTWIN` to the binaries that run GR
(the Family B Runner + the SWIFTSAT/SWIFTAUX extras REPLs); the Compiler — which
never enters GR and scrolls full-screen text correctly from row 0 — and the lite
REPLs leave it off and are byte-identical.

### Inverse-video `J` and `M` collide with cputc's CR/LF

The pre-IIe case-render draws an upper-case letter by feeding the screen code
`c - 0x40` through `cputc` (`A` → `$01` … `Z` → `$1A`). Two of those codes are
control characters `cputc` intercepts: `J` → `$0A` (LF) and `M` → `$0D` (CR). So
any upper-case `J`/`M` — common in ProDOS file names — acts as a stray newline
mid-word and wraps the rest of the line back over itself (`ENAMELEN` echoed as
`ELEN` once the `M` fired a CR). The fix is to write those two letters straight
to video RAM and step the cursor by hand (`emit_inverse_letter`, mirroring the
//e `emit_native_high` path), gated `WITH_INVERSE_JM`. It is on for every pre-IIe
(II+/Saturn) binary that echoes upper-case text: the **Runner** (arbitrary
upper-case program output plus its `Running:` file-name echo) and the **pre-IIe
Compilers** (their `Compiling:` / `Wrote:` ProDOS-path echo). The REPLs are at
the MAIN ceiling and can't absorb the ~80 B, so they stay byte-identical (no
upper-case path echo, so no regression). Two traps:

- **Match `cputc`'s polarity.** `cputc` stores `(ch ^ $80) & INVFLG`, so writing
  the *raw* inverse code (`$0A`/`$0D`) gives `J`/`M` the wrong video relative to
  their neighbours — with `INVFLG` at its default `$FF` (nothing calls
  `revers()`), the other letters render normal but a raw-coded `M` renders
  inverse. Write `inv ^ $80` to match. (If an inverse-text mode is ever added,
  restore the `& INVFLG` term so `J`/`M` track it.)
- **The //e Compiler does NOT use this — it needs `WITH_IIE`.** Every //e-disk
  binary (launcher, //e Runner, SWIFTAUX) is built `WITH_IIE`, which renders via
  the full-ASCII `emit_native_high`/`cputc` path and never hits
  `emit_inverse_letter`. The //e Compiler must be too; built without it, it falls
  into the pre-IIe runtime-`$FBB3`-probe path and garbles the same way (and has no
  `WITH_INVERSE_JM`). `WITH_IIE` also drops `emit_inverse_letter`, so it costs no
  budget — it frees it.

Host clang never sees any of this (it has no inverse-video render), so it needs
target/emulator validation.

### cc65 `apple2` cputc folds //e lowercase

Even on a //e, the cc65 `apple2` target folds `$60-$7F` to uppercase. The //e
build writes lowercase screen codes directly or routes through firmware COUT
in 80-column mode. Do not use runtime `$FBB3` model probes for this behavior;
the disk build declares the machine via `WITH_IIE`.

### Assert display soft-switch state instead of inheriting it

The //e lowercase path needs ALTCHAR, but ALTCHAR may only be on as a side
effect of entering 80-column firmware. Set required soft switches explicitly in
platform init or mode entry.

### Use stores, not discarded volatile reads, for soft switches

cc65 elided discarded volatile reads to Apple II soft switches. Use volatile
stores or inline assembly for I/O side effects, and check generated assembly
for the `$C0xx` address when the switch matters.

### ROM-wrap restore state is binary-specific

Wrapping a ROM call with "bank ROM in, call, bank RAM back in" only works if
the final bank is that binary's steady state. Copying a SWIFTSAT LC restore
sequence into a //e path would leave the wrong bank selected. Treat every bank
wrap as local to one binary's memory model.

### 80-column mode has two cursor models

The //e firmware COUT path and cc65 40-column conio track different cursors.
Backspace, HOME, `htab`, `vtab`, and `text()` are the seam where bugs appear.
In 80-column mode, use firmware cursor state such as OURCH where appropriate,
and reset the 40-column text window before returning to conio.

### `vtab` differs between //e firmware and Videx

Poking CV was enough for the Videx firmware, which recalculates line address
from row/column on output. The //e firmware caches the line base, so `vtab`
must also run the monitor `VTAB`/BASCALC path there. Do not generalize one
80-column card's cursor behavior to the other.

### Enter the //e 80-column firmware with motherboard ROM banked in

`JSR $C300` (the //e built-in 80-column firmware) runs its body out of the
internal `$C800` ROM and JSRs monitor routines at `$F800+`. Those reads only
land in ROM when the language card is banked to read motherboard ROM. If the LC
is banked to read RAM — which raw cc65 ProDOS file I/O leaves behind — the
firmware reads the language card instead, where ProDOS keeps its MLI body, and
runs (and corrupts) ProDOS code as if it were ROM. Every later MLI call then
returns `$01` "bad system call number" until reset.

The launcher normally runs with LC=ROM, so its boot-time 80-column enable is
fine. The trap is the in-process editor: its file load/save bank LC=RAM, and its
`Ctrl-W` 40/80 width toggle calls straight into the firmware enable from that
state. The damage stays hidden until the next MLI call — `Ctrl-Q` back to the
file browser, whose directory re-read fails, so the launcher draws whatever stale
bytes sit in the pane tables (which share the editor's gap-buffer window) as a
garbled listing. The fix is one `BIT $C082` to bank ROM before `JSR $C300`,
mirroring the editor's own re-bank-for-COUT step; it is harmless when LC is
already ROM. Belt-and-braces, the browser's pane re-read zeroes both panes on a
hard MLI error so any future failure degrades to an empty listing, not garbage.

### Videx needs AN0 to hand the display back

On a real II+ with Videx, PR#0 / CSW reset stops routing output but does not
necessarily switch the visible display back. `text()` must turn AN0 off, and
`text80()` must turn it on again on re-entry.

### Do not end an 80-column program by clearing back to text

Calling `text()` at the end of an 80-column program clears or hides the output
before the Runner pause. Leave 80-column mode on so the output remains visible;
the Runner is responsible for restoring a clean 40-column launcher state after
the user acknowledges the program.

### Apple II slot I/O starts at `$C080 + slot * 16`

The `$C010-$C07F` range is system soft switches, not slot-card I/O. Slot 1
starts at `$C090`, slot 7 at `$C0F0`. A wrong probe can flip display or input
switches and look like a hang.

### Saturn cards have no slot ROM

Saturn 128K cards are RAM-only. Detect them behaviorally through bank switching
and a sentinel write, not by scanning `$Cn00` ROM space for a signature.

### Saturn bank numbering is zero-based

The Saturn bank-select formula is zero-indexed even if prose labels say "bank
1". On slot 0, Saturn is also the language card, so built-in LC soft switches
affect it directly. Restore the expected bank before cc65 startup copies LC
bytes.

### Probe //e extended aux RAM with AUXMOVE

MACHID bit 4 says an 80-column card exists, but not whether it is a 1K basic
card or a 64K extended aux card. A sentinel round-trip through aux `$2000` via
ROM AUXMOVE distinguishes real 64K aux RAM and avoids routing a basic card to
SWIFTAUX.

### `$FBB3` is not a reliable build selector

Machine-ID bytes can be hidden by LC banking or misreported by emulator
presets. Use disk-selected build flags for machine-specific behavior, and use
runtime probes only for capabilities that can be tested directly.

### The II+ returns `$EA` at `$FBB3`, not `$38`

Per Apple Technical Note Misc #7 only the *original* Apple ][ returns `$38`;
the **II+ returns `$EA`** and the IIe family returns `$06`. A single
`== $38` "is this pre-IIe?" test misclassifies a real II+ as a //e. The
"pre-IIe" glyph cohort is the original ][ **and** the II+, so classify with
`APPLE_II_IS_PRE_IIE(m)` (`(m) != $06`), which also fails safe on garbage.
The interpreter never reads `$FBB3` (it runs LC-banked, so the read returns
RAM); only DEBUG.SYSTEM does, and only after banking ROM in with
`bit $C082`, because it loads into main RAM. Note `$EA` is the II+, not the
IIc — the IIc is `$06`.

## Multi-Binary Features and Overlays

### Host tests cannot prove target feature gates

Many gates include `!defined(__CC65__)`, so host tests exercise the feature
even when a cc65 build flavor compiles it out. When adding a binary family,
grep the gates and explicitly decide whether that binary gets each feature.
Run at least one target-build smoke test for promised surface area.

### Apply per-binary defines to LC object rules too

Some compiler files are built through special LC-object rules. A flag such as
`WITH_SWIFTSAT` must reach those rules as well as the main cl65 invocation, or
parser branches can silently disappear from the target binary.

### Compiler-side feature cost cannot be hidden in XLC

Saturn XLC helps with cold VM/runtime bodies, not parser/type-checker code.
Features such as `struct` or argument-label validation are dominated by
compiler-side code and cannot be saved by moving VM dispatchers to Saturn bank
1. Budget the compiler half separately.

### Builtin parser recognition is MAIN cost

Moving a builtin dispatcher to XLC or aux overlay does not move the parser row,
name checks, or argument parser. Those remain MAIN-resident because the compiler
runs in MAIN. For builtin families, amortize the parser tail through compact
tables or shared arity helpers.

### A receiver method can be an XLC builtin instead of a VM opcode

Array methods were too expensive as new opcodes but fit as
`OP_CALL_BUILTIN` calls with the receiver already on the stack. This avoids a
large `vm_run` case and lets the real work live in XLC or Runner-only code.

### New opcodes are expensive on cc65

A single VM case touching `Value`, helpers, and 16-bit math can cost around half
a kilobyte. Prefer desugaring to existing opcodes when the runtime already has
the operations. Moving cost from Compiler to Runner only helps if the Runner is
actually roomy.

### Relocate only fat cold opcode bodies

XLC relocation pays off when the inline VM body is large, such as
`OP_CALL_BUILTIN` with print/readLine/min/max. Thin wrappers around helper
functions barely save anything because the trampoline cost cancels the removed
case body.

### XLC builtin IDs must be in the XLC range

The dispatcher routes builtins to XLC by ID range. Old reserved MAIN IDs cannot
magically live in XLC; XLC-resident builtins need IDs at or above
`BUILTIN_XLC_FIRST`, with old slots left reserved or reused only deliberately.

### Void builtins need REPL echo suppression

A side-effect builtin that returns void can still leave a nil sentinel on the
stack and be echoed by the REPL bare-expression path. Suppress REPL echo for
`CT_VOID` expressions so calls such as `home()` do not print `nil`.

### SWIFTAUX is lite plus aux extras, not a SWIFTSAT clone

SWIFTAUX cannot execute aux code in place, and copying hot op bodies down on
every call would be slow. Its MAIN image stays lite-flavored, with cold bodies
copied from aux into a staging buffer. Evict cold fat bodies first; keep hot
stack/arith paths inline.

### Use ld65 overlays for aux copy-down bodies

cc65 code is position-dependent. Aux copy-down bodies must be linked to run at
the staging address, while still resolving MAIN helper calls. ld65 overlays do
that in one link and avoid brittle per-body symbol import schemes.

### Packed overlay directories beat fixed strides

Fixed-size aux park slots were simple until grouped bodies exceeded the stride
and padding bloated the disk image. A directory of `[offset,length]` entries
lets grouped builtin IDs share bodies, removes the stride ceiling, and copies
only the bytes needed for each call.

### Keep SWIFTSAT loader limits visible

The first XLC loader used a single scratch buffer and had a deliberate halt
guard once XLC outgrew it. Segment-size ceilings in custom loaders are not
shown by `make size` unless the build checks them. Prefer chunked staging once
a banked payload can grow.

## Compiler and Language Semantics

### Match builtin names by canonical Swift spelling

Keywords are lowercase, but builtins such as `readLine` are case-sensitive
Swift identifiers. Match the canonical spelling in compiler code; do not
lowercase known identifiers just because the II+ input layer can canonicalize
typing.

### Host-only language conveniences can mask target failures

Host builds accepted call-site argument labels that cc65 builds had dropped for
budget. A target-only "undeclared name" in a large program was a label, not a
streaming or paging bug. When a program fails only on target, suspect host-only
language surface before blaming the runtime machinery.

### Type parsing must test distinguishable outputs

A type-recognition bug returned `CT_UNKNOWN` for `Int` but printed the same
visible text through a fallback path, making a bad test look correct. Use
fixtures where each path produces a distinct result, such as optional or array
types.

### Spec completeness loses to demo-critical scope

Several phases stayed shippable by implementing the minimum form needed for
the demo and deferring costlier polish. The rule is not "cut randomly"; it is
"ship the coherent subset, provide an obvious workaround, and document the
missing shape in the right place."

### Array append semantics need to match user expectation

Returning a relocated array from `.append` was technically workable, but users
naturally wrote `xs.append(v)` and expected `xs` to update. Statement-level
write-back was worth restoring even under budget pressure because it fixed the
most obvious demo path.

### Error messages can carry missing-feature guidance

When a feature has a workaround or is intentionally absent on a binary, a terse
but targeted compile error saves time. The positional-argument diagnostic for
target builds is a good example.

### Over-length identifiers must fail, not truncate

Silent truncation lets two long identifiers alias the same symbol-table entry.
Reject names longer than the supported storage limit at declaration sites, and
keep defensive table checks so the invariant is not parser-only.

### A conditionally-declared local must be popped on its own branch

A range `for` keeps its loop bound on the value stack (the `OP_OVER` peek), so
the loop body must be stack-neutral. Loop bodies enforce this by popping every
local they declare before the back-jump (`parse_block_scoped`). The same applies
to `if`/`else` branches: a `let`/`var` declared in a branch is pushed only when
that branch runs, so the pop must live on the same conditional path. A branch
that used the unscoped `parse_block` instead deferred the pop to the enclosing
loop's cleanup, which ran unconditionally — so on iterations where the branch
was skipped the stack drifted by one slot, eventually feeding a wrong-typed
value into an op (`SE_TYPE_MISMATCH`, surfaced as "VM halted with error"). It
read as a runtime fault but was pure codegen: the symptom is platform-independent
and reproduces on the host build. Top level hides it — there body locals are
globals, never on the stack. Rule: every block that can declare a local and runs
conditionally (loop body, each `if`/`else` arm, each `case`) must scope its own
locals; do not rely on an outer scope to balance an inner conditional push.

## Family B Compiler and Runner

### Family B is not automatically roomy

The standalone Compiler and Runner shed parts of Family A, but later source
windows, bytecode arenas, file I/O, and builtins consumed most of that room.
Check actual margins for both binaries before deciding to move a feature from
one to the other.

### Compile against emitted bytecode, not source bytes

Big source files may contain many comments and little bytecode. When sizing
Compiler arenas and const pools, measure emitted bytecode and constants from
the stress sample rather than raw `.swift` size.

### Sliding source windows must protect lexer snapshots

Parser lookahead that saves a Lexer, skips separators, and restores the Lexer
can become invalid if skipping refills the source window. Use a no-slide skip
variant for save/restore probes such as `else` detection.

### A too-long statement can compile as a truncated valid statement

When a source window drains mid-statement, the prefix may still parse cleanly.
The eof/fully-consumed flag is part of the compile contract: reject if the
source was not fully consumed, even when parsing reported success.

### Avoid 32-bit arithmetic in progress reporting

A natural percent calculation using 32-bit multiply/divide pulled hundreds of
bytes of cc65 runtime. Approximate with 16-bit arithmetic when the display is
only progress feedback.

### `.swb` bounds validation is part of the format

The Runner must reject corrupt `.swb` images whose program start or function
PCs point outside bytecode. Any opcode, builtin, or function-record format
change needs a version bump so old artifacts fail clearly.

### Page only bytecode first

Paged Runners park bytecode in aux/Saturn memory and keep consts, funcs, and
heap in MAIN because those need random access. This raises code-size capacity
without pretending heap is solved.

### Compiler bytecode paging is append-only

The paged Compiler can flush completed function bytecode to aux because those
bodies are immutable and later calls refer by function index. Top-level scratch
and the in-progress function still need a resident window, so top-level-heavy
programs may fit flat Tier 1 but not paged Tier 3.

### Randomness needs input timing entropy

A fixed RNG seed made every target run choose the same "random" value. Fold a
keypress timing counter into the seed on Apple II, while host entropy returns
zero so tests remain deterministic. Programs that draw before any keypress need
an explicit "press a key to start" style seed point.

### File I/O builtins belong to Family B

Family A interpreters overwrite the MLI body and do not have the budget or OS
state for general file I/O. Keep `readFile` / `writeFile` in the Compiler/Runner
family, where raw MLI stays available.

## Tooling and Operations

### Changing Make variables does not force a rebuild

`make target WITH_FOO=0` can reuse stale objects built with `WITH_FOO=1`
because the command-line flags are not tracked dependencies. Clean the relevant
object tree, touch sources, or use a clean CI build when proving flag-gated
byte identity.

### Recipe changes do not rebuild existing objects

Changing the Makefile rule that compiles a file may leave the old object in
place if the source mtime is older than the object. Clean or remove the object
when changing flags, segment rules, or per-file compilation paths.

### Host tests are necessary but not sufficient

Host tests miss cc65 static-local re-entrancy, feature gates, LC banking, MLI
state, ProDOS errors, and soft-switch behavior. Any change in parser recursion,
banked code, target-only builtins, disk I/O, or platform display needs a target
build and emulator or hardware pass.

### On-target diagnostic output beats guessing

When a target-only compile failed deep into a streamed source, printing token
bytes and lexer offsets immediately identified a call-site label. Add narrow
on-disk diagnostics rather than inferring from percentage progress or nearby
features.

### Do not harden unproven emulator inferences into docs

The SWIFTSAT keyboard-echo investigation generated several plausible but wrong
causes before the symptom disappeared under later changes. Record "undiagnosed"
until a probe proves the cause; avoid turning a guess into project lore.

### Mariani may cache disk images

If a fresh `make disk` appears to run old code, fully quit Mariani or disable
its cache. Closing the window may not be enough.

### Mariani CLI mount syntax matters

Launching Mariani with a disk path alone may open the app without mounting and
booting the disk. Use the project script or the correct `--args -1 PATH` form.

### AppleCommander needs Homebrew Java on PATH

Homebrew OpenJDK can be keg-only, leaving `/usr/bin/java` as the macOS stub.
Ensure `/opt/homebrew/opt/openjdk@21/bin` is on PATH before relying on disk
image builds.

### Original Apple ][ needs a ROM-free cc65 startup path

cc65's stock Apple II crt0 copies LC bytes with the Applesoft BLTU2 routine,
which does not exist on the original Integer-BASIC Apple ][. SwiftII uses a
custom crt0 path that copies via cc65 `_memcpy` instead, keeping ProDOS 2.4.3
on original hardware viable.

### The $1B00 handoff page does not survive a compiling pass

The launcher parks the Saturn slot at $1B04 for the chained interpreters. That
survives a plain READ-and-chain, but the Family B **compiler** clobbers the
$1B00 page (its source-read scratch overlaps it), leaving a stray source byte
where the slot should be. The Tier-2 Saturn Runner then patched its `$C0xx` bank
switches from that byte — `(byte+8)*16` — landing on video registers instead of
the Saturn card, silently corrupting paged bytecode for any program big enough
to actually page the bank. Small programs survived only because their stray byte
happened to map to a working base. Fix: the compiler re-deposits the slot at
$1B04 right before chaining the Runner. Full postmortem + the three
compiler/Runner tiers' tradeoffs in
[design/020](design/020-tier2-saturn-paged-runner.md).

### A host replica of paging *logic* is not the bank *hardware* path

The Saturn / `//e`-aux bytecode paging logic (`bcwin.c`) has a faithful host
replica (linear `aux_store` memcpy), great for testing window math — but it does
**not** model the Saturn-bank assembly (`saturn_bc.s`). The slot-clobber bug
above was invisible on host and only reproduced on the emulator. For bank-driver
bugs, instrument the on-target Runner (a stage→read-back checksum under a build
flag) and scrape the screen; a green host run proves nothing about the bank path.
