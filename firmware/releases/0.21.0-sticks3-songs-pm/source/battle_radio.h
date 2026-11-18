#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <host/ble_hs.h>
#include "battle_session.h"
#include "ble_link.h"

// Battles (docs/FIRMWARE_BATTLES.md): the radio's side. A stick tells its
// launch frame as a non-connectable advertisement and hears other sticks'
// with a passive scan, on the stack the phone link already runs (ble_link.h).
// Nothing here connects, pairs or keeps anything: the session (battle_session.h)
// says when to tell and when to listen, and both end on their own.
//
// The stack has one advertisement. While a frame is told and no phone is
// connected, the frame and the stick's own advertisement (the one phones and
// Battle Pass apps find it by) take turns, SHARE_MS each, so a phone can find
// and connect to the stick in the middle of a battle. Once a phone is
// connected the stick advertises nothing of its own, and the frame has the
// air to itself. A connected phone stays connected throughout.
//
// The scan's callback runs on the NimBLE host task and only queues what
// arrived; the loop takes it from there.
namespace BattleRadio {
static constexpr unsigned QUEUE=8;
// A frame is told every 30 ms; it is rebuilt (its age moves) every 100 ms,
// and every fourth time the name goes out in its place.
static constexpr uint16_t BEACON_ITVL=48;                       // 0.625 ms units
static constexpr uint32_t REFRESH_MS=100,RETRY_MS=500;
// Each turn of the shared advertisement. Another stick listening half the
// time still hears several frames in every frame's turn.
static constexpr uint32_t SHARE_MS=200;
// The frame's turn, and the stick's own: from the clock, so it needs no state.
inline bool ownTurn(uint32_t now){return (now/SHARE_MS)%2==1;}
static constexpr unsigned NAME_EVERY=4;
// Listening for a launch made together: half the time. For a result, while a
// battle is open: a tenth, which still catches a frame told every 30 ms
// within a second.
static constexpr uint16_t DETECT_ITVL=160,DETECT_WINDOW=80,LISTEN_ITVL=640,LISTEN_WINDOW=64;

struct Heard {uint8_t bytes[Battle::MAX_FRAME];uint8_t length=0;int8_t rssi=0;uint32_t at=0;};
static Heard queue[QUEUE];
static volatile unsigned queued=0;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t heardFrames=0,droppedFrames=0;
// Telling (the session wants a frame out), and whether the frame is on the air now.
static bool beaconing=false,frameOnAir=false;
static Battle::Session::Scan scanning=Battle::Session::Scan::Off;
// What the serial log last showed of this telling: the launch frame, and whether the name.
static uint8_t toldLaunch[Battle::LAUNCH_BYTES];static bool toldAny=false,toldName=false;
static uint32_t refreshedAt=0,refreshes=0,beaconTriedAt=0,scanTriedAt=0,failures=0,beacons=0,scans=0,ownTurns=0;
static bool beaconTried=false,scanTried=false;

// A frame from the air (or the rig). A newer frame of the same kind from the
// same stick replaces the one still waiting, so a slow pass of the loop
// cannot crowd out another stick.
inline void push(const uint8_t *frame,size_t n,int rssi,uint32_t now) {
  if(n<6 || n>Battle::MAX_FRAME)return;
  portENTER_CRITICAL(&mux);
  Heard *slot=nullptr;
  for(unsigned i=0;i<queued;++i)if(queue[i].bytes[1]==frame[1] && !memcmp(queue[i].bytes+2,frame+2,4))slot=&queue[i];
  if(!slot && queued<QUEUE)slot=&queue[queued++];
  if(slot){memcpy(slot->bytes,frame,n);slot->length=uint8_t(n);slot->rssi=int8_t(std::max(-127,std::min(0,rssi)));slot->at=now;++heardFrames;}
  else ++droppedFrames;
  portEXIT_CRITICAL(&mux);
}
// The oldest frame waiting, for the loop.
inline bool take(Heard &out) {
  portENTER_CRITICAL(&mux);
  const bool any=queued>0;
  if(any){out=queue[0];--queued;for(unsigned i=0;i<queued;++i)queue[i]=queue[i+1];}
  portEXIT_CRITICAL(&mux);
  return any;
}
static int scanEvent(ble_gap_event *event,void *) {
  if(event->type!=BLE_GAP_EVENT_DISC)return 0;
  const uint8_t *frame=nullptr;size_t n=0;
  if(Battle::payload(event->disc.data,event->disc.length_data,frame,n))push(frame,n,event->disc.rssi,millis());
  return 0;
}

// The radio itself: started, and not the rig's tap.
inline bool onAir(){return BleLink::started && !BleLink::tapped() && ble_hs_synced();}
// Whether battles have a radio at all: the stack is up, or the rig stands in for it.
inline bool available(bool bluetoothAtBoot){return (bluetoothAtBoot && BleLink::started) || BleLink::tapped();}

// Takes the frame off the air. The library sets its own advertisement's data
// once: have it set them again, since they were this frame until now.
static void dropFrame() {
  if(!frameOnAir)return;
  frameOnAir=false;BleLink::battleBeacon=false;
  if(onAir()) {
    if(ble_gap_adv_active())ble_gap_adv_stop();
    BLEDevice::getAdvertising()->setScanResponse(true);
  }
}
static void stopBeacon() {
  if(!beaconing)return;
  beaconing=false;toldAny=toldName=false;
  dropFrame();  // the link restarts the stick's own advertisement
  Serial.println("BATTLE_RADIO beacon=0");
}
// The stick's own turn while telling: its advertisement, for a phone to find.
static void showOwn() {
  dropFrame();
  if(!ble_gap_adv_active() && BLEDevice::getAdvertising()->start())++ownTurns;
}
static void stopScan() {
  if(scanning==Battle::Session::Scan::Off)return;
  scanning=Battle::Session::Scan::Off;
  if(onAir() && ble_gap_disc_active())ble_gap_disc_cancel();
  Serial.println("BATTLE_RADIO scan=off");
}
// The frame as it is told. True when the radio has it (or the rig's tap).
static bool tell(const uint8_t *frame,size_t n,bool starting) {
  uint8_t data[Battle::ADVERTISEMENT_MAX];
  const size_t length=Battle::advertisement(frame,n,data);
  if(!length)return false;
  if(!onAir())return BleLink::tapped();
  if(!starting && ble_gap_adv_active() && !ble_gap_adv_set_data(data,int(length)))return true;
  // Starting, or the stack would not change the data of a running advertisement.
  if(ble_gap_adv_active())ble_gap_adv_stop();
  ble_gap_adv_params params{};
  params.conn_mode=BLE_GAP_CONN_MODE_NON;params.disc_mode=BLE_GAP_DISC_MODE_NON;params.itvl_min=params.itvl_max=BEACON_ITVL;
  return !ble_gap_adv_set_data(data,int(length)) && !ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC,nullptr,BLE_HS_FOREVER,&params,nullptr,nullptr);
}
static bool listen(Battle::Session::Scan mode) {
  if(!onAir())return BleLink::tapped();
  if(ble_gap_disc_active())ble_gap_disc_cancel();
  ble_gap_disc_params params{};
  const bool detect=mode==Battle::Session::Scan::Detect;
  params.itvl=detect?DETECT_ITVL:LISTEN_ITVL;params.window=detect?DETECT_WINDOW:LISTEN_WINDOW;
  // Passive: nothing is sent, and every frame is reported (their contents change).
  params.passive=1;params.filter_duplicates=0;
  return !ble_gap_disc(BLE_OWN_ADDR_PUBLIC,BLE_HS_FOREVER,&params,scanEvent,nullptr);
}

