# Sunblade (0.18, best_effect_armory.h, armorySunblade) in the engine's
# idiom: the blade is lines, the fan's rim and the wave arc trails, its
# ribs fading lines (natively dotted), the wave's heat on the header one
# heatring, the settling dust ring emitters. Look-alike, not exact.
effect "Sunblade"
uid 0x4c4c46580000013c
revision 1
duration 5000
readout 1750
sound 2600

const PI = 3.14159265
const CHARGE = .85
const BACK = 1.0
const SWING = 1.13
const REST = 2.3
const FADE = 3.7
const A0 = -.1
const A1 = 1.32
const gold = rgb(255, 198, 64)
const pale = rgb(255, 240, 186)
const deep = rgb(232, 132, 26)

frame {
  py = min(bottom - 6, ay1 + 100)
  len = min(88, py - ay1 - 12)
  # The blade's angle from upright: still while charging, drawn back, swung
  # across fast, then (dimmed) brought back upright.
  u = (t - BACK)/(SWING - BACK)
  angle = t < CHARGE ? 0 : (t < BACK ? A0*smooth((t - CHARGE)/(BACK - CHARGE)) : (t < SWING ? A0 + (A1 - A0)*u*u*(3 - 2*u)
        : A1 - A1*smooth((t - SWING - .45)/(REST - SWING - .45))))
  grow = easeout(t/.7)
  charge = smooth(t/CHARGE)
  bladeK = ((t < CHARGE ? (.6 + .4*charge)*(.82 + .18*sin(t*(14 + 30*charge))) : (t < SWING + .08 ? 1 : .7 + .3*(1 - smooth((t - SWING - .08)/.35))))
         *(t > REST ? .85 + .15*sin((t - REST)*5) : 1)*(1 - smooth((t - FADE)/.7)))
  da = sin(angle)
  dc = -cos(angle)
  hx = min(15, ax0 + 10)
  hy = py
  reach = len*grow
  hot = t < CHARGE ? .35 + .5*charge : (t < SWING + .08 ? 1 : .3*(1 - smooth((t - SWING - .08)/.3)))
  glint(SWING + .18, .4)
}

# The blade: a bright core between gold rows, warm edges outside them and,
# while it is hot, a soft outer glow; the crossguard and grip.
frame {
  exit bladeK <= .02
  line(hx + da*3, hy + dc*3, hx + da*reach, hy + dc*reach, mix(gold, white, hot), bladeK)
  line(hx + da*7, hy + dc*7, hx - da*7, hy - dc*7, gold, bladeK)
  line(hx - dc*7 - da, hy + da*7 - dc, hx + dc*7 - da, hy - da*7 - dc, deep, bladeK*.75)
  line(hx - da*2, hy - dc*2, hx - da*7, hy - dc*7, deep, bladeK*.75)
  plot(hx - da*8.5, hy - dc*8.5, gold, bladeK)
}
repeat 2 {
  exit bladeK <= .02
  s = 2*i - 1
  nx = -dc*s
  ny = da*s
  line(hx + da*3 + nx, hy + dc*3 + ny, hx + da*(reach - 2) + nx, hy + dc*(reach - 2) + ny, mix(gold, pale, .3 + .7*hot), bladeK*.875)
  line(hx + da*4 + nx*2, hy + dc*4 + ny*2, hx + da*(reach - 6) + nx*2, hy + dc*(reach - 6) + ny*2, mix(deep, gold, .4 + .6*hot),
       bladeK*(.35 + .3*min(1, hot*2)))
  line(hx + da*6 + nx*3, hy + dc*6 + ny*3, hx + da*(reach - 14) + nx*3, hy + dc*(reach - 14) + ny*3, mix(deep, accent, .12),
       bladeK*hot*.35*(hot > .3 && t < SWING))
}

# The gathering: motes spiral in from all round onto the blade.
repeat 34 {
  exit t >= CHARGE + .1
  u = (t - .55*rnd(i, 441))/.42
  exit u < 0 || u >= 1
  along = reach*(.15 + .85*rnd(i, 444))
  ex = hx + da*along
  ey = hy + dc*along
  e = u*u
  sw = (1 - e)*1.4*(i % 2 ? 1 : -1)
  c = cos(sw)
  sn = sin(sw)
  rx = (4 + (W - 8)*rnd(i, 442) - ex)*(1 - e)
  ry = (ay1 + 8 + (py - ay1 - 8)*rnd(i, 443) - ey)*(1 - e)
  dot(ex + rx*c - ry*sn, ey + rx*sn + ry*c, i % 3 ? gold : pale, .95*sin(PI*u) + .2)
}

