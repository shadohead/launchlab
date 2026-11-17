#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Push only the rows of a composed frame that changed (0.20.6, battery). A
// whole-frame push holds the SPI bus and busy-waits the UI core for about
// 13 ms, while most redraws change a small part of the screen: the level as
// the unit moves, a counter, a highlight. After composing, each band of rows
// is hashed; the push covers the first to the last band whose hash differs
// from what the panel shows, and nothing at all when none does. A band is one
// row, or two when a row is not a whole number of 32-bit words (the StickS3's
// 135 pixels), so every band starts word-aligned.
//
// The hashes describe the panel only while nothing else writes it: after the
// panel wakes or rotates, invalidate(), and the next push is the whole frame.
// A whole frame also goes out at least every FULL_MS, which bounds how long a
// hash collision (1 in 2^32 per changed band) could leave a band stale.
//
// The hashes live where begin() puts them: PSRAM on the device, because the
// songs hold the internal heap within a few hundred bytes of its floor. Until
// then, or without storage, every frame is pushed whole.
class FrameRows {
public:
  static constexpr unsigned MAX_BANDS=256;
  static constexpr size_t BYTES=MAX_BANDS*sizeof(uint32_t);
  static constexpr uint32_t FULL_MS=10000;
  // storage: BYTES, 4-byte aligned, or nullptr.
  void begin(uint32_t *storage){hashes_=storage;valid_=false;}
  bool ready()const{return hashes_!=nullptr;}
  struct Span {
    int first=0,count=0;  // rows
    bool whole=false;
    bool any()const{return count>0;}
  };
  // pixels: width*height 16-bit pixels, row by row, 4-byte aligned.
  Span changed(const void *pixels,int width,int height,uint32_t now) {
    Span span;
    if(!pixels || width<=0 || height<=0)return span;
    const size_t rowBytes=size_t(width)*2,bandRows=rowBytes%4?2:1,bandBytes=rowBytes*bandRows;
    const size_t totalBytes=rowBytes*size_t(height),bands=(size_t(height)+bandRows-1)/bandRows;
    const bool whole=!valid_ || width!=width_ || height!=height_ || uint32_t(now-wholeAt_)>=FULL_MS;
    if(!hashes_ || bands>MAX_BANDS) {
      valid_=false;++frames_;++wholeFrames_;rows_+=uint32_t(height);
      span.count=height;span.whole=true;return span;
    }
    const auto *bytes=static_cast<const uint8_t *>(pixels);
    int first=-1,last=-1;
    for(size_t band=0;band<bands;++band) {
      const size_t start=band*bandBytes,end=start+bandBytes<totalBytes?start+bandBytes:totalBytes;
      const uint32_t h=hash(bytes+start,end-start);
      if(whole || h!=hashes_[band]){hashes_[band]=h;if(first<0)first=int(band);last=int(band);}
    }
    ++frames_;
    if(whole) {
      valid_=true;width_=width;height_=height;wholeAt_=now;++wholeFrames_;rows_+=uint32_t(height);
      span.count=height;span.whole=true;return span;
    }
    if(first<0){++unchanged_;return span;}
    span.first=first*int(bandRows);
    const int end=(last+1)*int(bandRows);
    span.count=(end<height?end:height)-span.first;
    rows_+=uint32_t(span.count);
    return span;
  }
  void invalidate(){valid_=false;}
  // Since boot: composed frames, those pushed whole, those with nothing to
  // push, and the rows pushed.
  uint32_t frames()const{return frames_;}
  uint32_t wholeFrames()const{return wholeFrames_;}
  uint32_t unchanged()const{return unchanged_;}
  uint32_t rows()const{return rows_;}
private:
  // FNV-1a over 32-bit words, then a trailing half-word if any.
  static uint32_t hash(const uint8_t *p,size_t n) {
    uint32_t h=2166136261u;
    for(size_t i=0;i+4<=n;i+=4){uint32_t w;std::memcpy(&w,p+i,4);h^=w;h*=16777619u;}
    if(n%4){h^=uint32_t(p[n-2])|uint32_t(p[n-1])<<8;h*=16777619u;}
    return h;
  }
  uint32_t *hashes_=nullptr;
  uint32_t wholeAt_=0,frames_=0,wholeFrames_=0,unchanged_=0,rows_=0;
  int width_=0,height_=0;
  bool valid_=false;
};
