# Crimson (best_effect_eva.h, evaSphere) in the engine's idiom: the sea is a
# stroke with a body, the sphere's rim ellipses clipped at the sea, its
# turning arcs two trails, the motes emitters. Look-alike, not exact.
effect "Crimson"
uid 0x4c4c465800000112
revision 1
duration 5000
readout 1300
sound 2600

const PI = 3.14159265
const RISE = .8
const IGNITE = .82
const DISSOLVE = 3.9
const END = 5.0
const crimson = rgb(240, 22, 48)
const blood = rgb(160, 0, 28)
const lcl = rgb(255, 136, 56)
const pale = rgb(255, 230, 218)

frame {
  R = min(54, W*.5 - 12)
  horizon = min(bottom - 30, acy + R - 8)
  level = t < RISE ? H + 4 - (H + 4 - horizon)*easeout(t/RISE) : (t < DISSOLVE ? horizon : horizon + (H + 6 - horizon)*smooth((t - DISSOLVE)/(END - DISSOLVE - .2)))
  flash = t >= IGNITE ? 1 - smooth((t - IGNITE)/.4) : 0
  rising = t < RISE
  sea = rising ? 1 : (t < DISSOLVE ? .85 - .15*smooth((t - IGNITE)/2) : .7*(1 - smooth((t - DISSOLVE)/(END - DISSOLVE - .3))))
  chop = rising ? 1.2 + 1.6*(1 - t/RISE) : 1.0
  swell = rising ? 0 : 1.4*smooth((t - IGNITE)/.8)
  glint(IGNITE + .34, .45)
}

# The sea: its crest, a crimson body below it and, at ignition, a flare
# running out along the horizon from the centre.
frame {
  exit level >= H + 3
  stroke(x0: 0, x1: W - 1, y: level, a1: -chop, f1: .1, p1: t*6, a2: -.6*chop, f2: .27, p2: -t*4, a3: -swell, f3: .045, p3: -t*1.3,
         color: mix(crimson, pale, rising ? .74 : .55), k: sea*.74, smooth: 1, bodyColor: crimson, bodyK: sea*.62, bodyRows: rising ? 4 : (t < DISSOLVE ? 3 : 2))
  spread = 10 + 320*(t - IGNITE)
  stroke(x0: acx - spread, x1: acx, y: level - 1, a1: -chop, f1: .1, p1: t*6, a2: -.6*chop, f2: .27, p2: -t*4, a3: -swell, f3: .045, p3: -t*1.3,
         color: pale, k: 0, endK: flash)
  stroke(x0: acx, x1: acx + spread, y: level - 1, a1: -chop, f1: .1, p1: t*6, a2: -.6*chop, f2: .27, p2: -t*4, a3: -swell, f3: .045, p3: -t*1.3,
         color: pale, k: flash, endK: 0)
  # Rays thrown up by the rising wave, and motes rising ahead of it.
  emit(24, salt: 700, birth: 0, birthSpan: RISE, life: .25, x: W*.5, y: level - 2, spreadX: W*.48, dir: -PI/2, dirSpread: .05,
       speed: 60, color: lcl, endColor: crimson, k: sea*.6, fadeOut: 1, trail: .12)
  emit(12, salt: 701, birth: .05, birthSpan: .6, life: .45, x: W*.5, y: level - 8, spreadX: W*.46, dir: -PI/2, dirSpread: .2,
       speed: 55, speedSpan: 40, swayAmp: 2, swayFreq: 9, color: lcl, k: 1, fadeOut: 1, style: 1)
  emit(12, salt: 708, birth: IGNITE + .4, birthSpan: 2.9, life: 1.1, lifeSpan: .5, x: W*.5, y: level - 3, spreadX: W*.46, dir: -PI/2,
       dirSpread: .1, speed: 10, speedSpan: 8, swayAmp: 3, swayFreq: 3, color: lcl, endColor: pale, k: .75*sea, fadeIn: .15, fadeOut: 1, style: 1)
}

# Ignition: a cross-shaped flare where the sphere breaks the surface.
frame {
  exit t < IGNITE || t >= IGNITE + .26
  f = 1 - (t - IGNITE)/.26
  segment(acx, ay1 + 3, acx, horizon + 10, pale, f*f*1.3)
  spark(acx, horizon - 1, pale, 1.6*(1 - (t - IGNITE)/.18)*(t < IGNITE + .18), 3.2)
}

# The sphere rises out of the sea and breathes; the sea hides its lower part.
frame {
  exit t < IGNITE
  grow = easeout((t - IGNITE)/.45)
  sy = horizon + (acy - horizon)*grow
  breath = sin((t - IGNITE)*2.1)
  s = smooth((t - DISSOLVE)/(END - DISSOLVE - .15))
  r = R*(.25 + .75*grow) + 1.3*breath*smooth((t - IGNITE - .3)/.5) - 12*s*(t >= DISSOLVE)
  fade = t >= DISSOLVE ? 1 - s : 1
  glow = fade*(.86 + .08*breath)
  clip(level - 2)
  pts = min(256, max(24, floor(r*6.28)))
  # The rim in one pass: a hot line, its crimson outer edge and a soft limb inside.
  trail(acx, sy, r, r, 0, 2*PI, pts, mix(crimson, pale, .4 + .6*flash), k: (glow + .4*flash)*.69, falloff: 0, edge: .87,
        width: -1, bodyColor: mix(crimson, pale, .25))
  ellipse(acx, sy, r + 6, r + 6, blood, glow*.32*.63*(t < DISSOLVE), floor(pts/4), 0, 1)
  # Two brighter arcs turn slowly round the rim.
  spin = (t - IGNITE)*1.1
  trail(acx, sy, r, r, spin + PI/4, PI/2, floor(pts/4), pale, endColor: crimson, k: glow*.4, falloff: 1)
  trail(acx, sy, r, r, spin + PI + PI/4, PI/2, floor(pts/4), pale, endColor: crimson, k: glow*.4, falloff: 1)
  # Motes spiral in from the rim; as it dissolves, others drift up off it.
  on = smooth((t - IGNITE - .15)/.2)*(1 - smooth((t - DISSOLVE)/.3))
  emit(12, salt: 712, birth: IGNITE + .15, birthSpan: 3, life: 1.6, lifeSpan: .6, x: acx, y: sy, spreadX: r, spreadY: r*.85, dir: PI, dirSpread: PI,
       speed: -r*.6, drag: 1.2, swayAmp: 3, swayFreq: 2.4, color: lcl, endColor: pale, k: on, fadeIn: .1, style: 1, trail: .08, ring: 1)
  emit(10, salt: 706, birth: DISSOLVE, birthSpan: .6, life: .6, x: acx, y: sy, spreadX: r, spreadY: r, dir: -PI/2, dirSpread: PI,
       speed: 0, gravity: -40, color: lcl, endColor: crimson, k: .8, fadeOut: 1, style: 1, ring: 1)
  clip(H - 1)
}
