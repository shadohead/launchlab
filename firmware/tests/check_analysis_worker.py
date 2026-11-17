"""Host-execute the production capture callback and follower wait/stop paths."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'LaunchLabMini/mic_listener.h').read_text()
def method(signature):
    a=source.index(signature);b=source.index('{',a)+1;depth=1
    while depth:
        depth+=(source[b]=='{')-(source[b]=='}');b+=1
    return source[a:b]
prefix=r'''
#include "mic_trace.h"
#include "analysis_rpm_filter.h"
#include "mic_session.h"
#include <atomic>
#include <cassert>
#include <functional>
#include <vector>
#include <iostream>
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
static unsigned waits=0,delays=0,notifies=0,timeout=0,pending=0;
static std::function<void()> wake;
static int64_t esp_timer_get_time(){return 1000000;}
static void xTaskNotifyGive(void*){++notifies;++pending;}
static void invoke(){if(wake){auto f=wake;wake=nullptr;f();}}
static unsigned ulTaskNotifyTake(int,unsigned ticks){++waits;timeout=ticks;if(!pending)invoke();unsigned n=pending;pending=0;return n;}
static void vTaskDelay(unsigned ticks){++delays;assert(ticks==10);invoke();}
static struct {struct {bool isRecording(){return true;}} Mic;} M5;
struct Fake {
 static constexpr unsigned CHUNK=512,RATE=16000,RING_SAMPLES=RATE*8,USABLE=RING_SAMPLES-1024,MARGIN=RATE,TAPE_SPIN_S=90;
 struct Pull {bool analysis=true;float rpm=6000;uint32_t number=1;uint64_t releaseSample=0;}pull_;
 struct Tape {uint64_t written=0;}tape_;
 struct Stream {unsigned frames=0;bool spikes=false,acquiring=false,breakLock=false,relative=false,enabled=false;void reset(float,bool analysis=false){frames=0;enabled=analysis;}void record(Tape*){}void skip(uint64_t){}
   size_t feed(int16_t*,size_t n,MicRpm::Reading &r,bool &ready){ready=true;r.valid=true;r.rpm=spikes && frames==1?7600:6000;if(acquiring){r.valid=frames>=2;r.reason=r.valid?MicRpm::Reason::Tracking:MicRpm::Reason::Acquiring;
   r.candidateCycles=r.valid?0:2;r.cycles=r.valid?2:0;r.candidateHz=r.hz=breakLock && r.valid?100:200;}
   if(relative && frames==1){r.valid=false;r.rpm=0;r.reason=MicRpm::Reason::WeakHarmonics;r.relativeRpm=5800;r.pitchConfidence=128;}
   r.audio.bands[0]=uint8_t(80+frames);++frames;return std::min(n,size_t(512));}
   float seconds(){return frames*0.2f;}}streamStorage,*stream_=&streamStorage;
 struct Cost {void add(uint32_t){}}cost_[4];
 enum class Job{Idle,Following};Job job_=Job::Following;
 AudioMetadata::Recorder *audio_=nullptr;uint64_t audioStartedUs_=0;uint32_t unheard_=0;bool fresh_=false;
 std::atomic<bool> analysisFollowing_{false},tapeArmed_{false};std::atomic<uint32_t> stoppedLaunch_{0};
 bool quit_=false,waiting_=false,dumping_=false,hold_=false;
 void *task_=this;MicTrace::StoredTrace traceStorage;MicTrace::Trace *trace_=&traceStorage;
 inline static int16_t ring_[RING_SAMPLES]={};uint64_t written_=0;int64_t lastUs_=0;int peak_=0;
 uint32_t lostSteps_=0,intervalMin_=~0u,intervalMax_=0,late_=0,misses_=0,overruns_=0,lateStarts_=0,behindMax_=0,readingMax_=0,stackFree_=0;
 unsigned sends=0,finished=0,recovered=0;bool read=false,multiple=false;std::vector<uint16_t> results,rejected;std::vector<uint8_t> confidences,audioPeaks;
 struct Chain {bool refilled(bool,bool,bool){return false;}}chain_;
 bool queue(){return true;}uint64_t written(){return written_;}
 void send(const MicTrace::Step &s){++sends;audioPeaks.push_back(s.audio[8]);recovered+=s.recovered;results.push_back(s.rpm);rejected.push_back(s.rejectedRpm);confidences.push_back(s.pitchConfidence);if(s.rpm){read=true;if(!multiple)quit_=true;}if(multiple && sends==3)quit_=true;}
 void finish(const MicRpm::Summary&,float,float,float,int64_t,uint64_t,uint64_t){++finished;}
'''
tests=r'''
};
void reset(){waits=delays=notifies=timeout=pending=0;wake=nullptr;}
int main(){
 reset();{
   Fake f;AudioMetadata::Recorder recorder;recorder.reset(1);f.audio_=&recorder;f.traceStorage.begin(1,6000);
   MicTrace::Step step;step.launch=1;step.index=2;step.frame=true;step.audio[8]=77;
   f.add(step);uint8_t bytes[AudioMetadata::MAX_BYTES];auto n=recorder.pack(bytes,1000,0,0);
   assert(AudioMetadata::valid(bytes,n) && bytes[32+2*19+8]==77 && bytes[32+2*19+18]==20);
   f.freezeTrace(700);step.index=4;f.add(step);recorder.pack(bytes,1000,0,0);
   assert(!bytes[32+3*19+16]);
 }
 reset();{Fake f;f.traceStorage.begin(1,6000);wake=[&]{f.chunk(f.ring_,512);};f.follow();assert(f.read && waits==1 && delays==0 && timeout==100 && notifies==1 && !f.analysisFollowing_ && f.finished==1);}
 reset();{Fake f;f.pull_.analysis=false;wake=[&]{f.chunk(f.ring_,512);};f.follow();assert(f.read && waits==0 && delays==1 && notifies==0);}
 reset();{Fake f;f.traceStorage.begin(1,6000);wake=[&]{f.freezeTrace();};f.follow();assert(!f.read && waits==1 && notifies==1 && f.finished==1 && !f.analysisFollowing_);}
 reset();{Fake f;f.traceStorage.begin(1,6000);f.freezeTrace();f.follow();assert(waits==0 && delays==0 && !f.read);}
 reset();{Fake f;wake=[&]{f.waiting_=true;xTaskNotifyGive(f.task_);};f.follow();assert(waits==1 && f.sends==0 && !f.analysisFollowing_);}
 reset();{Fake f;wake=[&]{f.quit_=true;xTaskNotifyGive(f.task_);};f.follow();assert(waits==1 && f.sends==0 && !f.analysisFollowing_);}
 reset();{Fake f;wake=[&]{f.dumping_=true;xTaskNotifyGive(f.task_);};f.follow();assert(waits==1 && !f.read && !f.analysisFollowing_);}
 reset();{Fake f;f.chunk(f.ring_,512);assert(!notifies);f.follow();assert(f.read && !waits && !delays);}
 for(bool analysis:{false,true}){
   reset();Fake f;f.pull_.analysis=analysis;f.multiple=true;f.streamStorage.spikes=true;f.written_=1536;f.follow();
   assert(f.results.size()==3 && f.results[0]==6000 && f.results[2]==6000);
   assert(f.results[1]==(analysis?0:7600));assert(f.rejected[1]==(analysis?7600:0)); // Same production path: normal Mic RPM remains unfiltered.
 }
 for(bool mismatch:{false,true}){
   reset();Fake f;f.multiple=true;f.streamStorage.acquiring=true;f.streamStorage.breakLock=mismatch;
   f.written_=1536;f.follow();assert(f.recovered==(mismatch?0:2));
   assert(f.results.size()==3 && f.results[0]==(mismatch?0:6000) && f.results[2]==6000);
 }
 for(bool analysis:{false,true}){
   reset();Fake f;f.pull_.analysis=analysis;f.multiple=true;f.streamStorage.relative=true;
   f.written_=1536;f.follow();assert(f.streamStorage.enabled==analysis);
   assert(f.results.size()==3 && f.results[0]==6000 && f.results[2]==6000);
   assert(f.results[1]==(analysis?5800:0) && f.confidences[1]==(analysis?128:0));
   assert(f.recovered==unsigned(analysis));
   assert(f.audioPeaks==std::vector<uint8_t>({uint8_t(analysis?80:0),uint8_t(analysis?81:0),uint8_t(analysis?82:0)}));
 }
 std::cout<<"PASS: production Analysis sample wake, stop, replacement pull, exit, dump and ordinary Mic RPM polling\n";
}
'''
with tempfile.TemporaryDirectory(prefix='analysis-worker-') as directory:
    cpp=Path(directory)/'test.cpp';exe=Path(directory)/'test'
    cpp.write_text(prefix+method('void add(const MicTrace::Step &step)')+method('void freezeTrace(')+method('void chunk(')+method('void follow()')+tests)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-I'+str(root/'LaunchLabMini'),'-I'+str(root/'LaunchLabRpm'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
