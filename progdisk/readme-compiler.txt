SwiftII Compiler - Help
=======================

Version @VERSION@
Built @BUILT@

A Swift-like language for the Apple
][+ and //e. This is a COMPILER disk
(Family B): edit -> compile -> run.

No REPL - programs are compiled to a
.swb file, then run.

  COMPILER reads a .swift and writes
    a .swb beside it.
  @RUNNER@


MENU OPTIONS
  1 File selector - browse files.
  2 Debug - hardware diagnostic
    (disk space, detection, memory,
    slots; arrows page, Esc exits).
  3 About - version + the disk set.


FILE SELECTOR KEYS
  X    .swift: compile to a .swb and
       run it
  X    .swb: run it directly
  Ret  open a file in the editor
  E    edit
  F    new file
  R    rename
  D    delete
  N    new folder


EDITOR
  arrows  cursor left/right
  Ctrl-O/L  cursor up/down
  Ctrl-D  backspace
  Ctrl-S  save
  Ctrl-R  save + compile + run
  Ctrl-Q  back to the browser
  Ctrl-W  _ in Swift mode on ][+
  (open files from the browser)


Bigger programs than the REPL; a
.swb re-runs without recompiling.

Demos ship in SAMPLES/.


(C) Yeo Kheng Meng @YEAR@
