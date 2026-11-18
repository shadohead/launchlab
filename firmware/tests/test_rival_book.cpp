// Battles (docs/FIRMWARE_BATTLES.md): the record kept against each rival. A
// result can change after it was counted, so every check here is on totals
// after a change as well as before it.
#include <cassert>
#include <iostream>
#include <string>
#include "rival_book.h"

using namespace Battle;

// The totals a rival's battles should add up to, counted independently.
struct Tally {unsigned wins=0,losses=0,pointsFor=0,pointsAgainst=0,matchWins=0,matchLosses=0,me=0,them=0,to=MATCH_SHORT;};
static void score(Tally &t,Outcome o,Finish f) {
  if(o==Outcome::Won){++t.wins;t.pointsFor+=points(f);t.me+=points(f);}
  if(o==Outcome::Lost){++t.losses;t.pointsAgainst+=points(f);t.them+=points(f);}
  if(t.me>=t.to){++t.matchWins;t.me=t.them=0;}
  if(t.them>=t.to){++t.matchLosses;t.me=t.them=0;}
}
static bool same(const Rival &r,const Tally &t) {
  return r.wins==t.wins && r.losses==t.losses && r.pointsFor==t.pointsFor && r.pointsAgainst==t.pointsAgainst &&
    r.matchWins==t.matchWins && r.matchLosses==t.matchLosses && r.matchMe==t.me && r.matchThem==t.them && r.matchTo==t.to;
}

