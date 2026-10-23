# Times the engine's instructions alone: 500 runs of a typical mix of 40
# instructions (arithmetic, comparisons, a select, table sine, rnd) and no
# draw that lights a pixel. Device time / instructions = the cost per
# instruction in the frame cost model.
effect "Opbench"
uid 0x4c4c4658000000fe
duration 5000
readout 0
repeat 500 {
  a = i*1.5 + t
  b = a*a - 3
  c = sin(a)*.5 + .5
  d = b < c ? a : b
  e = rnd(i, 11)*c + d*.25
  f = min(e, 4) + max(a, 1)
  g = f*f*.01 - e
  h = cos(g)*a + b*c
  k = h*.3 + g*.7 - f
  m = abs(k) + floor(a*.1)
  q = m*.5 + h*c - d
  exit q > 1e30
}
