# Waterleaf (0.18, best_effect_aura.h, auraWaterleaf) in the engine's
# idiom: the water is one vessel call, the base and the dome arcs, the
# brim and rings ellipses, the leaf eight short segments round a midrib,
# each hand one stamp (rim and body weighted apart), the spray emitters.
# Look-alike, not exact.
effect "Waterleaf"
uid 0x4c4c46580000012f
revision 1
duration 5000
readout 1900
sound 2600

const PI = 3.14159265
const GLASS = .3
const HANDS = .25
const SWELL = .75
const SURGE = 1.25
const FADE = 3.8
const END = 4.9
const TOPW = 16
const BOTW = 13
const rim = rgb(214, 240, 255)
const water = rgb(96, 196, 255)
const deep = rgb(54, 140, 245)
const foam = rgb(225, 248, 255)
const leafLight = rgb(150, 230, 90)
const leafDark = rgb(40, 150, 60)

sprite hand ("......11......" "......11.11..." "...11.11.11..." "...11.11.11.11" "...11.11.11.11" "1..11.11.11.11" "11.11.11.11.11"
             ".11.1122122121" "..112222222221" "..122222222221" "..122222222221" "...1222222221." "....122222221." ".....1111111..")

frame {
  aura = mix(rgb(255, 236, 170), accent, .4)
  gt = ay1 + 12
  gb = ay1 + 45
  ys = gt + 1.5
  glassK = smooth(t/GLASS)*(1 - smooth((t - FADE - .3)/(END - FADE - .3)))
  handK = smooth((t - HANDS)/.3)*(1 - smooth((t - FADE)/.6))
  u = t - SURGE
  dome = ((u >= 0 ? 1.6 + 4.4*exp(-4*u)*cos(9*u)*min(1, u/.12) + (2.4 + .4*sin(t*3.1))*smooth(u/.4) : 1.6*smooth((t - SWELL)/(SURGE - SWELL)))
       *(1 - smooth((t - FADE)/.7)))
  lx = acx + 3*sin(t*1.3)
  ly = ys - dome + .5
  surge = u >= 0 && u < .4 ? 1 - u/.4 : 0
  endK = 1 - smooth((t - FADE)/(END - FADE - .2))
  glint(SURGE + .22, .42)
}

# The glass: the water, its walls and base with a highlight down the left
# wall, the brim (its back dim), and the surface or a dome of water over it.
frame {
  exit glassK <= .01
  vessel(acx, gt, gb, TOPW, BOTW, ys, water, deep, glassK, 3.5, floor(t*300))
  fline(acx - TOPW, gt, acx - BOTW, gb, rim, glassK, glassK)
  fline(acx + TOPW, gt, acx + BOTW, gb, rim, glassK*.75, glassK*.75)
  fline(acx - TOPW + 2.5, gt + 3, acx - BOTW + 2.2, gb - 4, foam, glassK*.5, glassK*.125)
  arc(acx, gb, BOTW, .8, rim, glassK)
  line(acx - BOTW, gb + 2, acx + BOTW, gb + 2, rim, glassK*.75)
  ellipse(acx, gt, TOPW, 1.8, rim, glassK, 40, .6, 1)
  arc(acx, ys, TOPW - 1, -dome, mix(water, foam, .5), glassK, 1)
  clip(ys - 1)
  disc(acx, ys, TOPW - 1, dome, water, glassK*.4*(dome > 1.2))
  clip(H - 1)
}

# Rings round the leaf as the aura stirs the water.
repeat 3 {
  exit glassK <= .01 || t <= .55 || t >= FADE + .4
  x = t - .55 + i*.23
  e = (x - .7*floor(x/.7))/.7
  rr = 3 + 11*e
  ellipse(lx, ly, rr, rr*.22, foam, .7*glassK*(1 - e)*(t < SURGE ? .9 : .5), 24, 0, 1)
}

# The leaf: a small green spindle on the surface, turning; it spins fast
# as the water surges, is tossed up off the dome and lands back.
frame {
  exit glassK <= .01
  turn = .8*t + 2.5*smooth((t - .6)/.6) + (u >= 0)*(4*(1 - exp(-3*u)) + .4*u)
  toss = u >= 0 && u < .45 ? 24*(u/.45)*(1 - u/.45) : 0
  ca = cos(turn)
  sa = sin(turn)*.35
  fx = lx
  fy = ly - 1 + (u >= 0 ? 1.2*sin(t*4.3)*smooth((u - .45)/.3) : 0) - toss
  fline(fx - 7*ca, fy - 7*sa, fx + 7*ca, fy + 7*sa, leafDark, glassK, glassK)
  plot(fx - 8*ca, fy - 8*sa, leafDark, glassK)
  plot(fx - 9*ca, fy - 9*sa - .5, leafDark, glassK*.7)
}
repeat 8 {
  exit glassK <= .01
  s = i < 4 ? -1 : 1
  a = table(i % 4, -6, -3, 0, 3)
  w0 = table(i % 4, .6367, 1.9592, 2.4, 1.9592)*s
  w1 = table(i % 4, 1.9592, 2.4, 1.9592, .6367)*s
  k = glassK*(s < 0 ? 1 : .8)
  fline(fx + ca*a - sa*w0*2.5, fy + sa*a - ca*w0*.6, fx + ca*(a + 3) - sa*w1*2.5, fy + sa*(a + 3) - ca*w1*.6, leafLight, k, k)
}

