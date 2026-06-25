// xsnake.swift - Tron-style trail
// game. NEEDS EXTRAS [S]/[A].
// Speed 1-9 sets paceLimit.

var paceLimit = 30

print("=== SNAKE ===")
print("leave a trail; crash into a")
print("wall or your own trail = over.")
print("steer:")
print("        I = up")
print("  J = left   K = right")
print("        M = down   Q = quit")
print("press 1 (slow) to 9 (fast)")
print("to set the speed and start...")

// Wait for a digit 1-9; bounded.
var go = 1
var sel = 5
var w = 0
while go == 1 {
  let s = peek(49152)
  if s >= 177 {
    sel = s - 176
    go = 0
  }
  w = w + 1
  if w > 30000 {
    go = 0
  }
}
if sel > 9 {
  sel = 9
}
// 1 = slow ... 9 = fastest
paceLimit = 108 - sel * 12
// consume the start key
poke(49168, 0)

// low-res GR: 40x40 grid + text
gr()
color(13) // bright yellow trail

var x = 20
var y = 20
var dx = 1
var dy = 0
var alive = true
var score = 0
plot(x, y)

while alive {
  // Keys = ASCII+128; clear strobe.
  let key = peek(49152)
  poke(49168, 0)
  if key == 201 {     // I - up
    dx = 0
    dy = 0 - 1
  }
  if key == 139 {     // up-arrow
    dx = 0
    dy = 0 - 1
  }
  if key == 205 {     // M - down
    dx = 0
    dy = 1
  }
  if key == 138 {     // down-arrow
    dx = 0
    dy = 1
  }
  if key == 202 {     // J - left
    dx = 0 - 1
    dy = 0
  }
  if key == 136 {     // left-arrow
    dx = 0 - 1
    dy = 0
  }
  if key == 203 {     // K - right
    dx = 1
    dy = 0
  }
  if key == 149 {     // right-arrow
    dx = 1
    dy = 0
  }
  if key == 209 {     // Q - quit
    alive = false
  }

  x = x + dx
  y = y + dy

  if x < 0 {
    alive = false
  } else if x > 39 {
    alive = false
  } else if y < 0 {
    alive = false
  } else if y > 39 {
    alive = false
  } else if scrn(x, y) != 0 {
    alive = false
  } else {
    plot(x, y)
    score = score + 1
  }

  // Frame delay (nested while).
  var p = 0
  while p < paceLimit {
    p = p + 1
  }
}

text()
print("crashed! trail length \(score)")
