// The stick's orientation on the LaunchLab profile (ble_motion.h): the live
// pose event and the tilt replay, byte for byte as docs/BLE_PROTOCOL.md says,
// and equal to what the recap itself draws.
#include "../LaunchLabMini/ble_motion.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
using namespace BleMotion;
using LaunchMotion::Quaternion;
static int16_t i16(const uint8_t *p){return int16_t(uint16_t(p[0])|uint16_t(p[1])<<8);}
static uint32_t u32(const uint8_t *p){uint32_t v;std::memcpy(&v,p,4);return v;}
static LaunchMotion::Trace trace() {
  LaunchMotion::Trace t;t.quality=LaunchMotion::Quality::Valid;t.fused=true;t.rpm=6000;t.durationMs=400;t.count=48;t.gravity[2]=1;t.number=1234;
  const float turn[3]={0,90,0};
  for(unsigned i=0;i<t.count;++i) {
    auto &p=t.points[i];p.ms=-500+int(i*1150/47);
    Quaternion q;q.integrate(turn,(p.ms+500)*.001f);
    p.q[0]=std::lround(q.w*16384);p.q[1]=std::lround(q.x*16384);p.q[2]=std::lround(q.y*16384);p.q[3]=std::lround(q.z*16384);
  }
  return t;
}
int main() {
  // The live pose: type 2, then i16 w, x, y, z scaled by 16384, never past +-1.
  uint8_t e[POSE_BYTES];assert(POSE_BYTES==9 && POSE_EVENT==2);
  encodePose(Quaternion{1,0,0,0},e);
  assert(e[0]==2 && i16(e+1)==16384 && i16(e+3)==0 && i16(e+5)==0 && i16(e+7)==0);
  encodePose(Quaternion{.5f,-.5f,.25f,-.25f},e);
  assert(i16(e+1)==8192 && i16(e+3)==-8192 && i16(e+5)==4096 && i16(e+7)==-4096);
  encodePose(Quaternion{2,-2,0,0},e);assert(i16(e+1)==16384 && i16(e+3)==-16384);
  // About 10 Hz, never in a pull, and only when the fusion is ready. Refused
  // ticks do not use up the period.
  Gate g;
  assert(!g.due(99,false,true));
  assert(g.due(100,false,true) && !g.due(150,false,true) && g.due(200,false,true));
  assert(!g.due(1000,true,true) && !g.due(1000,false,false));
  assert(g.last==200 && g.due(1000,false,true) && g.last==1000);
  {Gate wrap;wrap.last=UINT32_MAX-20;assert(!wrap.due(50,false,true) && wrap.due(79,false,true));}

  // The replay: the header, then the recap's 48 poses with their times.
  auto scratch=std::make_unique<MotionReplay::Replay>();auto drawn=std::make_unique<MotionReplay::Replay>();
  uint8_t out[REPLAY_BYTES+8];std::memset(out,0xee,sizeof(out));
  auto t=trace();
  assert(REPLAY_BYTES==11+48*12 && encodeReplay(t,*scratch,out,REPLAY_BYTES-1)==0);
  assert(encodeReplay(t,*scratch,out,sizeof(out))==REPLAY_BYTES && out[REPLAY_BYTES]==0xee);
  assert(u32(out)==1234 && u32(out+4)==400 && out[8]==48 && out[9]==0 && out[10]==0);
  assert(drawn->build(t));
  for(unsigned i=0;i<48;++i) {
    const uint8_t *p=out+REPLAY_HEADER+i*REPLAY_POINT;
    assert(int32_t(u32(p))==drawn->ms[i]);
    const auto &q=drawn->pose[i];
    assert(i16(p+4)==std::lround(q.w*16384) && i16(p+6)==std::lround(q.x*16384) && i16(p+8)==std::lround(q.y*16384) && i16(p+10)==std::lround(q.z*16384));
  }
  assert(int32_t(u32(out+REPLAY_HEADER))==-500 && int32_t(u32(out+REPLAY_HEADER+47*REPLAY_POINT))==650);  // the lead-in is negative
  // A pull the recap marks "Tilt approx." says so in flag bit 0.
  t.saturated=true;assert(encodeReplay(t,*scratch,out,sizeof(out))==REPLAY_BYTES && out[10]==1);
  // A pull the recap cannot draw (no fusion, or none since boot): the header alone, count 0.
  t=trace();t.fused=false;
  assert(encodeReplay(t,*scratch,out,sizeof(out))==REPLAY_HEADER && u32(out)==1234 && out[8]==0 && out[9]==0 && out[10]==0);
  LaunchMotion::Trace none;assert(encodeReplay(none,*scratch,out,sizeof(out))==REPLAY_HEADER && u32(out)==0 && out[8]==0);
  std::cout<<"Bluetooth orientation: the live pose event and its pace, and the tilt replay as the recap draws it\n";
}
