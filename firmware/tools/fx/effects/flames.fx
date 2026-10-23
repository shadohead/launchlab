# Flames, ported from BestEffectUI::Renderer::flames (best_effect_ui.h).
# Fire rises behind the readout, settles to a lower burn and dies to embers.
effect "Flames"
uid 0x4c4c465800000002
revision 1
duration 5100
readout 900
sound 1800

frame {
  clip(ay1 + 31)
  grow = easeout(t/.32)
  level = t < 1.3 ? 1 : (t < 3.9 ? 1 - .35*smooth((t - 1.3)/1.2) : .65*(1 - smooth((t - 3.9)/1.05)))
  glint(.3, .5)
  base = min(bottom - 2, ay1 + 16)
  # The sustain scrolls a little slower, like a fire burning lower.
  fire(base, grow*level, t < 1.3 ? t : 1.3 + (t - 1.3)*.7)
  emberK = grow*(t < 4.35 ? 1 : 1 - smooth((t - 4.35)/.65))
}

# Embers rise for as long as the fire lives, each slot recycling on a new column.
repeat 18 {
  exit emberK <= 0
  life = .8 + .6*rnd(i, 22)
  clock = t + rnd(i, 25)*7.0
  cycle = floor(clock/life)
  age = clock - cycle*life
  exit rnd(i*13 + cycle, 26) > .25 + .75*emberK
  seed = i*29 + cycle*7
  x = 4 + (W - 8)*rnd(seed, 23) + 3*sin(age*9 + seed)
  y = base - 4 - age*(38 + 34*rnd(seed, 24))
  u = age/life
  c = mix(rgb(255, 214, 120), rgb(220, 60, 0), u)
  plot(x, y, c, (1 - u)*emberK)
  plot(x, y + 2, c, (1 - u)*emberK*.45)
}
