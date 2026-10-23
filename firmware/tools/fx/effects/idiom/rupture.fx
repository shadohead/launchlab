# Rupture (best_effect_sorcery.h) in the engine's idiom: the crackling rims
# and the smoulder are noise-gated strokes, the embers an emitter; the
# readout ripples, the inverted frame and the black lightning are as in the
# exact port (effects/rupture.fx).
effect "Rupture"
uid 0x4c4c465800000126
revision 1
duration 4800
readout 1800
sound 1900

const PI = 3.14159265
const HIT = .7
const CRACKLE = .8
const LINGER = 3.0
const END = 4.8
const red = rgb(255, 34, 26)
const hot = rgb(255, 205, 190)
const blood = rgb(190, 8, 16)

frame {
  hw = (ax1 - ax0)*.5
  hh = (ay1 - ay0)*.5
  a = t - HIT
  s = t/HIT
}

repeat 7 {
  exit t >= HIT
  amp = 1.8*smooth(t/HIT)
  yc = ay0 - 3 + (ay1 - ay0 + 6)*(t*(1/.3) - floor(t*(1/.3)))
  y = max(ay0, floor(yc) - 3) + i
  exit y > min(ay1, floor(yc) + 3)
  shiftrow(y, round(amp*sin((y - yc)*.9)))
}

repeat 6 {
  exit a < .1 || a >= .6
  v = a - .1
  u = v < .25 ? v/.25 : (v - .25)/.25
  amp = (v < .25 ? 2.6 : 1.5)*(1 - u)
  side = i < 3 ? -1 : 1
  reach = (hh + 3)*u
  yc = acy + side*reach
  y = max(ay0, floor(yc) - 1) + i % 3
  exit y > min(ay1, floor(yc) + 1)
  shiftrow(y, round(amp*(y == floor(yc) ? 1 : .55)*side))
}

repeat 3 {
  exit a < .75 || a >= 1.05
  yc = ay0 - 2 + (ay1 - ay0 + 4)*((a - .75)/.3)
  y = max(ay0, floor(yc) - 1) + i
  exit y > min(ay1, floor(yc) + 1)
  shiftrow(y, y == floor(yc))
}

frame {
  shade(rgb(205, 8, 22), .93*(a >= 0 && a < .05), 0)
  shade(hot, .85*(a >= .05 && a < .1), 1)
  glint(HIT + .12, .4)
}

# Charge: the aura crackles along the rim, accent turning red, arcs snap off it.
frame {
  exit t >= HIT
  charge = mix(accent, red, s*s)
  prime(ay0 - 4, ay0 - 3)
  prime(ay1 + 3, ay1 + 4)
  stroke(x0: ax0 - 2, x1: ax1 + 2, y: ay0 - 3, noiseScale: .156, noisePhase: frame*.35, noiseSalt: 331, gate: .59, color: charge, k: (.25 + .55*s)*.6)
  stroke(x0: ax0 - 2, x1: ax1 + 2, y: ay1 + 3, noiseScale: .156, noisePhase: frame*.35 + 35, noiseSalt: 332, gate: .59, color: charge, k: (.25 + .55*s)*.6)
}

repeat 10 {
  exit t >= HIT
  exit i >= 1 + floor(9*s*s)
  charge = mix(accent, red, s*s)
  seed = frame*16 + i
  exit rnd(seed, 311) < .2
  top = rnd(seed, 312) < .3
  x = ax0 + 4 + (ax1 - ax0 - 8)*rnd(seed, 313)
  y = top ? ay0 - 3.5 : ay1 + 3.5
  len = 6 + 10*rnd(seed, 314)*(.5 + s)
  dir = rnd(seed, 315) < .5 ? -1 : 1
  out = top ? -1 : 1
  mx = x + dir*len*.45
  my = y + out*(1.5 + (top ? 1.5 : 4)*rnd(seed, 316))
  fline(x, y, mx, my, charge, 240/256, 220/256)
  fline(mx, my, x + dir*len, y + out*.5, charge, 220/256, 60/256)
}

frame {
  exit t >= HIT || t <= HIT - .16
  u = (t - HIT + .16)/.16
  sc = 1 + 1.5*(1 - u)*(1 - u)
  ellipse(acx, acy, (hw + 6)*sc, (hh + 6)*sc, mix(red, white, u*.6), floor((.35 + .65*u)*256)/256, 160, 0, 1)
}

