#pragma once
#include <string>
#include <vector>
#include <zlib.h>
#include "fx_compiler.h"
#include "fx_share.h"

// Share codes end to end (docs/FIRMWARE_LOADABLE_EFFECTS.md, "Sharing"):
// an effect's source to its LLFX1: code, and whatever was pasted (a code, an
// .llfx file, a link) back to source. fx_share.h holds the format; this adds
// zlib, for fxc, the tests and the web preview (WebAssembly).
namespace FxCodes {

// A source that compiles, packed (what a slot keeps next to the file) and as a code.
struct Made {bool ok=false;std::string error;int line=0;std::string packed,code;};
inline Made make(const std::string &source) {
  Made r;
  const auto c=FxCompiler::compile(source);
  if(!c.ok){r.error=c.error;r.line=c.line;return r;}
  auto p=FxShare::pack(source);
  if(!p.ok){r.error=p.error;r.line=p.line;return r;}
  uLongf size=compressBound(uLong(p.bytes.size()));std::vector<uint8_t> z(size);
  if(compress2(z.data(),&size,reinterpret_cast<const Bytef *>(p.bytes.data()),uLong(p.bytes.size()),9)!=Z_OK){r.error="cannot compress";return r;}
  z.resize(size);
  r.packed=std::move(p.bytes);r.code=FxShare::code(z);r.ok=true;return r;
}

// Source from packed bytes (a slot's, read back from a device).
struct Opened {bool ok=false;std::string error,packed,source;};
inline Opened unpacked(const std::string &packed) {
  Opened r;r.packed=packed;
  auto u=FxShare::unpack(packed);
  if(!u.ok){r.error=u.error;return r;}
  r.source=std::move(u.source);r.ok=true;return r;
}

// The source a pasted code holds, and its packed form. The source is not
// compiled here: the caller compiles and validates it like typed text.
inline Opened open(const std::string &pasted) {
  Opened r;
  const auto p=FxShare::payload(pasted);
  if(!p.ok){r.error=p.error;return r;}
  // Inflate no further than the largest packed source a code may hold.
  std::vector<uint8_t> packed(FxShare::MAX_PACKED+1);uLongf size=uLongf(packed.size());
  const int z=uncompress(packed.data(),&size,p.zlib.data(),uLong(p.zlib.size()));
  // A stream that stops short is Z_DATA_ERROR (zlib 1.2.9+): only a full buffer is Z_BUF_ERROR.
  if(z!=Z_OK){r.error=z==Z_BUF_ERROR?"code too large":"damaged code";return r;}
  return unpacked(std::string(packed.begin(),packed.begin()+long(size)));
}

}  // namespace FxCodes
