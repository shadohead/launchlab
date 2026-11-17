"""Round-trip the firmware's binary format through the host USB reader."""

import contextlib
import io
import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "scripts"))
from calibration_export import decode, download, fetch, save_download

# Exercise the sketch's real snapshot, NVS checkpoint and USB export paths.
sketch = (root / "firmware/LaunchLabMini/LaunchLabMini.ino").read_text()
helpers = sketch[
    sketch.index("static constexpr const char *CAL_KEY") : sketch.index(
        "LOOP_HELPER static void logAnalysisResults"
    )
]
fixture = (
    r"""
#include "audio_metadata_store.h"
#include "legacy_calibration.h"
#include "mic_trace.h"
#include "settings_save.h"
#include <cstdio>
#include <cassert>

#include <memory>
#include <vector>
#include <cstdlib>
#include "nvs_budget.h"
#define LOOP_HELPER
constexpr unsigned MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
static size_t liveBytes=0,peakBytes=0,lastAllocation=0;
void *heap_caps_malloc(size_t n,unsigned){
 auto *block=static_cast<size_t*>(std::malloc(sizeof(size_t)+n+16));if(!block)return nullptr;
 *block=n;auto *data=reinterpret_cast<uint8_t*>(block+1);std::memset(data+n,0xa5,16);
 liveBytes+=n;peakBytes=std::max(peakBytes,liveBytes);lastAllocation=n;return data;
}
void heap_caps_free(void *p){
 if(!p)return;auto *block=static_cast<size_t*>(p)-1;auto *data=static_cast<uint8_t*>(p);
 for(unsigned i=0;i<16;++i)assert(data[*block+i]==0xa5);
 liveBytes-=*block;std::free(block);
}
struct {uint8_t app_elf_sha256[32]={1};} app;
auto esp_app_get_description(){return &app;}
unsigned millis(){return 0;}
uint64_t esp_timer_get_time(){return 7000000;}
struct {bool live=true;uint32_t ms=82000;uint32_t number(){return 9;}bool running(){return live;}
 float rpm(){return 4300;}bool hasManualStop(){return true;}unsigned stopReason(){return 1;}
 uint64_t elapsedUs(uint64_t t){return live?t:uint64_t(ms)*1000;}} analysis;
SettingsSave settingsSave;
bool saveError=false,dirty=false;
static uint16_t slots[3000]{},raw[3000]{};static uint8_t recovery[375]{},confidence[3000]{};
static struct { MicTrace::Trace readings{slots,3000,raw,recovery,confidence};bool ready=true;bool warm(unsigned){return ready;}bool on(){return true;}bool drained=false;
 void drainTrace(){drained=true;}void freezeTrace(unsigned elapsed=UINT32_MAX){assert(drained);readings.freeze(elapsed);}
 AudioMetadata::Recorder audio;const AudioMetadata::Recorder *audioMetadata(uint32_t n){return n==audio.launch()?&audio:nullptr;}
 auto trace(){return &readings;}}micListener;
struct PracticeHistory{enum{IMAGE_BYTES=2600};};
static unsigned available=1000;
bool nvsAvailable(unsigned &v){v=available;return true;}
static Decay::Session input;static Decay::Results fit;static Decay::Curve curve;
struct {EarlySpin::Forecast snapshot{};EarlySpin::Forecast forecast(unsigned){return snapshot;}auto results(){return &fit;}auto input(){return &::input;}
 bool matches(decltype(analysis)&){return true;}auto winner(){return &curve;}}analysisResults;
struct {std::vector<uint8_t> stored;
 size_t getBytesLength(const char *k){return !strcmp(k,"analysis_cal")?stored.size():0;}
 size_t putBytes(const char*,const void *p,size_t n){auto b=(const uint8_t*)p;stored.assign(b,b+n);return n;}
 size_t getBytes(const char*,void *p,size_t n){if(n!=stored.size())return 0;memcpy(p,stored.data(),n);return n;}
}prefs;
struct Flash {
 std::vector<uint8_t> bytes=std::vector<uint8_t>(FlashLayout::PARTITION_BYTES,0xff);
 bool failCommit=false,failFit=false,failAudio=false;unsigned failAudioSequence=0;
 bool read(unsigned at,void *out,size_t n){if(at+n>bytes.size())return false;memcpy(out,&bytes[at],n);return true;}
 bool erase(unsigned at,size_t n){assert(at%4096==0 && n%4096==0);std::fill_n(&bytes.at(at),n,0xff);return true;}
 bool write(unsigned at,const void *p,size_t n){
   if((failCommit && n==16)||(failFit && n==4)||(failAudio && n==24))return false;
   if(n==24 && failAudioSequence && FlashLayout::get32(static_cast<const uint8_t*>(p)+4)==failAudioSequence)return false;
   for(size_t i=0;i<n;++i){auto x=((const uint8_t*)p)[i];assert((bytes.at(at+i)&x)==x);bytes[at+i]=x;}return true;}
} spiffsFlash;
struct {std::vector<uint8_t> payload;void println(const char*){}template<class...T>void printf(const char*s,T...){if(!strncmp(s,"CAL_DATA ",9))payload.clear();}
 size_t write(const uint8_t*p,size_t n){payload.insert(payload.end(),p,p+n);return n;}}Serial;
"""
    + helpers
    + r"""
int main(int argc,char **argv){
 input.launcherRpm=4300;input.hasStop=true;input.stopMs=82000;input.count=3;
 input.samples[0]={100,4300,false,false};input.samples[1]={500,7200,true,true};
 input.samples[2]={43100,1800,false,false};
 uint8_t sha[32]={1};Calibration::Preview prev;prev.atMs=7000;prev.count=2;prev.model=1;
 prev.kind=2;prev.lo=78;prev.hi=84;prev.stops[0]=82;
 Decay::Results result;result.launcherRpm=4300;result.actualStopValid=true;result.actualStop=82;
 result.sourceCount=3;result.sampleCount=2;result.best=-1;
 uint8_t bytes[1024];size_t n=Calibration::Legacy::pack(bytes,sizeof(bytes),9,sha,input,result,prev);
 assert(n>0 && !Calibration::Legacy::pack(bytes,10,9,sha,input,result,prev));

 // Stop before a final job even exists. The raw packet must survive reuse of the trace.
 input.count=2;fit.best=-1;captureCalibration(false);
 assert(!latestCalibration);analysis.live=false;
 auto &trace=micListener.readings;
 trace.begin(9,4300);micListener.audio.reset(9);
 AudioMetadata::Measurement acoustic{};acoustic[2]=90;acoustic[3]=80;acoustic[6]=3;acoustic[8]=77;
 micListener.audio.add(400,650,acoustic);
 for(unsigned i=0;i<3;++i){
   MicTrace::Step s;s.launch=9;s.index=2*i;s.rawRpm=input.samples[i].rpm;
   s.frame=true;s.reason=i==0?MicRpm::Reason::Acquiring:MicRpm::Reason::Tracking;s.recovered=i==0;s.rpm=i==1?0:s.rawRpm;s.rejectedRpm=i==1?s.rawRpm:0;trace.add(s);
   input.samples[i].ms=uint32_t(std::lround(trace.seconds(s.index)*1000));
 }
 analysisResults.snapshot={10000,2,112.5f};
 observeForecastCheckpoint(9999);assert(shadowLog.next==0);
 observeForecastCheckpoint(10000);assert(shadowLog.next==1);
 observeForecastCheckpoint(15000);assert(shadowLog.next==1); // No new forecast yet.
 analysisResults.snapshot={15001,3,88.25f};observeForecastCheckpoint(15001);
 analysisResults.snapshot={30000,3,95.75f};observeForecastCheckpoint(30000);
 captureStoppedCalibration();
 auto *captured=latestCalibration;assert(captureStoppedCalibration() && latestCalibration==captured);
 assert(shadowLog.next==3);shadowLog.reset();assert(shadowLog.next==0 && !shadowLog.rows[0].observedMs);
 assert(settingsSave.pending() && latestCalibration && !latestCalibration->fit);
 auto layout=FlashLayout::historyLayout(1);FlashLayout::Superblock::Image image;layout.encode(image);
 spiffsFlash.write(0,image.data(),image.size());
 spiffsFlash.failCommit=true;assert(!saveCalibration() && latestCalibration->pending());
 spiffsFlash.failCommit=false;assert(saveCalibration() && !latestCalibration->pending());
 auto first=latestCalibration->stored;
 Serial.payload.clear();assert(sendStoredCalibration(first));auto rawOnly=Serial.payload;
 auto dump=[](const char *path,const std::vector<uint8_t>&data){
   FILE *f=fopen(path,"wb");assert(f && fwrite(data.data(),1,data.size(),f)==data.size());fclose(f);
 };
 dump(argv[2],rawOnly);
 input.count=3;fit=result;captureCalibration(true);assert(latestCalibration->pending());
 spiffsFlash.failFit=true;assert(!saveCalibration());
 auto *pending=firstCalibration;firstCalibration=nullptr; // simulate a reboot after the body, before its marker
 assert(sendStoredCalibration(first) && Serial.payload==rawOnly);
 firstCalibration=pending;spiffsFlash.failFit=false;
 assert(saveCalibration() && !latestCalibration->pending());
 Serial.payload.clear();assert(sendStoredCalibration(first));auto enriched=Serial.payload;
 dump(argv[3],enriched);
 // After reboot, a damaged optional fit must not poison the intact raw trace.
 // A damaged raw trace itself must produce an error rather than exported data.
 pending=firstCalibration;firstCalibration=nullptr;
 const auto damagedFitAt=Calibration::Archive::fitOffset(first)+100;
 spiffsFlash.bytes[damagedFitAt]^=1;
 Serial.payload.clear();assert(sendStoredCalibration(first) && Serial.payload==rawOnly);
 spiffsFlash.bytes[damagedFitAt]^=1;
 const auto damagedRawAt=first.offset+Calibration::Archive::HEADER_BYTES+24;
 spiffsFlash.bytes[damagedRawAt]^=1;
 Serial.payload.clear();assert(!sendStoredCalibration(first) && Serial.payload.empty());
 spiffsFlash.bytes[damagedRawAt]^=1;firstCalibration=pending;
 Calibration::Archive::Index index;assert(Calibration::Archive::scan(spiffsFlash,index) && index.count==1);
 // Two stops with flash unavailable and no completed fits. Mutate the source
 // immediately. Once writes succeed, both frozen packets must still be present.
 analysisResults.snapshot={81000,2,81.5f};
 analysis.ms=83000;capturedAnalysisLaunch=0;captureStoppedCalibration();spiffsFlash.failCommit=true;assert(!saveCalibration());
 trace.begin(10,5000);analysisResults.snapshot={};analysis.ms=84000;capturedAnalysisLaunch=0;captureStoppedCalibration();
 assert(firstCalibration!=latestCalibration);
 spiffsFlash.failCommit=false;assert(saveCalibration());
 assert(Calibration::Archive::scan(spiffsFlash,index) && index.count==3);
 Serial.payload.clear();assert(sendStoredCalibration(index.entries[1]));
 assert(FlashLayout::get32(Serial.payload.data()+12)==83000 && FlashLayout::get32(Serial.payload.data()+20)==3);
 assert(FlashLayout::get32(Serial.payload.data()+32)==8 && FlashLayout::get32(Serial.payload.data()+28)==2);
 Serial.payload.clear();assert(sendStoredCalibration(index.entries[2]));
 assert(FlashLayout::get32(Serial.payload.data()+32)==0);
 assert(FlashLayout::get32(Serial.payload.data()+12)==84000 && FlashLayout::get32(Serial.payload.data()+20)==0);
 for(unsigned i=0;i<38;++i){analysis.ms++;capturedAnalysisLaunch=0;captureStoppedCalibration();assert(saveCalibration());}
 assert(Calibration::Archive::scan(spiffsFlash,index) && index.count==33);
 assert(index.entries[index.count-32].sequence==10 && index.entries[index.count-1].sequence==41);
 exportCalibrationBatch();assert(Serial.payload.size()==latestCalibration->raw+latestCalibration->audio);
 AudioMetadataStore::Index clips;assert(AudioMetadataStore::scan(spiffsFlash,clips) && clips.count==33);
 assert(clips.entries[1].sequence==10 && clips.entries[32].sequence==41);
 std::vector<uint8_t> acousticBytes(AudioMetadata::MAX_BYTES);size_t acousticSize=0;
 assert(AudioMetadataStore::read(spiffsFlash,41,FlashLayout::get32(latestCalibration->data()+latestCalibration->raw-4),acousticBytes.data(),acousticSize));
 assert(acousticSize==latestCalibration->audio && AudioMetadata::valid(acousticBytes.data(),acousticSize));
 // Interrupt a metadata commit after the new calibration is already durable.
 struct CutFlash {
   Flash &f;bool read(unsigned at,void *p,size_t n){return f.read(at,p,n);}
   bool erase(unsigned at,size_t n){return f.erase(at,n);}
   bool write(unsigned at,const void *p,size_t n){return n!=AudioMetadataStore::HEADER && f.write(at,p,n);}
 } cut{spiffsFlash};
 assert(!AudioMetadataStore::save(cut,42,123,acousticBytes.data(),acousticSize));
 assert(AudioMetadataStore::scan(spiffsFlash,clips) && clips.count==32 && clips.entries[31].sequence==41);
 // Once the ring is full, an interrupted raw commit must preserve all of
 // the latest 32 pulls and reject a late attachment to a reused slot.
 const auto beforeCut=spiffsFlash.bytes;
 spiffsFlash.failCommit=true;assert(!Calibration::Archive::append(spiffsFlash,bytes,n));
 spiffsFlash.failCommit=false;
 assert(Calibration::Archive::scan(spiffsFlash,index) && index.count==32);
 assert(index.entries[0].sequence==10 && index.entries[31].sequence==41);
 auto unchanged=spiffsFlash.bytes;
 assert(!Calibration::Archive::attach(spiffsFlash,first,enriched.data()+first.size,Calibration::fitBytes(3)));
 assert(spiffsFlash.bytes==unchanged);
 spiffsFlash.bytes=beforeCut;
 // Refuse an occupied region without changing flash.
 layout.sequence=100;layout.regions[layout.count++]={FlashLayout::Pulls,0,FlashLayout::PULLS_OFFSET,FlashLayout::PULLS_BYTES};
 layout.encode(image);spiffsFlash.erase(0,4096);spiffsFlash.write(0,image.data(),image.size());
 const auto original=spiffsFlash.bytes;assert(!Calibration::Archive::append(spiffsFlash,bytes,n));
 assert(spiffsFlash.bytes==original);
 FILE *f=fopen(argv[1],"wb");assert(f);assert(fwrite(bytes,1,n,f)==n);fclose(f);

 // Preserve original, confirmed acquisition and three pitch confidence levels
 // through the real trace, raw archive and independently attached final fit.
 trace.begin(11,4300);input.count=5;
 for(unsigned i=0;i<5;++i){
   MicTrace::Step s;s.launch=11;s.index=uint16_t(2*i);s.rawRpm=uint16_t(4000-100*i);
   s.recovered=i>0;s.pitchConfidence=i<2?0:i==2?1:i==3?127:255;
   s.rpm=i==3?0:s.rawRpm;s.rejectedRpm=i==3?s.rawRpm:0;trace.add(s);
   input.samples[i]={uint32_t(std::lround(trace.seconds(s.index)*1000)),float(s.rawRpm),i==3,i==3,s.recovered,s.pitchConfidence};
 }
 assert(trace.earlyReadings()==1);
 std::vector<uint8_t> pitch(Calibration::rawCapacity(5)+Calibration::fitBytes(5));
 ForecastShadow::Log emptyLog;
 const auto pitchRaw=Calibration::packRaw(pitch.data(),11,4300,82000,true,sha,{},&trace,emptyLog);
 result.sourceCount=5;result.sampleCount=4;
 Calibration::packFit(pitch.data()+pitchRaw,input,result);
 pitch.resize(pitchRaw+Calibration::fitBytes(5));dump(argv[4],pitch);

 // Full five-minute pitch recovery still fits the unchanged 16 KiB archive
 // slot with the final-fit attachment. Sparse late points exercise four-byte
 // deltas and must fit the count-based RAM allocation too.
 std::vector<uint16_t> denseSlots(Decay::MAX_SAMPLES),denseRaw(Decay::MAX_SAMPLES);
 std::vector<uint8_t> denseRecovery((Decay::MAX_SAMPLES+7)/8),denseConfidence(Decay::MAX_SAMPLES);
 MicTrace::Trace dense(denseSlots.data(),Decay::MAX_SAMPLES,denseRaw.data(),denseRecovery.data(),denseConfidence.data());
 dense.begin(12,4300);
 for(unsigned i=0;i<Decay::MAX_SAMPLES;++i){
   MicTrace::Step s;s.launch=12;s.index=uint16_t(i);s.rawRpm=s.rpm=2000;s.recovered=true;s.pitchConfidence=255;dense.add(s);
 }
 std::vector<uint8_t> packed(Calibration::rawCapacity(Decay::MAX_SAMPLES));
 const auto denseSize=Calibration::packRaw(packed.data(),12,4300,300000,true,sha,{},&dense,emptyLog);
 assert(denseSize==Calibration::MAX_RAW_BYTES && denseSize<=packed.size());
 Flash maxFlash;auto maxLayout=FlashLayout::historyLayout(1);maxLayout.encode(image);
 assert(maxFlash.write(0,image.data(),image.size()));Calibration::Archive::Entry stored;
 assert(Calibration::Archive::append(maxFlash,packed.data(),denseSize,&stored));
 input.count=result.sourceCount=result.sampleCount=Decay::MAX_SAMPLES;
 std::vector<uint8_t> denseFit(Calibration::fitBytes(input.count));Calibration::packFit(denseFit.data(),input,result);
 assert(Calibration::Archive::attach(maxFlash,stored,denseFit.data(),denseFit.size()));
 assert(Calibration::validFit(denseFit.data(),denseFit.size(),input.count));
 assert(!Calibration::validFit(denseFit.data(),denseFit.size()-1,input.count));
 assert(!Calibration::Archive::attach(maxFlash,stored,denseFit.data(),3));
 auto corruptedFit=denseFit;corruptedFit[100]^=1;
 assert(!Calibration::validFit(corruptedFit.data(),corruptedFit.size(),input.count));
 packed.resize(denseSize);dump(argv[5],packed);

 // Drivers may complete a program and then report failure. Retrying either
 // archive must recognize that commit rather than consume a duplicate slot.
 Flash ambiguous;maxLayout.encode(image);assert(ambiguous.write(0,image.data(),image.size()));
 struct AfterWrite {
   Flash &f;size_t failSize=0,corruptSize=0;
   bool read(unsigned at,void *p,size_t n){return f.read(at,p,n);}
   bool erase(unsigned at,size_t n){return f.erase(at,n);}
   bool write(unsigned at,const void *p,size_t n){
     if(!f.write(at,p,n))return false;
     if(n==corruptSize){f.bytes[at+20]^=1;corruptSize=0;}
     if(n==failSize){failSize=0;return false;}return true;
   }
 } uncertain{ambiguous};
 Calibration::Archive::Entry once{};
 uncertain.failSize=Calibration::Archive::HEADER_BYTES;
 assert(!Calibration::Archive::append(uncertain,packed.data(),packed.size(),&once) && !once.size);
 assert(Calibration::Archive::append(uncertain,packed.data(),packed.size(),&once));
 assert(Calibration::Archive::scan(ambiguous,index) && index.count==1 && once.sequence==1);
 uncertain.failSize=4;
 assert(!Calibration::Archive::attach(uncertain,once,denseFit.data(),denseFit.size()));
 assert(Calibration::Archive::attach(uncertain,once,denseFit.data(),denseFit.size()));
 uncertain.failSize=AudioMetadataStore::HEADER;
 assert(!AudioMetadataStore::save(uncertain,once.sequence,123,acousticBytes.data(),acousticSize));
 assert(AudioMetadataStore::save(uncertain,once.sequence,123,acousticBytes.data(),acousticSize));
 assert(AudioMetadataStore::scan(ambiguous,clips) && clips.count==1);
 auto durable=ambiguous.bytes;
 assert(!AudioMetadataStore::save(uncertain,once.sequence,124,acousticBytes.data(),acousticSize) && ambiguous.bytes==durable);
 // A successful driver status with changed bytes must never publish a raw
 // commit marker. Reusing that uncommitted slot remains safe.
 uncertain.corruptSize=n;
 assert(!Calibration::Archive::append(uncertain,bytes,n));
 assert(Calibration::Archive::scan(ambiguous,index) && index.count==1);
 assert(Calibration::Archive::append(uncertain,bytes,n));
 assert(Calibration::Archive::scan(ambiguous,index) && index.count==2);
 dense.begin(13,4300);
 MicTrace::Step late;late.launch=13;late.index=Decay::MAX_SAMPLES-1;late.rawRpm=late.rpm=1800;late.recovered=true;late.pitchConfidence=127;dense.add(late);
 packed.assign(Calibration::rawCapacity(1),0);
 const auto sparseSize=Calibration::packRaw(packed.data(),13,4300,300000,true,sha,{},&dense,emptyLog);
 assert(sparseSize<=packed.size());packed.resize(sparseSize);dump(argv[6],packed);

 // A persistently failing older metadata attachment must not keep subsequent raw traces in
 // RAM. Both are durable even while their optional audio retries are pending.
 auto resetCapture=[&](){
   while(firstCalibration){auto *old=firstCalibration;firstCalibration=old->next;old->~CalibrationPacket();heap_caps_free(old);}
   latestCalibration=nullptr;capturedAnalysisLaunch=0;
   spiffsFlash=Flash{};maxLayout.encode(image);assert(spiffsFlash.write(0,image.data(),image.size()));
   trace.begin(9,4300);
 };
 resetCapture();analysis.ms=90000;
 assert(captureStoppedCalibration());spiffsFlash.failAudio=true;
 assert(!saveCalibration() && latestCalibration->stored.size);
 ++analysis.ms;capturedAnalysisLaunch=0;assert(captureStoppedCalibration());
 assert(!saveCalibration() && firstCalibration!=latestCalibration);
 assert(Calibration::Archive::scan(spiffsFlash,index) && index.count==2);
 assert(firstCalibration->stored.size && latestCalibration->stored.size);
 spiffsFlash.failAudio=false;assert(saveCalibration() && !calibrationNeedsSave());

 // An older retry keeps only its own packet, not completed packets behind it.
 resetCapture();spiffsFlash.failAudioSequence=1;
 assert(captureStoppedCalibration() && !saveCalibration());
 const auto shortRoom=lastAllocation;
 assert(shortRoom==sizeof(CalibrationPacket)+Calibration::rawCapacity(0)+Calibration::fitBytes(0)+AudioMetadata::packedBytes(analysis.ms));
 assert(shortRoom<sizeof(CalibrationPacket)+Calibration::rawCapacity(0)+Calibration::fitBytes(0)+AudioMetadata::MAX_BYTES);
 for(unsigned i=0;i<12;++i){
   ++analysis.ms;capturedAnalysisLaunch=0;
   assert(captureStoppedCalibration() && !saveCalibration());
   assert(firstCalibration!=latestCalibration && firstCalibration->next==latestCalibration && !latestCalibration->next);
   assert(firstCalibration->pending() && !latestCalibration->pending());
   assert(liveBytes<=2*(shortRoom+AudioMetadata::RECORD_BYTES));
 }
 spiffsFlash.failAudioSequence=0;
 assert(saveCalibration() && !calibrationNeedsSave() && firstCalibration==latestCalibration);

 // A long flash outage can queue more than one complete ring. Once the newest
 // 32 raw traces are committed, expired optional attachments must drain too.
 resetCapture();input.count=fit.sourceCount=fit.sampleCount=0;
 for(unsigned i=0;i<40;++i){
   ++analysis.ms;input.stopMs=analysis.ms;capturedAnalysisLaunch=0;
   assert(captureStoppedCalibration());captureCalibration(true);assert(latestCalibration->fit);
 }
 assert(saveCalibration() && !calibrationNeedsSave());
 assert(Calibration::Archive::scan(spiffsFlash,index) && index.count==33);
 assert(index.entries[1].sequence==9 && index.entries[32].sequence==40);
 assert(firstCalibration==latestCalibration);
 // Exercise the allocation bounds with a full five-minute trace and metadata.
 resetCapture();analysis.ms=300000;trace.begin(9,4300);
 for(unsigned i=0;i<3000;++i){MicTrace::Step s;s.launch=9;s.index=i;s.rawRpm=s.rpm=4300;s.recovered=true;s.pitchConfidence=127;trace.add(s);}
 assert(captureStoppedCalibration() && latestCalibration->audio==AudioMetadata::MAX_BYTES);
 resetCapture();assert(liveBytes==0); // Free validates every allocation's canary.
}
"""
)


