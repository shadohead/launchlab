# Victory Lap: an original chiptune for a new best (LaunchLab, 0.20).
# Pulse lead and harmony, triangle bass and the built-in kit at 140 bpm;
# the burst of confetti lands on the downbeats. One 5.1 s phrase: a rise,
# the lap and the finish, as long as its celebration (0.20.1).
effect "Victory Lap"
uid 0x4c4c4d5500000001
revision 2
duration 5000
readout 900

const gold = rgb(255, 208, 72)

frame {
  glint(.1, .45)
  hw = (ax1 - ax0)*.42
}
repeat 2 {
  c = i == 0 ? accent : gold
  emit(18, salt: 71 + i*9, birth: i*.43, life: 1.1, lifeSpan: .5, x: acx, y: acy - 6, spreadX: hw, spreadY: 6,
       dir: -1.5708, dirSpread: 1.2, speed: 130, speedSpan: 90, drag: 1.4, gravity: 160, color: c, k: 1.4,
       fadeOut: .5, flicker: .25, style: 3, trail: .04)
}

voice lead = pulse(duty: .25, attack: .003, decay: 4, gain: .3)
voice harmony = pulse(duty: .125, attack: .003, decay: 6, gain: .16)
voice bass = tone(wave: triangle, attack: .002, decay: 1.6, gain: .55)
voice kick = drum(kick, gain: .85)
voice snare = drum(snare, gain: .5)
voice hat = drum(hat, gain: .3)

song(tempo: 140, rows: 4) {
  pattern rise {
    lead: "c5 . e5 g5 . e5 c6 . b5 . g5 . a5 . g5 ."
    harmony: "e4 . . . e4 . . . d4 . . . f4 . . ."
    bass: "c2 . c3 . c2 . c3 . g1 . g2 . f1 . f2 ."
    beat: "kick . hat . snare . hat . kick . hat kick snare . hat ."
  }
  pattern lap {
    lead: "e5 . d5 c5 . d5 e5 . g5 . . e5 d5 . . ."
    harmony: "c4 . . . g3 . . . c4 . . . b3 . . ."
    bass: "a1 . a2 . a1 . a2 . f1 . f2 . g1 . g2 ."
    beat: "kick . hat . snare . hat kick . kick hat . snare . v6+hat hat"
  }
  pattern finish {
    lead: "c6 . . g5 . . e5 . c6 . . . . . off ."
    harmony: "e5 . . e4 . . c4 . g4 . . . . . off ."
    bass: "c2 . . c2 . . g1 . c2 . . . . . . ."
    beat: "kick . . kick . . snare . kick . snare snare snare . . ."
  }
  order rise lap finish
}
