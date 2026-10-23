# Overdrive (best_effect_ninja.h, ninjaGates) in the engine's idiom: the
# aura shells are one aura call (the halo mask dilated a word at a time, as
# natively), the pulses ellipses, motes and steam emitters, the pebbles a
# small sprite each. Look-alike, not exact.
effect "Overdrive"
uid 0x4c4c46580000011f
revision 1
duration 4800
readout 1500
sound 2400

const PI = 3.14159265
const BURST = 1.03
const SETTLE = 3.8
const END = 4.8
const mint = rgb(190, 255, 210)
const steam = rgb(200, 240, 215)
const stone = rgb(150, 135, 110)

sprite pebble ("23" "11" "11")

frame {
  glint(BURST, .45)
  green = mix(rgb(60, 255, 120), accent, .12)
  # Power: a step per beat, each eased in over 60 ms.
  power = .125*(smooth((t - .06)/.06) + smooth((t - .32)/.06) + smooth((t - .53)/.06) + smooth((t - .69)/.06)
        + smooth((t - .81)/.06) + smooth((t - .9)/.06) + smooth((t - .97)/.06) + smooth((t - 1.03)/.06))
  flare = t < BURST ? 0 : 1 - smooth((t - BURST)/.55)
  fade = 1 - smooth((t - SETTLE)/(END - .15 - SETTLE))
  # Sustain: the aura holds lower and breathes; three fainter beats follow.
  calm = ((t < BURST ? 1 : .7 + .3*(1 - smooth((t - BURST)/.7)) + .07*sin((t - BURST)*3.3))
       + .3*(t >= 1.95)*exp(-9*(t - 1.95))*smooth((t - 1.95)/.04)
       + .3*(t >= 2.8)*exp(-9*(t - 2.8))*smooth((t - 2.8)/.04)
       + .3*(t >= 3.55)*exp(-9*(t - 3.55))*smooth((t - 3.55)/.04))
  strength = (.25 + .75*power)*(1 + .6*flare)*calm*fade
  hw = (ax1 - ax0)*.5
  hh = (ay1 - ay0)*.5
}

# Each beat throws a pulse off the readout, wider as the power builds.
repeat 8 {
  last = i == 7
  age = t - table(i, .06, .32, .53, .69, .81, .9, .97, 1.03)
  life = last ? .45 : .26
  exit age < 0 || age >= life
  e = age/life
  grow = (last ? 20 : 8 + 1.5*i)*easeout(e)
  rx = hw + 6 + grow
  ry = hh + 6 + grow*.4
  ellipse(acx, acy, rx, ry, last ? mint : green, (1 - e)*(1 - e)*(last ? 1 : .7), max(12, floor((rx + ry)*1.1)), 0, 1)
}

# The calmer beats of the sustain.
repeat 3 {
  age = t - table(i, 1.95, 2.8, 3.55)
  exit age < 0 || age >= .3
  e = age/.3
  rx = hw + 6 + 10*easeout(e)
  ry = hh + 6 + 4*easeout(e)
  ellipse(acx, acy, rx, ry, green, .45*(1 - e)*(1 - e)*fade, max(12, floor(rx + ry)), 0, 1)
}

# The aura: shells grown out of the UI's own outline, bands climbing through them.
frame {
  exit strength <= .03
  shells = t > BURST + .6 ? 2 : min(3, 1 + floor(power*2.4 + flare*1.5))
  aura(shells, mix(mint, green, .25), green, min(1.5, strength), floor(t*700), 1, 1)
}

# Motes drawn up into the aura from below and above the readout.
frame {
  emit(80, salt: 612, birth: -.6, birthSpan: 5.2, life: .45, lifeSpan: .2, x: acx, y: ay0 - 3, spreadX: hw - 3, dir: -PI/2, dirSpread: 0,
       speed: 15, swayAmp: 2, swayFreq: 16, color: mint, endColor: green, k: strength*.9, fadeIn: .5, fadeOut: .5, style: 1)
  emit(160, salt: 613, birth: -.6, birthSpan: 5.2, life: .45, lifeSpan: .2, x: acx, y: ay1 + 13, spreadX: hw - 3, dir: -PI/2, dirSpread: 0,
       speed: 17, swayAmp: 2, swayFreq: 16, color: green, endColor: mint, k: strength*.9, fadeIn: .5, fadeOut: .5, style: 1)
}

# Pebbles lift off the floor as the power builds, are blasted out by the
# last beat and fall away.
repeat 10 {
  k = smooth((t - .03)/.2)*(1 - smooth((t - BURST - .15)/.4))
  exit k <= .02
  x0 = ax0 + 4 + (ax1 - ax0 - 8)*(i + .5*rnd(i, 604))/10
  y0 = ay1 + 15 + 3*rnd(i, 605)
  lift = 4 + 5*rnd(i, 606)
  a = max(0, t - BURST)
  burst = t >= BURST
  x = x0 + (burst ? (x0 - acx)*1.1*a : power*.7*sin(t*45 + i*2.3))
  y = y0 + (burst ? -lift - (40 + 50*rnd(i, 607))*a + 160*a*a : -power*lift)
  stamp(pebble, x, y - 1, 1, 0, stone, green, green, k, k*power, k*power*.6)
}

# Steam: a burst off the readout, then wisps venting through the sustain.
frame {
  exit t < BURST
  emit(11, salt: 608, birth: BURST, birthSpan: .12, life: .7, lifeSpan: .5, x: acx, y: ay0 - 3, spreadX: hw - 4, dir: -PI/2, dirSpread: .3,
       speed: 13, swayAmp: 5, swayFreq: 5, color: steam, k: .8*fade, fadeOut: 1, style: 1)
  emit(5, salt: 609, birth: BURST, birthSpan: .12, life: .7, lifeSpan: .5, x: acx, y: ay1 + 5, spreadX: hw - 4, dir: PI/2, dirSpread: .3,
       speed: 10, swayAmp: 5, swayFreq: 5, color: steam, k: .8*fade, fadeOut: 1, style: 1)
  emit(12, salt: 614, birth: BURST + .5, birthSpan: 2.4, life: .7, lifeSpan: .5, x: acx, y: ay0 - 3, spreadX: hw - 4, dir: -PI/2, dirSpread: .3,
       speed: 10, swayAmp: 5, swayFreq: 5, color: steam, k: .5*fade, fadeOut: 1, style: 1)
}
