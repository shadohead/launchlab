#!/usr/bin/env python3
"""Run the actual acquisition owner and tickSensorIdle under bounded host fakes.

ADC API calls and queues are synchronous, single-threaded fakes. The complete
production acquisition header is compiled; samples pass through its process(),
and command()/publish() are never copied or reimplemented. This checks command
ownership and policy integration, not ESP32 DMA, scheduler timing or physical
wake latency. No hardware, flash, persistent files or application edits occur.
"""
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('firmware', nargs='?', type=pathlib.Path,
                    default=pathlib.Path(__file__).resolve().parents[1])
args = parser.parse_args()
source = (args.firmware / 'LaunchLabMini/LaunchLabMini.ino').read_text()
start = source.index('static void tickSensorIdle(')
cursor = source.index('{', start) + 1
depth = 1
while depth:
    depth += (source[cursor] == '{') - (source[cursor] == '}')
    cursor += 1
tick = source[start:cursor]


fixture = r'''
#include "fake_runtime.h"
#include "sensor_profile.h"
#include "rpm_estimator.h"
#include "sample_ring.h"
#include "history_checkpoint.h"
// Expose only acquisition internals to invoke its otherwise infinite owner
// loop one batch/command at a time. Dependencies were included before this.
#define private public
#include "acquisition.h"
#undef private
#include "sensor_idle.h"
#include "power_status.h"
#include "controls.h"
using Owner=StickS3Acquisition;using Action=Owner::Action;using Snapshot=Owner::Snapshot;using D=AnalogTachometer;
struct Fixture {
  Owner o;std::array<uint16_t,4096> storage{};
  explicit Fixture(StickS3SensorProfile::Mode profile=StickS3SensorProfile::Mode::Standard){
    assert(!failStop && !failStart && !failFlush);o.storage_=storage.data();o.ring_=SampleRing(storage.data(),storage.size());
    o.replies_=xQueueCreate(4,sizeof(Owner::Reply));o.events_=xQueueCreate(16,sizeof(Snapshot));
    o.sensorProfile_=profile;StickS3SensorProfile::configure(o.detector_,profile);assert(o.initAdc());level(1200000);assert(s().phase==D::Phase::Ready);
  }
  ~Fixture(){o.closeAdc();vQueueDelete(o.replies_);vQueueDelete(o.events_);}
  Fixture(const Fixture&)=delete;
  Snapshot s(){o.publish(true);Snapshot event;while(o.takeEvent(event)){}return o.snapshot();}
  bool cmd(Action action,uint32_t value=0){
    const auto id=++o.requestId_;o.command({action,value,id});Owner::Reply reply{};
    assert(xQueueReceive(o.replies_,&reply,0)==pdTRUE && reply.id==id);s();return reply.ok;
  }
  void level(unsigned us,int value=1000){
    assert(us%20==0 && o.ready_ && o.adc_->active);std::array<adc_digi_output_data_t,256> batch{};
    for(auto &sample:batch)sample.type2={uint16_t(value),o.channel_,0};
    unsigned remaining=us/20;while(remaining){const auto n=std::min(remaining,unsigned(batch.size()));
      clockUs+=n*20;o.lastRead_=clockUs;o.process(reinterpret_cast<const uint8_t*>(batch.data()),n*SOC_ADC_DIGI_RESULT_BYTES);remaining-=n;}
    s();
  }
  void square(unsigned period=10000,unsigned turns=12,int amplitude=100){for(unsigned j=0;j<turns;++j){level(period/2,1000+amplitude);level(period-period/2);}}
  Snapshot result(){square();level(1700000);auto out=s();assert(out.phase==D::Phase::Ready && out.launches==1 && out.valid && std::fabs(out.resultRpm-6000)<3);return out;}
};
static void retained(const Snapshot &before,const Snapshot &after){
  assert(after.valid==before.valid && after.launches==before.launches && after.revision==before.revision && after.ended==before.ended);
  assert(after.resultRpm==before.resultRpm && after.peakRpm==before.peakRpm && after.sustainedPeakRpm==before.sustainedPeakRpm && after.singlePeakRpm==before.singlePeakRpm && after.amplitude==before.amplitude);
}
struct Adapter {Fixture *f=nullptr;unsigned requests=0;void(*beforeRequest)()=nullptr;
  Snapshot snapshot(){assert(f);return f->s();}
  bool request(Action a,uint32_t value){++requests;if(beforeRequest){const auto hook=beforeRequest;beforeRequest=nullptr;hook();}return f->cmd(a,value);}
} acquisition;
static SensorIdle sensorIdle;static InactivityTimer inactivity;static Controls::State controls;
static bool wipeBlocked=false,historyPage=false,diagnostics=false,tournamentMode=false,motionPending=false,dirty=false;
struct Usb {bool blocked=false;bool inhibited(uint32_t)const{return blocked;}} usbAwake;
struct Feedback {bool visible=false;bool shown()const{return visible;}} launchFeedback;
struct PendingStore {bool unsaved=false;bool pending(int)const{return unsaved;}} practiceStore,bestStore;
struct PendingSettings {bool unsaved=false;bool pending()const{return unsaved;}} settingsSave;
static int practice=0,personalBests=0;
struct Imu {struct State {MotionActivity::State activity;} state;bool available=true;State snapshot()const{return state;}bool running()const{return available;}} imuAcquisition;
struct SerialFake {bool input=false;int available()const{return input;}template<class... T>void printf(const char*,T...){}} Serial;
static void resetPolicy(Fixture &f){acquisition={&f,0,nullptr};sensorIdle={};inactivity={};inactivity.begin(uint32_t(clockUs/1000)-30000);
  controls={};wipeBlocked=historyPage=diagnostics=tournamentMode=motionPending=dirty=false;usbAwake={};launchFeedback={};practiceStore={};bestStore={};settingsSave={};imuAcquisition={};Serial={};}
static void freshMotion(float gyro=0,bool moving=false){auto &a=imuAcquisition.state.activity;a={};a.valid=true;a.sampleUs=clockUs;a.gyroDps=gyro;a.moving=moving;}
'''

