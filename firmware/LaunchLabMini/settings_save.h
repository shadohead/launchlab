#pragma once
#include <cstdint>

// Applied settings take effect in RAM at once; flash follows inside the next
// ADC checkpoint the optical owner grants. A refused checkpoint (a fresh
// optical edge from the button press itself, a capture, an IMU transfer that
// will not yield) or a failed write leaves the item pending: it is retried on
// its own and written again inside the shutdown checkpoint. Applying a setting
// never depends on catching an idle instant.
class SettingsSave {
public:
  static constexpr uint32_t RETRY_MS=1000,FAILED_RETRY_MS=5000;
  enum Item:uint16_t {Sleep=1,Rpm=2,Brightness=4,Flip=8,Sensor=16,Appearance=32,Mount=64,Pin=128,ExtPower=256,Milestones=512};
  // Bit for a Controls::Setting row: Sleep, Rpm, Brightness, Flip, Sensor,
  // Theme, BestEffect, Celebrate, Sound, Mount. The three appearance rows
  // share a record; Celebrate is saved with the milestone table.
  static uint16_t item(unsigned row) {
    static constexpr uint16_t items[]={Sleep,Rpm,Brightness,Flip,Sensor,Appearance,Appearance,Milestones,Appearance,Mount};
    return row<sizeof(items)/sizeof(items[0])?items[row]:0;
  }
  // The owner rechecks everything itself. This only skips a request it would
  // certainly refuse, so a busy optical signal does not cost an IMU quiet
  // window each retry. Hold and Settling wait too: a checkpoint restarts
  // their quiet second, which would lengthen the blind time after a pull. A
  // sensor fault holds Hold indefinitely and no pull can be measured then.
  static bool ownerIdle(bool ready,bool suspended,bool opticalIdle,bool signalFault) {
    return ready && !suspended && (opticalIdle || signalFault);
  }
  void mark(uint16_t items){pending_|=items;}
  uint16_t pending()const{return pending_;}
  bool failed()const{return failed_;}
  bool due(uint32_t now)const {
    return pending_ && (!tried_ || uint32_t(now-lastTry_)>=(failed_?FAILED_RETRY_MS:RETRY_MS));
  }
  void attempted(uint32_t now){tried_=true;lastTry_=now;}
  void refused(){++refusals_;}
  // Called after writing inside a granted checkpoint. Once all is saved, the
  // next choice is written at once rather than after a retry interval.
  void wrote(uint16_t saved) {
    pending_&=uint16_t(~saved);failed_=pending_!=0;tried_=failed_;
    if(failed_)++errors_;else ++saves_;
  }
  uint32_t saves()const{return saves_;}
  uint32_t errors()const{return errors_;}
  uint32_t refusals()const{return refusals_;}
private:
  uint16_t pending_=0;
  uint32_t lastTry_=0,saves_=0,errors_=0,refusals_=0;
  bool tried_=false,failed_=false;
};