# The hands of aura either side, thumbs toward the glass, breathing, with
# wisps rising off them and light streaming from them into the water.
frame {
  breath = .75 + .25*sin(t*4.5)
  build = smooth((t - HANDS)/(SURGE - HANDS))
  build2 = smooth((t - .4)/.7)
}
repeat 2 {
  exit handK <= .01
  side = 2*i - 1
  hx = acx + side*(TOPW + 9)
  hy = gt + 6 + 1.5*sin(t*2 + side)
  stamp(hand, hx - 7, hy - 7, 1, i, mix(aura, white, surge), aura, aura, handK*(.75 + .35*breath + .4*surge),
        u > .6 ? 0 : handK*(.3 + .3*build*breath + .5*surge)*(u > 0 ? 1 - u/.6 : 1))
}
repeat 8 {
  exit handK <= .01
  side = i < 4 ? -1 : 1
  j = i % 4
  hx = acx + side*(TOPW + 9)
  hy = gt + 6 + 1.5*sin(t*2 + side)
  age = fract(t*1.1 + rnd(j, 571) + side)
  dot(hx + (rnd(j + side*9 + 9, 572) - .5)*12 + 1.5*sin(age*6 + j), hy - 6 - 12*age, aura, handK*(1 - age)*.8)
}
repeat 10 {
  exit handK <= .01
  side = i < 5 ? -1 : 1
  ph = fract(t*1.6 + (i % 5)*.2)
  hx = acx + side*(TOPW + 9)
  hy = gt + 6 + 1.5*sin(t*2 + side)
  dot(hx - side*(4 + (TOPW + 4)*ph), hy - 2 - 6*sin(ph*PI), mix(aura, foam, ph), handK*build*sin(PI*ph)*.9)
}

# The hands burn: aura licks up round each in short tongues, taller as it
# builds and flaring white at the surge.
repeat 2 {
  exit handK <= .01
  side = 2*i - 1
  rays(acx + side*(TOPW + 9), gt + 6 + 1.5*sin(t*2 + side), 9, 14, 1.5, 5, .4 + .6*build2 + .4*surge, .4, 1.2, 1.2,
       mix(aura, white, surge*.5), handK*(.7 + .3*build2), handK*.15, floor(t*900), 582, side*5000)
}

# The crack: a zigzag across the front of the glass, white at the surge,
# fading to a faint line that glints now and then.
repeat 5 {
  exit u < 0
  k = glassK*(u < .15 ? 1.3 : .45 + .25*max(0, sin(t*2.7)))
  fline(acx + table(i, -9, -4, -7, -1, -3), gt + table(i, 3, 8, 12, 17, 21), acx + table(i, -4, -7, -1, -3, 3), gt + table(i, 8, 12, 17, 21, 27),
        u < .15 ? white : foam, k, k)
}
frame {
  exit u < 0
  k = glassK*(u < .15 ? 1.3 : .45 + .25*max(0, sin(t*2.7)))
  fline(acx - 4, gt + 8, acx - 11, gt + 11, foam, k*.7, 0)
  fline(acx - 1, gt + 17, acx + 5, gt + 15, foam, k*.6, 0)
  # The surge: water sprays out of the dome to both sides, arcing down the
  # screen, sparkling.
  emit(8, salt: 573, birth: SURGE, birthSpan: .05, life: .55, lifeSpan: .3, x: acx + 6.5, y: ys - dome, spreadX: 6.5, dir: -.16*PI, dirSpread: .14*PI,
       speed: 60, speedSpan: 70, gravity: 380, color: mix(water, foam, .4), k: 1.7, fadeOut: 1, style: 1, trail: .025)
  emit(8, salt: 574, birth: SURGE, birthSpan: .05, life: .55, lifeSpan: .3, x: acx - 6.5, y: ys - dome, spreadX: 6.5, dir: -.84*PI, dirSpread: .14*PI,
       speed: 60, speedSpan: 70, gravity: 380, color: foam, k: 1.7, fadeOut: 1, style: 1)
  emit(8, salt: 575, birth: SURGE, birthSpan: .05, life: .55, lifeSpan: .3, x: acx, y: ys - dome, spreadX: 13, dir: -PI/2, dirSpread: .48*PI,
       speed: 60, speedSpan: 70, gravity: 380, color: foam, k: 1.4, fadeOut: 1, style: 2)
}

# Sustain: water wells over the brim and runs down both sides of the glass,
# then drips off its base and falls away down the screen.
repeat 10 {
  exit u < .2
  period = 1.1 + .6*rnd(i, 578)
  x = u - .2 + rnd(i, 579)*period
  age = x - period*floor(x/period)
  side = i % 2 ? 1 : -1
  k = endK*smooth(age/.08)*(1 - smooth((age - period + .15)/.15))
  sliding = age < .7528
  fall = age - .7528
  exit !sliding && fall > .28
  y = sliding ? gt + 60*age*age : gb + 2 + fall*(40 + 160*fall)
  x = acx + side*(sliding ? TOPW - 3*(y - gt)/33 + 1.6 : BOTW + 1.6 + 5*fall)
  dot(x, y, sliding || i % 3 ? water : foam, k*(sliding ? 1 : 1 - smooth((fall - .15)/.12)))
  dot(x, y - (sliding ? 1.5 : 2.5), sliding ? mix(water, foam, .5) : water, k*(sliding ? .6 : .4))
}

# Sparkles on the dome.
repeat 6 {
  exit u < 0
  a1 = t*.8 + rnd(i, 580)
  twinkle(acx + (rnd(i + floor(a1)*7, 581) - .5)*24, ys - dome*.7 - .5, foam, endK*sin(PI*fract(a1))*.9, 1)
}
