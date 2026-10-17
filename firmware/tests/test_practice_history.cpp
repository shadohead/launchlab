#include "../LaunchLabMini/practice_history.h"
#include "../LaunchLabMini/history_checkpoint.h"
#include "../LaunchLabMini/sensor_profile.h"
#include <cassert>
#include <iostream>
#include <limits>
static void flat(AnalogTachometer &d,unsigned us){for(unsigned i=0;i<us/20;++i)d.feed(140);}
static void pull(AnalogTachometer &d,unsigned period=10000) {
  for(unsigned i=0;i<12;++i) {
    for(unsigned j=0;j<period/40;++j)d.feed(240);
    for(unsigned j=0;j<period/40;++j)d.feed(140);
  }
}
static bool near(float a,float b){return std::fabs(a-b)<.01f;}
int main() {
  PracticeHistory h;
  assert(!h.size() && !h.sessions());
  assert(!h.accept(1,false,6000,0) && !h.accept(1,true,NAN,0));
  assert(!h.accept(1,true,999,0) && !h.accept(1,true,INFINITY,0));
  assert(h.accept(1,true,4000,100));
  assert(!h.accept(1,true,7000,1000)); // repeated revision / result stays on screen
  assert(h.accept(2,true,8000,30100));
  assert(h.session()->count==2 && near(h.session()->mean(),6000));
  assert(near(h.session()->best,8000) && h.session()->seconds==30);
  assert(h.accept(3,true,9000,30100+PracticeHistory::SESSION_IDLE_MS-1));
  assert(h.sessions()==1);
  assert(h.accept(4,true,5000,30100+2*PracticeHistory::SESSION_IDLE_MS-1));
  assert(h.sessions()==2 && h.session()->count==1 && h.session()->number==2);
  h.newSession();h.newSession();assert(h.sessions()==2); // no empty sessions
  assert(h.accept(5,true,7000,1600000));assert(h.sessions()==3 && h.recent()->number==5);
  PracticeHistory wrap;
  assert(wrap.accept(1,true,5000,UINT32_MAX-1000));
  assert(wrap.accept(2,true,7000,2000));assert(wrap.sessions()==1 && wrap.session()->seconds==3);
  for(unsigned i=6;i<=300;++i)h.accept(i,true,6000+i,1600000+i*1000);
  assert(h.size()==128 && h.recent()->number==300 && h.recent(127)->number==173 && !h.recent(128));
  // Mean uses all 296 launches in session 3, not just its retained 128.
  double expected=7000;for(unsigned i=6;i<=300;++i)expected+=6000+i;
  assert(h.session()->count==296 && near(h.session()->mean(),float(expected/296)));
  assert(near(h.recentMean(0,5),6298));
  PracticeHistory::Image image;h.encode(image);
  PracticeHistory restored;assert(restored.decode(image.data(),image.size()));
  assert(restored.size()==128 && restored.session()->count==296 && restored.sessionPending());
  assert(restored.accept(1,true,8000,100)); // device count restarts on reboot
  assert(restored.session()->number==4 && restored.recent()->number==301);
  const unsigned size=restored.size();const uint32_t generation=restored.generation();
  image[150]^=1;assert(!restored.decode(image.data(),image.size()));
  assert(restored.size()==size && restored.generation()==generation);
  assert(!restored.decode(image.data(),image.size()-1));
  h.encode(image);image[4]=2;assert(!restored.decode(image.data(),image.size()));
  for(unsigned i=0;i<40;++i){restored.newSession();restored.accept(i+2,true,7000+i,1000+i*1000);}
  assert(restored.sessions()==24 && restored.session()->number==44 && restored.session(23)->number==21);
  restored.encode(image);PracticeHistory again;assert(again.decode(image.data(),image.size()));
  assert(near(again.recentSessionMean(0,2),7038.5f));
  // Known-period optical pull followed by a flash gap. Finished reading must
  // remain exact, while a rewind immediately after the gap cannot arm.
  AnalogTachometer d;StickS3SensorProfile::configure(d);flat(d,1200000);
  assert(d.phase==AnalogTachometer::Phase::Ready && !HistoryCheckpoint::allowed(d));
  pull(d);assert(d.phase==AnalogTachometer::Phase::Launch && !HistoryCheckpoint::allowed(d));
  flat(d,50000);assert(d.phase==AnalogTachometer::Phase::Hold && d.launches==1);
  const float result=d.resultRpm;const uint32_t revision=d.resultRevision;const uint32_t rearm=d.rearmMs();
  HistoryCheckpoint checkpoint(d);checkpoint.restore(d);
  assert(d.resultValid && d.resultRpm==result && d.resultRevision==revision && d.launches==1);
  // REARM resumes with the quiet it had served and still expects the rewind
  // (restarting a full second swallowed the next pull).
  assert(d.phase==AnalogTachometer::Phase::Hold && d.rearmMs()==rearm && rearm<1000 && d.rewindPending);
  pull(d,15000);assert(d.launches==1 && d.phase==AnalogTachometer::Phase::Hold);
  flat(d,1600000);pull(d,8000);flat(d,60000);
  assert(d.launches==2 && std::fabs(d.resultRpm-7500)<3);
  d.setEnabled(false);HistoryCheckpoint paused(d);paused.restore(d);
  assert(d.phase==AnalogTachometer::Phase::Paused && !d.enabled);
  std::cout<<"PASS: accepted-only/deduplicated records, idle/manual/reboot sessions, exact means, bounded rings, wraparound, version/CRC rejection, checkpoint result retention and rewind/rearm safety\n";
}
