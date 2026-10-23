# Tidal (best_effect_elemental.h) in the engine's idiom: the sea is one
# stroke with three waves, the crash's surge and rings and a deep body; the
# splash, bubbles, glints, hopping droplets and drips are emitters.
effect "Tidal"
uid 0x4c4c465800000108
revision 1
duration 5000
readout 1100
sound 2600

const PI = 3.14159265
const RISE = .6
const EBB = 3.55
const DRAINED = 4.75
const G = 420
const foam = rgb(215, 248, 255)
const spray = rgb(120, 206, 255)
const deep = rgb(20, 96, 255)

frame {
  ripple = mix(spray, accent, .35)
  rest = min(bottom - 8, ay1 + 12)
  roll = smooth((t - 1.45)/.8)*(1 - smooth((t - EBB)/.5))
  level = (t < RISE ? H + 6 - (H + 6 - rest)*easeout(t/RISE) : (t < EBB ? rest : rest + (H + 10 - rest)*smooth((t - EBB)/(DRAINED - EBB)))) + roll*1.2*sin(t*1.7)
  glint(RISE - .04, .45)
  u = t - RISE
  calm = 1 - smooth((t - EBB)/.35)
  sea = level < H + 4
}

# The sea: a choppy crest over a deep blue body; at the crash a surge rises
# under the readout and rings spread from it.
frame {
  exit !sea
  chop = 1.2 + 2.6*(t < RISE ? 1 - t/RISE : 0)
  stroke(x0: 0, x1: W - 1, y: level, a1: -chop, f1: .11, p1: -t*9, a2: -.6*chop, f2: .23, p2: t*6, a3: -1.7*roll, f3: .045, p3: -t*2.3,
         bumpX: acx, bumpH: -10*(1 - smooth(u/.4))*(u > 0), bumpW: 15, ringA: -3*exp(-1.8*u)*(u > 0), ringF: .3, ringP: -16*u,
         color: mix(foam, spray, .35), k: .98, smooth: 1, bodyColor: deep, bodyK: .66, bodyRows: 4)
  emit(10, salt: 107, birth: 0, birthSpan: 4.5, life: .8, lifeSpan: .6, x: W*.5, y: level + 40, spreadX: W*.46, spreadY: 32, dir: -PI/2, dirSpread: .05,
       speed: 30, speedSpan: 30, swayAmp: 2, swayFreq: 8, color: spray, k: .55, fadeIn: .2, fadeOut: .2, style: 1)
  emit(4, salt: 175, birth: 1.45, birthSpan: 2, life: 1.2, x: W*.5, y: level - 1.5, spreadX: W*.42, dir: 0, dirSpread: PI, speed: 13, speedSpan: 7,
       color: foam, k: .75*roll, fadeIn: .4, fadeOut: .4, flicker: .45, style: 2)
}

# Ripple rings spread over the surface in perspective, one after another.
repeat 3 {
  a = (u - .3*i)/1.0
  exit a <= 0 || a >= 1
  r = 8 + 66*easeout(a)
  ellipse(acx, rest + 2, r, r*.15, ripple, .8*(1 - a)*calm, max(12, floor(r*1.15*1.8)), 0, 1)
}

# The splash: a crown of droplets thrown up from both flanks of the surge;
# then droplets hop off the crests; as the sea drains, drips run off the readout.
frame {
  exit t < RISE
  emit(24, salt: 101, birth: RISE, birthSpan: .06, life: .66, lifeSpan: .9, x: acx - 21, y: rest - 3, spreadX: 13, dir: -PI/2 - .45, dirSpread: .35,
       speed: 150, speedSpan: 200, gravity: G, color: spray, endColor: foam, k: 1, fadeOut: .3, style: 1, trail: .02)
  emit(24, salt: 201, birth: RISE, birthSpan: .06, life: .66, lifeSpan: .9, x: acx + 21, y: rest - 3, spreadX: 13, dir: -PI/2 + .45, dirSpread: .35,
       speed: 150, speedSpan: 200, gravity: G, color: spray, endColor: foam, k: 1, fadeOut: .3, style: 1, trail: .02)
  emit(12, salt: 171, birth: 1.75, birthSpan: 1.8, life: .4, lifeSpan: .3, x: W*.5, y: level - 1, spreadX: W*.45, dir: -PI/2, dirSpread: .25,
       speed: 75, speedSpan: 70, gravity: G, color: foam, endColor: spray, k: .9*sea, style: 1, trail: .02)
  emit(6, salt: 105, birth: EBB - .05, birthSpan: .45, life: .5, x: acx, y: ay1 + 3, spreadX: (ax1 - ax0)*.5 - 6, gravity: G*.5,
       color: spray, endColor: foam, k: .9, fadeOut: .2, style: 1)
}
