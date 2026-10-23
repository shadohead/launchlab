# Bladework (best_effect_nier.h, nierFlurry) in the engine's idiom: every
# crescent, afterimage and echo is one arc-band trail call (the native code
# steps hundreds of samples a frame), motes and debris are emitters.
effect "Bladework"
uid 0x4c4c465800000122
revision 1
duration 4800
readout 1500
sound 2400

const PI = 3.14159265
const D = PI/180
const SWING = .075
const FINISH = .95
const sepia = rgb(222, 196, 140)

frame {
  glint(FINISH + .06, .45)
  steel = mix(rgb(214, 228, 255), accent, .18)
  endFade = 1 - smooth((t - 1.75)/.3)
  linger = 1 - smooth((t - 3.95)/.65)
}

# Anticipation: a hairline glint along the blade's edge under the readout.
frame {
  exit t >= .3
  u = easeout(t/.2)
  ka = 1 - smooth((t - .18)/.1)
  half = (W*.5 - 8)*u
  stroke(x0: acx - half, x1: acx, y: ay1 + 5, color: steel, k: 0, endK: .75*ka, smooth: 1)
  stroke(x0: acx, x1: acx + half, y: ay1 + 5, color: steel, k: .75*ka, endK: 0, smooth: 1)
  twinkle(acx + half, ay1 + 5, white, ka, 3)
}

# The combo: five slashes and the two-stroke finisher, each a crescent.
repeat 7 {
  a = i
  age = t - table(i, .24, .37, .5, .63, .76, FINISH, FINISH + .07)
  exit age < 0
  fin = i >= 5
  hold = fin*.32
  after = max(0, age - SWING - hold)
  k = (age < SWING + hold ? 1 : 1 - smooth(after/.3))*endFade
  exit k <= .02
  ox = table(a, -8, 12, 0, -34, 32, -60, 60)
  oy = table(a, 46, 40, 60, 36, 36, -10, -10)
  rx = table(a, 58, 54, 62, 30, 30, 110, 110)
  ry = table(a, 34, 28, 24, 60, 60, 70, 70)
  rot = table(a, -25, 20, 0, 14, -14, 0, 0)*D
  a0 = table(a, 160, 25, 200, -80, 260, -12, 192)*D
  a1 = table(a, 330, -150, 340, 80, 100, 82, 98)*D
  h = easeout(age/SWING)
  tl = age < SWING ? max(0, h - .75) : .25 + .75*smooth(after/.28)
  exit h <= tl
  width = (fin ? 3.3 : 3.2)*(age < SWING ? 1 : 1 - .5*smooth((age - SWING)/.25))
  span = (a1 - a0)*(h - tl)
  trail(acx + ox, acy + oy, rx, ry, a0 + (a1 - a0)*h, span, max(2, floor(abs(span)*(rx + ry)*.5)) + 1, white,
        k: k, falloff: 0, rot: rot, width: width, bodyColor: steel, tailK: .44)
}

# Each stroke's sepia afterimage, 60 ms behind and offset.
repeat 7 {
  a = i
  fin = i >= 5
  hold = fin*.32
  ea = t - table(i, .24, .37, .5, .63, .76, FINISH, FINISH + .07) - .06
  exit ea <= 0 || ea >= SWING + hold + .3
  eh = easeout(ea/SWING)
  eAfter = max(0, ea - SWING - hold)
  et = ea < SWING ? max(0, eh - .75) : .25 + .75*smooth(eAfter/.28)
  exit eh <= et
  ox = table(a, -8, 12, 0, -34, 32, -60, 60)
  oy = table(a, 46, 40, 60, 36, 36, -10, -10)
  rx = table(a, 58, 54, 62, 30, 30, 110, 110)
  ry = table(a, 34, 28, 24, 60, 60, 70, 70)
  rot = table(a, -25, 20, 0, 14, -14, 0, 0)*D
  a0 = table(a, 160, 25, 200, -80, 260, -12, 192)*D
  a1 = table(a, 330, -150, 340, 80, 100, 82, 98)*D
  espan = (a1 - a0)*(eh - et)
  trail(acx + ox + (fin ? 0 : -2), acy + oy + (fin ? 3 : 2), rx, ry, a0 + (a1 - a0)*eh, espan, max(2, floor(abs(espan)*(rx + ry)*.5)) + 1, sepia,
        k: .5*(1 - smooth(eAfter/.25))*endFade, falloff: 0, rot: rot, width: fin ? 0 : 1.6, tailK: .44)
}

# The finisher lands: the earlier strokes echo as faint whole afterimages.
repeat 5 {
  a = i
  fa = t - FINISH - .07
  exit fa <= 0 || fa >= .4
  ox = table(a, -8, 12, 0, -34, 32, -60, 60)
  oy = table(a, 46, 40, 60, 36, 36, -10, -10)
  rx = table(a, 58, 54, 62, 30, 30, 110, 110)
  ry = table(a, 34, 28, 24, 60, 60, 70, 70)
  rot = table(a, -25, 20, 0, 14, -14, 0, 0)*D
  a0 = table(a, 160, 25, 200, -80, 260, -12, 192)*D
  a1 = table(a, 330, -150, 340, 80, 100, 82, 98)*D
  span = (a1 - a0)*.54
  trail(acx + ox, acy + oy, rx, ry, a0 + (a1 - a0)*.92, span, max(2, floor(abs(span)*(rx + ry)*.5)) + 1, sepia,
        k: .34*(1 - fa/.4)*smooth(fa/.05), falloff: 0, rot: rot, tailK: .44)
}

