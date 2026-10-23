# Sparkles, ported from BestEffectUI::Renderer::sparkles (best_effect_ui.h).
# A confetti burst from the readout, twinkling stars and a slow rain of glitter.
effect "Sparkles"
uid 0x4c4c465800000003
revision 1
duration 5000
readout 700
sound 1600

const PI = 3.14159265
const DRAG = 1.6
const G = 170
const gold = rgb(255, 208, 72)

frame {
  glint(.12, .45)
  # Linear drag with gravity in closed form, for the particle and its two trail points.
  at0 = max(0, t)
  tr0 = (1 - exp(-DRAG*at0))/DRAG
  fa0 = G/DRAG*(tr0 - at0)
  at1 = max(0, t - .025)
  tr1 = (1 - exp(-DRAG*at1))/DRAG
  fa1 = G/DRAG*(tr1 - at1)
  at2 = max(0, t - .05)
  tr2 = (1 - exp(-DRAG*at2))/DRAG
  fa2 = G/DRAG*(tr2 - at2)
  lateFade = 1 - smooth((t - 4.35)/.55)
}

repeat 56 {
  life = 1.0 + .55*rnd(i, 31)
  exit t >= life
  a = 2*PI*rnd(i, 32)
  speed = 110 + 140*rnd(i, 33)
  ca = cos(a)
  sa = sin(a)
  x0 = acx + ca*(ax1 - ax0)*.42
  y0 = acy + sa*(ay1 - ay0)*.42
  vx = ca*speed
  vy = sa*speed*.8 - 55
  k = t < life*.55 ? 1 : 1 - (t - life*.55)/(life*.45)
  flicker = .7 + .3*sin(t*38 + i*2.4)
  m = i % 3
  c = m == 0 ? accent : (m == 1 ? white : gold)
  plot(x0 + vx*tr2, y0 + vy*tr2 + fa2, c, k*.4)
  dot(x0 + vx*tr1, y0 + vy*tr1 + fa1, c, k*.75)
  spark(x0 + vx*tr0, y0 + vy*tr0 + fa0, c, k*flicker*1.5, 2.1)
}

# Stars keep appearing sparsely through the sustain, each at its own moment.
repeat 26 {
  birth = .1 + 3.9*rnd(i, 41)
  age = t - birth
  life = .6 + .3*rnd(i, 46)
  exit age < 0 || age >= life
  header = i % 13 < 6
  side = i % 2
  x = header ? 6 + (W - 12)*rnd(i, 42) : (side ? 5 + 12*rnd(i, 43) : W - 6 - 12*rnd(i, 43))
  y = header ? (rnd(i, 44) < .5 ? ay0 - 15 + 6*rnd(i, 45) : ay1 + 5 + 12*rnd(i, 45)) : 60 + 150*rnd(i, 44)
  star(x, y, sin(PI*age/life)*lateFade, side ? gold : accent)
}

# Glitter flutters down around the readout and winks out where it lands.
repeat 18 {
  birth = .85 + 2.7*rnd(i, 47)
  life = 1.5 + .5*rnd(i, 48)
  age = t - birth
  exit age < 0 || age >= life
  u = age/life
  x = 6 + (W - 12)*rnd(i, 49) + 4*sin(age*5 + i*1.9)
  y = ay1 + 6 + 90*rnd(i, 50) + (16 + 12*rnd(i, 51))*age
  k = lateFade*sin(PI*u)*(.65 + .35*sin(t*31 + i*2.7))
  m = i % 3
  c = m == 0 ? accent : (m == 1 ? white : gold)
  plot(x, y, c, k*1.3)
  plot(x, y - 2, c, k*.5)
}
