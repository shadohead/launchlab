#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Battery lab: test builds only (LAUNCHLAB_POWER_LAB=1, firmware/build-m5-lab.sh,
// docs/battery/POWER_LAB.md). Never ship it.
//
// The StickS3 measures battery voltage but not current, so runtime is measured
// by running the battery down. A run logs a record every RECORD_S seconds to
// flash, so it survives the battery going flat while awake, and rotates through
// up to eight power profiles on a fixed segment length, so one discharge
// compares them at the same state of charge (firmware/tools/power_lab.py).
//
// Storage: the first LOG_BYTES of the app1 partition, which nothing uses (the
// updater writes app0 only and boot never selects app1). Sector 0 holds the
// run header, the rest 32-byte records appended in order (8,064 records, 22 h
// at RECORD_S). A record only programs flash; a sector is erased when the log
// reaches it. Each record's CRC-32 is seeded with the run id, so records a
// previous run left in a not yet erased sector never read as this run's, and a
// record a power loss tore is skipped rather than ending the run.
namespace PowerLab {
constexpr uint32_t SECTOR=4096,LOG_BYTES=64*SECTOR,RECORD_BYTES=32;
constexpr uint32_t RECORDS=(LOG_BYTES-SECTOR)/RECORD_BYTES;
constexpr uint32_t RECORD_S=10;
constexpr unsigned MAX_PROFILES=8;

// One power profile. Each field's zero value is the shipped behaviour.
struct Knobs {
  int8_t backlight=-1;  // b: -1 shipped (selected level, dim policy); 0 display asleep, nothing drawn; 1-100 fixed percent, never dimmed
  uint8_t adc=0;        // a: 0 shipped (pauses when still, see restS), 1 always sampling, 2 always paused
  uint8_t cpu=0;        // c: 0 shipped (80 MHz, 40 when nothing holds a lock), 1 fixed 80 MHz, 2 fixed 40 MHz with the ADC re-clocked (no sounds)
  uint8_t frameMs=0;    // f: 0 shipped; else at least frameMs*10 ms between ordinary redraws
  uint8_t imuMs=0;      // i: 0 shipped (20 ms); 20-40 ms between IMU FIFO reads
  uint8_t dimS=0;       // d: 0 shipped (30 s, movement holds full brightness); else dim after dimS s without a button, pull or command, moving or not
  uint8_t dimLevel=0;   // l: 0 shipped (10%, raw 26); 1-26 raw backlight value while dimmed
  uint8_t restS=0;      // r: 0 shipped (30 s); else seconds still before the optical ADC rests
  uint8_t push=0;       // p: 0 shipped (changed rows only), 1 every frame whole (before 0.20.6)
  uint8_t spare[7]={};
  bool operator==(const Knobs &o)const{return !std::memcmp(this,&o,sizeof(*this));}
};
static_assert(sizeof(Knobs)==16,"knobs are stored as 16 bytes");

// "b30 a1 c0 f10": letter then number, any order, b- for the shipped
// backlight. Unknown letters or out-of-range values fail the whole profile.
inline bool parse(const char *text,Knobs &out) {
  Knobs k;
  for(const char *p=text;*p;) {
    if(*p==' ' || *p==','){++p;continue;}
    const char key=*p++;
    if(key=='b' && *p=='-'){k.backlight=-1;++p;continue;}
    char *end=nullptr;const long v=std::strtol(p,&end,10);
    if(end==p)return false;
    p=end;
    switch(key) {
      case 'b':if(v<0 || v>100)return false;k.backlight=int8_t(v);break;
      case 'a':if(v<0 || v>2)return false;k.adc=uint8_t(v);break;
      case 'c':if(v<0 || v>2)return false;k.cpu=uint8_t(v);break;
      case 'f':if(v<0 || v>255)return false;k.frameMs=uint8_t(v);break;
      case 'i':if(v && (v<20 || v>40))return false;k.imuMs=uint8_t(v);break;
      case 'd':if(v<0 || v>255)return false;k.dimS=uint8_t(v);break;
      case 'l':if(v<0 || v>26)return false;k.dimLevel=uint8_t(v);break;
      case 'r':if(v<0 || v>255)return false;k.restS=uint8_t(v);break;
      case 'p':if(v<0 || v>1)return false;k.push=uint8_t(v);break;
      default:return false;
    }
  }
  out=k;return true;
}
inline int format(const Knobs &k,char *out,size_t n) {
  char b[8];if(k.backlight<0)std::snprintf(b,sizeof(b),"b-");else std::snprintf(b,sizeof(b),"b%d",k.backlight);
  return std::snprintf(out,n,"%s a%u c%u f%u i%u d%u l%u r%u p%u",b,k.adc,k.cpu,k.frameMs,k.imuMs,k.dimS,k.dimLevel,k.restS,k.push);
}

inline uint32_t crc32(const uint8_t *p,size_t n,uint32_t crc=0) {
  crc=~crc;
  while(n--){crc^=*p++;for(unsigned k=0;k<8;++k)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1)));}
  return ~crc;
}
inline void put16(uint8_t *p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
inline void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
inline uint16_t get16(const uint8_t *p){return uint16_t(p[0]|p[1]<<8);}
inline uint32_t get32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}