# Sustain: the X stays as a breathing afterimage...
repeat 2 {
  a = 5 + i
  hk = .5*smooth((t - 1.45)/.35)*linger*(.8 + .2*sin((t - 1.45)*2.2))
  exit hk <= .02
  ox = table(a, -8, 12, 0, -34, 32, -60, 60)
  oy = table(a, 46, 40, 60, 36, 36, -10, -10)
  rx = table(a, 58, 54, 62, 30, 30, 110, 110)
  ry = table(a, 34, 28, 24, 60, 60, 70, 70)
  rot = table(a, -25, 20, 0, 14, -14, 0, 0)*D
  a0 = table(a, 160, 25, 200, -80, 260, -12, 192)*D
  a1 = table(a, 330, -150, 340, 80, 100, 82, 98)*D
  span = (a1 - a0)*.54
  trail(acx + ox, acy + oy, rx, ry, a0 + (a1 - a0)*.92, span, max(2, floor(abs(span)*(rx + ry)*.5)) + 1, sepia,
        k: hk, falloff: 0, rot: rot, width: 1.4, tailK: .44)
}

# ...a glint runs slowly along one of its strokes, then the other...
repeat 3 {
  a = 5 + i % 2
  gu = (t - table(i, 1.75, 2.6, 3.4))/.75
  exit gu <= 0 || gu >= 1
  ox = table(a, -8, 12, 0, -34, 32, -60, 60)
  oy = table(a, 46, 40, 60, 36, 36, -10, -10)
  rx = table(a, 58, 54, 62, 30, 30, 110, 110)
  ry = table(a, 34, 28, 24, 60, 60, 70, 70)
  rot = table(a, -25, 20, 0, 14, -14, 0, 0)*D
  a0 = table(a, 160, 25, 200, -80, 260, -12, 192)*D
  a1 = table(a, 330, -150, 340, 80, 100, 82, 98)*D
  gh = .4 + .5*smooth(gu)
  gspan = (a1 - a0)*.12
  trail(acx + ox, acy + oy, rx, ry, a0 + (a1 - a0)*(gh + .06), gspan, max(2, floor(abs(gspan)*(rx + ry)*.5)) + 1, white,
        k: .95*sin(PI*gu)*linger, falloff: 0, rot: rot, width: 1.1, bodyColor: steel, tailK: .44)
}

# ...and fainter echoes of the combo sweep by.
repeat 5 {
  a = table(i, 2, 0, 4, 1, 3)
  eage = t - table(i, 1.85, 2.35, 2.95, 3.3, 3.75)
  exit eage < 0 || eage >= .6
  ox = table(a, -8, 12, 0, -34, 32, -60, 60)
  oy = table(a, 46, 40, 60, 36, 36, -10, -10)
  rx = table(a, 58, 54, 62, 30, 30, 110, 110)
  ry = table(a, 34, 28, 24, 60, 60, 70, 70)
  rot = table(a, -25, 20, 0, 14, -14, 0, 0)*D
  a0 = table(a, 160, 25, 200, -80, 260, -12, 192)*D
  a1 = table(a, 330, -150, 340, 80, 100, 82, 98)*D
  eh = easeout(eage/.14)
  etl = eage < .14 ? max(0, eh - .7) : .3 + .7*smooth((eage - .14)/.4)
  espan = (a1 - a0)*(eh - etl)
  trail(acx + ox + (i % 2 ? 4 : -5), acy + oy + (i % 3)*3, rx, ry, a0 + (a1 - a0)*eh, espan, max(2, floor(abs(espan)*(rx + ry)*.5)) + 1, steel,
        k: .55*linger*(1 - smooth((eage - .14)/.42))*(eh > etl), falloff: 0, rot: rot, width: 1.6, bodyColor: sepia, tailK: .44)
}

# Glints catch the light; square motes rise and sway; debris falls out of the finisher.
frame {
  emit(7, salt: 427, birth: 1.6, birthSpan: 2.2, life: .3, x: acx, y: ay1 + 38, spreadX: 55, spreadY: 30, color: white, k: linger, fadeIn: .5, fadeOut: .5, style: 2)
  emit(20, salt: 481, birth: 1.3, birthSpan: 2.6, life: 1.3, lifeSpan: .7, x: acx, y: ay1 + 43, spreadX: 62, spreadY: 29, dir: -PI/2, dirSpread: 0,
       speed: 6, speedSpan: 8, swayAmp: 3, swayFreq: 1.3, color: sepia, endColor: steel, k: .95*linger, fadeIn: .5, fadeOut: .5, flicker: .28, style: 1)
  emit(16, salt: 421, birth: FINISH + .08, birthSpan: .12, life: .8, lifeSpan: .4, x: acx, y: ay1 + 30, spreadX: 45, spreadY: 15, dir: -PI/2, dirSpread: .8,
       speed: 25, speedSpan: 30, gravity: 120, color: white, endColor: sepia, k: endFade, fadeOut: 1)
}
