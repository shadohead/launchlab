# Slash in the engine's idiom: the streak and the cooling seam are strokes
# along the cut, sparks and embers are emitters (effects/slash.fx is the
# exact port, one run per column).
effect "Slash"
uid 0x4c4c465800000105
revision 1
duration 5000
readout 1400
sound 1700

const PI = 3.14159265
const SWING = .12
const SPLIT = .16
const OPEN = .3
const CLOSE = .95
const SHUT = .35
const FUSE = .18
const SLOPE = .3
const gold = rgb(255, 214, 110)

frame {
  shut = CLOSE + SHUT
  open = easeout((t - SPLIT)/OPEN)*(1 + .08*min(1, (t - SPLIT)/(CLOSE - SPLIT)))
  along = t >= SPLIT && t < shut ? 3.2*open*(t < CLOSE ? 1 : 1 - smooth((t - CLOSE)/SHUT)) : 0
  head = -24 + (W + 48)*easeout(t/SWING)
  heat = t >= shut - .06 && t < shut + FUSE ? .55*(1 - smooth((t - shut + .06)/(FUSE + .06))) : (t < .32 ? 1 - smooth((t - .1)/.22) : 0)
  sustain = t > shut
  endFade = 1 - smooth((t - 4.0)/.5)
  sfade = t < SWING ? 1 : 1 - smooth((t - SWING)/.17)
  stail = t < SWING ? head - 70 : -24
  # The cut line: y = acy - (x - acx)*SLOPE.
  cutY0 = acy + acx*SLOPE
}

frame {
  exit heat < 1/256 && along <= 0
  cut(along, min(3.2, along), heat, t < SWING ? round(head) : W)
}

frame {
  exit t >= .26
  blade(ax0 - 2, min(ax1 + 2, head), acx, acy, SLOPE, white, t < .16 ? 1 : 1 - smooth((t - .16)/.1))
}

# The streak along the cut, bright at the blade's head and fading to its tail.
frame {
  exit sfade <= 0
  stroke(x0: stail, x1: head, y: cutY0 - stail*SLOPE, slope: -SLOPE, color: mix(accent, white, .3), endColor: white,
         k: 0, endK: sfade*(t < SWING ? 1 : .65), width: 2)
}

# Sparks off both edges of the cut while the halves open.
frame {
  emit(9, salt: 91, birth: SPLIT - .06, birthSpan: .22, life: .35, lifeSpan: .3, x: acx, y: acy - 3, spreadX: (ax1 - ax0)*.5,
       dir: -PI/2 - .29, dirSpread: .5, speed: 90, speedSpan: 110, gravity: 240, color: white, endColor: gold, k: 1.3, fadeOut: 1, style: 2, trail: .025)
  emit(9, salt: 191, birth: SPLIT - .06, birthSpan: .22, life: .35, lifeSpan: .3, x: acx, y: acy + 3, spreadX: (ax1 - ax0)*.5,
       dir: PI/2 - .29, dirSpread: .5, speed: 90, speedSpan: 110, gravity: 240, color: accent, endColor: gold, k: 1.3, fadeOut: 1, style: 2, trail: .025)
}

# Sustain: the seam cools beside the digits with a shimmer, two glint
# sweeps run along it, and embers drop off its ends.
frame {
  exit !sustain
  cool = smooth((t - shut)/1.3)
  seamK = (1 - .5*smooth((t - shut)/2.6))*endFade
  seam = mix(white, accent, .15 + .85*cool)
  stroke(x0: 2, x1: W - 3, y: cutY0 - 2*SLOPE, slope: -SLOPE, color: seam, k: seamK*.8, shimmer: .44, shimmerFreq: .09, shimmerPhase: -t*3.1, width: 1)
  g1 = t >= 2.3 && t < 2.8 ? -10 + (W + 20)*easeout((t - 2.3)/.5) : -1000
  g2 = t >= 3.35 && t < 3.85 ? -10 + (W + 20)*easeout((t - 3.35)/.5) : -1000
  g = max(g1, g2)
  stroke(x0: g - 26, x1: g, y: cutY0 - (g - 26)*SLOPE, slope: -SLOPE, color: seam, endColor: white, k: 0, endK: seamK*.7, width: 1)
  emit(22, salt: 96, birth: shut + .05, birthSpan: 2.5, life: 1.0, lifeSpan: .6, x: acx, y: acy, spreadX: W*.45, spreadY: 4,
       dir: PI/2, dirSpread: .6, speed: 8, speedSpan: 6, gravity: 124, color: white, endColor: gold, k: endFade*1.35, fadeIn: .4, fadeOut: .5, trail: .03)
  emit(5, salt: 100, birth: shut + .3, birthSpan: 2.9, life: .5, x: acx, y: acy, spreadX: W*.4, color: mix(white, accent, .25), k: endFade*1.15,
       fadeIn: .5, fadeOut: .5, style: 2)
}

# The sound (the built-in synth's Slash): a whoosh as the blade crosses, a
# bright inharmonic ring as it cuts, a low knock as the halves part with
# spark ticks, and a short click when they close. The noise voices share
# one seed, so they hear the same noise, as the synth's one generator.
voice whoosh = noise(seed: 0x27d4eb2f, filter: band, k2: .04)
voice ring1 = tone(attack: .003, decay: 7)
voice ring2 = tone(attack: .003, decay: 9)
voice ring3 = tone(attack: .003, decay: 12)
voice ring4 = tone(attack: .003, decay: 16)
voice knock = tone(decay: 22)
voice tick = noise(seed: 0x27d4eb2f, filter: low, k: .75, decay: 1100)
voice click = tone(decay: 90)
voice crack = noise(seed: 0x27d4eb2f, decay: 90)
note(ring1, at: SWING - .02, level: .34, hz: 1873)
note(ring2, at: SWING - .02, level: .26, hz: 2741)
note(ring3, at: SWING - .02, level: .18, hz: 3919)
note(ring4, at: SWING - .02, level: .12, hz: 5233)
note(knock, at: SPLIT, level: .55)
# Spark ticks: a chance in each 25 ms from the split to 0.7 s, at a random moment in it.
notes 28 {
  exit i < 7 || rnd(i, 6) >= .65
  note(tick, at: (i + rnd(i, 5))/40, level: .32)
}
note(click, at: 1.24, level: .24, hz: 3150)
note(crack, at: 1.24, level: .16)
sound {
  whoosh(k: .18 + .5*clamp01(t/.12), gain: .6*smooth(t/.1)*(1 - smooth((t - .1)/.1))*(t < .2))
  knock(hz: 260 - 90*clamp01((t - SPLIT)/.08))
}
