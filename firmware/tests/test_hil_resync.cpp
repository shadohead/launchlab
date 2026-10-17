// After a failed HIL upload, binary chunk data must never reach a command
// parser (it powered both rig M5s off on 2026-10-17).
#include "../LaunchLabMini/hil_resync.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
using R=HilResync::Result;
int main() {
  HilResync r;assert(r.feed('O',0)==R::Pass); // Idle: everything passes.
  // 64 KiB of pseudo-random chunk bytes at USB speed: every byte swallowed,
  // including letters, newlines and stray '!' that do not complete "!sync\n".
  r.begin(1000);uint32_t state=12345,t=1000;unsigned passed=0;
  for(unsigned i=0;i<65536;++i) {
    state=state*1664525u+1013904223u;const char c=char(state>>24);
    if(i%64==0)++t;
    const R result=r.feed(c,t);
    if(result==R::Pass || result==R::Quiet)++passed;
  }
  assert(passed==0 && r.active && r.discarded==65536);
  // The host's resync line ends it; the next command is parsed normally.
  for(char c:std::string("\n!sync"))assert(r.feed(c,t)==R::Swallowed);
  assert(r.feed('\n',t)==R::Synced && !r.active && r.feed('!',t)==R::Pass);
  // A partial "!syn" inside the stream does not end it; "!!sync\n" does.
  r.begin(t);for(char c:std::string("!syn!!sync"))assert(r.feed(c,t)==R::Swallowed);
  assert(r.feed('\n',t)==R::Synced && !r.active);
  // One second of quiet ends it, and that byte is processed normally.
  r.begin(5000);assert(r.feed('x',5500)==R::Swallowed && r.feed('O',6499)==R::Swallowed);
  assert(r.feed('!',7499)==R::Quiet && !r.active && r.feed('h',7499)==R::Pass);
  std::cout<<"PASS: after a failed upload, 64 KiB of binary is swallowed until \"!sync\" or 1 s of quiet; nothing reaches a parser\n";
}
