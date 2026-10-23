# Rupture, ported from BestEffectUI::Renderer::sorcRupture (best_effect_sorcery.h).
# Cursed energy crackles round the readout and ripples its rows, the hit
# inverts the digits to crimson and flashes them white, black lightning
# bursts out, and red energy smoulders along the readout's foot.
effect "Rupture"
uid 0x4c4c465800000006
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

# Distortion: a ripple band runs down the readout as the energy builds.
repeat 7 {
  exit t >= HIT
  amp = 1.8*smooth(t/HIT)
  yc = ay0 - 3 + (ay1 - ay0 + 6)*(t*(1/.3) - floor(t*(1/.3)))
  y = max(ay0, floor(yc) - 3) + i
  exit y > min(ay1, floor(yc) + 3)
  shiftrow(y, round(amp*sin((y - yc)*.9)))
}

# After the flash two stronger bands run out from the middle row.
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

# A last soft ripple.
repeat 3 {
  exit a < .75 || a >= 1.05
  yc = ay0 - 2 + (ay1 - ay0 + 4)*((a - .75)/.3)
  y = max(ay0, floor(yc) - 1) + i
  exit y > min(ay1, floor(yc) + 1)
  shiftrow(y, y == floor(yc))
}

# The hit: an inverted frame (deep crimson digits), then a white flash, then a glint.
frame {
  shade(rgb(205, 8, 22), .93*(a >= 0 && a < .05), 0)
  shade(hot, .85*(a >= .05 && a < .1), 1)
  glint(HIT + .12, .4)
  charge = mix(accent, red, s*s)
  kq = floor((.25 + .55*s)*256)
}

# Charge: an aura flickers along the readout's rim, the accent turning red.
frame {
  exit t >= HIT
  prime(ay0 - 4, ay0 - 3)
  prime(ay1 + 3, ay1 + 4)
}

repeat 192 {
  exit t >= HIT
  x = max(0, ax0 - 2) + i
  exit x > min(W - 1, ax1 + 2)
  n1 = noise(x*40, frame*90, 331)
  n2 = noise(x*40 + 9000, frame*90, 332)
  plot(x, ay0 - 3, charge, (n1 > 150)*floor(kq*(n1 - 120)/128)/256)
  plot(x, ay1 + 3, charge, (n2 > 150)*floor(kq*(n2 - 120)/128)/256)
}

# Arcs crackle off the rim, more as the hit nears.
repeat 10 {
  exit t >= HIT
  exit i >= 1 + floor(9*s*s)
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

# The split second before contact: a ring snaps in onto the readout.
frame {
  exit t >= HIT || t <= HIT - .16
  u = (t - HIT + .16)/.16
  sc = 1 + 1.5*(1 - u)*(1 - u)
  ellipse(acx, acy, (hw + 6)*sc, (hh + 6)*sc, mix(red, white, u*.6), floor((.35 + .65*u)*256)/256, 160, 0, 1)
}

# The blaze round the inverted digits: red bands hugging the readout.
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

# Black lightning bursts out of the readout, re-struck every frame; then
# only a stray, weaker bolt now and then.
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

# Smoulder: red energy crawls along the readout's foot through the sustain.
frame {
  smoulder = smooth((a - .3)/.4)*(1 - smooth((a - 2.6)/.8))
  exit smoulder <= 0
  prime(ay0 - 3, ay0 - 3)
  prime(ay1 + 3, ay1 + 4)
  skq = floor(.8*smoulder*256)
  drift = floor(t*700)
  warm = mix(red, hot, .25)
}

repeat 192 {
  exit smoulder <= 0
  x = max(0, ax0 - 2) + i
  exit x > min(W - 1, ax1 + 2)
  nb = noise(x*45 + drift, floor(t*260), 333)
  m = noise(x*45 - drift + 5000, floor(t*230), 334)
  k = floor(skq*(nb - 100)/128)
  plot(x, ay1 + 3, nb > 200 ? warm : red, (nb > 120)*k/256)
  plot(x, ay1 + 4, blood, (nb > 120)*floor(k/2)/256)
  plot(x, ay0 - 3, red, (m > 150)*floor(skq*(m - 130)/256)/256)
}

# Embers drift off the rim and cool, from the hit to well into the sustain.
repeat 44 {
  age = a - .1 - 2.8*rnd(i, 331)
  life = .8 + .9*rnd(i, 332)
  exit age < 0 || age >= life
  top = i % 4 == 0
  x0 = ax0 + (ax1 - ax0)*rnd(i, 333)
  y0 = top ? ay0 - 3 : ay1 + 3
  vx = (rnd(i, 334) - .5)*50
  vy = top ? -10 - 12*rnd(i, 335) : 10 + 38*rnd(i, 335)
  x = x0 + vx*age + 2.5*sin(age*6 + i)
  y = y0 + vy*age - 12*age*age
  v = age/life
  k = (1 - v)*(1 - smooth((t - END + .7)/.5))*(.75 + .25*sin(age*25 + i))
  c = mix(mix(hot, red, min(1, v*2)), blood, smooth((v - .5)/.5))
  dot(x, y, c, k*1.3)
  plot(x - vx*.03, y - (vy - 24*age)*.03, c, k*.5)
}