enum HeaderFlag:uint8_t {ChargeOff=1};
struct Header {
  static constexpr uint32_t MAGIC=0x4c504c4c;  // "LLPL"
  static constexpr uint16_t VERSION=2;  // 2: 16-byte knobs (0.20.6)
  static constexpr unsigned BYTES=16+MAX_PROFILES*sizeof(Knobs)+4;
  uint32_t runId=0;         // the host's Unix time at start: names the run and seeds record CRCs
  uint16_t segmentS=600;    // seconds per profile before the rotation moves on
  uint8_t count=1,flags=0;
  Knobs profiles[MAX_PROFILES];
  uint8_t profileAt(uint32_t seconds)const{return count>1?uint8_t((seconds/segmentS)%count):0;}
  bool valid()const{return runId && count>=1 && count<=MAX_PROFILES && segmentS>=RECORD_S;}
  void encode(uint8_t *b)const {
    std::memset(b,0xff,BYTES);put32(b,MAGIC);put16(b+4,VERSION);b[6]=count;b[7]=flags;
    put32(b+8,runId);put16(b+12,segmentS);put16(b+14,uint16_t(RECORD_S));
    for(unsigned i=0;i<MAX_PROFILES;++i)if(i<count)std::memcpy(b+16+sizeof(Knobs)*i,&profiles[i],sizeof(Knobs));
    put32(b+BYTES-4,crc32(b,BYTES-4));
  }
  bool decode(const uint8_t *b) {
    if(get32(b)!=MAGIC || get16(b+4)!=VERSION || get32(b+BYTES-4)!=crc32(b,BYTES-4))return false;
    Header h;h.count=b[6];h.flags=b[7];h.runId=get32(b+8);h.segmentS=get16(b+12);
    if(get16(b+14)!=RECORD_S || !h.valid())return false;
    for(unsigned i=0;i<h.count;++i)std::memcpy(&h.profiles[i],b+16+sizeof(Knobs)*i,sizeof(Knobs));
    *this=h;return true;
  }
};

enum Flag:uint8_t {Charging=1,ChargeKnown=2,Vin=4,UsbHost=8,Sampling=16,DisplayOn=32,Booted=64,End=128};
struct Record {
  uint32_t seconds=0;      // since the run started; continues across reboots
  uint16_t batteryMv=0;    // mean of the reads taken for this record; 0 unreadable
  uint16_t minMv=0,maxMv=0;// the spread of those reads
  uint16_t vinMv=0;        // PM1 5 V input; 0 none or unreadable
  uint16_t frames=0;       // display pushes since the previous record
  uint8_t profile=0,flags=0,backlight=0;
  uint8_t cpuLowPct=255;   // share of the interval below 80 MHz; 255 unknown
  int8_t tempC=-128;       // ESP32-S3 die temperature; -128 unknown
  uint8_t boots=0;         // reboots into this run, low byte
  uint32_t loops=0;        // UI loop passes since the previous record
  uint32_t rows=0;         // display rows pushed since the previous record (240 a whole frame)
  void encode(uint8_t *b,uint32_t runId)const {
    put32(b,seconds);put16(b+4,batteryMv);put16(b+6,minMv);put16(b+8,maxMv);put16(b+10,vinMv);put16(b+12,frames);
    b[14]=profile;b[15]=flags;b[16]=backlight;b[17]=cpuLowPct;b[18]=uint8_t(tempC);b[19]=boots;
    put32(b+20,loops);put32(b+24,rows);put32(b+28,crc32(b,28,runId));
  }
  bool decode(const uint8_t *b,uint32_t runId) {
    if(get32(b+28)!=crc32(b,28,runId))return false;
    seconds=get32(b);batteryMv=get16(b+4);minMv=get16(b+6);maxMv=get16(b+8);vinMv=get16(b+10);frames=get16(b+12);
    profile=b[14];flags=b[15];backlight=b[16];cpuLowPct=b[17];tempC=int8_t(b[18]);boots=b[19];
    loops=get32(b+20);rows=get32(b+24);
    return true;
  }
};
static_assert(RECORD_BYTES==32,"the record layout above is 32 bytes");

