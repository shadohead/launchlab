#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <cstring>
#include <set>
class Preferences {
public:
  static inline std::map<std::string,std::vector<unsigned char>> data;
  static inline bool failWrite=false,shortRead=false;
  static inline void (*beforeWrite)()=nullptr;
  // A key (space+key) whose writes fail, as a write a lost power cut short.
  static inline std::string failWriteKey;
  // Keys (space+key) starting with any of these refuse writes, as a full NVS refuses a blob.
  static inline std::vector<std::string> failWritePrefixes;
  // The next `failReads` getBytes of keys (space+key) starting with this
  // return nothing: a write that landed and does not read back.
  static inline std::string failReadPrefix;
  static inline unsigned failReads=0;
  // Existing keys can return zero blob length on type/query failure; the real
  // ESP32 Preferences getBytesLength() folds all nvs_get_blob errors to zero.
  static inline std::set<std::string> zeroLengthKeys;
  // The namespaces ever opened read-write, as NVS keeps them (nvs_erase_all
  // clears a namespace's keys, not the namespace): empty only on a partition
  // never used since it was erased (namespace_count 0 in nvs_get_stats).
  static inline std::set<std::string> spaces;
  std::string space;
  bool begin(const char *s,bool readOnly){space=s;if(!readOnly)spaces.insert(s);return true;}
  bool isKey(const char *key){return data.count(space+key)!=0;}
  size_t getBytesLength(const char *key){auto i=data.find(space+key);return i==data.end() || zeroLengthKeys.count(space+key)?0:i->second.size();}
  size_t getBytes(const char *key,void *out,size_t size) {
    auto i=data.find(space+key);if(i==data.end())return 0;
    if(failReads && !failReadPrefix.empty() && (space+key).compare(0,failReadPrefix.size(),failReadPrefix)==0){--failReads;return 0;}
    const size_t n=std::min(size,i->second.size());std::memcpy(out,i->second.data(),n);return shortRead?n-1:n;
  }
  // The real store keeps a typed u8; here it is a one-byte record.
  uint8_t getUChar(const char *key,uint8_t fallback=0){auto i=data.find(space+key);return i==data.end() || i->second.size()!=1?fallback:i->second[0];}
  size_t putUChar(const char *key,uint8_t value){return putBytes(key,&value,1);}
  bool remove(const char *key){if(beforeWrite)beforeWrite();if(failWrite)return false;return data.erase(space+key)!=0;}
  size_t putBytes(const char *key,const void *bytes,size_t n) {
    if(beforeWrite)beforeWrite();
    if(failWrite || failWriteKey==space+key)return 0;
    for(const auto &prefix:failWritePrefixes)if((space+key).compare(0,prefix.size(),prefix)==0)return 0;
    const auto *p=static_cast<const unsigned char *>(bytes);data[space+key]={p,p+n};return n;
  }
};
