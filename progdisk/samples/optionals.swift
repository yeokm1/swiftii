// optionals.swift - optionals (T?),
// if-let, if-let-else, and the
// nil-coalescing operator ??. Runs
// on any SwiftII REPL (lite or
// extras).

// A lookup that can fail: returns
// the 1-based position of `target`
// in the array, or nil if it isn't
// there.
func indexOf(_ target: Int, _ xs: [Int]) -> Int? {
  var i = 0
  while i < xs.count {
    if xs[i] == target {
      return i + 1
    }
    i = i + 1
  }
  return nil
}

let xs = [4, 8, 15, 16, 23]

// if-let unwraps the some case.
if let pos = indexOf(15, xs) {
  print("found 15 at position \(pos)")
}

// if-let-else runs the else arm on
// nil.
if let pos = indexOf(99, xs) {
  print("found 99 at position \(pos)")
} else {
  print("99 is not in the list")
}

// ?? supplies a default when the
// optional is nil.
let where99 = indexOf(99, xs) ?? 0
print("where99 = \(where99)")