# The fan: ribs at the angles the blade has swept, the newest brightest, and
# a bright rim along the tip's path that fades more slowly.
frame {
  fanK = t < BACK ? 0 : 1 - smooth((t - SWING)/.4)
  swept = t < SWING ? angle : A1
  sweep = swept - A0
}
repeat 7 {
  exit fanK <= .02
  a = A0 + (A1 - A0)*i/6
  exit a > swept + .01
  k = fanK*(1 - .55*clamp01((swept - a)/1.6))*.95*(1 - smooth((t - SWING)/.14))
  exit k < .02
  ra = sin(a)
  rc = -cos(a)
  r0 = len - 30 + i % 2
  fline(hx + ra*r0, hy + rc*r0, hx + ra*(len + 2), hy + rc*(len + 2), i % 2 ? gold : pale, k*.5*(r0 - len + 34)/36, k*.5)
}
frame {
  exit fanK <= .02 || sweep <= 0
  trail(hx, hy, len + 5, len + 5, swept - PI/2, sweep, max(2, floor(sweep*(len + 5)*1.05)), pale, k: fanK, falloff: 0)
  trail(hx, hy, len + 4, len + 4, swept - PI/2, sweep, max(2, floor(sweep*(len + 4)*1.05)), gold, k: fanK*.8, falloff: 0)
  trail(hx, hy, len + 2, len + 2, swept - PI/2, sweep, max(2, floor(sweep*(len + 2)*1.05)), deep, k: fanK*.45*(t < SWING + .02), falloff: 0)
}

# The wave: released at the end of the swing, it races out along the fan and
# up through the header, heating the glyphs it crosses.
frame {
  wa = t - SWING + .03
  exit wa <= 0 || wa >= .45
  front = len + 6 + 300*wa
  k = 1 - smooth((wa - .25)/.2)
  wide = A1 - A0 + .24
  trail(hx, hy, front, front, A1 + .12 - PI/2, wide, min(512, floor(wide*front*1.05)), white, k: k, falloff: 0)
  trail(hx, hy, front - 2.5, front - 2.5, A1 + .12 - PI/2, wide, min(512, floor(wide*(front - 2.5)*1.05)), gold,
        k: .8*k*(1 - smooth((front - len - 6)/14)), falloff: 0)
  heatring(hx, hy, front - 3.5, front + .5, pale, .85*k)
}

# Gold dust settles through the fan; motes rise off the resting blade.
frame {
  dustK = smooth((t - SWING)/.3)*(1 - smooth((t - 4.1)/.7))
  exit dustK <= 0
  emit(12, salt: 445, birth: SWING + .05, birthSpan: 2.6, life: 1.3, lifeSpan: .8, x: hx, y: hy, spreadX: 36, spreadY: 36, dir: (A0 + A1)/2 - PI/2,
       dirSpread: (A1 - A0)/2, gravity: 15, swayAmp: 3, swayFreq: 2.5, color: gold, k: dustK*1.15, fadeIn: .5, fadeOut: .5, flicker: .4, ring: 1)
  emit(11, salt: 446, birth: SWING + .05, birthSpan: 2.6, life: 1.3, lifeSpan: .8, x: hx, y: hy, spreadX: 56, spreadY: 56, dir: (A0 + A1)/2 - PI/2,
       dirSpread: (A1 - A0)/2, gravity: 15, swayAmp: 3, swayFreq: 2.5, color: pale, k: dustK*1.15, fadeIn: .5, fadeOut: .5, flicker: .4, ring: 1)
  emit(11, salt: 447, birth: SWING + .05, birthSpan: 2.6, life: 1.3, lifeSpan: .8, x: hx, y: hy, spreadX: 76, spreadY: 76, dir: (A0 + A1)/2 - PI/2,
       dirSpread: (A1 - A0)/2, gravity: 15, swayAmp: 3, swayFreq: 2.5, color: gold, k: dustK*1.15, fadeIn: .5, fadeOut: .5, flicker: .4, ring: 1)
}
repeat 8 {
  exit t <= REST - .3 || bladeK <= .02
  clock = t + rnd(i, 450)
  u = fract(clock)
  along = len*(.2 + .75*rnd(i*7 + floor(clock), 451))
  s = i % 2 ? 1 : -1
  plot(hx + da*along + 3*s + 4*u*s, hy + dc*along - 10*u, pale, bladeK*1.2*sin(PI*u))
}
