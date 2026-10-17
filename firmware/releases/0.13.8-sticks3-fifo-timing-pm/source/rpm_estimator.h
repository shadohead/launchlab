#pragma once
#include <cstdint>
namespace RpmEstimator {
enum class Mode:uint8_t { ThreeTurn, SingleTurn };
static constexpr Mode DEFAULT_MODE=Mode::ThreeTurn;
inline bool valid(uint8_t value){return value<=uint8_t(Mode::SingleTurn);}
inline Mode next(Mode mode){return mode==Mode::ThreeTurn?Mode::SingleTurn:Mode::ThreeTurn;}
inline const char *name(Mode mode){return mode==Mode::ThreeTurn?"three_turn_peak":"single_turn_peak";}
inline const char *label(Mode mode){return mode==Mode::ThreeTurn?"3-turn average":"1-turn peak";}
inline float select(Mode mode,float three,float single){return mode==Mode::ThreeTurn?three:single;}
}
