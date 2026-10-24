# Ripcord: an original four-second fanfare for a celebration (LaunchLab, 0.20).
effect "Ripcord"
uid 0x4c4c4d5500000003
revision 1
duration 4000
readout 900

frame {
  glint(.08, .4)
  hw = (ax1 - ax0)*.45
  emit(24, salt: 97, life: .9, lifeSpan: .4, x: acx, y: acy - 4, spreadX: hw, spreadY: 4, dir: -1.5708, dirSpread: 1.4,
       speed: 150, speedSpan: 110, drag: 1.5, gravity: 190, color: accent, endColor: white, k: 1.5, fadeOut: .45, flicker: .2, style: 3, trail: .05)
}

voice brass = fm(ratio: 1, index: 1.4, attack: .02, decay: 1.2, gain: .3)
voice top = pulse(duty: .3, attack: .01, decay: 1.5, gain: .16)
voice low = tone(wave: triangle, attack: .005, decay: 1, gain: .5)
voice snare = drum(snare, gain: .45)
voice crash = drum(openhat, gain: .4)

song(tempo: 120, rows: 4) {
  pattern call {
    brass: "g4 . g4 g4 c5 . . . e5 . d5 . c5 . . ."
    top: "g5 . g5 g5 c6 . . . e6 . d6 . c6 . . ."
    low: "c2 . . . c2 . . . g1 . . . c2 . . ."
    drums: "snare . snare snare crash . . . snare . snare . crash . . ."
  }
  order call
}
