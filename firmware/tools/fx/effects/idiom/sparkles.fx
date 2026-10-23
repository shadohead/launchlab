# Sparkles in the engine's idiom: the confetti burst, the stars and the
# glitter rain are emitters (effects/sparkles.fx is the exact port).
effect "Sparkles"
uid 0x4c4c465800000103
revision 1
duration 5000
readout 700
sound 1600

const PI = 3.14159265
const gold = rgb(255, 208, 72)

frame {
  glint(.12, .45)
  lateFade = 1 - smooth((t - 4.35)/.55)
  hw = (ax1 - ax0)*.42
  hh = (ay1 - ay0)*.42
}

# The burst: three colours, each a ring of particles leaving the readout's
# rim with drag and gravity, sparks at the head and a short trail.
repeat 3 {
  c = i == 0 ? accent : (i == 1 ? white : gold)
  emit(19, salt: 31 + i*10, life: 1.0, lifeSpan: .55, x: acx, y: acy - 8, spreadX: hw, spreadY: hh, dir: 0, dirSpread: PI,
       speed: 110, speedSpan: 140, drag: 1.6, gravity: -170, color: c, k: 1.6, fadeOut: .45, flicker: .3, style: 3, trail: .05, ring: 1)
}

# Stars twinkle at seeded moments around the header and down the sides.
frame {
  emit(12, salt: 41, birth: .1, birthSpan: 3.9, life: .6, lifeSpan: .3, x: W*.5, y: ay0 - 12, spreadX: W*.45, spreadY: 3,
       color: accent, endColor: gold, k: lateFade, fadeIn: .5, fadeOut: .5, style: 4)
  emit(14, salt: 61, birth: .1, birthSpan: 3.9, life: .6, lifeSpan: .3, x: W*.5, y: 135, spreadX: W*.46, spreadY: 75,
       color: gold, endColor: accent, k: lateFade, fadeIn: .5, fadeOut: .5, style: 4)
}

# Glitter flutters down around the readout and winks out.
repeat 3 {
  c = i == 0 ? accent : (i == 1 ? white : gold)
  emit(6, salt: 47 + i*10, birth: .85, birthSpan: 2.7, life: 1.5, lifeSpan: .5, x: W*.5, y: ay1 + 51, spreadX: W*.45, spreadY: 45,
       dir: PI/2, dirSpread: .1, speed: 16, speedSpan: 12, swayAmp: 4, swayFreq: 5, color: c, k: lateFade*1.3, fadeIn: .5, fadeOut: .5, flicker: .35, trail: .1)
}

# The sound (the built-in synth's Sparkles): a burst of eight glassy pings
# in C major pentatonic, then eighteen sparse ones at the synth's own random
# moments; each rings 0.32 s, and at most fourteen ring at once.
voice ping[14] = tone(octave: .25, attack: .002, decay: 16, length: .32)
notes 26 {
  note(ping, at: i < 8 ? .012*i : .1 + 1.1*rnd(i, 1), level: i < 8 ? .2 : .16,
       hz: table(i, 2093, 2637, 2093, 3520, 3520, 4186, 3520, 2637, 2093, 3136, 3136, 2093, 3136,
                 3136, 2637, 2349.3, 3136, 3136, 4186, 3520, 4186, 2349.3, 4186, 2093, 3136, 3136))
}
