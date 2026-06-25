// xgrdemo.swift - a large LO-RES
// GRAPHICS showcase (the graphical
// companion to xbig's number tour).
// Five scenes paint the 40x40 GR
// grid: colour bars, concentric
// squares, a flying starfield, a
// bouncing ball, and a rainbow
// flash. The starfield is the old
// 3D-screensaver kind. Scenes
// auto-advance on a short
// countdown - no keypress needed.
//
// SPEED NOTE: every scene draws
// with COLUMN fills (hlin, vlin)
// and sparse points, never a
// per-pixel fill with a divide in
// the loop. On a 1 MHz 6502 running
// bytecode, 1600 software divides a
// frame is what makes a fill crawl;
// column fills and a handful of
// plotted points stay quick.
//
// Like xbig this file is far
// bigger than the 2 KB Family-A
// staging cap, so it ships ONLY on
// the data disk and runs ONLY
// through the Family B Compiler,
// which streams the source from
// disk through its 4 KB window. Run
// it from the file selector with
// [X].
//
// STRUCTURE: every scene is its own
// function, called from a thin top
// level at the end. That matters for
// the compiler, not just for tidiness:
// the Family B compiler can only hold
// the top-level code plus the ONE
// function it is compiling in memory at
// once (completed functions flush out -
// to the Saturn bank or the //e aux
// card on the Tier 2/3 disks, or just
// stay in the flat buffer on Tier 1).
// Keeping the top level to a short list
// of calls means no single piece is big,
// so it compiles on EVERY Family B disk
// (flat II+ Tier 1, Saturn Tier 2, //e
// aux Tier 3). An earlier version put
// all five scenes at the top level; that
// top-level block overflowed the Tier
// 2/3 compilers' small resident window
// ("program too big for memory") even
// though the finished bytecode is tiny.
//
// It also needs the EXTRAS
// graphics builtins (gr/color/
// plot/hlin/vlin), which the Runner
// has on every Family B disk. On
// the host the GR builtins are
// no-ops, so it draws nothing - but
// the CHECKSUM below is pure
// integer maths (never a screen
// read), so one number verifies the
// run on host and hardware alike:
// expect "checksum = 1552".

// The only global: the running
// checksum every scene folds into.
// Each scene keeps its own scratch as
// function locals, so the global
// namespace stays a single name.
var check = 0

// ---------------------------------
// helpers
// ---------------------------------
//

// Wipe the 40x40 grid to black. 40
// column fills are quick: each vlin
// is one builtin call, not 40
// plots.
func clearGrid() {
  color(0)
  var col = 0
  while col < 40 {
    vlin(0, 39, col)
    col = col + 1
  }
}

// One square border (top, bottom,
// left, right edges) in colour c.
func border(_ lo: Int, _ hi: Int, _ c: Int) {
  color(c)
  hlin(lo, hi, lo)
  hlin(lo, hi, hi)
  vlin(lo, hi, lo)
  vlin(lo, hi, hi)
}

// Hold each scene for a short auto
// countdown - no key needed - then
// carry on. The count prints in the
// 4-line text window under the GR.
func pause() {
  var n = 3
  while n > 0 {
    print("next in \(n)...")
    wait(500)
    n = n - 1
  }
}

// ---------------------------------
// scene 1 - colour bars
// ---------------------------------
//
// the 16 lo-res colours as vertical
// bands - one vlin per column,
// colour cycling x % 16.
func scene1() {
  print("1: colour bars")
  var x = 0
  while x < 40 {
    color(x % 16)
    vlin(0, 39, x)
    check = check + x % 16
    x = x + 1
  }
  pause()
}

// ---------------------------------
// scene 2 - concentric squares
// ---------------------------------
//
// nest 20 square frames toward the
// middle, cycling the colour each
// ring - a bullseye of squares from
// hlin/vlin (no per-pixel work).
func scene2() {
  clearGrid()
  print("2: concentric squares")
  var i = 0
  while i < 20 {
    border(i, 39 - i, i % 16)
    check = check + i % 16
    i = i + 1
  }
  pause()
}

