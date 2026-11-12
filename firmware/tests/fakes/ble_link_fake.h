#pragma once
// Stand-in for ble_link.h and the sketch's BleCommands, for harnesses that
// extract production functions calling them. A test sets `linkUp` for a phone.
#include <cstddef>
#include <cstdint>

namespace BleLink {
using Handler=int32_t(*)(uint8_t op,const uint8_t *args,size_t length,uint8_t *out,size_t capacity);
inline Handler handler=nullptr;
inline bool linkUp=false;
inline bool connected(){return linkUp;}
inline bool tick(uint32_t,bool,bool){return false;}
inline bool comparison(uint32_t &,bool &){return false;}
inline bool replyFlushed(uint32_t){return true;}
inline void answerComparison(bool){}
inline void setPromptBusy(bool){}
inline int bondCount(){return 0;}
inline bool takeBondsChanged(){return false;}
inline void forget(){}
inline void *bondPrefs=nullptr;
inline const char *fullName(){return "LaunchLab-TEST";}
template<class Prefs>inline bool saveBonds(Prefs &){return true;}
template<class Prefs>inline bool saveName(Prefs &){return true;}
template<class Prefs>inline void loadName(Prefs &){}
inline void publishLaunch(uint32_t,uint32_t,float){}
inline void status(){}
template<class Canvas>inline void drawIcon(Canvas &,uint16_t,uint16_t,uint16_t,uint16_t){}
}
namespace BattlePass {inline constexpr char NAME[]="BEYBLADE_TOOL01";}
namespace BattlePassFeed {inline void tick(uint32_t){}inline void launched(){}}
namespace BleCommands {
inline int32_t run(uint8_t,const uint8_t *,size_t,uint8_t *,size_t){return -4;}
template<class State>inline bool answerPairing(bool,const State &){return false;}
template<class State>inline void tickLiveOrientation(uint32_t,const State &){}
}
