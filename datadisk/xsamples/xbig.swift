// xbig.swift - a deliberately LARGE
// program (Phase 15 / design doc
// 018). The Family A REPL stages at
// most 2 KB of source, and before
// doc 016 the Family B compiler
// capped at ~2.8 KB; this file is
// about three times that (and over
// twice the compiler's 4 KB source
// window), so it only compiles on a
// Family B disk, where the compiler
// streams the source from disk
// through the window, sliding at
// statement boundaries. Run it from
// the file selector with [X]; it is
// also too big for the on-target
// editor's buffer, which is rather
// the point. Every section folds
// its results into a running
// checksum printed at the end, so
// one number verifies the whole
// run: expect "checksum = 6265".
//
// Each section is wrapped in a
// function so its bytecode flushes
// to the paged store and its arrays
// /strings free on return - that is
// what lets the big program compile
// AND run on all three tiers (flat
// II+, Tier 2 Saturn, Tier 3 //e
// aux), not just the flat II+ one.
// The checksum is purely numeric, so
// the terse labels do not change it.
//
// Sections: number theory,
// fibonacci + array statistics,
// string building, sorting + binary
// search, collatz, times tables,
// optionals, and a fizzbuzz finale.

var check = 0

// greatest common divisor,
// iterative (euclid's algorithm).
func gcd(_ a: Int, _ b: Int) -> Int {
  var x = a
  var y = b
  while y != 0 {
    let t = y
    y = x % y
    x = t
  }
  return x
}

// integer exponentiation by
// repeated multiplication.
func power(_ base: Int, _ exp: Int) -> Int {
  var result = 1
  var i = 0
  while i < exp {
    result = result * base
    i = i + 1
  }
  return result
}

// trial-division primality test;
// only odd divisors after 2.
func isprime(_ n: Int) -> Bool {
  if n < 2 {
    return false
  }
  if n % 2 == 0 {
    return n == 2
  }
  var d = 3
  while d * d <= n {
    if n % d == 0 {
      return false
    }
    d = d + 2
  }
  return true
}

// sum of the proper divisors of n
// (used to spot perfect numbers).
func divsum(_ n: Int) -> Int {
  var s = 0
  var d = 1
  while d < n {
    if n % d == 0 {
      s = s + d
    }
    d = d + 1
  }
  return s
}

// binary search over a sorted array:
// O(log n) lookups.
func bsearch(_ key: Int, _ xs: [Int]) -> Int {
  var lo = 0
  var hi = xs.count - 1
  while lo <= hi {
    let mid = (lo + hi) / 2
    if xs[mid] == key {
      return mid
    }
    if xs[mid] < key {
      lo = mid + 1
    } else {
      hi = mid - 1
    }
  }
  return -1
}

// length of the collatz chain from
// n down to 1 (the 3n+1 problem).
func collatz(_ start: Int) -> Int {
  var n = start
  var steps = 0
  while n != 1 {
    if n % 2 == 0 {
      n = n / 2
    } else {
      n = 3 * n + 1
    }
    steps = steps + 1
  }
  return steps
}

// 1-based position of target in xs,
// or nil when absent.
func indexof(_ target: Int, _ xs: [Int]) -> Int? {
  var i = 0
  while i < xs.count {
    if xs[i] == target {
      return i + 1
    }
    i = i + 1
  }
  return nil
}

// section 1 - number theory
func section1() {
  print("== 1 numtheory ==")
  print("gcd 1071,462 = \(gcd(1071, 462))")
  print("2^12 = \(power(2, 12))")
  check = check + gcd(1071, 462) + power(2, 12)
  var row = ""
  var n = 2
  var nprimes = 0
  while n < 60 {
    if isprime(n) {
      row = row + String(n) + " "
      nprimes = nprimes + 1
      check = check + n
    }
    n = n + 1
  }
  print("primes <60:")
  print(row)
  print("count = \(nprimes)")
  n = 2
  while n <= 30 {
    if divsum(n) == n {
      print("\(n) perfect")
      check = check + n
    }
    n = n + 1
  }
}

