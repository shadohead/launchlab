# Orbit, ported from BestEffectUI::Renderer::orbit (best_effect_ui.h).
# Two comets spin up around the readout, lock with a ring flash, keep
# turning while the ring sheds dust motes, and run down.
effect "Orbit"
uid 0x4c4c465800000001
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
  # Orbit keeps its light within 21 rows under the readout.
  clip(ay1 + 21)
  # Angle: quadratic spin-up, a sustain whose rate sags, an exponential run-down.
  s = t/SPIN
  a1 = t - SPIN
  b = t - SETTLE
  fS = exp(-K1*(SETTLE - SPIN))
  angle = t < SPIN ? 2*PI*LAPS*s*s : (t < SETTLE ? 2*PI*LAPS + RATE*(1 - exp(-K1*a1))/K1 : 2*PI*LAPS + RATE*(1 - fS)/K1 + RATE*fS*(1 - exp(-K2*b))/K2)
  speedF = t < SPIN ? s : (t < SETTLE ? exp(-K1*a1) : fS*exp(-K2*b))
  angle = angle - PI/2
  speed = RATE*speedF
  fade = 1 - smooth((t - 4.15)/.75)
  live = fade > 0
  glint(SPIN - .05, .45)
  rx = W*.5 - 3
  # A faint track, lingering dimmer through the sustain.
  trackK = t < SPIN ? .2*smooth(t/.2) : .13*(1 - smooth((t - SPIN)/3.0) + .25)
  ellipse(acx, acy, rx, 23, accent, trackK*fade*live, 190, .45, 1)
}

# Two comets of up to 85 points, drawn from the tail to the head.
repeat 170 {
  comet = floor(i/85)
  j = i - comet*85
  tail = min(2.2, .35 + speed*.075)
  m = max(4, floor(tail*38))
  exit !live || j > m
  q = m - j
  u = q/m
  a = angle + comet*PI - tail*u
  ca = cos(a)
  sa = sin(a)
  x = acx + rx*ca
  y = acy + 23*sa
  k = fade*(.6 + .4*sa)*(1 - u)*(1 - u)
  c = mix(white, accent, min(1, u*2.5))
  plot(x, y, c, k*1.5)
  plot(x + ca*.9, y + sa*.9, c, k*.7*(q % 2 == 0))
}

repeat 2 {
  exit !live
  head = angle + i*PI
  twinkle(acx + rx*cos(head), acy + 23*sin(head), accent, fade*(1.1 + .3*sin(head)), 1)
}

# The lock: the ring flashes once and grows a little, throwing sparks.
frame {
  e = (t - SPIN)/.5
  lock = t >= SPIN && t < SPIN + .5
  scale = 1 + .06*easeout(e)
  ellipse(acx, acy, rx*scale, 23*scale, mix(white, accent, min(1, e*2.2)), (1 - e)*(1 - e)*lock, 190, .45, 1)
}

repeat 18 {
  age = t - SPIN
  life = .3 + .25*rnd(i, 3)
  exit !lock || age >= life
  a = i*2*PI/18 + rnd(i, 1)*.3
  v = 35 + 45*rnd(i, 2)
  px = acx + rx*cos(a)
  py = acy + 23*sin(a)
  d = v*easeout(age/life)*.5
  kk = 1 - age/life
  ux = cos(a)
  uy = sin(a)*.6
  plot(px + ux*d, py + uy*d, mix(white, accent, age/life), kk)
  plot(px + ux*(d - 2), py + uy*(d - 2), accent, kk*.5)
}

# Sustain: dust motes shed from the ring drift outward and wink out.
repeat 14 {
  birth = 1.3 + 2.3*rnd(i, 4)
  life = 1.1 + .8*rnd(i, 5)
  age = t - birth
  exit t < SPIN || age < 0 || age >= life
  a0 = 2*PI*rnd(i, 6)
  u = age/life
  sc = 1 + .1*easeout(u)
  an = a0 + .35*age
  px = acx + rx*sc*cos(an)
  py = acy + 23*sc*sin(an)
  k = fade*sin(PI*u)
  plot(px + cos(a0)*7*easeout(u), py + sin(a0)*3*easeout(u) - 3*u, mix(white, accent, u), k*1.1)
  plot(px, py, accent, k*.5)
}

# The sound, ported from BestEffectSound::detail::Orbit (best_effect_sound.h):
# a hum rising as the comets spin up, pulsing at each pass, then the lock's
# E-major chime (as in effects/idiom/orbit.fx).
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
