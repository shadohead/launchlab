// Changed-row display pushes (frame_rows.h): the first frame and every
// FULL_MS go whole, an unchanged frame pushes nothing, a change pushes the
// band from its first to its last changed row, and 135-pixel rows (not a
// whole number of words) are paired so every band is word-aligned.
#include "../LaunchLabMini/frame_rows.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

// Storage as the device takes it from PSRAM (FrameRows::begin), one block
// per FrameRows, never freed as on the device: static, so no leak to report.
static uint32_t *storage() {
  static uint32_t pool[8][FrameRows::MAX_BANDS];static unsigned used=0;
  assert(used<8);return pool[used++];
}

int main() {
  constexpr int W=135,H=240;
  std::vector<uint16_t> frame(W*H,0x1234);
  FrameRows rows;
  // Without storage (before begin, or a failed PSRAM allocation) every frame goes whole.
  assert(!rows.ready() && rows.changed(frame.data(),W,H,0).whole && rows.changed(frame.data(),W,H,1).whole);
  rows=FrameRows{};rows.begin(storage());assert(rows.ready());
  auto push=[&](uint32_t now){return rows.changed(frame.data(),W,H,now);};
  auto r=push(0);assert(r.whole && r.first==0 && r.count==H);
  r=push(10);assert(!r.any() && !r.whole && rows.unchanged()==1);
  // One pixel on row 101 (odd): its pair is rows 100-101.
  frame[101*W+7]=0xffff;r=push(20);assert(!r.whole && r.first==100 && r.count==2);
  r=push(30);assert(!r.any());
  // Rows 10 and 200: one band from the first to the last.
  frame[10*W]=1;frame[200*W+134]=2;r=push(40);assert(r.first==10 && r.count==192);
  // The last pixel of the frame.
  frame[H*W-1]=3;r=push(50);assert(r.first==238 && r.count==2);
  // A change undone before the next frame pushes nothing.
  frame[5*W]=9;frame[5*W]=0x1234;r=push(60);assert(!r.any());
  // invalidate(), a size change or FULL_MS since the last whole frame: whole.
  rows.invalidate();r=push(70);assert(r.whole && r.count==H);
  r=push(70+FrameRows::FULL_MS-1);assert(!r.any());
  r=push(70+FrameRows::FULL_MS);assert(r.whole);
  r=rows.changed(frame.data(),W,H-1,70+FrameRows::FULL_MS+1);assert(r.whole && r.count==H-1);
  // Even widths use single-row bands; an odd row count ends in a short band.
  std::vector<uint16_t> even(136*239,0);FrameRows e;e.begin(storage());
  assert(e.changed(even.data(),136,239,0).whole);
  even[238*136+5]=1;r=e.changed(even.data(),136,239,1);assert(r.first==238 && r.count==1);
  even[3*136]=1;r=e.changed(even.data(),136,239,2);assert(r.first==3 && r.count==1);
  std::vector<uint16_t> odd(135*239,0);FrameRows o;o.begin(storage());
  assert(o.changed(odd.data(),135,239,0).whole);
  odd[238*135+134]=1;r=o.changed(odd.data(),135,239,1);assert(r.first==238 && r.count==1);
  // Too many bands to track: always whole, never cached.
  std::vector<uint16_t> tall(2*600,0);FrameRows t;t.begin(storage());
  assert(t.changed(tall.data(),2,600,0).whole && t.changed(tall.data(),2,600,1).whole);
  assert(!rows.changed(nullptr,W,H,0).any());
  // Counters for PERF and the lab: composed frames, whole ones, empty ones, rows sent.
  FrameRows c;c.begin(storage());std::vector<uint16_t> f(W*H,0);
  c.changed(f.data(),W,H,0);c.changed(f.data(),W,H,1);f[W]=1;c.changed(f.data(),W,H,2);
  assert(c.frames()==3 && c.wholeFrames()==1 && c.unchanged()==1 && c.rows()==H+2);
  std::cout<<"PASS: changed-row pushes: whole without storage, first and every 10 s, nothing when unchanged, the changed band, word-aligned pairs\n";
}
