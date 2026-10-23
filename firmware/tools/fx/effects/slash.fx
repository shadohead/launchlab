# Slash, ported from BestEffectUI::Renderer::slash (best_effect_ui.h).
# A blade cuts the readout, the halves slide apart and fuse, and the seam
# glows beside the digits while it cools.
effect "Slash"
uid 0x4c4c465800000005
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
# The cut rises to the right through the readout's centre: tan = 0.3.
const SLOPE = .3
const COS = 245/256
const SIN = 73/256
const gold = rgb(255, 214, 110)

frame {
  shut = CLOSE + SHUT
  open = easeout((t - SPLIT)/OPEN)*(1 + .08*min(1, (t - SPLIT)/(CLOSE - SPLIT)))
  along = t >= SPLIT && t < shut ? 3.2*open*(t < CLOSE ? 1 : 1 - smooth((t - CLOSE)/SHUT)) : 0
  head = -24 + (W + 48)*easeout(t/SWING)
  # Hot edges glow behind the blade and cool; the seam flashes once as the halves meet.
  heat = t >= shut - .06 && t < shut + FUSE ? .55*(1 - smooth((t - shut + .06)/(FUSE + .06))) : (t < .32 ? 1 - smooth((t - .1)/.22) : 0)
  sustain = t > shut
  endFade = 1 - smooth((t - 4.0)/.5)
  sfade = t < SWING ? 1 : 1 - smooth((t - SWING)/.17)
  stail = t < SWING ? head - 70 : -24
}

frame {
  exit heat < 1/256 && along <= 0
  cut(along, min(3.2, along), heat, t < SWING ? round(head) : W)
}

# The blade's line across the readout itself, white behind the head.
frame {
  exit t >= .26
  blade(ax0 - 2, min(ax1 + 2, head), acx, acy, SLOPE, white, t < .16 ? 1 : 1 - smooth((t - .16)/.1))
}

# The streak: a white core with an accent glow along the cut, fading toward its tail.
repeat 192 {
  exit sfade <= 0
  x = max(0, floor(stail)) + i
  exit x > min(W - 1, floor(head))
  u = t < SWING ? (head - x)/70 : 0
  # Weights in Q8, as the native stroke computes them.
  kq = floor(sfade*(1 - u)*(1 - u)*(t < SWING ? 1 : .65)*256)
  y = round(acy - (x - acx)*SLOPE)
  ke = floor(kq*150/256)/256
  kh = floor(kq*50/256)/256
  plot(x, y, white, kq/256)
  plot(x, y - 1, accent, ke)
  plot(x, y + 1, accent, ke)
  plot(x, y - 2, mix(accent, white, .3), kh)
  plot(x, y + 2, mix(accent, white, .3), kh)
}

# Sparks fly off both edges of the cut while the halves open.
repeat 18 {
  birth = SPLIT - .06 + .22*rnd(i, 91)
  age = t - birth
  life = .35 + .3*rnd(i, 92)
  exit age < 0 || age >= life
  x0 = ax0 + (ax1 - ax0)*rnd(i, 93)
  y0 = acy - (x0 - acx)*SLOPE
  side = i % 2 ? 1 : -1
  speed = 70 + 110*rnd(i, 94)
  drift = 40 + 90*rnd(i, 95)
  vx = -side*SIN*speed + COS*drift
  vy = -side*COS*speed - SIN*drift
  x = x0 + vx*age
  y = y0 + vy*age + 120*age*age
  k = 1 - age/life
  m = i % 3
  c = m == 0 ? white : (m == 1 ? accent : gold)
  plot(x - vx*.025, y - vy*.025 - 6*age, c, k*.45)
  twinkle(x, y, c, k*1.3, 1)
}

# Sustain: the closed cut's seam glows beside the digits while it cools,
# with a shimmer along it and two brighter glint sweeps.
frame {
  cool = smooth((t - shut)/1.3)
  seamK = (1 - .5*smooth((t - shut)/2.6))*endFade
  seam = mix(white, accent, .15 + .85*cool)
  edge = mix(white, accent, .35 + .65*cool)
  head1 = t >= 2.3 && t < 2.8 ? -10 + (W + 20)*easeout((t - 2.3)/.5) : -1000
  head2 = t >= 3.35 && t < 3.85 ? -10 + (W + 20)*easeout((t - 3.35)/.5) : -1000
}

repeat 188 {
  exit !sustain || seamK <= .01
  x = 2 + i
  exit x >= W - 2
  y = round(acy - (x - acx)*SLOPE)
  d1 = head1 - x
  d2 = head2 - x
  near = min(d1, d2)
  far = max(d1, d2)
  boost = far >= 0 && far < 26 ? 1 + .9*(1 - far/26) : (near >= 0 && near < 26 ? 1 + .9*(1 - near/26) : 1)
  kq = floor(seamK*(.8 + .35*sin(x*.09 - t*3.1))*boost*256)
  ke = floor(kq*102/256)/256
  plot(x, y, seam, kq/256)
  plot(x, y - 1, edge, ke)
  plot(x, y + 1, edge, ke)
}

# Embers shed mostly off the seam's ends drop clear of the glyph band.
repeat 22 {
  exit !sustain
  birth = shut + .05 + 2.5*rnd(i, 96)
  life = 1.0 + .6*rnd(i, 97)
  age = t - birth
  exit age < 0 || age >= life
  m = i % 3
  x0 = m == 0 ? 4 + 22*rnd(i, 98) : (m == 1 ? W - 5 - 22*rnd(i, 98) : 8 + (W - 16)*rnd(i, 98))
  side = i % 2 ? 1 : -1
  u = age/life
  x = x0 + (rnd(i, 99)*8 - 4)*age
  y = acy - (x0 - acx)*SLOPE - side*COS*10*age + 62*age*age
  k = endFade*sin(PI*u)*(.75 + .35*sin(t*23 + i*2.9))
  c = m == 0 ? white : (m == 1 ? accent : gold)
  plot(x, y, c, k*1.35)
  plot(x, y - 2, c, k*.6)
}

repeat 5 {
  exit !sustain
  birth = shut + .3 + 2.9*rnd(i, 100)
  age = t - birth
  exit age < 0 || age >= .5
  x = 14 + (W - 28)*rnd(i, 101)
  twinkle(x, acy - (x - acx)*SLOPE, mix(white, accent, .25), endFade*sin(PI*age/.5)*1.15, 2)
}
