// tfiledir.swift - directory
// builtins + edge cases (doc 017).
// FAMILY B ONLY (see README). Covers
// createDirectory / listDirectory /
// deleteDirectory / renameFile +
// failure paths. Cleans up. Last
// line "fail 0" = pass.

var npass = 0
var nfail = 0
func chk(_ g: Int, _ w: Int) {
  if g == w {
    npass = npass + 1
  } else {
    nfail = nfail + 1
    print("FAIL got \(g) want \(w)")
  }
}
func chkb(_ g: Bool, _ w: Bool) {
  if g == w {
    npass = npass + 1
  } else {
    nfail = nfail + 1
    print("FAIL bool")
  }
}

// pre-clean any leftovers from a
// failed run (results discarded)
var z = false
z = deleteFile("TD/A.TXT")
z = deleteFile("TD/B.TXT")
z = deleteDirectory("TD")
z = deleteFile("TDX.TXT")
z = deleteFile("TDY.TXT")

// listDirectory on a missing
// directory -> empty
chk(listDirectory("TD").count, 0)

// createDirectory new -> true;
// exists; create-again -> false;
// lists empty
chkb(createDirectory("TD"), true)
chkb(fileExists("TD"), true)
chkb(createDirectory("TD"), false)
chk(listDirectory("TD").count, 0)

// add files; the listing grows
// (files + subdirs, header skipped)
chkb(writeFile("TD/A.TXT", "a"), true)
chk(listDirectory("TD").count, 1)
chkb(writeFile("TD/B.TXT", "b"), true)
chk(listDirectory("TD").count, 2)

// deleteDirectory refuses a
// NON-EMPTY directory -> false; it
// survives
chkb(deleteDirectory("TD"), false)
chkb(fileExists("TD"), true)

// empty it, then delete the
// now-empty directory; missing ->
// false
chkb(deleteFile("TD/A.TXT"), true)
chkb(deleteFile("TD/B.TXT"), true)
chk(listDirectory("TD").count, 0)
chkb(deleteDirectory("TD"), true)
chkb(fileExists("TD"), false)
chkb(deleteDirectory("TD"), false)

// renameFile: success moves it,
// missing source -> false
chkb(writeFile("TDX.TXT", "x"), true)
chkb(renameFile("TDX.TXT", "TDY.TXT"), true)
chkb(fileExists("TDX.TXT"), false)
chkb(fileExists("TDY.TXT"), true)
chkb(renameFile("NOPE.TXT", "Q.TXT"), false)
z = deleteFile("TDY.TXT")

print("pass \(npass) fail \(nfail)")
