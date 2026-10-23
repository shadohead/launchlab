// USB `X` (fx_transfer.h): a loaded-effect request (put, delete, clear or
// source) round-trips; any damage,
// a wrong length, an unknown op, reserved bytes or a delete outside the
// slots is refused before anything is judged; the receiver takes base64
// lines into the caller's buffer and refuses what does not fit.
#include "../LaunchLabMini/fx_transfer.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

int main() {
  using FxTransfer::Request;
  std::vector<uint8_t> file(1500),source(700);
  for(size_t i=0;i<file.size();++i)file[i]=uint8_t(i*7+3);
  for(size_t i=0;i<source.size();++i)source[i]=uint8_t(i*11+5);
  // Put, delete and clear round-trip.
  std::vector<uint8_t> frame(Request::bytes(uint32_t(file.size()),uint32_t(source.size())));
  assert(Request::encode(Request::Op::Put,0,file.data(),uint32_t(file.size()),source.data(),uint32_t(source.size()),frame.data())==frame.size());
  {
    Request r;assert(r.decode(frame.data(),frame.size()) && r.op==Request::Op::Put && r.fileBytes==file.size() && r.sourceBytes==source.size());
    assert(std::equal(file.begin(),file.end(),r.file) && std::equal(source.begin(),source.end(),r.source));
    for(size_t i=0;i<frame.size();i+=7)for(uint8_t bit:{1,0x80}){auto bad=frame;bad[i]^=bit;Request x;assert(!x.decode(bad.data(),bad.size()));}
    Request x;assert(!x.decode(frame.data(),frame.size()-1));
  }
  {
    uint8_t d[Request::HEADER+Request::TRAILER];
    assert(Request::encode(Request::Op::Delete,31,nullptr,0,nullptr,0,d)==sizeof(d));
    Request r;assert(r.decode(d,sizeof(d)) && r.op==Request::Op::Delete && r.slot==31 && !r.file);
    assert(Request::encode(Request::Op::Delete,32,nullptr,0,nullptr,0,d)==sizeof(d) && !r.decode(d,sizeof(d)));  // no slot 32
    assert(Request::encode(Request::Op::Clear,0,nullptr,0,nullptr,0,d)==sizeof(d) && r.decode(d,sizeof(d)) && r.op==Request::Op::Clear);
    assert(Request::encode(Request::Op::Clear,1,nullptr,0,nullptr,0,d)==sizeof(d) && !r.decode(d,sizeof(d)));   // clear names no slot
    // Source names a slot and carries nothing.
    assert(Request::encode(Request::Op::Source,7,nullptr,0,nullptr,0,d)==sizeof(d) && r.decode(d,sizeof(d)) && r.op==Request::Op::Source && r.slot==7);
    assert(Request::encode(Request::Op::Source,32,nullptr,0,nullptr,0,d)==sizeof(d) && !r.decode(d,sizeof(d)));
    {std::vector<uint8_t> carrying(Request::bytes(0,4));const uint8_t four[4]={1,2,3,4};
     Request::encode(Request::Op::Source,0,nullptr,0,four,4,carrying.data());assert(!r.decode(carrying.data(),carrying.size()));}
    // A put without a file, and a delete with one, are refused.
    std::vector<uint8_t> empty(Request::bytes(0,0));Request::encode(Request::Op::Put,0,nullptr,0,nullptr,0,empty.data());assert(!r.decode(empty.data(),empty.size()));
    std::vector<uint8_t> odd(Request::bytes(4,0));const uint8_t four[4]={1,2,3,4};
    Request::encode(Request::Op::Delete,0,four,4,nullptr,0,odd.data());assert(!r.decode(odd.data(),odd.size()));
    // Unknown ops and set reserved bytes, checksummed: still refused.
    for(unsigned at:{4u,6u,7u}){uint8_t bad[sizeof(d)];Request::encode(Request::Op::Clear,0,nullptr,0,nullptr,0,bad);bad[at]=at==4?5:1;
      DeviceBackup::put32(bad+Request::HEADER,Request::hash(bad,Request::HEADER));assert(!r.decode(bad,sizeof(bad)));}
  }
  // The receiver: the frame as base64 lines into the caller's buffer.
  {
    std::string text;DeviceBackup::base64Lines(frame.data(),frame.size(),96,[&](const char *line){text+=line;text+='\n';});
    text+=".\n";
    std::vector<char> buffer((frame.size()+2)/3*4);
    FxTransfer::Receiver rx;rx.start(buffer.data(),unsigned(buffer.size()),0);
    unsigned chunks=0;FxTransfer::Event last=FxTransfer::Event::None;
    for(char c:text){last=rx.feed(c,0);chunks+=last==FxTransfer::Event::Chunk;if(last==FxTransfer::Event::Complete)break;}
    assert(last==FxTransfer::Event::Complete && chunks==(buffer.size()+95)/96 && !rx.active());
    std::vector<uint8_t> back(frame.size());size_t n=0;assert(rx.take(back.data(),back.size(),n) && n==frame.size() && back==frame);
    // One byte of buffer short: too large, refused at once.
    FxTransfer::Receiver small;small.start(buffer.data(),unsigned(buffer.size()-1),0);
    for(char c:text){last=small.feed(c,0);if(last==FxTransfer::Event::Error)break;}
    assert(last==FxTransfer::Event::Error && !std::strcmp(small.error(),"too_large"));
    // Idle for 15 s: timed out; no buffer: nothing fits.
    FxTransfer::Receiver idle;idle.start(buffer.data(),unsigned(buffer.size()),1000);assert(!idle.timedOut(15999) && idle.timedOut(16000));
    FxTransfer::Receiver none;none.start(nullptr,100,0);assert(none.feed('A',0)==FxTransfer::Event::Error);
  }
  std::puts("test_fx_transfer: ok");
}