// ---------------------------------
// scene 3 - flying starfield
// ---------------------------------
//
// the other screen-saver classic:
// fly through space. Each star has
// a fixed direction (stx,sty) and a
// depth stz that shrinks per frame;
// projecting dir/depth sweeps it
// out from the centre, faster as it
// nears. Past the viewer it
// respawns deep. Near stars are
// brighter.
func scene3() {
  clearGrid()
  print("3: starfield")
  // 12 stars over 4 frames - kept small
  // so the per-frame divide + array work
  // stays quick on a 1 MHz 6502 (the scene
  // was sluggish at 18 stars x 30 frames).
  // Deterministic (so the checksum stays
  // host-checkable), spread + staggered.
  var stx = [0]
  var sty = [0]
  var stz = [0]
  // spx/spy remember where each star was
  // last plotted (-1 = nowhere), so a
  // frame erases just that one pixel
  // instead of wiping the whole grid -
  // 12 plots beat 40 column fills.
  var spx = [0]
  var spy = [0]
  stx[0] = -20
  sty[0] = -20
  stz[0] = 8
  spx[0] = -1
  spy[0] = -1
  var i = 1
  while i < 12 {
    stx.append(i * 37 % 41 - 20)
    sty.append(i * 53 % 41 - 20)
    stz.append(8 + i % 14 * 4)
    spx.append(-1)
    spy.append(-1)
    i = i + 1
  }
  var fs = 0
  while fs < 4 {
    i = 0
    while i < 12 {
      // erase this star's last pixel
      // (cheap: one plot, not a grid wipe).
      if spx[i] >= 0 {
        color(0)
        plot(spx[i], spy[i])
      }
      stz[i] = stz[i] - 3
      if stz[i] < 4 {
        stz[i] = 60
      }
      let z = stz[i]
      let px = 20 + stx[i] * 32 / z
      let py = 20 + sty[i] * 32 / z
      var col = 15
      if z > 22 {
        col = 9
      }
      if z > 42 {
        col = 6
      }
      // only plot on-grid stars; remember
      // where, so next frame erases it.
      spx[i] = -1
      if px >= 0 {
        if px <= 39 {
          if py >= 0 {
            if py <= 39 {
              color(col)
              plot(px, py)
              check = check + px + py
              spx[i] = px
              spy[i] = py
            }
          }
        }
      }
      i = i + 1
    }
    wait(15)
    fs = fs + 1
  }
  pause()
}

// ---------------------------------
// scene 4 - bouncing ball
// ---------------------------------
//
// a 2x2 ball ricochets off the four
// walls, erasing its last position
// each step so it leaves no trail.
// tone() chirps the speaker on each
// wall bounce.
func scene4() {
  clearGrid()
  border(0, 39, 5)   // grey court
  print("4: bouncing ball")
  var bx = 4
  var by = 6
  var bvx = 1
  var bvy = 1
  var fnum = 0
  while fnum < 80 {
    // erase the old ball (black 2x2).
    color(0)
    plot(bx, by)
    plot(bx + 1, by)
    plot(bx, by + 1)
    plot(bx + 1, by + 1)
    // advance and bounce off the
    // inner walls (1..37 leaves room
    // for the 2-wide ball, inside the
    // border).
    bx = bx + bvx
    by = by + bvy
    var hit = false
    if bx <= 1 {
      bvx = 1
      hit = true
    }
    if bx >= 37 {
      bvx = 0 - 1
      hit = true
    }
    if by <= 1 {
      bvy = 1
      hit = true
    }
    if by >= 37 {
      bvy = 0 - 1
      hit = true
    }
    if hit {
      tone(60, 40)   // short chirp
      check = check + 1
    }
    // draw the ball (bright white
    // 2x2).
    color(15)
    plot(bx, by)
    plot(bx + 1, by)
    plot(bx, by + 1)
    plot(bx + 1, by + 1)
    // pace the animation.
    wait(8)
    fnum = fnum + 1
  }
  pause()
}

// ---------------------------------
// finale - rainbow flash
// ---------------------------------
//
// flood the whole grid with one solid
// colour. A quick finale: step across the
// palette in 4s (0, 4, 8, 12) and land
// back on colour 0, so it fades to black
// before we drop to text - ~1/3 the frames
// of a full 16-colour sweep, still a clean
// dark ending (not a colour cut off mid-
// cycle).
func finale() {
  clearGrid()
  print("done - flash!")
  var flashes = 0
  while flashes < 5 {
    color(flashes * 4 % 16)
    var x = 0
    while x < 40 {
      vlin(0, 39, x)
      x = x + 1
    }
    wait(20)
    check = check + flashes * 4 % 16
    flashes = flashes + 1
  }
}

// ---------------------------------
// the show: low-res GR (40x40 grid +
// 4-line text window), then each
// scene in turn. The top level is
// just this short call list, so no
// one piece strains the compiler.
// ---------------------------------
//
gr()
scene1()
scene2()
scene3()
scene4()
finale()
text()  // back to 40-column text

// ---------------------------------
// the verdict: one number, summed
// from every scene's colour and
// coordinate maths (never a screen
// read), so it matches on host and
// on real hardware.
// ---------------------------------
//
print("== done ==")
print("checksum = \(check)")