tests = r'''
static Snapshot tick(){freshMotion();Snapshot s=acquisition.snapshot();tickSensorIdle(uint32_t(clockUs/1000),s);return s;}
int main(){
  for(auto profile:{StickS3SensorProfile::Mode::Standard,StickS3SensorProfile::Mode::Tcrt}){
    // The UI snapshot can still say idle when the owner gets the next mark.
    Fixture f(profile);const auto idle=f.s();const auto stopCount=stops;
    f.level(2000,1100);assert(f.s().phase==D::Phase::Ready && !f.s().opticalIdle);
    assert(!f.cmd(Action::IdlePause,idle.launches) && stops==stopCount && f.s().ready);
    f.square();assert(f.s().phase==D::Phase::Launch);assert(!f.cmd(Action::IdlePause,idle.launches) && stops==stopCount);
    f.level(60000);assert(f.s().phase==D::Phase::Hold);assert(!f.cmd(Action::IdlePause,f.s().launches) && stops==stopCount);
    f.level(1700000);assert(f.s().phase==D::Phase::Ready && f.s().launches==1);
    assert(!f.cmd(Action::IdlePause,0) && stops==stopCount);assert(f.cmd(Action::IdlePause,1));assert(f.cmd(Action::IdleResume));
    f.o.detector_.setEnabled(false);assert(!f.cmd(Action::IdlePause,1));
    f.o.detector_.setEnabled(true);assert(f.s().phase==D::Phase::Settling);assert(!f.cmd(Action::IdlePause,1));
  }
  std::cout<<"PASS: production owner rejects late first mark/full pull, stale launch counter, Hold, Paused and Settling in both profiles\n";
  for(auto profile:{StickS3SensorProfile::Mode::Standard,StickS3SensorProfile::Mode::Tcrt})
  for(auto mode:{RpmEstimator::Mode::ThreeTurn,RpmEstimator::Mode::SingleTurn}){
    Fixture f(profile);f.o.estimator_=mode;const auto before=f.result();const auto total=f.o.ring_.total();
    f.o.rate_=50000;f.o.processingPercent_=12;f.o.displayBoundary(true);
    assert(f.cmd(Action::IdlePause,1));auto paused=f.s();retained(before,paused);
    assert(paused.resting && paused.suspended && !paused.ready && !f.o.adc_->active && !std::strcmp(paused.stateName(),"RESTING"));
    assert(paused.rate==0 && paused.processingPercent==0 && paused.ringCount==before.ringCount);
    clockUs+=120000000;assert(f.o.ring_.total()==total && f.s().samples==before.samples);
    assert(f.cmd(Action::IdleResume));auto resumed=f.s();retained(before,resumed);
    assert(resumed.phase==D::Phase::Ready && resumed.ready && !resumed.suspended && !resumed.resting && f.o.adc_->active);
    assert(resumed.ringCount==0 && resumed.ringFirst==total && !f.o.pendingMarks_);
    // An immediate new pull must count, rather than disappear in Settling.
    f.square();assert(f.s().phase==D::Phase::Launch);f.level(1700000);
    assert(f.s().launches==2 && f.s().valid && std::fabs(f.s().resultRpm-6000)<3);
    assert(!(f.storage[total%f.storage.size()]&0xc000));
  }
  std::cout<<"PASS: pause publishes RESTING/zero sampling metrics; both profiles/estimators retain results; recorder discontinuity and immediate Ready pull after120s gap\n";
  {
    Fixture f;const auto before=f.result();const auto stopCount=stops;failStop=true;
    assert(!f.cmd(Action::IdlePause,1));auto failed=f.s();retained(before,failed);
    assert(stops==stopCount+1 && failed.ready && !failed.resting && !failed.suspended && !f.o.checkpoint_ && f.o.adc_->active && failed.error==ESP_FAIL);
    assert(f.cmd(Action::IdlePause,1));assert(f.cmd(Action::IdleResume));
  }
  for(bool flushFailure:{true,false}){
    Fixture f;const auto before=f.result();assert(f.cmd(Action::IdlePause,1));const auto startCount=starts,flushCount=flushes;
    if(flushFailure)failFlush=true;else failStart=true;
    assert(!f.cmd(Action::IdleResume));auto failed=f.s();retained(before,failed);
    assert(!failed.ready && !failed.resting && !failed.suspended && !f.o.checkpoint_ && !f.o.adc_->active && failed.error==ESP_FAIL);
    assert(!std::strcmp(failed.stateName(),"ADC ERROR") && failed.ringCount==0 && flushes==flushCount+1 && starts==startCount+unsigned(!flushFailure));
    // Driver recovery is lost samples, not a reset: the result and Ready are
    // kept, so a pull right after it is measured instead of swallowed.
    assert(!f.cmd(Action::IdleResume));assert(f.o.recover());
    assert(f.s().phase==D::Phase::Ready && f.s().ready && f.s().valid && f.s().resultRpm==before.resultRpm);
    f.level(1200000);assert(f.s().phase==D::Phase::Ready);
  }
  std::cout<<"PASS: stop/flush/start failures publish consistent ownership; failed flush cannot start ADC; normal driver recovery rearms\n";
  {
    Fixture f;const auto before=f.result();assert(f.cmd(Action::IdlePause,1));const auto stopCount=stops,startCount=starts,flushCount=flushes;
    assert(!f.cmd(Action::ShutdownBegin,0));assert(f.s().resting && f.o.checkpoint_);
    assert(f.cmd(Action::ShutdownBegin,1));auto shutdown=f.s();retained(before,shutdown);
    assert(!shutdown.resting && shutdown.suspended && !shutdown.ready && f.o.checkpoint_ && !std::strcmp(shutdown.stateName(),"EXPORT"));
    assert(stops==stopCount && starts==startCount && flushes==flushCount);assert(!f.cmd(Action::IdleResume));
    // Same rollback command used when sleep is deferred by USB/save/wake errors.
    assert(f.cmd(Action::CheckpointEnd));auto rollback=f.s();retained(before,rollback);
    assert(rollback.ready && rollback.phase==D::Phase::Ready && !rollback.resting && !rollback.suspended && !f.o.checkpoint_);
    assert(starts==startCount+1 && flushes==flushCount+1 && stops==stopCount && rollback.ringCount==0);
  }
  {
    Fixture f;f.result();assert(f.cmd(Action::IdlePause,1));assert(!f.cmd(Action::Pin,0));assert(f.s().resting && f.o.checkpoint_);
    assert(f.cmd(Action::Pin,2));assert(f.s().ready && f.s().pin==2 && f.s().phase==D::Phase::Settling && !f.s().resting && !f.s().suspended && !f.o.checkpoint_);
    f.level(1200000);assert(f.cmd(Action::IdlePause,1));failStart=true;
    assert(!f.cmd(Action::Pin,3));assert(!f.s().ready && !f.s().resting && !f.s().suspended && !f.o.checkpoint_ && !f.o.adc_);
  }
  std::cout<<"PASS: stopped idle owner converts to shutdown without ADC restart; deferred shutdown rollback retains results; valid/failed Pin clears idle/checkpoint flags\n";
  // Exact main-loop helper, with a command-backed owner rather than a policy
  // imitation. Every blocker prevents pause and resumes an existing pause.
  for(unsigned blocker=0;blocker<13;++blocker){
    Fixture f;resetPolicy(f);
    auto block=[&](bool on){switch(blocker){
      case 0:usbAwake.blocked=on;break;case 1:inactivity.begin(uint32_t(clockUs/1000)-30000,on);break;
      case 2:wipeBlocked=on;break;case 3:controls.page=on?Controls::Page::Menu:Controls::Page::Main;break;
      case 4:historyPage=on;break;case 5:diagnostics=on;break;case 6:tournamentMode=on;break;
      case 7:motionPending=on;break;case 8:launchFeedback.visible=on;break;case 9:practiceStore.unsaved=on;break;
      case 10:bestStore.unsaved=on;break;case 11:Serial.input=on;break;case 12:settingsSave.unsaved=on;break;}};
    block(true);assert(!tick().resting && !acquisition.requests && !dirty);
    block(false);assert(tick().resting && acquisition.requests==1 && dirty);
    block(true);assert(!tick().resting && acquisition.requests==2 && sensorIdle.resumes()==1);
  }
  {
    Fixture f;resetPolicy(f);imuAcquisition.available=false;assert(!tick().resting && !acquisition.requests);
    imuAcquisition.available=true;assert(tick().resting);imuAcquisition.available=false;assert(!tick().resting);
  }
  {
    Fixture f;resetPolicy(f);auto state=tick();assert(state.resting);
    freshMotion(MotionActivity::GYRO_OFF);tickSensorIdle(uint32_t(clockUs/1000),state);assert(!state.resting && state.ready && state.phase==D::Phase::Ready);
    clockUs+=1000000;assert(!tick().resting);clockUs+=29000000;assert(tick().resting);
    // A stale/invalid IMU snapshot must also resume conservatively.
    imuAcquisition.state.activity.valid=false;tickSensorIdle(uint32_t(clockUs/1000),state);assert(!state.resting && state.ready);
  }
  {
    Fixture f;resetPolicy(f);Snapshot stale=f.s();freshMotion();
    acquisition.beforeRequest=[](){acquisition.f->level(2000,1100);};
    tickSensorIdle(uint32_t(clockUs/1000),stale);
    assert(!stale.resting && !stale.opticalIdle && acquisition.requests==1 && !sensorIdle.pauses());
    assert(!tick().resting && acquisition.requests==1); // retry bounded.
  }
  std::cout<<"PASS: exact tickSensorIdle with actual owner covers USB/probation/views/unsaved-data/unsaved-settings/Serial/IMU blockers, lower-threshold motion/stale wake,30s re-entry and stale-snapshot rejection\n";
  assert(!handles && !failStop && !failStart && !failFlush);
}
'''

with tempfile.TemporaryDirectory(prefix='launchlab_sensor_idle_') as tmp:
    root = pathlib.Path(tmp)
    cpp = root / 'sensor_idle_pipeline.cpp'
    cpp.write_text(fixture + '\n' + tick + '\n' + tests)
    executable = root / 'sensor_idle_pipeline'
    subprocess.run(['c++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer',
                    '-I' + str(pathlib.Path(__file__).resolve().parent / 'fakes/acquisition_host'),
                    '-I' + str(args.firmware / 'LaunchLabMini'),
                    '-I' + str(args.firmware / 'LaunchLabRpm'),
                    str(cpp), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=30)