frame {
  exit a < 0 || a >= .1
  prime(ay0 - 4, ay0 - 3)
  prime(ay1 + 3, ay1 + 4)
  bk = a < .05 ? 1 : .45
  fline(max(0, ax0 - 3), ay0 - 3, min(W - 1, ax1 + 3), ay0 - 3, mix(red, hot, .3), bk*1.1, bk*1.1)
  fline(max(0, ax0 - 3), ay1 + 3, min(W - 1, ax1 + 3), ay1 + 3, mix(red, hot, .3), bk*1.1, bk*1.1)
  fline(max(0, ax0 - 4), ay0 - 4, min(W - 1, ax1 + 4), ay0 - 4, red, bk*.8, bk*.8)
  fline(max(0, ax0 - 4), ay1 + 4, min(W - 1, ax1 + 4), ay1 + 4, red, bk*.8, bk*.8)
}

frame {
  bu = (a - .1)/CRACKLE
  late = (a - .1 - CRACKLE)/(LINGER - .1 - CRACKLE)
  bolts = bu < 1 ? floor(7 - 4*bu) : 1
  power = bu < 1 ? 1 - .4*bu : .6 - .25*late
}

repeat 7 {
  exit a < .1 || a >= LINGER
  exit i >= bolts
  seed = frame*8 + i
  exit bu >= 1 && rnd(seed, 327) > .62 - .3*late
  up = i == bolts - 1 && bolts > 2
  an = up ? -PI*(.25 + .5*rnd(seed, 323)) : PI*(.12 + .76*(i + rnd(seed, 323))/max(1, bolts - 1))
  ca = cos(an)
  sa = sin(an)
  ex = ca/(hw + 3)
  ey = sa/(hh + 3)
  r0 = 1/sqrt(ex*ex + ey*ey)
  x = acx + r0*ca
  y = acy + r0*sa
  len0 = (up ? 9 + 6*rnd(seed, 324) : 24 + 34*rnd(seed, 324))*power
  len = up ? min(len0, (y - 1)/max(.3, -sa)) : len0
  exit len < 5
  rimbolt(x, y, x + len*ca, y + len*sa, seed, red, blood, min(1, power*1.2), 3)
  dot(x, y + (up ? -1 : 1), hot, power)
}

# Smoulder along the readout's foot, and embers drifting off the rim.
frame {
  smoulder = smooth((a - .3)/.4)*(1 - smooth((a - 2.6)/.8))
  exit smoulder <= 0
  prime(ay0 - 3, ay0 - 3)
  prime(ay1 + 3, ay1 + 4)
  stroke(x0: ax0 - 2, x1: ax1 + 2, y: ay1 + 3, noiseScale: .176, noisePhase: t*.9 + t*2.7, noiseSalt: 333, gate: .47, color: red, endColor: mix(red, hot, .25),
         k: .8*smoulder*.75, bodyColor: blood, bodyK: .8*smoulder*.35, bodyRows: 1)
  stroke(x0: ax0 - 2, x1: ax1 + 2, y: ay0 - 3, noiseScale: .176, noisePhase: t*.9 + 20, noiseSalt: 334, gate: .59, color: red, k: .8*smoulder*.4)
}

frame {
  emit(11, salt: 331, birth: HIT + .1, birthSpan: 2.8, life: .8, lifeSpan: .9, x: acx, y: ay0 - 3, spreadX: hw, dir: -PI/2, dirSpread: .9,
       speed: 16, speedSpan: 12, gravity: -24, swayAmp: 2.5, swayFreq: 6, color: hot, endColor: blood, k: 1.3*(1 - smooth((t - END + .7)/.5)),
       fadeOut: 1, flicker: .25, style: 1, trail: .03)
  emit(33, salt: 341, birth: HIT + .1, birthSpan: 2.8, life: .8, lifeSpan: .9, x: acx, y: ay1 + 3, spreadX: hw, dir: PI/2, dirSpread: .9,
       speed: 24, speedSpan: 30, gravity: -24, swayAmp: 2.5, swayFreq: 6, color: hot, endColor: blood, k: 1.3*(1 - smooth((t - END + .7)/.5)),
       fadeOut: 1, flicker: .25, style: 1, trail: .03)
}