// Flash is any type with partition-relative
//   bool read(uint32_t offset,void *out,size_t n)
//   bool write(uint32_t offset,const void *data,size_t n)  // NOR: 1 bits to 0
//   bool erase(uint32_t offset,size_t n)                   // whole sectors
template<class Flash> class Log {
public:
  explicit Log(Flash &flash):flash_(flash){}
  // Reads the header and finds this run's records. True when a run is open:
  // a valid header whose last record is not End, with room left. Every slot
  // is read: valid records are this run's, in order; a slot that is neither
  // valid nor erased is a torn write and is passed over.
  bool begin() {
    header_=Header();count_=0;end_=0;haveLast_=false;open_=false;
    uint8_t b[Header::BYTES];
    if(!flash_.read(0,b,sizeof(b)) || !header_.decode(b))return false;
    for(uint32_t slot=0;slot<RECORDS;++slot) {
      uint8_t r[RECORD_BYTES];Record rec;
      if(!flash_.read(at(slot),r,sizeof(r)))return false;
      if(rec.decode(r,header_.runId)){last_=rec;haveLast_=true;++count_;end_=slot+1;}
    }
    // Resume after the last record, past torn slots. append() erases a sector
    // on entry (start() the first), so a torn slot at a later sector's start
    // is erased rather than passed over.
    while(end_<RECORDS && (at(end_)%SECTOR || !end_) && !erased(end_))++end_;
    open_=!(haveLast_ && (last_.flags&End)) && end_<RECORDS;
    return open_;
  }
  // A new run: erases the header sector and the first record sector, then
  // writes the header. Records a previous run left further on fail this run's
  // CRC and are erased before the log reaches them.
  bool start(const Header &h) {
    if(!h.valid())return false;
    uint8_t b[Header::BYTES];h.encode(b);
    open_=false;count_=end_=0;haveLast_=false;
    if(!flash_.erase(0,2*SECTOR) || !flash_.write(0,b,sizeof(b)))return false;
    header_=h;open_=true;return true;
  }
  // A failed write consumes its slot: the next record goes after it.
  bool append(const Record &r) {
    if(!open_ || end_>=RECORDS)return false;
    const uint32_t slot=end_++,offset=at(slot);
    if(offset%SECTOR==0 && slot && !flash_.erase(offset,SECTOR))return false;
    uint8_t b[RECORD_BYTES];r.encode(b,header_.runId);
    if(!flash_.write(offset,b,sizeof(b)))return false;
    last_=r;haveLast_=true;++count_;
    if((r.flags&End) || end_>=RECORDS)open_=false;
    return true;
  }
  // The run's records in order: call with slot=0 and keep calling while true.
  bool next(uint32_t &slot,Record &r) {
    for(;slot<end_;++slot) {
      uint8_t b[RECORD_BYTES];
      if(flash_.read(at(slot),b,sizeof(b)) && r.decode(b,header_.runId)){++slot;return true;}
    }
    return false;
  }
  bool open()const{return open_;}
  bool hasRun()const{return header_.valid();}
  const Header &header()const{return header_;}
  uint32_t count()const{return count_;}
  uint32_t slots()const{return end_;}
  bool last(Record &r)const{if(haveLast_)r=last_;return haveLast_;}
private:
  static uint32_t at(uint32_t slot){return SECTOR+slot*RECORD_BYTES;}
  bool erased(uint32_t slot) {
    uint8_t b[RECORD_BYTES];
    if(!flash_.read(at(slot),b,sizeof(b)))return false;
    for(uint8_t v:b)if(v!=0xff)return false;
    return true;
  }
  Flash &flash_;
  Header header_;
  Record last_;
  uint32_t count_=0,end_=0;
  bool haveLast_=false,open_=false;
};

// "APB_MIN" time from IDF's esp_pm_dump_locks() mode table, in microseconds:
// the time at the minimum frequency. IDF 5.5 prints each mode as
// "%-8s  %-3luM%-7s %-10lld  %-2d%%", so "APB_MIN   40 M        123456      3%".
// -1 when the line is missing or malformed.
inline long long minFrequencyUs(const char *dump) {
  for(const char *line=dump;line && *line;) {
    while(*line==' ')++line;
    if(!std::strncmp(line,"APB_MIN",7)) {
      const char *p=line+7;
      while(*p==' ' || (*p>='0' && *p<='9'))++p;  // the frequency, "40 "
      if(*p++!='M')return -1;
      char *end=nullptr;const long long us=std::strtoll(p,&end,10);
      return end==p?-1:us;
    }
    line=std::strchr(line,'\n');if(line)++line;
  }
  return -1;
}
}