int main() {
  const uint32_t KAI=0xd5d23c11,MAX=0x9fd58c80;
  // A launch together starts a rival's record and counts the faster launch.
  {
    Book book;assert(!book.count() && !book.find(KAI) && !book.recent(0));
    const Rival &r=book.begin(KAI,100,200,6420,5980);
    assert(book.count()==1 && book.find(KAI)==&r && book.recent(0)==&r && !book.recent(1));
    assert(r.duels==1 && r.duelWins==1 && !r.battles() && r.myTag==100 && r.theirTag==200 && r.myRpm==6420 && r.theirRpm==5980 && !r.claim.rev);
    char text[20];displayName(r,text,sizeof(text));assert(std::string(text)=="LL-3C11");
    // Its own name once heard; the same name again changes nothing.
    const uint32_t before=book.generation();
    assert(book.rename(KAI,"Kai") && book.generation()==before+1 && !book.rename(KAI,"Kai") && book.generation()==before+1);
    displayName(r,text,sizeof(text));assert(std::string(text)=="Kai");
    assert(!book.rename(MAX,"Max") && !book.rename(KAI,"") && !book.rename(KAI,"tab\there") && !book.rename(KAI,"SeventeenCharsLong"));
    assert(book.rename(KAI,"SixteenCharsLong") && std::string(r.name)=="SixteenCharsLong");
    // A slower launch is a duel, not a duel won; an equal one neither.
    book.begin(KAI,101,201,5000,5980);assert(r.duels==2 && r.duelWins==1);
    book.begin(KAI,102,202,5000,5000);assert(r.duels==3 && r.duelWins==1);
  }
  // A result enters the totals once, however often it changes.
  {
    Book book;Tally t;
    const Rival &r=book.begin(KAI,1,2,6000,5000);
    assert(book.claim(KAI,Outcome::Won,Finish::Spin) && r.wins==1 && r.pointsFor==1 && r.winsBy[0]==1 && r.matchMe==1 && r.claim.rev==1);
    assert(book.totalWins()==1 && book.totalPoints()==1 && !book.totalLosses());
    // Corrected to an Xtreme finish: still one win, three points.
    assert(book.claim(KAI,Outcome::Won,Finish::Xtreme) && r.wins==1 && r.pointsFor==3 && !r.winsBy[0] && r.winsBy[3]==1 && r.matchMe==3 && r.claim.rev==2);
    assert(book.totalWins()==1 && book.totalPoints()==3);
    // Corrected to a loss: the win leaves the totals altogether.
    assert(book.claim(KAI,Outcome::Lost,Finish::Burst) && !r.wins && r.losses==1 && !r.pointsFor && r.pointsAgainst==2 && !r.winsBy[3] && !r.matchMe && r.matchThem==2);
    assert(!book.totalWins() && !book.totalPoints() && book.totalLosses()==1);
    // The other stick's press for this battle, by its tag; any other tag is not this battle.
    Claim theirs;theirs.rev=4;theirs.outcome=Outcome::Won;theirs.finish=Finish::Over;
    assert(!book.merge(KAI,99,theirs) && !book.merge(MAX,2,theirs) && !book.merge(KAI,2,theirs,7) && !book.merge(KAI,0,theirs));
    assert(book.merge(KAI,2,theirs,1) && r.wins==1 && !r.losses && r.pointsFor==2 && r.claim.rev==4 && !book.merge(KAI,2,theirs));
    score(t,Outcome::Won,Finish::Over);assert(same(r,t));
    // Nothing to claim against a stick never battled.
    assert(!book.claim(MAX,Outcome::Won,Finish::Spin));
  }
  // Matches are first to four; a result that ends one and is then changed
  // gives the match back as it stood.
  {
    Book book;Tally t;
    const Rival &r=book.begin(KAI,1,2,1,1);
    assert(book.claim(KAI,Outcome::Won,Finish::Xtreme));score(t,Outcome::Won,Finish::Xtreme);
    book.begin(KAI,3,4,1,1);assert(r.preMe==3 && !r.preThem && r.matchMe==3);
    assert(book.claim(KAI,Outcome::Won,Finish::Spin));
    assert(r.matchWins==1 && !r.matchMe && !r.matchThem && r.matchEnded() && r.finalMe()==4 && r.finalThem()==0);
    // It was a loss after all: the match is 3-2 and still on.
    assert(book.claim(KAI,Outcome::Lost,Finish::Over) && !r.matchWins && r.matchMe==3 && r.matchThem==2 && !r.matchEnded());
    score(t,Outcome::Lost,Finish::Over);assert(same(r,t));
    // The next battle is lost by burst: 3-4, the match is theirs.
    book.begin(KAI,5,6,1,1);assert(book.claim(KAI,Outcome::Lost,Finish::Burst));score(t,Outcome::Lost,Finish::Burst);
    assert(same(r,t) && r.matchLosses==1 && r.matchEnded() && r.finalMe()==3 && r.finalThem()==4);
    // Void takes it out again; the next battle starts from 3-2.
    Claim none;none.rev=9;none.outcome=Outcome::Void;
    assert(book.merge(KAI,6,none) && !r.matchLosses && r.matchMe==3 && r.matchThem==2 && r.losses==1);
    book.begin(KAI,7,8,1,1);assert(r.preMe==3 && r.preThem==2 && !r.claim.rev);
  }
  // A match's length is fixed when it begins (the session asks for the longer
  // of the two players' choices) and kept to its end, corrections included.
  {
    Book book;
    const Rival &r=book.begin(KAI,1,2,1,1,MATCH_LONG);assert(r.matchTo==MATCH_LONG);
    assert(book.claim(KAI,Outcome::Won,Finish::Xtreme));
    book.begin(KAI,3,4,1,1,MATCH_SHORT);assert(r.matchTo==MATCH_LONG && r.preMe==3);
    assert(book.claim(KAI,Outcome::Won,Finish::Spin) && r.matchMe==4 && !r.matchWins && !r.matchEnded() && !r.matchPoint());
    book.begin(KAI,5,6,1,1);assert(book.claim(KAI,Outcome::Won,Finish::Xtreme) && r.matchWins==1 && r.matchEnded() && r.finalMe()==7);
    assert(book.claim(KAI,Outcome::Lost,Finish::Xtreme) && !r.matchWins && r.matchMe==4 && r.matchThem==3 && r.matchTo==MATCH_LONG);
    assert(book.claim(KAI,Outcome::Won,Finish::Over) && !r.matchWins && r.matchMe==6 && r.matchPoint());
    assert(book.claim(KAI,Outcome::Won,Finish::Xtreme) && r.matchWins==1 && r.matchEnded() && r.finalMe()==7);
    // Match point: either player one finish from the end.
    book.begin(KAI,7,8,1,1,MATCH_SHORT);assert(r.matchTo==MATCH_SHORT && !r.preMe && !r.matchPoint());
    assert(book.claim(KAI,Outcome::Lost,Finish::Xtreme) && r.matchThem==3 && r.matchPoint());
    // A length no firmware offers reads as first to 4.
    book.begin(MAX,1,2,1,1,0);assert(book.find(MAX)->matchTo==MATCH_SHORT);
    book.begin(0x1234,1,2,1,1,MATCH_MAX+1);assert(book.find(0x1234)->matchTo==MATCH_SHORT);
    book.begin(0x5678,1,2,1,1,MATCH_MAX);assert(book.find(0x5678)->matchTo==MATCH_MAX);
  }
  // A long run of results, each changed a few times, against a tally kept alongside.
  {
    Book book;Tally t;uint32_t seed=12345;
    auto next=[&]{seed=seed*1664525u+1013904223u;return seed>>16;};
    for(unsigned battle=1;battle<=400;++battle) {
      // Each battle asks for either length; only one that starts a match gets it.
      const uint8_t asked=next()&1?MATCH_LONG:MATCH_SHORT;
      if(!t.me && !t.them)t.to=asked;
      const Rival &r=book.begin(KAI,uint16_t(battle),uint16_t(1000+battle),uint16_t(next()),uint16_t(next()),asked);
      Outcome o=Outcome::None;Finish f=Finish::Spin;
      for(unsigned change=next()%4;change>0;--change) {
        o=Outcome(1+next()%3);f=Finish(next()%FINISHES);
        if(next()&1)assert(book.claim(KAI,o,f));
        else {Claim c=r.claim;c.set(o,f);assert(book.merge(KAI,r.theirTag,c));}
        if(o==Outcome::Void)f=Finish::Spin;
      }
      score(t,o,f);
      assert(same(r,t) && r.duels==battle);
      assert(book.totalWins()==t.wins && book.totalLosses()==t.losses && book.totalPoints()==t.pointsFor);
      unsigned by=0;for(unsigned k=0;k<FINISHES;++k)by+=r.winsBy[k];
      assert(by==t.wins && r.matchMe<r.matchTo && r.matchThem<r.matchTo);
    }
  }
  // Twelve rivals; a thirteenth takes the place of the one battled longest
  // ago, and the stick's own totals keep what it scored against that one.
  {
    Book book;
    for(uint32_t i=1;i<=Book::CAPACITY;++i){book.begin(i,uint16_t(i),uint16_t(i),1,1);assert(book.claim(i,Outcome::Won,Finish::Spin));}
    assert(book.count()==Book::CAPACITY && book.totalWins()==Book::CAPACITY);
    book.begin(1,50,50,1,1);  // the first is now the most recent
    assert(book.recent(0)->id==1 && book.recent(1)->id==Book::CAPACITY && book.recent(Book::CAPACITY-1)->id==2);
    const Rival &fresh=book.begin(99,60,60,1,1);
    assert(book.count()==Book::CAPACITY && !book.find(2) && book.find(1) && book.find(99)==&fresh && !fresh.wins && fresh.duels==1);
    assert(book.totalWins()==Book::CAPACITY && book.totalPoints()==Book::CAPACITY && book.recent(0)->id==99);
  }
  // A rival never scored against (a stranger whose launch happened to
  // coincide) is the first to make room, however recent, and never takes the
  // place of a scored one while another stranger is there to go.
  {
    Book book;
    for(uint32_t i=1;i<=Book::CAPACITY-1;++i){book.begin(i,uint16_t(i),uint16_t(i),1,1);assert(book.claim(i,i&1?Outcome::Won:Outcome::Lost,Finish::Spin));}
    book.begin(500,50,50,1,1);                    // a stranger: the most recent of all
    assert(book.count()==Book::CAPACITY);
    book.begin(501,51,51,1,1);                    // another: it replaces the first stranger
    assert(book.count()==Book::CAPACITY && !book.find(500) && book.find(501) && book.find(1) && book.find(Book::CAPACITY-1));
    assert(book.claim(501,Outcome::Won,Finish::Burst)); // now a rival like the others
    book.begin(502,52,52,1,1);                    // no stranger left: the rival battled longest ago goes
    assert(!book.find(1) && book.find(2) && book.find(501) && book.find(502));
    // A result that was void in the end still leaves a stranger a stranger.
    Claim none;none.rev=3;none.outcome=Outcome::Void;assert(book.merge(502,52,none));
    book.begin(503,53,53,1,1);
    assert(!book.find(502) && book.find(503) && book.find(2) && book.totalWins()==7 && book.totalLosses()==5);
  }
  // The stored image restores every field, and nothing else reads as one.
  {
    Book book;
    book.begin(KAI,11,22,6420,5980);book.rename(KAI,"Kai");book.claim(KAI,Outcome::Won,Finish::Xtreme);
    book.begin(MAX,33,44,5100,7000);book.claim(MAX,Outcome::Lost,Finish::Over);
    book.begin(KAI,55,66,6000,6100);
    Book::Image image;book.encode(image);
    assert(image.size()==Book::IMAGE_BYTES && Book::IMAGE_BYTES==800);
    assert(image[0]=='L' && image[1]=='L' && image[2]=='R' && image[3]=='B' && image[4]==2 && image[5]==2);
    Book loaded;assert(loaded.decode(image.data(),image.size()));
    assert(loaded.count()==2 && loaded.generation()==book.generation() && loaded.totalWins()==1 && loaded.totalLosses()==1 && loaded.totalPoints()==3);
    for(uint32_t id:{KAI,MAX}) {
      const Rival &a=*book.find(id),&b=*loaded.find(id);
      assert(std::string(a.name)==b.name && a.wins==b.wins && a.losses==b.losses && a.pointsFor==b.pointsFor && a.pointsAgainst==b.pointsAgainst &&
             a.duels==b.duels && a.duelWins==b.duelWins && a.matchWins==b.matchWins && a.matchLosses==b.matchLosses && a.matchMe==b.matchMe &&
             a.matchThem==b.matchThem && a.myTag==b.myTag && a.theirTag==b.theirTag && a.claim==b.claim && a.preMe==b.preMe && a.preThem==b.preThem &&
             a.myRpm==b.myRpm && a.theirRpm==b.theirRpm && a.stamp==b.stamp && a.matchTo==b.matchTo);
      for(unsigned f=0;f<FINISHES;++f)assert(a.winsBy[f]==b.winsBy[f]);
    }
    assert(loaded.recent(0)->id==KAI && loaded.recent(1)->id==MAX);
    // The image is the same again, and the loaded book carries on from it.
    Book::Image again;loaded.encode(again);assert(again==image);
    assert(loaded.claim(KAI,Outcome::Won,Finish::Spin) && loaded.find(KAI)->matchMe==0 && loaded.find(KAI)->matchWins==1);
    // An empty book is a valid record too.
    Book empty,readEmpty;readEmpty.begin(5,1,1,1,1);empty.encode(again);assert(readEmpty.decode(again.data(),again.size()) && !readEmpty.count());
    // Any changed byte, another length or another version leaves the target alone.
    Book kept;kept.begin(7,1,1,1,1);
    for(size_t at=0;at<image.size();++at) {
      Book::Image bad=image;bad[at]^=0x40;
      assert(!kept.decode(bad.data(),bad.size()));
    }
    assert(!kept.decode(image.data(),image.size()-1) && kept.count()==1 && kept.find(7));
    // With a valid checksum: a record no firmware wrote.
    auto forged=[&](size_t at,uint8_t value) {
      Book::Image bad=image;bad[at]=value;
      uint32_t h=2166136261u;for(size_t i=0;i<bad.size()-4;++i){h^=bad[i];h*=16777619u;}
      put32(bad.data()+bad.size()-4,h);
      return !kept.decode(bad.data(),bad.size()) && kept.count()==1;
    };
    const size_t first=Book::HEADER;
    assert(forged(4,3) && forged(4,0));      // a later version, or none
    assert(forged(5,13));                    // more rivals than fit
    assert(forged(6,1));                     // a reserved byte
    {Book::Image bad=image;put32(bad.data()+first,0);uint32_t h=2166136261u;for(size_t i=0;i<bad.size()-4;++i){h^=bad[i];h*=16777619u;}
     put32(bad.data()+bad.size()-4,h);assert(!kept.decode(bad.data(),bad.size()));}  // no ID
    assert(forged(first+4,0x07));            // a control character in a name
    assert(forged(first+4+5,'x'));           // a name with a gap in it
    assert(forged(first+44,4) && forged(first+45,4) && forged(first+51,4) && forged(first+52,4));  // a match score past its end
    assert(forged(first+61,0) && forged(first+61,MATCH_MAX+1));  // a match length no firmware offers
    assert(forged(first+61,3));              // a match score past the end of a shorter match
    assert(forged(first+62,1));              // a reserved byte
    // Version 1 (before match lengths) left the length byte 0: first to 4.
    {
      auto resealed=[](Book::Image &b){uint32_t h=2166136261u;for(size_t i=0;i<b.size()-4;++i){h^=b[i];h*=16777619u;}put32(b.data()+b.size()-4,h);};
      Book::Image old=image;old[4]=1;old[first+61]=old[first+Book::RIVAL_BYTES+61]=0;resealed(old);
      Book readOld;assert(readOld.decode(old.data(),old.size()) && readOld.count()==2 && readOld.find(KAI)->matchTo==MATCH_SHORT);
      Book::Image wrote;readOld.encode(wrote);assert(wrote==image);
      old[first+61]=MATCH_SHORT;resealed(old);assert(!kept.decode(old.data(),old.size()) && kept.count()==1);
    }
    {Book::Image bad=image;put32(bad.data()+first+Book::RIVAL_BYTES,KAI);uint32_t h=2166136261u;for(size_t i=0;i<bad.size()-4;++i){h^=bad[i];h*=16777619u;}
     put32(bad.data()+bad.size()-4,h);assert(!kept.decode(bad.data(),bad.size()));}  // the same rival twice
    assert(forged(first+2*Book::RIVAL_BYTES+3,1));  // bytes after the last rival
    assert(forged(first+60,0x7f));           // used later than the book's own clock
    // Cleared: no rivals, no totals, and a generation that asks for a save.
    const uint32_t generation=book.generation();
    book.clear();assert(!book.count() && !book.totalWins() && !book.totalPoints() && book.generation()==generation+1);
  }
  // What a phone reads (docs/BLE_PROTOCOL.md, Get rivals): the totals, then
  // each rival, most recent first, and never part of one.
  {
    Book book;uint8_t out[512];
    assert(report(book,out,sizeof(out))==REPORT_HEAD && out[0]==0 && !report(book,out,REPORT_HEAD-1));
    book.begin(KAI,11,22,6420,5980);book.rename(KAI,"Kai");book.claim(KAI,Outcome::Won,Finish::Xtreme);
    book.begin(MAX,33,44,5100,7000);book.claim(MAX,Outcome::Lost,Finish::Over);
    const size_t n=report(book,out,sizeof(out));
    assert(n==REPORT_HEAD+(5+0+34)+(5+3+34) && out[0]==2 && get32(out+1)==1 && get32(out+5)==1 && get32(out+9)==3);
    const uint8_t *max=out+REPORT_HEAD,*kai=max+5+34;
    assert(get32(max)==MAX && max[4]==0 && get16(max+5)==0 && get16(max+7)==1 && get16(max+11)==2 && max[5+21]==2 && max[5+30]==(2|1<<2));
    assert(get16(max+5+22)==1 && get16(max+5+24)==0 && get16(max+5+26)==5100 && get16(max+5+28)==7000);
    assert(get32(kai)==KAI && kai[4]==3 && !std::memcmp(kai+5,"Kai",3));
    const uint8_t *k=kai+8;
    assert(get16(k)==1 && get16(k+2)==0 && get16(k+4)==3 && get16(k+6)==0 && get16(k+8+6)==1 && k[20]==3 && k[21]==0 && k[30]==(1|3<<2));
    assert(get16(k+22)==1 && get16(k+24)==1 && get16(k+26)==6420 && get16(k+28)==5980);
    assert(k[31]==MATCH_SHORT && max[5+31]==MATCH_SHORT && !k[32] && !k[33]);
    // Room for one rival only: one is listed, whole.
    assert(report(book,out,REPORT_HEAD+5+34+10)==REPORT_HEAD+5+34 && out[0]==1);
    // A full book of the longest names fits a reply.
    Book full;for(uint32_t i=1;i<=Book::CAPACITY;++i){full.begin(i,uint16_t(i),uint16_t(i),1,1);full.rename(i,"SixteenCharsLong");}
    uint8_t reply[4096];assert(report(full,reply,sizeof(reply))==REPORT_HEAD+Book::CAPACITY*REPORT_RIVAL && reply[0]==Book::CAPACITY);
  }
  std::cout<<"rival book ok\n";
}
