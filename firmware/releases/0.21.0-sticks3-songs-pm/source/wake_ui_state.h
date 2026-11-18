#pragma once
#include <cstdint>

// One-shot record written only before automatic sleep, consumed at startup.
// Version and bounds checks reject corrupt or incompatible saved UI values.
namespace WakeUi {
static constexpr uint32_t MAGIC=0x4C570100;
struct State {bool tournament=false,history=false;uint8_t tournamentView=0,historyView=0;};
inline uint32_t encode(State state) {
  return MAGIC | uint32_t(state.tournament) | (uint32_t(state.tournamentView)<<1)
    | (uint32_t(state.history && !state.tournament)<<3) | (uint32_t(state.historyView)<<4);
}
inline bool decode(uint32_t value,uint8_t maximumHistoryView,State &out) {
  if((value&0xFFFFFF80)!=MAGIC)return false;
  State state;state.tournament=value&1;state.tournamentView=(value>>1)&3;
  state.history=(value>>3)&1;state.historyView=(value>>4)&7;
  if(state.tournamentView>1 || state.historyView>maximumHistoryView ||
    (state.tournament && state.history))return false;
  out=state;return true;
}
}
