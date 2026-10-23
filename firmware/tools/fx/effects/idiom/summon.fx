# Summon (best_effect_ninja.h, ninjaSummon) in the engine's idiom: each
# line of script is one runes call, the closing ring a trail, the smoke
# nine puffs drawn as one cloud (a shared dithered body and one outline),
# the wisps an emitter. Look-alike, not exact.
effect "Summon"
uid 0x4c4c465800000120
revision 1
duration 5000
readout 1100
sound 2600

const PI = 3.14159265
const SLAM = .04
const POOF = .62
const SETTLE = 3.9
const END = 5.0
const SQUASH = .32
const smoke = rgb(230, 234, 242)

frame {
  glint(POOF, .45)
  ink = mix(rgb(255, 214, 150), accent, .2)
  shade = mix(rgb(150, 160, 185), accent, .25)
  px = acx
  py = ay1 + 13
  sealFade = 1 - smooth((t - POOF - .2)/.5)
  a = t - POOF
}

# The slam: a flash and dust kicked sideways.
frame {
  exit t < SLAM || t >= SLAM + .2
  u = (t - SLAM)/.2
  spark(px, py, ink, 1.4*(1 - u), 3)
}
repeat 8 {
  exit t < SLAM || t >= SLAM + .2
  u = (t - SLAM)/.2
  side = i < 4 ? -1 : 1
  j = i % 4
  dot(px + side*(5 + j*5 + 30*u), py + 1 - j*.7 - 6*u, smoke, (1 - u)*(.8 - .15*j))
}

# Script: six lines of glyphs written outward across the floor.
repeat 6 {
  exit sealFade <= 0 || t < SLAM
  ang = i*PI/3 + .26
  ca = cos(ang)
  sa = sin(ang)*SQUASH
  full = 50 + 8*rnd(i, 701)
  len = full*easeout((t - SLAM - .03*i)/.42)
  runes(px + 6*ca, py + 6*sa, 4*ca, 4*sa, len >= 6 ? floor((len - 6)/4) + 1 : 0, ink, .9*sealFade, i*64)
  dot(px + len*ca, py + len*sa, white, sealFade*(len < full - 1))
}

# The ring closes round the lines' ends.
frame {
  ringU = clamp01((t - .42)/.16)
  exit sealFade <= 0 || ringU <= 0
  sweep = 2*PI*ringU
  trail(px, py, 58, 58*SQUASH, .26 + sweep, sweep, max(2, floor(150*ringU)), ink, k: .8*sealFade, falloff: 0)
}

# The pop: a white flash and burst lines.
frame {
  exit a < 0 || a >= .1
  r = 6 + 7*a/.1
  glow(px, py + 3, r, r, smoke, .9*(1 - a/.1))
}
repeat 10 {
  exit a < 0 || a >= .16
  u = a/.16
  ang = i*PI/5 + .3
  r0 = 10 + 46*easeout(u)
  ca = cos(ang)
  sa = sin(ang)*.5
  line(px + r0*ca, py + r0*sa, px + (r0 + 9)*ca, py + (r0 + 9)*sa, smoke, (1 - u)*.9)
}

# The puffs: thrown out along the floor and up, swelling, then drifting
# apart and bobbing, thinning away by about 3.6 s.
repeat 9 {
  exit a < 0
  age = max(0, a - .04*rnd(i, 702))
  u = min(1, age/(3 + .5*rnd(i, 703)))
  k = (1 - smooth((u - .25)/.75))*smooth(age/.03)
  exit k <= .02
  side = i == 0 ? 0 : (i % 2 ? 1 : -1)
  slot = floor((i + 1)/2)
  out = easeout(age/.35)
  drift = age < .6 ? age : .6 + (age - .6)*.45
  x = px + side*(12*slot + 6*rnd(i, 704))*out + side*8*drift + 1.6*sin(age*(1.1 + .5*rnd(i, 707)) + i)
  y = py + 2 + (i == 0 ? 2 : (slot > 3 ? -1 : 9 - 3*slot))*out - (3 + 3*rnd(i, 705))*drift + 1.2*sin(age*1.4 + 2*i)
  r = (3 + (slot > 3 ? 7 : 9 + 3*rnd(i, 706))*out)*(1 + .1*drift)
  puff(x, y, r, k)
}
frame {
  cloud(shade, smoke, top: ay1 + 2)
}

# Wisps curl off the cloud through the sustain and the settle.
frame {
  exit a < .6
  fade = 1 - smooth((t - SETTLE)/(END - .1 - SETTLE))
  emit(20, salt: 710, birth: POOF + .6, birthSpan: 3.5, life: .8, lifeSpan: .6, x: px, y: py + 15, spreadX: 55, spreadY: 11, dir: -PI/2, dirSpread: .3,
       speed: 11, swayAmp: 3, swayFreq: 4, color: smoke, k: .7*fade, fadeIn: .5, fadeOut: .5, style: 1)
  emit(10, salt: 711, birth: POOF + .6, birthSpan: 3.5, life: .8, lifeSpan: .6, x: px, y: py + 15, spreadX: 55, spreadY: 11, dir: -PI/2, dirSpread: .3,
       speed: 11, swayAmp: 3, swayFreq: 4, color: shade, k: .7*fade, fadeIn: .5, fadeOut: .5, style: 1)
}
