// The Mic RPM page: its trace, its axes and how it is reached.
#include "../LaunchLabMini/controls.h"
#include "../LaunchLabMini/mic_session.h"
#include "../LaunchLabMini/mic_trace.h"
#include "../LaunchLabMini/usb_command_guard.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace MicTrace;

static Step reading(unsigned index,unsigned rpm,bool last=false,uint32_t launch=7) {
  Step s;s.launch=launch;s.index=uint16_t(index);s.rpm=uint16_t(rpm);
  s.reason=rpm?MicRpm::Reason::Tracking:MicRpm::Reason::WeakHarmonics;s.last=last;return s;
}
static bool near(float a,float b,float slack=0.01f){return std::fabs(a-b)<=slack;}
struct Vertex {int column;float rpm;bool join;};
static std::vector<Vertex> vertices(const Trace &t,int width,float span) {
  std::vector<Vertex> out;
  columns(t,width,span,[&](int column,float rpm,bool join){out.push_back({column,rpm,join});});
  return out;
}

// Time counts from the launch, so a late first reading stays late: the point
// of the view against the apps, whose axis starts at the first reading.
static void the_first_reading_keeps_its_time_after_the_launch() {
  static StoredTrace t;t.begin(7);
  for(unsigned i=0;i<6;++i)t.add(reading(i,0));
  assert(t.started() && !t.heard() && !t.latestRpm());
  t.add(reading(6,6700));t.add(reading(7,6680));
  assert(t.heard()==2 && t.firstRpm()==6700 && t.latestRpm()==6680 && t.peakRpm()==6700);
  assert(near(t.firstSeconds(),0.8f));
  assert(near(t.lastSeconds(),0.9f));
  const auto line=vertices(t,100,10);
  assert(line.size()==2 && line[0].column==8 && !line[0].join && line[1].join);
}
// One launch's readings only: a slow task must not paint old spin on a new one.
static void another_launchs_readings_are_ignored() {
  static StoredTrace t;t.begin(8);
  t.add(reading(0,6000,false,7));
  assert(t.count()==0);
  t.add(reading(0,6000,false,8));t.add(reading(0,5000,false,8));
  assert(t.count()==1 && t.latestRpm()==6000);
  static StoredTrace idle;idle.add(reading(0,6000,false,0));
  assert(!idle.count());
}
static void a_lost_reading_reads_as_none_and_the_last_step_ends_the_trace() {
  static StoredTrace t;t.begin(7);
  t.add(reading(0,6000));t.add(reading(3,5900));
  assert(t.count()==4 && t.rpm(1)==0 && t.rpm(2)==0 && !t.ended());
  t.add(reading(4,0,true));
  assert(t.ended() && t.latestRpm()==5900);
  t.begin(9);
  assert(!t.ended() && !t.count() && !t.peakRpm() && !t.heard() && t.launch()==9);
}
static void the_trace_is_bounded() {
  static StoredTrace t;t.begin(7);
  t.add(reading(MAX_READINGS-1,5000));t.add(reading(MAX_READINGS,5000));t.add(reading(60000,5000));
  assert(t.count()==MAX_READINGS);
  assert(!finished(MAX_READINGS-2,0) && finished(MAX_READINGS-1,0));
}
// As the apps: 8 s without evidence ends the spin, and a reading on its way
// to being trusted counts as evidence.
static void the_spin_ends_as_it_does_in_the_apps() {
  assert(!finished(10,LOST_READINGS-1) && finished(10,LOST_READINGS));
  MicRpm::Reading r;
  assert(!evidence(r));
  r.reason=MicRpm::Reason::Acquiring;r.support=3;r.score=MicRpm::MIN_SCORE;r.margin=MicRpm::MIN_MARGIN;
  assert(evidence(r));
  r.support=2;assert(!evidence(r));
  r.support=3;r.reason=MicRpm::Reason::WeakHarmonics;assert(!evidence(r));
  r.valid=true;r.hz=220;r.rpm=6600;r.cycles=2;r.reason=MicRpm::Reason::Tracking;
  assert(evidence(r) && step(1,2,r,false).rpm==6600);  // RPM, not 60 x Hz
  r.valid=false;r.rpm=0;assert(!step(1,2,r,false).rpm);
}
// The apps break the line at a gap of 0.4 s; a shorter one stays joined.
static void a_gap_breaks_the_line_as_in_the_apps() {
  static StoredTrace t;t.begin(7);
  t.add(reading(10,6000));t.add(reading(11,5990));
  t.add(reading(12,0));t.add(reading(13,0));  // 0.3 s between readings
  t.add(reading(14,5950));
  for(unsigned i=15;i<20;++i)t.add(reading(i,0));
  t.add(reading(20,5900));  // 0.6 s
  const auto line=vertices(t,400,20);
  assert(line.size()==4);
  assert(!line[0].join && line[1].join && line[2].join && !line[3].join);
}
// A two-minute spin is one point per column, whatever its length.
static void a_long_spin_costs_one_point_per_column() {
  static StoredTrace t;demo(t,MAX_READINGS,8,100);
  const float span=spanSeconds(t.elapsed());
  const auto line=vertices(t,104,span);
  assert(line.size()<=105 && line.size()>90);
  for(size_t i=1;i<line.size();++i)assert(line[i].column>line[i-1].column && line[i].join && line[i].rpm<line[i-1].rpm);
  assert(line.back().rpm>=1800);  // the tracker's floor: it never reads lower
  assert(near(t.changePercent(),100*(float(t.latestRpm())/6700-1),0.1f));
}
// The optical peak is the trace's own field, never a reading: the line starts
// where the sound does, and a launch with no peak has no marker.
static void the_optical_peak_stays_off_the_line() {
  static StoredTrace t;t.begin(7,6900);
  assert(t.opticalRpm()==6900 && !t.peakRpm() && !t.heard());
  t.add(reading(7,6500));t.add(reading(9,6480));
  assert(t.peakRpm()==6500 && t.firstRpm()==6500 && t.opticalRpm()==6900);
  const auto line=vertices(t,100,10);
  assert(line.size()==2 && line[0].column==9 && !line[0].join);
  assert(near(t.changePercent(),100*(6480.f/6500-1)));  // from the first heard, not the peak
  t.clear();assert(!t.opticalRpm());
  t.begin(8);assert(!t.opticalRpm());
  t.begin(9,90000);assert(t.opticalRpm()==65535);
}
// A locked spin fills every second slot: joined, as 0.2 s is under the gap.
static void a_locked_spin_stays_one_line() {
  static StoredTrace t;t.begin(7);
  for(unsigned i=0;i<20;i+=2)t.add(reading(i,6000-i));
  assert(t.count()==19 && t.heard()==10);
  const auto line=vertices(t,400,20);
  assert(line.size()==10);
  for(size_t i=1;i<line.size();++i)assert(line[i].join);
}
static void slots_follow_the_reading_time() {
  assert(slot(0.2f)==0 && slot(0.3f)==1 && slot(0.8f)==6 && slot(0.1f)==0);
  Follower f;MicRpm::Reading quiet;
  assert(f.next(7,quiet,0.2f).index==0 && f.next(7,quiet,0.4f).index==2 && f.slot()==2);
  // Quiet counts in time, whatever the hop.
  float seconds=0.4f;
  while(!f.done() && seconds<20){seconds+=0.2f;f.next(7,quiet,seconds);}
  assert(f.done() && near(seconds,8.2f,0.21f));
}
// Seconds the ring lost before a reading were never heard: not quiet ones.
static void lost_sound_is_not_quiet() {
  Follower f;MicRpm::Reading quiet;
  f.skip();
  assert(!f.next(7,quiet,9.2f).last && !f.done() && f.slot()==90);
  float seconds=9.2f;
  while(!f.done() && seconds<30){seconds+=0.2f;f.next(7,quiet,seconds);}
  assert(f.done() && near(seconds,17.2f,0.21f));  // and 8 s of quiet after it still ends the spin
  Follower g;assert(g.next(7,quiet,9.2f).last);  // without the skip the same reading ends it
}
// 0 at the foot as in the apps; whole thousands at each division.
static void the_axes_stay_round() {
  assert(topRpm(0)==9000 && topRpm(8300)==9000 && topRpm(8400)==12000 && topRpm(12400)==15000);
  for(float peak=0;peak<21000;peak+=7){const float top=topRpm(peak);assert(top>=peak*1.08f-0.01f && !std::fmod(top,TOP_STEP_RPM));}
  assert(spanSeconds(0)==10 && spanSeconds(9.9f)==10 && spanSeconds(10.1f)==20 && spanSeconds(120.1f)==130);
}
// Sound through the real tracker to the trace, as the analysis task sends it:
// launch noise, then a tone falling 3 Hz/s (180 RPM/s) that stops 9 s in.
static void a_spin_down_reaches_the_trace_and_ends() {
  static uint32_t seed=12345;
  auto noise=[&]{seed=1664525u*seed+1013904223u;return float(double(seed)/4294967296.0*2-1);};
  std::unique_ptr<MicRpm::Space> space(new MicRpm::Space);
  std::unique_ptr<MicRpm::Stream> stream(new MicRpm::Stream(space.get()));stream->reset(6900);
  static StoredTrace t;t.begin(7,6900);
  Follower follower;double phase=0;
  for(uint32_t i=0;!follower.done() && i<MicRpm::CAPTURE_HZ*40;++i) {
    const double seconds=double(i)/MicRpm::CAPTURE_HZ,hz=110-3*seconds;
    float v=noise()*0.00003f;
    if(seconds<0.5)v=noise()*0.3f;
    else if(seconds<9) {
      phase+=2*M_PI*hz/MicRpm::CAPTURE_HZ;
      v+=float(0.1*std::sin(phase)+0.05*std::sin(2*phase)+0.03*std::sin(3*phase)+0.02*std::sin(4*phase));
    }
    MicRpm::Reading r;
    if(!stream->feed(v,r))continue;
    t.add(follower.next(7,r,stream->seconds()));
  }
  assert(follower.done() && t.ended());
  // The first window clear of the launch noise, then three readings in a row.
  assert(t.firstSeconds()>=0.7f && t.firstSeconds()<=1.3f);
  assert(t.heard()>=30);  // 0.2 s apart once locked
  // Each reading is within 1% of the tone at its window's centre, and falling.
  for(unsigned i=0;i<t.count();++i) {
    if(!t.rpm(i))continue;
    const float truth=(110-3*Trace::seconds(i))*60;
    assert(std::fabs(float(t.rpm(i))-truth)<=truth*0.01f);
  }
  assert(t.changePercent()<-15 && t.changePercent()>-30);
  // 8 s without evidence after the tone stopped.
  assert(t.elapsed()>=16 && t.elapsed()<=18);
  const auto line=vertices(t,104,spanSeconds(t.elapsed()));
  for(size_t i=1;i<line.size();++i)assert(line[i].rpm<=line[i-1].rpm+30);
}
// a8ee2234, owner's pulls 3-6: the task took each pull with its release over
// 8 s behind the newest sample, and the spin was dropped whole. As the listener
// now reads one: from the oldest sample clear of the writer, on the spin's own
// clock, with the lost seconds not counted as quiet.
static void a_spin_the_ring_has_partly_lost_is_heard_in_time() {
  static constexpr uint32_t RATE=MicRpm::CAPTURE_HZ;
  static constexpr uint64_t USABLE=RATE*8-512*2,MARGIN=RATE;
  std::vector<float> sound(RATE*42);double phase=0;
  auto truth=[](double seconds){return 110-1.5*seconds;};
  for(size_t i=0;i<sound.size();++i) {
    const double seconds=double(i)/RATE;
    if(seconds>=30)continue;
    phase+=2*M_PI*truth(seconds)/RATE;
    sound[i]=float(0.1*std::sin(phase)+0.05*std::sin(2*phase)+0.03*std::sin(3*phase)+0.02*std::sin(4*phase));
  }
  std::unique_ptr<MicRpm::Space> space(new MicRpm::Space);
  std::unique_ptr<MicRpm::Stream> stream(new MicRpm::Stream(space.get()));stream->reset(6600);
  static StoredTrace t;t.begin(9,6600);
  Follower follower;
  uint64_t at=0;const uint64_t written=uint64_t(RATE*16);  // released at 0, taken 16 s later
  const uint64_t from=MicSession::resume(at,written,USABLE,MARGIN);
  assert(from>at);
  stream->skip(from-at);follower.skip();at=from;
  for(;at<sound.size() && !follower.done();++at) {
    MicRpm::Reading r;
    if(stream->feed(sound[at],r))t.add(follower.next(9,r,stream->seconds()));
  }
  // Heard from where the ring still held it, not dropped and not ended at once.
  assert(t.heard()>=40);
  assert(t.firstSeconds()>=float(from)/RATE && t.firstSeconds()<=float(from)/RATE+1.5f);
  // Each reading whose window is all tone sits at its own time since the release.
  for(unsigned i=0;i<t.count();++i)if(t.rpm(i) && Trace::seconds(i)<=29.8f)assert(std::fabs(float(t.rpm(i))-float(truth(Trace::seconds(i))*60))<=float(truth(Trace::seconds(i))*60)*0.01f);
  // It ends 8 s after the tone does, as any spin.
  assert(t.ended() && t.lastSeconds()>=28 && t.elapsed()<=39);
}
static void the_page_opens_from_the_menu_and_every_way_back_leaves_it() {
  using namespace Controls;
  assert(MENU_COUNT==8);
  State s;s.page=Page::Menu;s.menu=MIC_ROW;
  s.tapA();assert(s.page==Page::MicRpm);
  assert(!s.autoRecap());  // a pull stays on the view, without a recap
  s.tapB();assert(s.page==Page::MicRpm);
  s.tapA();assert(s.page==Page::Menu);
  s.page=Page::MicRpm;s.holdB();assert(s.page==Page::Menu);
  s.page=Page::MicRpm;assert(s.holdA()==Effect::None && s.page==Page::MicRpm);
  s.page=Page::Menu;s.menu=MIC_ROW+1;s.tapA();assert(s.page==Page::Main);  // Back is still last
}
// The mic never starts on wake: the view comes back as its Menu row.
static void waking_returns_to_the_menu_row() {
  using namespace Controls;
  State s;s.page=Page::MicRpm;s.menu=2;
  State out;uint8_t view=0;
  assert(decodeWake(encodeWake(s,0),out,view));
  assert(out.page==Page::Menu && out.menu==MIC_ROW);
}
static void its_usb_letters_are_guarded() {
  for(const char c:{'m','A','Y'})assert(UsbCommandGuard::guarded(c) && !UsbCommandGuard::immediate(c));
}

int main() {
  the_first_reading_keeps_its_time_after_the_launch();
  another_launchs_readings_are_ignored();
  a_lost_reading_reads_as_none_and_the_last_step_ends_the_trace();
  the_trace_is_bounded();
  the_spin_ends_as_it_does_in_the_apps();
  a_gap_breaks_the_line_as_in_the_apps();
  a_long_spin_costs_one_point_per_column();
  the_axes_stay_round();
  the_optical_peak_stays_off_the_line();
  a_locked_spin_stays_one_line();
  slots_follow_the_reading_time();
  a_spin_down_reaches_the_trace_and_ends();a_spin_the_ring_has_partly_lost_is_heard_in_time();lost_sound_is_not_quiet();
  the_page_opens_from_the_menu_and_every_way_back_leaves_it();
  waking_returns_to_the_menu_row();
  its_usb_letters_are_guarded();
  std::puts("mic_view tests passed");
}
