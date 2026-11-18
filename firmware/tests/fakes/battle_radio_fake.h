#pragma once
// Stand-in for battle_radio.h, for harnesses that extract the sketch's
// BattleFeed: the session and the record are the real ones, and the air is a
// list of frames a test fills and reads.
#include <cstring>
#include <vector>
#include "battle_session.h"

namespace BattleRadio {
struct Heard {uint8_t bytes[Battle::MAX_FRAME];uint8_t length=0;int8_t rssi=0;uint32_t at=0;};
inline std::vector<Heard> heard;
// What the sketch last asked of the radio.
inline bool radio=true,allowed=false,measuring=false;
inline unsigned ticks=0;
inline void reset(){heard.clear();radio=true;allowed=measuring=false;ticks=0;}
inline bool available(bool){return radio;}
inline void push(const uint8_t *frame,size_t n,int rssi,uint32_t now) {
  Heard h;std::memcpy(h.bytes,frame,n);h.length=uint8_t(n);h.rssi=int8_t(rssi);h.at=now;heard.push_back(h);
}
inline bool take(Heard &out){if(heard.empty())return false;out=heard.front();heard.erase(heard.begin());return true;}
inline void tick(uint32_t,bool on,bool pull,Battle::Session &,const Battle::Book &,const char *){allowed=on;measuring=pull;++ticks;}
inline void status(){}
}
