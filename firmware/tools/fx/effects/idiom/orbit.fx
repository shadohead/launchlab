# Orbit in the engine's own idiom: the comets are arc trails and the lock
# sparks and dust are emitters, so a frame runs a handful of blocks instead
# of one run per comet point (effects/orbit.fx is the exact port).
effect "Orbit"
uid 0x4c4c465800000101
revision 1
duration 5000
readout 1500
sound 1700

const PI = 3.14159265
const SPIN = 1.05
const LAPS = 2.3
const SETTLE = 4.0
const RATE = 4*PI*LAPS/SPIN
const K1 = .22
const K2 = 1.2

frame {
  clip(ay1 + 21)
  s = t/SPIN
  a1 = t - SPIN
  b = t - SETTLE
  fS = exp(-K1*(SETTLE - SPIN))
  angle = t < SPIN ? 2*PI*LAPS*s*s : (t < SETTLE ? 2*PI*LAPS + RATE*(1 - exp(-K1*a1))/K1 : 2*PI*LAPS + RATE*(1 - fS)/K1 + RATE*fS*(1 - exp(-K2*b))/K2)
  speedF = t < SPIN ? s : (t < SETTLE ? exp(-K1*a1) : fS*exp(-K2*b))
  angle = angle - PI/2
  fade = 1 - smooth((t - 4.15)/.75)
  live = fade > 0
  glint(SPIN - .05, .45)
  rx = W*.5 - 3
  trackK = t < SPIN ? .2*smooth(t/.2) : .13*(1 - smooth((t - SPIN)/3.0) + .25)
  ellipse(acx, acy, rx, 23, accent, trackK*fade*live, 190, .45, 1)
  tail = min(2.2, .35 + RATE*speedF*.075)
}

# The two comets: white heads fading through the accent along the orbit.
repeat 2 {
  exit !live
  head = angle + i*PI
  trail(acx, acy, rx, 23, head, tail, max(4, floor(tail*38)) + 1, white, endColor: accent, k: fade*1.5, tilt: .4, edge: .47)
  twinkle(acx + rx*cos(head), acy + 23*sin(head), accent, fade*(1.1 + .3*sin(head)), 1)
}

# The lock: the ring flashes once and throws sparks outward.
frame {
  e = (t - SPIN)/.5
  lock = t >= SPIN && t < SPIN + .5
  scale = 1 + .06*easeout(e)
  ellipse(acx, acy, rx*scale, 23*scale, mix(white, accent, min(1, e*2.2)), (1 - e)*(1 - e)*lock, 190, .45, 1)
  emit(18, salt: 1, birth: SPIN, life: .3, lifeSpan: .25, x: acx, y: acy, spreadX: rx, spreadY: 23, dir: PI, dirSpread: PI,
       speed: 60, speedSpan: 70, drag: 6, color: white, endColor: accent, fadeOut: 1, trail: .03, ring: 1)
  # Sustain: dust motes shed from the ring drift outward and wink out.
  emit(14, salt: 4, birth: 1.3, birthSpan: 2.3, life: 1.1, lifeSpan: .8, x: acx, y: acy, spreadX: rx, spreadY: 23, dir: PI, dirSpread: PI,
       speed: 6, speedSpan: 4, gravity: -4, color: white, endColor: accent, k: fade*1.1, fadeIn: .5, fadeOut: .5, ring: 1)
}

# The sound (the built-in synth's Orbit): a hum rising with the spin and
# pulsing at each pass, then the lock's E-major chime.
voice hum = tone(cubic: .3)
voice chime[3] = tone(attack: .004, decay: 5.5)
note(chime[0], at: SPIN, level: .42, hz: 1318.5)
note(chime[1], at: SPIN, level: .3, hz: 1975.5)
note(chime[2], at: SPIN, level: .2, hz: 2637)
sound {
  s = min(t, SPIN)/SPIN
  pulse = .55 + .45*sin(2*PI*fract(2*LAPS*s*s))
  hum(hz: 240 + 1500*s*s, gain: .32*smooth(t/.12)*(1 - smooth((t - SPIN)/.06))*pulse*(t < SPIN + .06))
}
