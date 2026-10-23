# Shockwave, ported from BestEffectUI::Renderer::shockwave (best_effect_ui.h).
# An impact flash, burst lines and two rings, then echo ripples, a pulse and dust.
effect "Shockwave"
uid 0x4c4c465800000004
revision 1
duration 4900
readout 600
sound 1300

const PI = 3.14159265

frame {
  glint(.06, .42)
  # The impact flash as two expanding ellipse strokes.
  fk = .9*(1 - t/.18)*(1 - t/.18)*(t < .18)
  fk = fk > .02 ? fk : 0
  rx1 = 20 + 90*t
  ry1 = 11 + 48*t
  ellipse(acx, acy, rx1, ry1, mix(accent, white, .4), fk, max(12, floor((rx1 + ry1)*2.2)), 0, 1)
  rx2 = 13 + 60*t
  ry2 = 7 + 32*t
  ellipse(acx, acy, rx2, ry2, white, fk*.7 > .02 ? fk*.7 : 0, max(12, floor((rx2 + ry2)*2.2)), 0, 1)
  endFade = 1 - smooth((t - 4.5)/.4)
}

repeat 8 {
  start = .05*rnd(i, 51)
  age = t - start
  exit age < 0 || age >= .26
  a = 2*PI*(i + .5*rnd(i, 52))/8
  ux = cos(a)
  uy = sin(a)
  u = age/.26
  r0 = 24 + 120*easeout(u)
  len = 22*(1 - u) + 4
  line(acx + ux*r0, acy + uy*r0*.75, acx + ux*(r0 + len), acy + uy*(r0 + len)*.75, mix(white, accent, u), (1 - u)*.95)
}

# Two main rings of three strokes each, squashed to the screen's perspective.
repeat 6 {
  ring = floor(i/3)
  band = i - ring*3
  u = (t - (ring == 0 ? 0 : .18))/(ring == 0 ? .95 : .85)
  exit u <= 0 || u >= 1
  r = 10 + 250*easeout(u)
  width = 3 + 5*u
  k = (ring == 0 ? 1 : .55)*(1 - u)*(1 - u*.2)*1.3
  rr = r - width*(band == 0 ? 0 : (band == 1 ? .3 : .65))
  exit k <= .02 || rr < 1
  # Stroke colours mixed in Q8 from white toward the accent, as the native ring does.
  mq = band == 1 ? 70 : 170
  c = color((7936 + floor((red(accent)*256 - 7936)*mq/256))/256, (16128 + floor((green(accent)*256 - 16128)*mq/256))/256, (7936 + floor((blue(accent)*256 - 7936)*mq/256))/256)
  ellipse(acx, acy, rr, rr, c, k*(band == 0 ? .45 : (band == 1 ? 1 : .6)), min(460, max(24, floor(r*3.8))), 0, .72)
}

# Three echo ripples of four strokes, each weaker and shorter.
repeat 12 {
  echo = floor(i/4)
  band = i - echo*4
  u = (t - (echo == 0 ? 1.1 : (echo == 1 ? 2.35 : 3.6)))/1.15
  exit u <= 0 || u >= 1
  r = 10 + (echo == 0 ? 120 : (echo == 1 ? 105 : 90))*easeout(u)
  width = 3 + 5*u
  k = (echo == 0 ? .7 : (echo == 1 ? .6 : .5))*endFade*(1 - u)*(1 - u*.2)*1.3
  rr = r - width*(band == 0 ? 0 : (band == 1 ? .22 : (band == 2 ? .45 : .7)))
  exit k <= .02 || rr < 1
  mq = band == 0 ? 170 : (band == 1 ? 60 : (band == 2 ? 120 : 190))
  bf = band == 0 ? .5 : (band == 1 ? 1 : (band == 2 ? .85 : .55))
  c = color((7936 + floor((red(accent)*256 - 7936)*mq/256))/256, (16128 + floor((green(accent)*256 - 16128)*mq/256))/256, (7936 + floor((blue(accent)*256 - 7936)*mq/256))/256)
  ellipse(acx, acy, rr, rr, c, k*bf, min(300, max(24, floor(r*2.6))), 0, .72)
}

# The last ring lingers around the readout as a slow pulse.
frame {
  env = smooth((t - .8)/.3)*(1 - smooth((t - 4.05)/.55))*(1 - .15*smooth((t - .8)/3.0))
  beat = sin((t - .8)*4.4)
  prx = (ax1 - ax0)*.5 + 9 + 1.5*beat
  pry = (ay1 - ay0)*.5 + 10 + beat
  pk = env*(.85 + .15*beat)*.95*(t > .8)*(env > .01)
  pk = pk > .02 ? pk : 0
  ellipse(acx, acy, prx, pry, mix(accent, white, .35), pk, max(12, floor((prx + pry)*2.2)), 0, 1)
}

# Dust knocked loose by the blast drifts outward and flickers as it settles.
repeat 20 {
  birth = .05 + 1.5*rnd(i, 53)
  life = 2.4 + 1.4*rnd(i, 54)
  age = t - birth
  exit age < 0 || age >= life
  a = 2*PI*rnd(i, 55)
  v = 16 + 34*rnd(i, 56)
  u = age/life
  ca = cos(a)
  sa = sin(a)
  d = v*age*(1 - .08*age)
  x = acx + ca*d
  y = acy + sa*d*.75 + 6*age
  k = endFade*sin(PI*u)*(.7 + .3*sin(t*17 + i*2.3))
  c = mix(white, accent, u)
  plot(x, y, c, k*1.25)
  plot(x - ca*2, y - sa*1.5, c, k*.55)
}
