// Arrays demo. Phase 4 c5 shipped literals, subscript read, count,
// and append. Subscript-set + .isEmpty filled in at Phase 7 c6.

let xs = [10, 20, 30]
print(xs[0])               // 10
print(xs.count)            // 3
print(xs.isEmpty)          // false

let empty: [Int] = []
print(empty.isEmpty)       // true

// Subscript-set updates a slot in place.
var ys = [1, 2, 3]
ys[1] = 99
print(ys[1])               // 99

// Append must capture the result explicitly to update the variable.
var zs = [1]
zs = zs.append(2)
zs = zs.append(3)
zs = zs.append(4)
zs = zs.append(5)
print(zs.count)            // 5
print(zs[4])               // 5

// Sum via a function reading subscripts. The `[Int]` annotation
// is required as of Phase 7 commit 5 — earlier Phase 4 builds
// took plain `Int` because no validation ran.
func sumThree(_ a: [Int]) -> Int {
  var t = a[0] + a[1]
  t = t + a[2]
  return t
}
print(sumThree(xs))        // 60

// Phase 9 array methods: removeLast, removeAll, contains.
var ws = [1, 2, 3]
let last = ws.removeLast()
print(last)                // 3
print(ws.count)            // 2
print(ws.contains(2))      // true
print(ws.contains(3))      // false
ws.removeAll()
print(ws.isEmpty)          // true
print(ws.count)            // 0
