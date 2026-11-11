#include "decay_record.h"
#include <cassert>
#include <iostream>
int main(){
  Decay::Results in;in.initialRpm=9000;in.acousticStart=.2f;in.lastTime=40;in.launcherRpm=11000;in.actualStop=100;in.actualStopValid=true;in.manualAnchor=true;in.tailRpm=3000;in.tailSlope=-100;
  for(unsigned i=0;i<4;++i){auto &r=in.models[i];r.model=Decay::Model(i);r.valid=r.converged=true;r.params={.03f,.001f,.001f,.001f,1.5f};r.all={true,10,8,20};r.stopValid=true;r.stopSeconds=110;}
  Decay::Record::Image image;Decay::Record::encode(in,3,image);Decay::Results out;unsigned selected=0;
  assert(Decay::Record::decode(image,out,selected) && selected==3 && out.initialRpm==in.initialRpm && out.models[3].params.n==1.5f);
  assert(out.tailRpm==3000 && out.tailSlope==-100);
  for(unsigned i=0;i<4;++i)assert(out.models[i].all.maxError==20 && out.models[i].valid);
  image[80]^=1;assert(!Decay::Record::decode(image,out,selected));
  Decay::Record::encode(in,4,image);assert(!Decay::Record::decode(image,out,selected));
  in.models[0].params.a=NAN;Decay::Record::encode(in,0,image);assert(!Decay::Record::decode(image,out,selected));
  in={};Decay::Record::encode(in,0,image);assert(Decay::Record::decode(image,out,selected));
  // Reject a CRC-valid but invalid model index before converting it to signed int.
  image[52]=0;image[53]=0;image[54]=0;image[55]=0x80;
  auto crc=Decay::Record::checksum(image.data(),image.size()-4);
  for(unsigned i=0;i<4;++i)image[image.size()-4+i]=uint8_t(crc>>(i*8));
  assert(!Decay::Record::decode(image,out,selected));
  std::cout<<"PASS: versioned result record round-trip and corruption rejection\n";
}
