#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "appearance.h"
#include "palette.h"

// The user's custom themes: CUSTOM_THEMES slots, selected as theme IDs
// THEMES.. in the appearance record. Pure data: validation, the stored image
// and the USB request. A slot that is empty, malformed or fails validation
// shows the built-in default instead; its stored bytes are kept as they are,
// so a newer firmware's slot survives a visit to an older one.
namespace Themes {
constexpr unsigned SLOTS=Appearance::CUSTOM_THEMES,SLOT_BYTES=Palette::Custom::BYTES;
static_assert(SLOT_BYTES==32,"one slot is a 32-byte record");
constexpr Appearance::Theme FALLBACK=Appearance::Theme::Classic;
inline Appearance::Theme slotTheme(unsigned slot){return Appearance::Theme(Appearance::THEMES+slot);}
inline unsigned themeSlot(Appearance::Theme t){return unsigned(t)-Appearance::THEMES;}
inline Palette::Colors builtIn(Appearance::Theme t){return Palette::builtIn(Appearance::accent(t),t==Appearance::Theme::Classic);}

enum class SlotState:uint8_t {Empty,Valid,Invalid};
inline const char *stateName(SlotState s){return s==SlotState::Valid?"valid":s==SlotState::Invalid?"invalid":"empty";}

class Library {
public:
  // magic, revision, the slots, FNV-1a over everything before it.
  static constexpr uint32_t MAGIC=0x01544c4c; // "LLT\1"
  static constexpr unsigned IMAGE_BYTES=8+SLOTS*SLOT_BYTES+4;
  using Image=std::array<uint8_t,IMAGE_BYTES>;
  uint32_t revision=0;
  uint32_t generation()const{return revision;}

  SlotState state(unsigned slot)const{return slot<SLOTS?state_[slot]:SlotState::Empty;}
  // Bit i set: slot i holds a valid theme.
  uint8_t validMask()const{uint8_t m=0;for(unsigned i=0;i<SLOTS;++i)if(state_[i]==SlotState::Valid)m|=1u<<i;return m;}
  bool get(unsigned slot,Palette::Custom &out)const{if(state(slot)!=SlotState::Valid)return false;out=custom_[slot];return true;}
  const uint8_t *raw(unsigned slot)const{return raw_[slot];}

  // A theme that can be shown: any built-in, or a custom slot holding a valid theme.
  bool available(Appearance::Theme t)const {
    return unsigned(t)<Appearance::THEMES || (Appearance::custom(t) && state(themeSlot(t))==SlotState::Valid);
  }
  // What a selection shows: itself when available, otherwise the default.
  Appearance::Theme effective(Appearance::Theme t)const{return available(t)?t:FALLBACK;}
  Palette::Colors colors(Appearance::Theme t)const {
    t=effective(t);return Appearance::custom(t)?custom_[themeSlot(t)].colors():builtIn(t);
  }
  const char *label(Appearance::Theme t)const {
    t=effective(t);return Appearance::custom(t)?custom_[themeSlot(t)].name:Appearance::label(t);
  }
  // Settings order: the built-ins, then each valid custom slot.
  Appearance::Theme next(Appearance::Theme t)const {
    unsigned id=unsigned(effective(t));
    do id=(id+1)%Appearance::ALL_THEMES;while(!available(Appearance::Theme(id)));
    return Appearance::Theme(id);
  }

