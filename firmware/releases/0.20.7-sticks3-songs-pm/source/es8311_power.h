#pragma once
#include <cstdint>

// The StickS3's ES8311 codec, powered down once a sound has ended.
// M5Unified's speaker disable on StickS3 only clears the amplifier enable
// (PM1 GPIO3); its enable list powers up the analog section (0x0D=0x01), the
// DAC (0x12=0x00) and the output driver (0x13=0x10), and nothing powered them
// down again, so from the first sound until a reboot they stayed on. These
// writes undo that list, in M5Unified's microphone-disable order (analog
// section, then the state machine); Speaker.begin() rewrites the full list.
// The amplifier is already off, so the speaker cannot click.
namespace Es8311Power {
constexpr uint8_t ADDRESS=0x18;
constexpr uint32_t I2C_HZ=100000;  // M5Unified's own codec writes
struct Write {uint8_t reg,value;};
constexpr Write DOWN[]={
  {0x32,0x00},  // DAC volume to mute
  {0x13,0x00},  // output to the HP driver off
  {0x12,0x02},  // DAC powered down
  {0x0D,0xFC},  // analog circuitry powered down
  {0x00,0x00},  // codec state machine off
};
// Every write is attempted; true when all were acknowledged.
template<class Bus> bool down(Bus &bus) {
  bool ok=true;
  for(const auto &w:DOWN)ok=bus.writeRegister8(ADDRESS,w.reg,w.value,I2C_HZ) && ok;
  return ok;
}
}
