// tfileio.swift - whole-file
// builtins + edge cases (design doc
// 019). FAMILY B ONLY (see README).
// Writes to the current prefix and
// cleans up after itself. Last line
// "fail 0" = pass.

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

var z = false
z = deleteFile("TF.TXT")
z = deleteFile("TF2.TXT")

// writeFile creates; readFile
// returns the content length
chkb(writeFile("TF.TXT", "hello"), true)
var n = 0 - 1
if let s = readFile("TF.TXT") {
  n = s.count
}
chk(n, 5)

// writeFile OVERWRITES (truncate,
// not append): "hi" replaces
// "hello"
chkb(writeFile("TF.TXT", "hi"), true)
n = 0 - 1
if let s = readFile("TF.TXT") {
  n = s.count
}
chk(n, 2)

// appendFile grows an existing
// file: "hi" + " world" = 8
chkb(appendFile("TF.TXT", " world"), true)
n = 0 - 1
if let s = readFile("TF.TXT") {
  n = s.count
}
chk(n, 8)

// writeFile "" -> readFile returns
// some("") (length 0), NOT nil
chkb(writeFile("TF.TXT", ""), true)
var gotSome = false
n = 0 - 1
if let s = readFile("TF.TXT") {
  gotSome = true
  n = s.count
}
chkb(gotSome, true)
chk(n, 0)

// readFile on a missing file -> nil
var gotNil = false
if let s = readFile("NOPE.TXT") {
  z = true
} else {
  gotNil = true
}
chkb(gotNil, true)

// appendFile creates the file when
// absent
z = deleteFile("TF2.TXT")
chkb(appendFile("TF2.TXT", "abc"), true)
chkb(fileExists("TF2.TXT"), true)
n = 0 - 1
if let s = readFile("TF2.TXT") {
  n = s.count
}
chk(n, 3)

// deleteFile: success, then missing
// -> false
chkb(deleteFile("TF.TXT"), true)
chkb(fileExists("TF.TXT"), false)
chkb(deleteFile("TF.TXT"), false)
chkb(deleteFile("TF2.TXT"), true)

print("pass \(npass) fail \(nfail)")