  bool put(unsigned slot,const Palette::Custom &theme) {
    if(slot>=SLOTS || !theme.valid())return false;
    uint8_t bytes[SLOT_BYTES];theme.encode(bytes);
    return set(slot,bytes);
  }
  bool remove(unsigned slot) {
    if(slot>=SLOTS)return false;
    const uint8_t empty[SLOT_BYTES]={};return set(slot,empty);
  }
  void encode(Image &b)const {
    b.fill(0);Appearance::put32(b.data(),MAGIC);Appearance::put32(b.data()+4,revision);
    for(unsigned i=0;i<SLOTS;++i)std::memcpy(b.data()+8+i*SLOT_BYTES,raw_[i],SLOT_BYTES);
    Appearance::put32(b.data()+IMAGE_BYTES-4,Appearance::hash(b.data(),IMAGE_BYTES-4));
  }
  // The image as a whole must be intact; each slot is then judged on its own.
  bool decode(const uint8_t *b,size_t n) {
    if(n!=IMAGE_BYTES || Appearance::get32(b)!=MAGIC || Appearance::get32(b+IMAGE_BYTES-4)!=Appearance::hash(b,IMAGE_BYTES-4))return false;
    revision=Appearance::get32(b+4);
    for(unsigned i=0;i<SLOTS;++i){std::memcpy(raw_[i],b+8+i*SLOT_BYTES,SLOT_BYTES);judge(i);}
    return true;
  }
  bool operator==(const Library &o)const{return revision==o.revision && std::memcmp(raw_,o.raw_,sizeof(raw_))==0;}
  bool sameSlots(const Library &o)const{return std::memcmp(raw_,o.raw_,sizeof(raw_))==0;}
  bool empty()const{for(unsigned i=0;i<SLOTS;++i)if(state_[i]!=SlotState::Empty)return false;return true;}

private:
  uint8_t raw_[SLOTS][SLOT_BYTES]={};
  Palette::Custom custom_[SLOTS]={};
  SlotState state_[SLOTS]={};
  bool set(unsigned slot,const uint8_t *bytes) {
    if(std::memcmp(raw_[slot],bytes,SLOT_BYTES)){std::memcpy(raw_[slot],bytes,SLOT_BYTES);judge(slot);++revision;}
    return true;
  }
  // Empty is all zero; anything else must decode and pass validation.
  void judge(unsigned slot) {
    bool zero=true;for(unsigned i=0;i<SLOT_BYTES;++i)zero=zero && !raw_[slot][i];
    custom_[slot]={};
    if(zero){state_[slot]=SlotState::Empty;return;}
    Palette::Custom c;
    state_[slot]=c.decode(raw_[slot]) && c.valid()?SlotState::Valid:SlotState::Invalid;
    if(state_[slot]==SlotState::Valid)custom_[slot]=c;
  }
};

// One USB theme request (the `T` transfer): magic, op, argument, two zero
// bytes, a slot record for Put (zeros otherwise) and FNV-1a over the rest.
struct Request {
  static constexpr uint32_t MAGIC=0x01514c4c; // "LLQ\1"
  static constexpr unsigned BYTES=8+SLOT_BYTES+4;
  enum class Op:uint8_t {Put=1,Delete=2,Select=3,Clear=4};
  Op op=Op::Put;
  uint8_t argument=0; // Put/Delete: slot; Select: theme ID; Clear: 0
  Palette::Custom theme;
  static const char *name(Op op) {
    switch(op){case Op::Put:return "put";case Op::Delete:return "delete";case Op::Select:return "select";case Op::Clear:return "clear";}
    return "unknown";
  }
  // Shape only: the slot, ID and theme are judged by whoever applies it.
  bool decode(const uint8_t *b,size_t n) {
    if(n!=BYTES || Appearance::get32(b)!=MAGIC || Appearance::get32(b+BYTES-4)!=Appearance::hash(b,BYTES-4) || b[6] || b[7])return false;
    if(b[4]<uint8_t(Op::Put) || b[4]>uint8_t(Op::Clear))return false;
    op=Op(b[4]);argument=b[5];theme={};
    if(op==Op::Put)return theme.decode(b+8);
    for(unsigned i=8;i<8+SLOT_BYTES;++i)if(b[i])return false;
    return op!=Op::Clear || !argument;
  }
  void encode(uint8_t *b)const {
    std::memset(b,0,BYTES);Appearance::put32(b,MAGIC);b[4]=uint8_t(op);b[5]=argument;
    if(op==Op::Put)theme.encode(b+8);
    Appearance::put32(b+BYTES-4,Appearance::hash(b,BYTES-4));
  }
};
}
