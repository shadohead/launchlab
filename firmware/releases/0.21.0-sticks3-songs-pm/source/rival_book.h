#pragma once
#include <algorithm>
#include <array>
#include <cstdio>
#include "battle_beacon.h"

// Battles (docs/FIRMWARE_BATTLES.md): the record this stick keeps against each
// stick it has battled. Host-testable: no radio, no flash.
//
// Each rival has running totals and the last battle against it. The totals
// always include that last battle's result as it stands, so a result that
// changes (a press corrected, the other stick's press arriving late) is taken
// out and counted again rather than counted twice.
namespace Battle {
struct Rival {
  uint32_t id=0;
  char name[NAME_CHARS+1]={};          // as the rival last gave it; empty until heard
  uint16_t wins=0,losses=0,pointsFor=0,pointsAgainst=0;
  uint16_t winsBy[FINISHES]={};
  // Launches made together, and those where this stick's was the faster.
  uint16_t duels=0,duelWins=0;
  uint16_t matchWins=0,matchLosses=0;
  uint8_t matchMe=0,matchThem=0;     // the match being played
  // The length of the match the last battle belongs to: first to this many
  // points. Fixed when its match begins, so a setting changed mid-match takes
  // effect at the next one.
  uint8_t matchTo=MATCH_SHORT;
  // The last battle: both launch tags, its result, both launches and the
  // match score it started from.
  uint16_t myTag=0,theirTag=0;
  Claim claim;
  uint8_t preMe=0,preThem=0;
  uint16_t myRpm=0,theirRpm=0;
  uint32_t stamp=0;                  // order of last use; the lowest is replaced first
  unsigned battles()const{return unsigned(wins)+losses;}
  // The last battle ended its match: the score it ended on.
  bool matchEnded()const{return claim.scored() && !matchMe && !matchThem;}
  unsigned finalMe()const{return preMe+(claim.outcome==Outcome::Won?points(claim.finish):0);}
  unsigned finalThem()const{return preThem+(claim.outcome==Outcome::Lost?points(claim.finish):0);}
  // One more battle won by either player ends the match: every finish scores at least a point.
  bool matchPoint()const{return unsigned(matchMe)+1>=matchTo || unsigned(matchThem)+1>=matchTo;}
};
// What to call a rival: its name, or LL- and the end of its ID until it is heard.
inline void displayName(const Rival &r,char *out,size_t size) {
  if(r.name[0])snprintf(out,size,"%s",r.name);
  else snprintf(out,size,"LL-%04X",unsigned(r.id&0xffff));
}

class Book {
public:
  static constexpr unsigned CAPACITY=12;
  static constexpr uint32_t MAGIC=0x42524c4c;  // "LLRB"
  // Version 2 (match lengths) adds the match length; a version 1 record reads
  // as first to 4, the only length it knew.
  static constexpr uint8_t VERSION=2;
  // Header, the rivals, then the points total and the checksum.
  static constexpr size_t HEADER=24,RIVAL_BYTES=64,IMAGE_BYTES=HEADER+CAPACITY*RIVAL_BYTES+8;
  using Image=std::array<uint8_t,IMAGE_BYTES>;

  unsigned count()const{return count_;}
  // Changes on every change to the record, for the save.
  uint32_t generation()const{return generation_;}
  // Everything this stick has scored, rivals since replaced included.
  uint32_t totalWins()const{return totalWins_;}
  uint32_t totalLosses()const{return totalLosses_;}
  uint32_t totalPoints()const{return totalPoints_;}
  const Rival *find(uint32_t id)const{for(unsigned i=0;i<count_;++i)if(rivals_[i].id==id)return &rivals_[i];return nullptr;}
  // The i-th most recently battled rival.
  const Rival *recent(unsigned i)const {
    if(i>=count_)return nullptr;
    unsigned order[CAPACITY];for(unsigned k=0;k<count_;++k)order[k]=k;
    std::sort(order,order+count_,[&](unsigned a,unsigned b){return rivals_[a].stamp>rivals_[b].stamp;});
    return &rivals_[order[i]];
  }
  // A launch made together with `id`: its record, made when new, starts a
  // battle. A battle that starts a match plays it to `matchTo`, the longer of
  // the two players' choices; one inside a match keeps that match's length.
  // In a full book the new rival takes the place of one never scored
  // against, if there is one (a stranger whose launch happened to coincide),
  // and otherwise of the rival battled longest ago.
  const Rival &begin(uint32_t id,uint16_t myTag,uint16_t theirTag,uint16_t myRpm,uint16_t theirRpm,uint8_t matchTo=MATCH_SHORT) {
    Rival *r=mine(id);
    if(!r) {
      if(count_<CAPACITY)r=&rivals_[count_++];
      else {
        r=&rivals_[0];
        for(unsigned i=1;i<count_;++i) {
          Rival &c=rivals_[i];
          const bool stranger=!c.battles(),chosenStranger=!r->battles();
          if(stranger!=chosenStranger?stranger:c.stamp<r->stamp)r=&c;
        }
      }
      *r=Rival{};r->id=id;
    }
    r->myTag=myTag;r->theirTag=theirTag;r->myRpm=myRpm;r->theirRpm=theirRpm;
    r->claim=Claim{};r->preMe=r->matchMe;r->preThem=r->matchThem;
    if(!r->matchMe && !r->matchThem)r->matchTo=validMatch(matchTo)?matchTo:MATCH_SHORT;
    bump(r->duels);if(myRpm>theirRpm)bump(r->duelWins);
    r->stamp=++clock_;++generation_;
    return *r;
  }
  // The rival's own name, when it differs. False for a stick never battled.
  bool rename(uint32_t id,const char *name) {
    Rival *r=mine(id);
    const size_t n=strnlen(name,NAME_CHARS+1);
    if(!r || !n || !validName(name,n) || !std::strncmp(r->name,name,NAME_CHARS+1))return false;
    std::memset(r->name,0,sizeof(r->name));std::memcpy(r->name,name,n);++generation_;return true;
  }
  // A press on this stick for the last battle with `id`.
  bool claim(uint32_t id,Outcome outcome,Finish finish) {
    Rival *r=mine(id);
    if(!r || !r->myTag)return false;
    Claim next=r->claim;
    if(!next.set(outcome,finish))return false;
    replace(*r,next);return true;
  }
  // What `id` says of the battle in which its tag was `theirTag` (and, when
  // given, this stick's was `myTag`), mirrored to this stick's view. True when
  // that is this stick's last battle with it and its result changed.
  bool merge(uint32_t id,uint16_t theirTag,Claim theirs,uint16_t myTag=0) {
    Rival *r=mine(id);
    if(!r || !theirTag || r->theirTag!=theirTag || (myTag && r->myTag!=myTag))return false;
    Claim next=r->claim;
    if(!next.merge(theirs))return false;
    replace(*r,next);return true;
  }
  // Forgets every rival and total.
  void clear(){const uint32_t g=generation_+1;*this=Book{};generation_=g;}

