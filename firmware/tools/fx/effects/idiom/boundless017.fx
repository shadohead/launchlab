# Boundless as it was in 0.17 (best_effect_sorcery.h before #33's
# full-screen redesign, sorcBoundless) in the engine's idiom: the rim and
# the receding rings are arcs (the funnel's lower ellipse a column at a
# time, as natively), the streaming stars a dot and a fading line each, the
# overload bands heat lines. Look-alike, not exact. The redesign covers the
# whole screen (a negative of the UI), which loaded effects may not.
effect "Boundless 0.17"
uid 0x4c4c465800010127
revision 1
duration 5200
readout 2100
sound 2600

const PI = 3.14159265
const OPEN = .5
const SPREAD = .45
const CALM = 2.2
const CLOSE = 4.2
const SHUT = .5
const S0 = 150
const RIM = 178
const ice = rgb(205, 235, 255)
const blue = rgb(80, 165, 255)
const violet = rgb(180, 125, 255)
const pink = rgb(255, 140, 220)

frame {
  hw = (ax1 - ax0)*.5
  hh = (ay1 - ay0)*.5
  sx = acx
  sy = ay1 + 12
  # The boundary: how far out the void reaches (tunnel units). It closes
  # from the screen's foot, swallowing the void as it rises.
  rim = t < CLOSE ? RIM*easeout((t - OPEN)/SPREAD) : 152*(1 - smooth((t - CLOSE)/SHUT))
  glint(OPEN + .02, .45)
}

# The sign: two strokes cross and close to a narrow X, then flash.
repeat 2 {
  exit t >= OPEN + .12
  th = .95 - .75*easeout(t/OPEN)
  k = smooth(t/.1)*(t < OPEN ? 1 : 1 - (t - OPEN)/.12)
  ux = sin(th)*(2*i - 1)
  uy = -cos(th)
  segment(sx - ux*7, sy - uy*7, sx + ux*14, sy + uy*14, mix(blue, accent, .3), k*1.2)
}
frame {
  exit t <= OPEN - .06 || t >= OPEN + .12
  spark(sx, sy - 3, ice, 1.4*(1 - abs(t - OPEN)/.12), 2.6)
}

# The rim while it moves: a white front with an afterglow toward the readout.
frame {
  opening = t >= OPEN && t < OPEN + SPREAD
  exit !opening && (t < CLOSE || t >= CLOSE + SHUT)
  k = opening ? 1 - .35*(t - OPEN)/SPREAD : .85
  s = rim - 5*(opening ? 1 : -1)
  arc(acx, acy, hw + 5 + .7*rim, hh + 5 + 1.4*rim, white, k)
  arc(acx, acy, hw + 5 + .7*s, hh + 5 + 1.4*s, mix(blue, violet, .4), k*.5)
}

# Rings recede into the readout: one every 0.4 s, crossing in 1.2 s; in the
# calm one every 0.8 s, crossing in 2 s and fainter.
repeat 4 {
  exit t < OPEN
  span = (CALM - OPEN)/.4
  ph = (min(t, CALM) - OPEN)/.4 + max(0, t - CALM)/.8
  ring = floor(ph) - i
  exit ring < 0
  calm = ring >= span
  u = (t - (calm ? CALM + (ring - span)*.8 : OPEN + ring*.4))/(calm ? 2 : 1.2)
  exit u < 0 || u >= 1
  q = 1 - u
  s = S0*q*q*(.8 + .2*q)
  exit s > rim - 2
  arc(acx, acy, hw + 5 + .7*s, hh + 5 + 1.4*s, mix(table(ring % 4, ice, blue, violet, pink), white, .25 + .3*u),
      smooth(u/.12)*(1 - smooth((u - .8)/.2))*(.45 + .75*u)*(calm ? .6 : 1), calm)
}

# Starlight streams in along the funnel, each star with a streak behind it
# as long as it is fast; slower in the calm.
repeat 16 {
  exit t < OPEN
  travel = min(t, CALM) - OPEN + max(0, t - CALM)*.4
  life = .9 + .6*rnd(i, 341)
  phase = rnd(i, 342) + travel/life
  cycle = floor(phase)
  u = phase - cycle
  q = 1 - u
  s = S0*q*q*(.8 + .2*q)
  exit s > rim - 3
  k = smooth(u/.15)*(1 - smooth((u - .82)/.18))*(.6 + .6*u)
  exit k < .03
  seed = i*67 + cycle
  a = PI*(.1 + .8*rnd(seed, 343))
  ca = cos(a)
  sa = sin(a)
  x = acx + (hw + 5 + .7*s)*ca
  y = acy + (hh + 5 + 1.4*s)*sa
  c = table((i + cycle) % 4, mix(ice, white, .3), mix(blue, white, .3), mix(violet, white, .3), mix(pink, white, .3))
  ds = S0*(1.6 + .6*q)*q*(.04/life)*(t < CALM ? 1 : .4)
  ex = .7*ca*ds
  ey = 1.4*sa*ds
  e2 = ex*ex + ey*ey
  f = min(1, 7/max(.001, sqrt(e2)))
  fline(x + ex*f, y + ey*f, x, y, c, 0, k*.8*(e2 > 1.44))
  dot(x, y, mix(c, white, .4), k*1.2)
}

# Stars glitter round the header and the readout's flanks while the void is open.
repeat 6 {
  exit t < OPEN
  ty = table(i, 5, 3, 4, 8, 49, 51)
  k = smooth((rim - 14)/10)*smooth((t - OPEN - .05 - .04*i)/.15)*(.45 + .55*sin(t*(2.2 + 1.7*rnd(i, 344)) + i*2.3))
  exit k <= .05
  twinkle(table(i, 8, 27, 110, 126, 3, 131)*W/135, ty < 12 ? ty : ay1 + ty - 43,
          table(i, mix(ice, white, .4), mix(blue, white, .4), mix(violet, white, .4), mix(pink, white, .4), mix(ice, white, .4), mix(blue, white, .4)), k, 1)
}

# Overload: two bands of light race down through the digits once the rim
# is out, gone by 2.05 s.
repeat 2 {
  over = smooth((t - OPEN - .2)/.15)*(1 - smooth((t - 1.75)/.3))
  exit over <= 0
  p = t/.3 + i*.5
  yc = ay0 - 2 + floor((ay1 - ay0 + 5)*fract(p))
  c = i ? rgb(255, 200, 245) : white
  heatline(ax0, yc, ax1, yc, c, over*(yc >= ay0 && yc <= ay1))
  heatline(ax0, yc - 1, ax1, yc - 1, c, over*.6*(yc - 1 >= ay0 && yc - 1 <= ay1))
  heatline(ax0, yc + 1, ax1, yc + 1, c, over*.6*(yc + 1 >= ay0 && yc + 1 <= ay1))
}

# Closed: a last glimmer at the sign, and a few stars twinkle out.
frame {
  z = t - CLOSE - SHUT
  exit z <= -.05
  twinkle(sx, sy, ice, z < 0 ? (z + .05)/.05 : 1 - smooth(z/.3), 3)
}
repeat 8 {
  age = t - CLOSE - SHUT - .03*i
  exit age < 0 || age >= .32
  twinkle(6 + (W - 12)*rnd(i, 361), ay1 + 8 + 90*rnd(i, 362), i % 2 ? ice : violet, sin(PI*age/.32)*.8, 1)
}
