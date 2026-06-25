# 019 - Keyboard-dispatch extraction

How the interactive keyboard handling is structured for testability, and what
that means for the shortcut matrix in
[`TESTING-keyboard.md`](../../testing/TESTING-keyboard.md).

## Pure dispatch, host-unit-tested

The editor's key handling is a pure decision function with no keyboard, screen,
ROM, or ProDOS dependency:

- [`editor_dispatch(state, key)`](../../../src/editor/keymap.c) mutates the
  editor state (gap buffer + cursor + dirty flag) and returns an action enum
  naming the I/O the loop must then perform (save / run / quit).
- [`editor_cook_key`](../../../src/editor/keymap.c) folds the II+ cooked-mode
  input — Ctrl-W → `_`, typed `A`–`Z` auto-lowercased to the canonical buffer.

Both are host-unit-tested key by key
([`tests/editor/keymap_test.c`](../../../tests/editor/keymap_test.c),
[`tests/editor/session_test.c`](../../../tests/editor/session_test.c)), so every
editor shortcut is ✅ in the matrix. The cc65 loop is just read → dispatch →
render; the host test calls `dispatch` directly and asserts on the resulting
state plus the returned action, with no screen.

The same pattern extracted the **//e REPL history ring** into
[`histring.{c,h}`](../../../src/platform/apple2/histring.c): the ring storage,
the de-dup, and the off-by-one-prone park/restore nav index are pure and
host-tested
([`tests/platform/histring_test.c`](../../../tests/platform/histring_test.c)).
The ring is file-static rather than a caller-owned pointer on purpose — cc65
emits much smaller code for absolute access than for pointer indirection
(+189 B versus +441 B on each //e binary), and history is //e-only
(`WITH_LINE_HISTORY` / `WITH_IIE`), so only the roomy //e binaries pay it; the
II+ REPL binaries stay byte-identical.

## What stays inline, and why

Every other interactive UI path — the launcher main menu, volume picker, file
browser, Debug pager, test runner, and the shared REPL line keys — reads the
keyboard **inline** in its cc65 program loop and acts in place, so none is a
separately host-testable function.

They are inline by **budget**, not oversight. Extracting the launcher's key
decode into a called `*_dispatch` function cost ~187 B of code, and the
launcher's link config places the BSS segment as an overlay on ONCE — so code
growth shrinks the BSS area one-for-one, and that ~187 B overflowed both
launchers' ~50 B of BSS-area slack. The shared REPL line keys hit the same wall
from the other side: SWIFTSAT has 36 B of MAIN headroom, which the cc65
call/marshalling overhead of a called extraction overflows. Funding either
means trimming an already-tuned C-stack reservation on a boot-everything binary,
a regression risk of its own — not worth it for UI that is frozen for 1.0.

> **Lesson.** On a cc65 binary where BSS overlays ONCE, a "code-headroom" figure
> is not your budget; the BSS-area slack is, and an extracted dispatcher's code
> spends it. (Also recorded in [`LESSONS.md`](../LESSONS.md).)

## How the inline loops are covered

The izapple2 **acceptance harness**
([`tools/host/acceptance/`](../../../tools/host/acceptance/), design
[018](018-on-target-test-harness.md)) drives these paths **end-to-end**: its
`keyboard` configs inject the real keystroke into the emulator and scrape the
screen, so the whole stack — read → dispatch → render — runs unattended under
`make acceptance`. They walk the launcher menu, volume picker, file browser, the
editor, the REPL (line keys + meta-commands + //e history), and the Debug pager.

[`TESTING-keyboard.md`](../../testing/TESTING-keyboard.md) is the row-by-row
record: ✅ = host-unit-tested, ⚙ = driven end-to-end by the harness,
✋ = the by-hand backstop for what neither layer reaches (the Ctrl-G cooked/raw
toggle's save-prompt + reload, browser rename/delete, the test runner). A manual
pass of the ✋ rows on real hardware is a release-gate step.