  void encode(Image &out)const {
    out.fill(0);uint8_t *p=out.data();
    put32(p,MAGIC);p[4]=VERSION;p[5]=uint8_t(count_);put32(p+8,generation_);put32(p+12,clock_);
    put32(p+16,totalWins_);put32(p+20,totalLosses_);
    for(unsigned i=0;i<count_;++i) {
      const Rival &r=rivals_[i];uint8_t *q=p+HEADER+i*RIVAL_BYTES;
      put32(q,r.id);std::memcpy(q+4,r.name,NAME_CHARS);
      put16(q+20,r.wins);put16(q+22,r.losses);put16(q+24,r.pointsFor);put16(q+26,r.pointsAgainst);
      for(unsigned f=0;f<FINISHES;++f)put16(q+28+2*f,r.winsBy[f]);
      put16(q+36,r.duels);put16(q+38,r.duelWins);put16(q+40,r.matchWins);put16(q+42,r.matchLosses);
      q[44]=r.matchMe;q[45]=r.matchThem;put16(q+46,r.myTag);put16(q+48,r.theirTag);
      q[50]=r.claim.byte();q[51]=r.preMe;q[52]=r.preThem;put16(q+53,r.myRpm);put16(q+55,r.theirRpm);put32(q+57,r.stamp);
      q[61]=r.matchTo;
    }
    put32(p+IMAGE_BYTES-8,totalPoints_);
    put32(p+IMAGE_BYTES-4,checksum(p,IMAGE_BYTES-4));
  }
  // Unchanged unless every byte reads as a record this firmware wrote.
  bool decode(const uint8_t *in,size_t n) {
    if(n!=IMAGE_BYTES || get32(in)!=MAGIC || (in[4]!=VERSION && in[4]!=1) || in[5]>CAPACITY || in[6] || in[7] ||
       get32(in+n-4)!=checksum(in,n-4))return false;
    Book b;b.count_=in[5];b.generation_=get32(in+8);b.clock_=get32(in+12);
    b.totalWins_=get32(in+16);b.totalLosses_=get32(in+20);b.totalPoints_=get32(in+n-8);
    for(unsigned i=0;i<b.count_;++i) {
      Rival &r=b.rivals_[i];const uint8_t *q=in+HEADER+i*RIVAL_BYTES;
      r.id=get32(q);std::memcpy(r.name,q+4,NAME_CHARS);
      r.wins=get16(q+20);r.losses=get16(q+22);r.pointsFor=get16(q+24);r.pointsAgainst=get16(q+26);
      for(unsigned f=0;f<FINISHES;++f)r.winsBy[f]=get16(q+28+2*f);
      r.duels=get16(q+36);r.duelWins=get16(q+38);r.matchWins=get16(q+40);r.matchLosses=get16(q+42);
      r.matchMe=q[44];r.matchThem=q[45];r.myTag=get16(q+46);r.theirTag=get16(q+48);
      r.claim=Claim::from(q[50]);r.preMe=q[51];r.preThem=q[52];r.myRpm=get16(q+53);r.theirRpm=get16(q+55);r.stamp=get32(q+57);
      r.matchTo=in[4]==1?(q[61]?0:MATCH_SHORT):q[61];  // version 1 left the byte 0
      const size_t length=strnlen(r.name,NAME_CHARS);
      bool padded=true;for(size_t k=length;k<NAME_CHARS;++k)padded=padded && !r.name[k];
      if(!r.id || !validName(r.name,length) || !padded || q[62] || q[63] || !validMatch(r.matchTo) ||
         r.matchMe>=r.matchTo || r.matchThem>=r.matchTo || r.preMe>=r.matchTo || r.preThem>=r.matchTo ||
         r.stamp>b.clock_ || (r.myTag==0)!=(r.theirTag==0) || (!r.myTag && r.claim.rev))return false;
      for(unsigned k=0;k<i;++k)if(b.rivals_[k].id==r.id)return false;
    }
    for(size_t at=HEADER+b.count_*RIVAL_BYTES;at<n-8;++at)if(in[at])return false;
    *this=b;return true;
  }
private:
  static uint32_t checksum(const uint8_t *p,size_t n){uint32_t h=2166136261u;while(n--){h^=*p++;h*=16777619u;}return h;}
  static void bump(uint16_t &v,unsigned by=1){v=uint16_t(std::min<unsigned>(65535,v+by));}
  static void drop(uint16_t &v,unsigned by=1){v=uint16_t(v>by?v-by:0);}
  Rival *mine(uint32_t id){for(unsigned i=0;i<count_;++i)if(rivals_[i].id==id)return &rivals_[i];return nullptr;}
  // The last battle's result leaves the totals, and `next` enters them.
  void replace(Rival &r,Claim next) {
    const unsigned old=points(r.claim.finish);
    if(r.claim.outcome==Outcome::Won) {
      drop(r.wins);drop(r.pointsFor,old);drop(r.winsBy[unsigned(r.claim.finish)]);
      if(r.preMe+old>=r.matchTo)drop(r.matchWins);
      if(totalWins_)--totalWins_;
      totalPoints_-=std::min<uint32_t>(totalPoints_,old);
    } else if(r.claim.outcome==Outcome::Lost) {
      drop(r.losses);drop(r.pointsAgainst,old);
      if(r.preThem+old>=r.matchTo)drop(r.matchLosses);
      if(totalLosses_)--totalLosses_;
    }
    r.claim=next;r.matchMe=r.preMe;r.matchThem=r.preThem;
    const unsigned scored=points(next.finish);
    if(next.outcome==Outcome::Won) {
      bump(r.wins);bump(r.pointsFor,scored);bump(r.winsBy[unsigned(next.finish)]);
      ++totalWins_;totalPoints_+=scored;
      if(r.preMe+scored>=r.matchTo){bump(r.matchWins);r.matchMe=r.matchThem=0;}
      else r.matchMe=uint8_t(r.preMe+scored);
    } else if(next.outcome==Outcome::Lost) {
      bump(r.losses);bump(r.pointsAgainst,scored);++totalLosses_;
      if(r.preThem+scored>=r.matchTo){bump(r.matchLosses);r.matchMe=r.matchThem=0;}
      else r.matchThem=uint8_t(r.preThem+scored);
    }
    ++generation_;
  }
  Rival rivals_[CAPACITY];
  unsigned count_=0;
  uint32_t generation_=0,clock_=0,totalWins_=0,totalLosses_=0,totalPoints_=0;
};

// The record as a paired phone reads it (docs/BLE_PROTOCOL.md, Get rivals):
// the stick's totals, then each rival, most recently battled first. A rival
// that does not fit `capacity` is left out whole, with those after it.
static constexpr size_t REPORT_HEAD=13,REPORT_RIVAL=4+1+NAME_CHARS+34;
inline size_t report(const Book &book,uint8_t *out,size_t capacity) {
  if(capacity<REPORT_HEAD)return 0;
  out[0]=0;put32(out+1,book.totalWins());put32(out+5,book.totalLosses());put32(out+9,book.totalPoints());
  size_t at=REPORT_HEAD;
  for(unsigned i=0;i<book.count();++i) {
    const Rival &r=*book.recent(i);const size_t n=strnlen(r.name,NAME_CHARS);
    if(at+5+n+34>capacity)break;
    uint8_t *p=out+at;
    put32(p,r.id);p[4]=uint8_t(n);std::memcpy(p+5,r.name,n);p+=5+n;
    put16(p,r.wins);put16(p+2,r.losses);put16(p+4,r.pointsFor);put16(p+6,r.pointsAgainst);
    for(unsigned f=0;f<FINISHES;++f)put16(p+8+2*f,r.winsBy[f]);
    put16(p+16,r.matchWins);put16(p+18,r.matchLosses);p[20]=r.matchMe;p[21]=r.matchThem;
    put16(p+22,r.duels);put16(p+24,r.duelWins);put16(p+26,r.myRpm);put16(p+28,r.theirRpm);
    p[30]=uint8_t(uint8_t(r.claim.outcome)|uint8_t(r.claim.finish)<<2);p[31]=r.matchTo;p[32]=p[33]=0;
    at+=5+n+34;++out[0];
  }
  return at;
}
}