class Port:
    def __init__(self, data):
        self.data = io.BytesIO(data)
        self.sent = []

    def readline(self):
        return self.data.readline()

    def read(self, n):
        return self.data.read(min(13, n))

    def write(self, data):
        self.sent.append(data)


with tempfile.TemporaryDirectory() as tmp:
    folder = Path(tmp)
    cpp = folder / "fixture.cpp"
    exe = folder / "fixture"
    out = folder / "packet.bin"
    cpp.write_text(fixture)
    subprocess.run(
        [
            os.environ.get("CXX", "c++"),
            "-std=c++17",
            "-I" + str(root / "firmware/LaunchLabMini"),
            "-I" + str(root / "firmware/tests"),
            str(cpp),
            "-o",
            str(exe),
        ],
        check=True,
    )
    raw_path, fit_path = folder / "raw.bin", folder / "fit.bin"
    pitch_path, dense_path, sparse_path = (folder / name for name in ("pitch.bin", "dense.bin", "sparse.bin"))
    subprocess.run([str(exe), str(out), str(raw_path), str(fit_path), str(pitch_path), str(dense_path), str(sparse_path)], check=True)
    raw_blob, fit_blob = raw_path.read_bytes(), fit_path.read_bytes()

    def framed(blob):
        return fetch(Port(f"CAL_DATA {len(blob)} flash\n".encode() + blob))

    raw_run, fit_run = framed(raw_blob), framed(fit_blob)
    audio = raw_run["audio_metadata"]
    assert audio == fit_run["audio_metadata"] and audio["collector_available"]
    assert len(audio["records"]) == 202 and audio["duration_ms"] == 82000
    assert audio["records"][0]["missing"]
    assert audio["records"][1]["broadband_frames"] == 1
    assert audio["records"][1]["available_by_ms"] == 660
    assert audio["records"][1]["band_peak_db"][0] == -51
    # Keep legacy-format coverage below independent of the new attachment.
    raw_blob = raw_blob[:raw_blob.index(b"LAF1")]
    fit_blob = fit_blob[:fit_blob.index(b"LAF1")]
    assert framed(raw_blob)["audio_metadata"] is None
    checkpoints = raw_run["forecast_checkpoints"]
    assert [s["checkpoint_s"] for s in checkpoints] == [10, 15, 30]
    assert [s["observed_at_ms"] for s in checkpoints] == [10000, 15001, 30000]
    assert [s["source"] for s in checkpoints] == ["Stamina predictor"] * 3
    assert [s["displayed_finish_s"] for s in checkpoints] == [112.5, 88.25, 95.75]
    assert raw_run["latest_preview"]["at_ms"] == 30000
    assert raw_run["latest_preview"]["finish_s"] == [95.75, 95.75]
    assert fit_run["forecast_checkpoints"] == checkpoints

    assert raw_run["final_fit"] is None and raw_run["observed_stop_ms"] == 82000
    assert [p["raw_rpm"] for p in raw_run["samples"]] == [4300, 7200, 1800]
    assert raw_run["samples"][0]["recovered_acquisition"]
    assert (
        raw_run["mic_frame_counts"]["acquiring"] == 1
        and raw_run["mic_frame_counts"]["tracking"] == 2
    )
    assert raw_run["latest_preview"]["winner"] == "Stamina predictor"
    import struct
    import zlib

    pitch_blob = pitch_path.read_bytes()
    pitch_run = framed(pitch_blob)
    assert pitch_blob[:4] == raw_blob[:4] == b"LLCA"
    assert [p["relative_pitch_confidence"] for p in pitch_run["samples"]] == [0, 0, 1, 127, 255]
    assert [p["relative_pitch_recovered"] for p in pitch_run["samples"]] == [False, False, True, True, True]
    assert [p["confirmed_acquisition_recovered"] for p in pitch_run["samples"]] == [False, True, False, False, False]
    # The original flag remains generic so existing count-based tools exclude
    # both recovery kinds instead of treating pitch observations as fresh locks.
    assert [p["recovered_acquisition"] for p in pitch_run["samples"]] == [False, True, True, True, True]
    assert pitch_run["samples"][3]["live_filter_rejected"] and pitch_run["samples"][3]["final_fit_excluded"]
    dense_run, sparse_run = framed(dense_path.read_bytes()), framed(sparse_path.read_bytes())
    assert len(dense_run["samples"]) == 3000
    assert all(p["relative_pitch_confidence"] == 255 for p in dense_run["samples"])
    assert sparse_run["samples"][0]["time_ms"] == dense_run["samples"][-1]["time_ms"]
    assert sparse_run["samples"][0]["relative_pitch_confidence"] == 127

    def crc_packet(payload):
        return bytes(payload) + struct.pack("<I", zlib.crc32(payload))

    def observation_bytes(samples, bits):
        payload = bytearray()
        previous = 0
        for sample in samples:
            ms = sample["time_ms"]
            delta = ((ms - previous) << bits) | int(sample["live_filter_rejected"])
            if bits == 3:
                delta |= int(sample["recovered_acquisition"]) << 2
            previous = ms
            while delta >= 128:
                payload.append((delta & 127) | 128)
                delta >>= 7
            payload.append(delta)
            payload.extend(struct.pack("<H", sample["raw_rpm"]))
        return payload

    # A valid checksum cannot make contradictory pitch provenance valid.
    pitch_raw_size = len(pitch_blob) - (520 + 1)
    bad_confidence = bytearray(pitch_blob[:pitch_raw_size - 4])
    bad_confidence[108] = 0  # Third observation's optional confidence byte.
    bad_generic = bytearray(pitch_blob[:pitch_raw_size - 4])
    bad_generic[104] &= ~4   # Pitch flag without generic recovery.
    unsupported = bytearray(raw_blob[:-4])
    unsupported[:4] = b"LLCB"
    for invalid in (
        crc_packet(bad_confidence), crc_packet(bad_generic), crc_packet(unsupported),
        pitch_blob[:-1], pitch_blob[:108],
        pitch_blob[:108] + bytes([pitch_blob[108] ^ 1]) + pitch_blob[109:],
    ):
        try:
            decode(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid pitch packet accepted")

    # Reconstruct a legacy packet, including the historical shadow slots.
    slots = b"".join((
        struct.pack("<IIfffHH", 10000, 6, 80., 77., float("nan"), 0, 0),
        struct.pack("<IIfffHH", 15001, 5, 88.25, float("nan"), 55., 0, 1),
        struct.pack("<IIfffHH", 30000, 7, 94.131, float("nan"), float("nan"), 0, 0),
    ))
    prior = bytearray(raw_blob[:-40] + slots)
    prior[:4] = b"LLC9"
    struct.pack_into("<I", prior, 32, 7)
    prior = crc_packet(prior)
    legacy_checks = decode(prior)["forecast_checkpoints"]
    assert [s["source"] for s in legacy_checks] == ["Early mic estimate", "Fixed Riccati", "Population estimate"]
    assert legacy_checks[0]["corrected_finish_s"] == 77.
    assert legacy_checks[1]["riccati_limited_finish_s"] == 55.
    for reason, name in ((0, "timeout"), (1, "manual"), (2, "interrupted")):
        ended = bytearray(raw_blob[:-4])
        struct.pack_into("<I", ended, 16, reason)
        decoded = decode(crc_packet(ended))
        assert decoded["stop_reason"] == name
        assert decoded["observed_stop_ms"] == (82000 if reason == 1 else None)
    older_pitch = bytearray(prior[:-4])
    older_pitch[:4] = b"LLC8"
    assert decode(crc_packet(older_pitch))["samples"] == raw_run["samples"]
    struct.pack_into("<I", older_pitch, 16, 2)
    try:
        decode(crc_packet(older_pitch))
    except ValueError:
        pass
    else:
        raise AssertionError("LLC8 accepted an LLC9 endpoint reason")
    # Older packets keep their original flag layouts and checkpoint payloads.
    prefix = bytearray(prior[:96]) + observation_bytes(raw_run["samples"], 3) + prior[-102:-76]
    legacy4 = bytearray(prefix + b"".join(slots[i:i+16] for i in range(0,72,24)))
    legacy4[:4] = b"LLC4"
    legacy4.extend(struct.pack("<I", zlib.crc32(legacy4)))
    old = decode(bytes(legacy4) + fit_blob[len(raw_blob):])
    assert old["final_fit"] and old["forecast_checkpoints"] == [
        {k: v for k, v in row.items() if not k.startswith("riccati_")} for row in legacy_checks
    ]
    legacy = bytearray(prefix)
    legacy[:4] = b"LLC3"
    legacy.extend(struct.pack("<I", zlib.crc32(legacy)))
    assert "forecast_checkpoints" not in decode(bytes(legacy))
    for version in (5, 6, 7):
        upgraded = bytearray(prefix)
        upgraded[:4] = f"LLC{version}".encode()
        for index in range(3):
            upgraded.extend(slots[index*24:(index+1)*24])
            if version >= 6:
                upgraded.extend(struct.pack("<f", 80 + index))
            if version >= 7:
                upgraded.extend(struct.pack("<f", 90 + index))
        old = decode(crc_packet(upgraded) + fit_blob[len(raw_blob):])
        assert old["samples"] == [
            {k: v for k, v in sample.items() if k not in (
                "confirmed_acquisition_recovered", "relative_pitch_recovered", "relative_pitch_confidence"
            )} for sample in fit_run["samples"]
        ]
        for index, checkpoint in enumerate(old["forecast_checkpoints"]):
            if version >= 6:
                assert checkpoint["ml_candidate_finish_s"] == 80 + index
            if version >= 7:
                assert checkpoint["ml_v1_1_finish_s"] == 90 + index
    # LLC2 predates frame diagnostics and uses two flag bits in each time delta.
    legacy2 = bytearray(raw_blob[:96])
    legacy2[:4] = b"LLC2"
    legacy2[24:64] = bytes(40)  # No published preview on this older capture.
    previous = 0
    for sample in raw_run["samples"]:
        ms = sample["time_ms"]
        delta = ((ms - previous) << 2) | int(sample["live_filter_rejected"])
        previous = ms
        while delta >= 128:
            legacy2.append((delta & 127) | 128)
            delta >>= 7
        legacy2.append(delta)
        legacy2.extend(struct.pack("<H", sample["raw_rpm"]))
    legacy2.extend(struct.pack("<I", zlib.crc32(legacy2)))
    old2 = decode(bytes(legacy2) + fit_blob[len(raw_blob):])
    assert old2["final_fit"] == fit_run["final_fit"]
    assert [s["time_ms"] for s in old2["samples"]] == [s["time_ms"] for s in raw_run["samples"]]
    assert old2["samples"][1]["live_filter_rejected"] and "mic_frame_counts" not in old2
    assert decode(bytes(prior))["latest_preview"]["winner"] == "Population estimate"

    assert raw_run["samples"][1]["live_filter_rejected"]
    assert all(p["final_fit_excluded"] is None for p in raw_run["samples"])
    assert (
        fit_run["samples"][1]["final_fit_excluded"]
        and fit_run["final_fit"]["retained_count"] == 2
    )
    assert raw_run["trace_sha256"] == fit_run["trace_sha256"]
    saved = folder / "export"
    for records, counts in (
        ([], (0, 0)),
        ([raw_run], (1, 1)),
        ([fit_run], (1, 0)),
        ([raw_run], (1, 0)),
    ):
        with contextlib.redirect_stdout(io.StringIO()) as log:
            save_download(records, saved)
        assert (
            f"Pulls returned by Stick: {counts[0]} | Newly saved: {counts[1]}"
            in log.getvalue()
        )
    assert len(list(saved.glob("*.json"))) == 2
    assert (
        __import__("json").loads((saved / "latest.json").read_text())["final_fit"]
        is not None
    )

    blob = out.read_bytes()
    port = Port(b"log line\n" + f"CAL_DATA {len(blob)} ram\n".encode() + blob)
    data = fetch(port)
    assert port.sent == [b"J\n"] and data["observed_stop_ms"] == 82000
    assert data["latest_preview"]["finish_s"] == [78, 84]
    assert [s["time_ms"] for s in data["samples"]] == [100, 500, 43100]
    assert data["samples"][1] == {
        "time_ms": 500,
        "raw_rpm": 7200,
        "live_filter_rejected": True,
        "final_fit_excluded": True,
    }
    assert data["final_fit"]["retained_count"] == 2
    # Stored model numbers retain their original meanings across the Riccati-only firmware change.
    from calibration_export import decode_fit

    for index, name in enumerate(("Coulomb-Viscous", "Coulomb-Quadratic", "Riccati", "Power-Law Damping")):
        record = bytearray(blob[96:608])
        struct.pack_into("<I", record, 8, index)
        struct.pack_into("<I", record, 52, index + 1)
        struct.pack_into("<I", record, 508, zlib.crc32(record[:508]))
        assert decode_fit(bytes(record))["winner"] == name
        assert not decode_fit(bytes(record))["endpoint_estimate"]
        if index == 2:
            record[36] |= 32
            struct.pack_into("<I", record, 508, zlib.crc32(record[:508]))
            assert decode_fit(bytes(record))["winner"] == "Fixed Riccati"
            struct.pack_into("<I", record, 464, 1)
            struct.pack_into("<I", record, 508, zlib.crc32(record[:508]))
            assert decode_fit(bytes(record))["endpoint_estimate"]
    # Preview id 5 uses the Riccati stop slot. The embedded final record still
    # uses the original four-model mapping, including free Riccati.
    import struct
    import zlib

    fixed = bytearray(blob)
    struct.pack_into("<IIff", fixed, 32, 5, 1, 82, 82)
    struct.pack_into("<4f", fixed, 48, 0, 0, 82, 0)
    struct.pack_into("<I", fixed, len(fixed) - 4, zlib.crc32(fixed[:-4]))
    updated = decode(bytes(fixed))
    assert updated["latest_preview"]["winner"] == "Fixed Riccati"
    assert updated["latest_preview"]["candidate_finish_s"] == {"Fixed Riccati": 82}
    assert updated["final_fit"] == data["final_fit"]
    frame = f"CAL_DATA {len(blob)} flash\n".encode() + blob
    batch = Port(b"CAL_HISTORY\n" + frame + b"CAL_COUNT 2\n" + frame * 2 + b"CAL_END\n")
    assert len(list(download(batch))) == 2 and batch.sent == [b"J\n", b"Z\n"]
    try:
        list(
            download(
                Port(b"CAL_HISTORY\n" + frame + b"CAL_COUNT 2\n" + frame + b"CAL_END\n")
            )
        )
    except ValueError:
        pass
    else:
        raise AssertionError("short batch accepted")
    old = fetch(Port(b"CAL_FIT 512 flash\n" + blob[96:608]))
    assert (
        old["schema"] == "launchlab-fit-only-v1"
        and old["final_fit"] == data["final_fit"]
    )
    for invalid in (blob[:-1], blob[:200] + bytes([blob[200] ^ 1]) + blob[201:]):
        try:
            decode(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError("corrupt or truncated data accepted")
print(
    "PASS: firmware calibration packet, fragmented USB read, legacy summary and CRC rejection"
)
