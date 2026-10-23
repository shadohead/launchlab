# A hostile but valid file: as many instructions as the format allows a
# frame, big sparks all over the screen, a large glow and a full-width fire.
# It must be refused by the validator and, sent to a device anyway, be cut
# short by the engine's guard (docs/FIRMWARE_LOADABLE_EFFECTS.md).
effect "Worst"
uid 0x4c4c4658000000ff
duration 5000
readout 0
frame {
  fire(H - 10, 1.5, t)
  glow(acx, acy + 80, 80, 60, accent, 1)
}
repeat 512 {
  a = i*1.37 + t
  b = sin(a)*cos(a*.5) + sqrt(a*a + 1)
  c = rnd(i, 7) + noise(i*40, floor(t*90), 3)/255
  d = min(max(b, c), a)/(1 + c*c)
  e = exp(-d*.1)*easeout(c) + smooth(b)
  x = fract(e*7.3 + c)*W
  y = fract(d*3.1 + b)*H
  spark(x, y, mix(white, accent, c), 1, 3.5)
}