// From the loop, every pass: makes the radio do what the session asks.
// `measuring`: a pull is being measured, so nothing listens until it ends.
// `name` is this stick's own, empty for none.
inline void tick(uint32_t now,bool allowed,bool measuring,Battle::Session &session,const Battle::Book &book,const char *name) {
  const bool wantBeacon=allowed && session.beacon(now);
  const auto wantScan=allowed && !measuring?session.scan(now):Battle::Session::Scan::Off;
  if(!wantBeacon){stopBeacon();beaconTried=false;}
  else if(onAir() && !BleLink::linkUp && ownTurn(now)){showOwn();beaconTried=false;}
  else if(frameOnAir?now-refreshedAt>=REFRESH_MS:!beaconTried || now-beaconTriedAt>=RETRY_MS) {
    uint8_t frame[Battle::MAX_FRAME];size_t n=Battle::LAUNCH_BYTES;
    const bool starting=!frameOnAir;
    if(!beaconing)refreshes=0;
    if(name[0] && refreshes%NAME_EVERY==NAME_EVERY-1) {
      Battle::NameFrame who;who.id=session.id();snprintf(who.name,sizeof(who.name),"%s",name);n=who.encode(frame);
    } else session.frame(now,book).encode(frame);
    beaconTried=true;beaconTriedAt=now;refreshedAt=now;
    if(!tell(frame,n,starting))++failures;
    else {
      if(starting){frameOnAir=true;BleLink::battleBeacon=true;}
      if(!beaconing){beaconing=true;++beacons;Serial.println("BATTLE_RADIO beacon=1");}
      ++refreshes;
      // For the serial log and the rig: each frame whose content is new, its age aside.
      const bool launch=frame[1]==Battle::LAUNCH_FRAME;
      const bool shown=launch?toldAny && !memcmp(toldLaunch,frame,8) && !memcmp(toldLaunch+10,frame+10,Battle::LAUNCH_BYTES-10):toldName;
      if(!shown) {
        char text[2*Battle::MAX_FRAME+1];size_t at=0;
        for(size_t i=0;i<n;++i)at+=snprintf(text+at,sizeof(text)-at,"%02x",frame[i]);
        Serial.printf("BATTLE_TELL %s\n",text);
        if(launch){memcpy(toldLaunch,frame,Battle::LAUNCH_BYTES);toldAny=true;}else toldName=true;
      }
    }
  }
  if(wantScan==Battle::Session::Scan::Off){stopScan();scanTried=false;}
  else if(wantScan!=scanning || (onAir() && !ble_gap_disc_active())) {
    // A change of mode at once; a scan the stack ended or refused, every RETRY_MS.
    if(wantScan!=scanning && scanning!=Battle::Session::Scan::Off)scanTried=false;
    if(!scanTried || now-scanTriedAt>=RETRY_MS) {
      scanTried=true;scanTriedAt=now;
      if(!listen(wantScan))++failures;
      else {
        if(wantScan!=scanning){++scans;Serial.printf("BATTLE_RADIO scan=%s\n",wantScan==Battle::Session::Scan::Detect?"detect":"listen");}
        scanning=wantScan;
      }
    }
  }
}
inline void status() {
  Serial.printf("BATTLE_RADIO_STATUS on_air=%d beacon=%d scan=%u beacons=%lu scans=%lu heard=%lu dropped=%lu failures=%lu own_turns=%lu\n",
    onAir(),beaconing,unsigned(scanning),(unsigned long)beacons,(unsigned long)scans,(unsigned long)heardFrames,(unsigned long)droppedFrames,(unsigned long)failures,
    (unsigned long)ownTurns);
}
}
