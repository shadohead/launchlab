#include "../LaunchLabMini/audio_metadata_store.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace AudioMetadataStore;
struct Flash {
  std::vector<uint8_t> bytes=std::vector<uint8_t>(FlashLayout::PARTITION_BYTES,0xff);
  unsigned writes=0,failAt=0,reads=0;bool afterWrite=false;
  bool read(unsigned at,void *out,size_t n){++reads;assert(at+n<=bytes.size());std::memcpy(out,bytes.data()+at,n);return true;}
  bool erase(unsigned at,size_t n){assert(at%4096==0 && n%4096==0);std::fill_n(bytes.data()+at,n,0xff);return true;}
  bool write(unsigned at,const void *in,size_t n){
    bool fail=++writes==failAt;if(fail && !afterWrite)return false;
    auto p=static_cast<const uint8_t*>(in);
    for(size_t i=0;i<n;++i){assert((bytes[at+i]&p[i])==p[i]);bytes[at+i]=p[i];}
    return !fail;
  }
  void seed(bool wav){
    auto table=FlashLayout::historyLayout(1);
    table.regions[table.count++]={Calibration::Archive::KIND,0,Calibration::Archive::BASE,Calibration::Archive::BYTES};
    if(wav)table.regions[table.count++]={5,0,BASE,OLD_WAV_BYTES};
    table.regions[table.count++]={FlashLayout::Effects,0,FlashLayout::EFFECTS_OFFSET,FlashLayout::PARTITION_BYTES-FlashLayout::EFFECTS_OFFSET};
    FlashLayout::Superblock::Image image;table.encode(image);assert(write(0,image.data(),image.size()));
    std::fill(bytes.begin()+Calibration::Archive::BASE,bytes.begin()+BASE,0x53);
    std::fill(bytes.begin()+BASE,bytes.begin()+BASE+OLD_WAV_BYTES,0x71);
    std::fill(bytes.begin()+FlashLayout::EFFECTS_OFFSET,bytes.end(),0x42);
    writes=0;
  }
};
int main(){
  std::vector<uint8_t> data(AudioMetadata::MAX_BYTES),out(data.size());
  const auto n=AudioMetadata::pack(data.data(),nullptr,300000,0,0,true);
  // Cut each stage of migration and append, both before programming and after
  // a driver has programmed bytes but returned failure. Retry must be safe.
  for(bool legacy:{false,true})for(bool ambiguous:{false,true})for(unsigned cut=1;cut<=3;++cut){
    Flash f;f.seed(legacy);const auto original=f.bytes;f.failAt=cut;f.afterWrite=ambiguous;
    assert(!save(f,1,123,data.data(),n));
    assert(std::equal(original.begin()+Calibration::Archive::BASE,original.begin()+BASE,f.bytes.begin()+Calibration::Archive::BASE));
    assert(std::equal(original.begin()+FlashLayout::EFFECTS_OFFSET,original.end(),f.bytes.begin()+FlashLayout::EFFECTS_OFFSET));
    f.failAt=0;assert(save(f,1,123,data.data(),n));Index index;assert(scan(f,index) && index.count==1);
    size_t used=0;assert(read(f,1,123,out.data(),used) && used==n && out==data);
    assert(read(f,1,124,out.data(),used) && !used);
    const auto durable=f.bytes;assert(save(f,1,123,data.data(),n) && f.bytes==durable);
    assert(!save(f,1,124,data.data(),n) && f.bytes==durable);
  }
  Flash f;f.seed(true);Index index;assert(scan(f,index) && !index.count);
  // Attachments can arrive out of order after one flash write fails. Saving
  // the next pull must not erase the older attachment that just retried.
  assert(save(f,2,123,data.data(),n) && save(f,1,123,data.data(),n) && save(f,3,123,data.data(),n));
  assert(scan(f,index) && index.count==3);
  for(unsigned i=1;i<=40;++i)assert(save(f,i,123,data.data(),n));
  assert(scan(f,index) && index.count==33 && index.entries[1].sequence==9 && index.entries[32].sequence==40);
  size_t used=0;assert(read(f,8,123,out.data(),used) && !used);
  for(unsigned i=9;i<=40;++i)assert(read(f,i,123,out.data(),used) && used==n);
  const auto beforeBatch=f.reads;
  for(unsigned i=9;i<=40;++i)assert(read(f,index,i,123,out.data(),used) && used==n && out==data);
  assert(f.reads-beforeBatch==32); // One payload read per pull, without rescanning 33 headers.
  // A torn append consumes the spare slot, never any of the newest 32.
  f.failAt=f.writes+2;assert(!save(f,41,123,data.data(),n));
  assert(scan(f,index) && index.count==32);
  for(unsigned i=9;i<=40;++i)assert(read(f,i,123,out.data(),used) && used==n);
  f.failAt=0;assert(save(f,41,123,data.data(),n));
  assert(scan(f,index));f.bytes[index.entries[index.count-1].offset+HEADER+100]^=1;
  assert(!read(f,41,123,out.data(),used));
  // Refuse an overlapping non-WAV region without touching any flash.
  Flash occupied;occupied.seed(false);auto table=FlashLayout::historyLayout(5);
  table.regions[table.count++]={7,0,BASE,OLD_WAV_BYTES};FlashLayout::Superblock::Image image;table.encode(image);
  occupied.erase(0,4096);occupied.write(0,image.data(),image.size());const auto before=occupied.bytes;
  assert(!save(occupied,1,123,data.data(),n) && occupied.bytes==before);
  std::puts("PASS: 32 metadata recordings, WAV migration, interrupted commits, retries and corruption checks");
}
