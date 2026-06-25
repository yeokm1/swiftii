// 800_fileio.swift — Family B file/dir CRUD end-to-end (design 017): the
// WITH_SWB builtins compiled from disk. /tmp paths; fits FILE_SRC_SIZE.

func ok(_ c: Bool) -> Int {
  if c {
    return 1
  }
  return 0
}

// Best-effort cleanup of residue from a prior failed run (discarded).
var junk = 0
junk = junk + ok(deleteFile("/tmp/sw_it_dir/inner.txt"))
junk = junk + ok(deleteDirectory("/tmp/sw_it_dir"))
junk = junk + ok(deleteFile("/tmp/sw_it_a.txt"))
junk = junk + ok(deleteFile("/tmp/sw_it_b.txt"))

var pass = 0
var total = 0
print("fileio integration")

total = total + 1
pass = pass + ok(writeFile("/tmp/sw_it_a.txt", "hello"))
total = total + 1
pass = pass + ok(fileExists("/tmp/sw_it_a.txt"))

var rlen = 0 - 1
if let s = readFile("/tmp/sw_it_a.txt") {
  rlen = s.count
}
print("read len = \(rlen)")
total = total + 1
pass = pass + ok(rlen == 5)

total = total + 1
pass = pass + ok(appendFile("/tmp/sw_it_a.txt", " world"))
var rlen2 = 0 - 1
if let s = readFile("/tmp/sw_it_a.txt") {
  rlen2 = s.count
}
print("append len = \(rlen2)")
total = total + 1
pass = pass + ok(rlen2 == 11)

total = total + 1
pass = pass + ok(renameFile("/tmp/sw_it_a.txt", "/tmp/sw_it_b.txt"))
total = total + 1
pass = pass + ok(fileExists("/tmp/sw_it_a.txt") == false)
total = total + 1
pass = pass + ok(fileExists("/tmp/sw_it_b.txt"))

total = total + 1
pass = pass + ok(createDirectory("/tmp/sw_it_dir"))
total = total + 1
pass = pass + ok(writeFile("/tmp/sw_it_dir/inner.txt", "x"))

let names = listDirectory("/tmp/sw_it_dir")
total = total + 1
pass = pass + ok(names.count == 1)
var i = 0
while i < names.count {
  print("list: \(names[i])")
  i = i + 1
}

total = total + 1
pass = pass + ok(deleteFile("/tmp/sw_it_dir/inner.txt"))
total = total + 1
pass = pass + ok(deleteDirectory("/tmp/sw_it_dir"))
total = total + 1
pass = pass + ok(deleteFile("/tmp/sw_it_b.txt"))
total = total + 1
pass = pass + ok(fileExists("/tmp/sw_it_b.txt") == false)

if pass == total {
  print("RESULT: PASS")
} else {
  print("RESULT: FAIL")
}