// section 2 - fibonacci and array
// statistics
func section2() {
  print("== 2 fib ==")
  var fibs = [0, 1]
  while fibs.count < 19 {
    fibs.append(fibs[fibs.count - 1] + fibs[fibs.count - 2])
  }
  print("\(fibs.count) fibs, last \(fibs[fibs.count - 1])")
  var sum = 0
  var biggest = fibs[0]
  for i in 0..<fibs.count {
    sum = sum + fibs[i]
    biggest = max(biggest, fibs[i])
  }
  print("sum \(sum) big \(biggest)")
  check = check + sum % 1000
  var lo = 0
  var hi = fibs.count - 1
  while lo < hi {
    let t = fibs[lo]
    fibs[lo] = fibs[hi]
    fibs[hi] = t
    lo = lo + 1
    hi = hi - 1
  }
  print("rev head \(fibs[0]) tail \(fibs[fibs.count - 1])")
  check = check + fibs[0] % 1000
  var coprime = true
  for i in 0..<fibs.count - 1 {
    if gcd(fibs[i], fibs[i + 1]) != 1 {
      coprime = false
    }
  }
  print("coprime: \(coprime)")
}

// section 3 - building strings
func section3() {
  print("== 3 bars ==")
  let bars = [3, 7, 1, 5, 9, 2]
  for i in 0..<bars.count {
    var stars = ""
    var k = 0
    while k < bars[i] {
      stars = stars + "*"
      k = k + 1
    }
    print("\(bars[i])|\(stars)")
    check = check + bars[i]
  }
  var v = 9081
  var digits = ""
  while v > 0 {
    digits = digits + String(v % 10)
    v = v / 10
  }
  print("9081 rev \(digits)")
  print("len \(digits.count)")
  check = check + digits.count
}

// section 4 - sorting and searching
func section4() {
  print("== 4 sort ==")
  var cards = [31, 4, 15, 92, 65, 35, 8, 97, 9, 27]
  // bubble sort, smallest first. quadratic,
  // but the list is short and the 6502
  // doesn't mind honest work.
  var pass = 0
  var swapped = true
  while swapped {
    swapped = false
    for i in 0..<cards.count - 1 {
      if cards[i] > cards[i + 1] {
        let tmp = cards[i]
        cards[i] = cards[i + 1]
        cards[i + 1] = tmp
        swapped = true
      }
    }
    pass = pass + 1
  }
  print("sorted in \(pass) passes")
  var row = ""
  for i in 0..<cards.count {
    row = row + String(cards[i]) + " "
  }
  print(row)
  check = check + cards[0] + cards[cards.count - 1]
  print("65 -> \(bsearch(65, cards))")
  print("50 -> \(bsearch(50, cards))")
  check = check + bsearch(65, cards)
}

// section 5 - collatz flights
func section5() {
  print("== 5 collatz ==")
  var champ = 1
  var record = 0
  var n = 1
  while n < 30 {
    let len = collatz(n)
    if len > record {
      record = len
      champ = n
    }
    n = n + 1
  }
  print("longest <30: \(champ), \(record) steps")
  check = check + champ + record
}

// section 6 - times tables
func section6() {
  print("== 6 tables ==")
  for r in 2..<5 {
    var line = ""
    for c in 2..<8 {
      line = line + String(r * c) + " "
    }
    print("\(r)x: \(line)")
    check = check + r
  }
  for c in 0..<5 {
    let cel = c * 25
    let fah = cel * 9 / 5 + 32
    print("\(cel)c = \(fah)f")
    check = check + fah % 10
  }
}

// section 7 - optionals
func section7() {
  print("== 7 optionals ==")
  let deck = [4, 8, 15, 16, 23, 42]
  if let pos = indexof(23, deck) {
    print("23 at \(pos)")
    check = check + pos
  } else {
    print("23 missing")
  }
  if let pos = indexof(99, deck) {
    print("99 at \(pos)")
  } else {
    print("99 not in deck")
  }
  let fallback = indexof(99, deck) ?? 0
  print("fallback = \(fallback)")
}

// section 8 - fizzbuzz finale
func section8() {
  print("== 8 fizzbuzz ==")
  var n = 1
  while n <= 15 {
    if n % 15 == 0 {
      print("fizzbuzz")
    } else if n % 3 == 0 {
      print("fizz")
    } else if n % 5 == 0 {
      print("buzz")
    } else {
      print(n)
    }
    n = n + 1
  }
  check = check + 15
}

// the verdict: a single number that
// depends on every section above.
// expected 6265: gcd 21 + power
// 4096 + primes 440 + perfects 34 +
// fib sum (6764 % 1000 = 764) +
// reversed head (2584 % 1000 = 584)
// + bars 27 + digits 4 + sort ends
// (4 + 97) + bsearch index 7 +
// collatz (27 + 111) + table rows 9
// + fahrenheit last digits 20 +
// position 5 + fizz 15.
section1()
section2()
section3()
section4()
section5()
section6()
section7()
section8()
print("== done ==")
print("checksum = \(check)")
