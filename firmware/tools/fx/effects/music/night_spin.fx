# Night Spin: an original, calmer groove (LaunchLab, 0.20). FM bells over a
# drawn wavetable pad and a soft beat at 100 bpm, in A minor. Both phrases
# once and a cadence home to A minor: 6.0 s, as long as its celebration (0.20.1).
effect "Night Spin"
uid 0x4c4c4d5500000002
revision 2
duration 6000
readout 1000

const blue = rgb(96, 160, 255)

frame {
  glint(.15, .5)
  lateFade = 1 - smooth((t - 5.3)/.6)
  emit(16, salt: 83, birth: .1, birthSpan: 4.6, life: .9, lifeSpan: .4, x: W*.5, y: ay0 - 14, spreadX: W*.44, spreadY: 4,
       color: blue, endColor: accent, k: lateFade, fadeIn: .5, fadeOut: .5, style: 4)
  emit(10, salt: 89, birth: .3, birthSpan: 4.4, life: 1.6, lifeSpan: .5, x: W*.5, y: ay1 + 40, spreadX: W*.44, spreadY: 40,
       dir: 1.5708, dirSpread: .1, speed: 12, speedSpan: 8, swayAmp: 3, swayFreq: 4, color: accent, k: lateFade, fadeIn: .5, fadeOut: .5, flicker: .3)
}

voice bell = fm(ratio: 3.5, index: 1.8, attack: .002, decay: 3.5, gain: .26)
voice pad = wave(points: [0, .38, .7, .92, 1, .92, .7, .38, 0, -.2, -.5, -.6, -.5, -.2, 0, .1], attack: .08, decay: .7, gain: .2, filter: low, k: .2)
voice bass = tone(wave: sine, octave: .3, attack: .004, decay: 2.2, gain: .5)
voice kick = drum(kick, gain: .6)
voice hat = drum(hat, gain: .18)
voice rim = drum(rim, gain: .3)

song(tempo: 100, rows: 4) {
  pattern one {
    bell: "a5 . c6 . e6 . c6 . b5 . . . g5 . . ."
    pad: "a3 . . . . . . . g3 . . . . . . ."
    bass: "a1 . . . . . a2 . g1 . . . . . . ."
    beat: "kick . hat . rim . hat . kick . hat kick rim . hat ."
  }
  pattern two {
    bell: "f5 . a5 . c6 . a5 . e5 . g#5 . b5 . . ."
    pad: "f3 . . . . . . . e3 . . . . . . ."
    bass: "f1 . . . . . f2 . e1 . . . e2 . . ."
    beat: "kick . hat . rim . hat . kick . hat . rim . v10+hat hat"
  }
  pattern end {
    bell: "e6 . c6 . a5 . . off"
    pad: "a3 . . . . . . off"
    bass: "a1 . . . a2 . . off"
    beat: "kick . hat . kick . . ."
  }
  order one two end
}
