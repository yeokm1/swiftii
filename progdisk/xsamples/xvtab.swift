// xvtab.swift - VERIFY vtab() in 80-column mode (//e aux + II+ Videx).
//
// vtab() used to be a no-op on an 80-col card (it moved cc65's hidden
// 40-col cursor, not the firmware's). screen.c now writes the monitor row
// CV ($25) in 80-col, mirroring how htab writes OURCH ($057B). This program
// proves it.
//
// The markers are stamped in a JUMBLED row order (8, 3, 11, 5, 1) on
// purpose: print() ends each line with a newline that advances the cursor
// one row by itself, so a marker sequence that merely steps DOWN would
// prove nothing. If vtab works, the bracketed labels land on exactly rows
// 8/3/11/5/1; if vtab is still a no-op, they pile onto consecutive rows
// near the top (the newlines only) and the row numbers won't match where
// they appear.
//
// FAMILY B ONLY (run with [X]); needs an 80-col display: a //e with aux
// RAM, or a II+ with a Saturn 128K + Videx Videoterm. text80() is a no-op
// on 40-col.

text80()
home()

vtab(8)
htab(20)
print("[row 8, col 20]")

vtab(3)
htab(40)
print("[row 3, col 40]")

vtab(11)
htab(10)
print("[row 11, col 10]")

vtab(5)
htab(30)
print("[row 5, col 30]")

vtab(1)
htab(1)
print("[row 1, col 1]")

vtab(14)
htab(1)
print("vtab/htab work in 80 cols IF each label sits at the row+col it names.")
print("(this 80-column result stays up until you continue)")
