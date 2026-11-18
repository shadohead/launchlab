// Battles (docs/FIRMWARE_BATTLES.md): the frames two sticks exchange, byte for
// byte as the document lays them out, and the result register they carry.
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "battle_beacon.h"

using namespace Battle;

// Two sticks' registers for one battle, each in its own view.
static bool agree(Claim a,Claim b){return a==b.mirrored();}
// Frames go back and forth until neither register moves.
static void settle(Claim &a,Claim &b) {
  for(unsigned round=0;round<8;++round) {
    const Claim sentA=a,sentB=b;
    const bool movedA=a.merge(sentB.mirrored()),movedB=b.merge(sentA.mirrored());
    if(!movedA && !movedB)return;
  }
  assert(false && "registers never settled");
}

int main() {
  // Points, as Beyblade X scores a finish.
  assert(points(Finish::Spin)==1 && points(Finish::Over)==2 && points(Finish::Burst)==2 && points(Finish::Xtreme)==3);
  assert(std::string(label(Finish::Xtreme))=="Xtreme" && std::string(name(Outcome::Void))=="void");

  // The register's byte: outcome in bits 0-1, finish in 2-3, rev in 4-7.
  {
    Claim c;assert(c.byte()==0 && !c.scored());
    assert(c.set(Outcome::Won,Finish::Burst) && c.byte()==(1|2<<2|1<<4) && c.scored());
    assert(Claim::from(c.byte())==c);
    const Claim m=c.mirrored();
    assert(m.outcome==Outcome::Lost && m.finish==Finish::Burst && m.rev==1 && m.mirrored()==c);
    // Void and None look the same from both sides, and carry no finish.
    assert(Claim::from(3|3<<2|2<<4).finish==Finish::Spin && Claim::from(3|2<<4).mirrored()==Claim::from(3|2<<4));
    assert(Claim::from(0|3<<2).finish==Finish::Spin);
    // The count stops at 15: a register changed that often stays as it is.
    Claim worn;for(unsigned i=0;i<15;++i)assert(worn.set(i&1?Outcome::Won:Outcome::Lost,Finish::Spin));
    assert(worn.rev==15 && !worn.set(Outcome::Won,Finish::Spin) && worn.outcome==Outcome::Lost);
  }
  // One press reaches the other stick, which then holds the mirror of it.
  {
    Claim a,b;a.set(Outcome::Won,Finish::Xtreme);
    assert(b.merge(a.mirrored()) && b.outcome==Outcome::Lost && b.finish==Finish::Xtreme && b.rev==1);
    assert(!a.merge(b.mirrored()) && agree(a,b));
    // A later press on either side wins over the earlier one.
    b.set(Outcome::Won,Finish::Spin);
    assert(a.merge(b.mirrored()) && a.outcome==Outcome::Lost && a.rev==2 && agree(a,b));
    // An older frame still in the air changes nothing.
    Claim stale;stale.set(Outcome::Won,Finish::Over);
    assert(!a.merge(stale) && a.rev==2);
  }
  // Both press at once. The same winner: the winner's own finish stands.
  {
    Claim a,b;a.set(Outcome::Won,Finish::Spin);b.set(Outcome::Lost,Finish::Burst);
    settle(a,b);
    assert(a.outcome==Outcome::Won && a.finish==Finish::Spin && agree(a,b) && a.rev==1);
  }
  // Both claim the win: void on both, until one of them presses again.
  {
    Claim a,b;a.set(Outcome::Won,Finish::Spin);b.set(Outcome::Won,Finish::Over);
    settle(a,b);
    assert(a.outcome==Outcome::Void && b.outcome==Outcome::Void && agree(a,b));
    assert(b.set(Outcome::Lost,Finish::Over));
    settle(a,b);
    assert(a.outcome==Outcome::Won && a.finish==Finish::Over && a.rev==2 && agree(a,b));
  }
  // Whatever two sticks hold, exchanging frames ends with both holding the
  // same result, and in the same result whichever frame arrives first.
  {
    std::vector<Claim> all;
    for(unsigned rev=0;rev<=3;++rev)for(unsigned o=0;o<4;++o)for(unsigned f=0;f<FINISHES;++f) {
      const Claim c=Claim::from(uint8_t(o|f<<2|rev<<4));
      if(rev || o==0)all.push_back(c);
    }
    for(const Claim &x:all)for(const Claim &y:all) {
      Claim a=x,b=y;settle(a,b);assert(agree(a,b));
      // One at a time, either order.
      Claim a1=x,b1=y;b1.merge(a1.mirrored());a1.merge(b1.mirrored());settle(a1,b1);
      Claim a2=x,b2=y;a2.merge(b2.mirrored());b2.merge(a2.mirrored());settle(a2,b2);
      assert(agree(a1,b1) && agree(a2,b2) && a1.outcome==a.outcome && a2.outcome==a.outcome);
      // A register never goes back to an older rev.
      assert(a.rev>=x.rev && b.rev>=y.rev);
    }
  }

  // The launch frame: 23 bytes, little endian.
  {
    LaunchFrame f;f.id=0xd5d23c11;f.tag=0xbeef;f.age=123;f.rpm=6420;f.peer=0x9fd58c80;f.peerTag=0x1234;
    f.claim.set(Outcome::Won,Finish::Over);f.prevTag=0x4321;f.prevClaim=Claim::from(2|1<<2|3<<4);f.matchTo=MATCH_LONG;
    uint8_t b[LAUNCH_BYTES];f.encode(b);
    const uint8_t expected[LAUNCH_BYTES]={0x4c,0xb1,0x11,0x3c,0xd2,0xd5,0xef,0xbe,123,0,0x14,0x19,0x80,0x8c,0xd5,0x9f,0x34,0x12,
      uint8_t(1|1<<2|1<<4),0x21,0x43,uint8_t(2|1<<2|3<<4),7};
    assert(!std::memcmp(b,expected,sizeof(b)));
    LaunchFrame g;assert(g.decode(b,sizeof(b)));
    assert(g.id==f.id && g.tag==f.tag && g.age==f.age && g.rpm==f.rpm && g.peer==f.peer && g.peerTag==f.peerTag &&
           g.claim==f.claim && g.prevTag==f.prevTag && g.prevClaim==f.prevClaim && g.matchTo==MATCH_LONG);
    // Anything else is not a launch frame, and leaves the target as it was.
    LaunchFrame kept=g;
    auto refused=[&](unsigned at,uint8_t value) {
      uint8_t bad[LAUNCH_BYTES];std::memcpy(bad,b,sizeof(bad));bad[at]=value;
      return !kept.decode(bad,sizeof(bad)) && kept.id==f.id && kept.tag==f.tag;
    };
    assert(refused(0,0x4d) && refused(1,0xb2));
    assert(!kept.decode(b,LAUNCH_BYTES-1) && !kept.decode(b,LAUNCH_BYTES+1));
    LaunchFrame odd=f;
    auto invalid=[&](const LaunchFrame &x){uint8_t bytes[LAUNCH_BYTES];x.encode(bytes);LaunchFrame out;return !out.decode(bytes,sizeof(bytes));};
    odd=f;odd.id=0;assert(invalid(odd));
    odd=f;odd.tag=0;assert(invalid(odd));
    odd=f;odd.peer=f.id;assert(invalid(odd));                 // its own opponent
    odd=f;odd.peerTag=0;assert(invalid(odd));                 // an opponent without its launch
    odd=f;odd.peer=0;assert(invalid(odd));
    odd=f;odd.peer=0;odd.peerTag=0;assert(invalid(odd));      // a result with no opponent
    odd=f;odd.prevTag=0;assert(invalid(odd));                 // an earlier result with no earlier battle
    // Any match length up to MATCH_MAX is taken, so a later firmware may offer another.
    for(unsigned to=0;to<=255;++to){odd=f;odd.matchTo=uint8_t(to);assert(invalid(odd)==!(to>=1 && to<=MATCH_MAX));}
    // A launch with no opponent yet and no earlier battle.
    LaunchFrame alone;alone.id=7;alone.tag=9;alone.rpm=5000;
    assert(!invalid(alone));
  }
  // The name frame: the ID, a length and that many printable characters.
  {
    NameFrame n;n.id=0xd5d23c11;std::strcpy(n.name,"Kai");
    uint8_t b[MAX_FRAME];const size_t length=n.encode(b);
    const uint8_t expected[]={0x4c,0xb2,0x11,0x3c,0xd2,0xd5,3,'K','a','i'};
    assert(length==sizeof(expected) && !std::memcmp(b,expected,length));
    NameFrame m;assert(m.decode(b,length) && m.id==n.id && std::string(m.name)=="Kai");
    NameFrame longest;longest.id=1;std::strcpy(longest.name,"SixteenCharsLong");
    assert(longest.encode(b)==MAX_FRAME && m.decode(b,MAX_FRAME) && std::string(m.name)=="SixteenCharsLong");
    // The length must be the frame's; no empty name, no control characters.
    n.encode(b);
    assert(!m.decode(b,length-1) && !m.decode(b,length+1));
    b[6]=0;assert(!m.decode(b,NAME_HEAD));
    n.encode(b);b[8]=0x07;assert(!m.decode(b,length));
    n.encode(b);b[8]=uint8_t(0x80);assert(!m.decode(b,length));
    assert(std::string(m.name)=="SixteenCharsLong");
    assert(validName("LL-D23C",7) && !validName("SeventeenCharsLong",17));
  }
  // As advertising data: flags, then manufacturer data with no company.
  {
    LaunchFrame f;f.id=0x01020304;f.tag=5;f.rpm=4000;
    uint8_t frame[LAUNCH_BYTES],ad[ADVERTISEMENT_MAX];f.encode(frame);
    const size_t length=advertisement(frame,sizeof(frame),ad);
    assert(length==LAUNCH_BYTES+7 && length<=ADVERTISEMENT_MAX);
    assert(ad[0]==2 && ad[1]==0x01 && ad[2]==0x04 && ad[3]==LAUNCH_BYTES+3 && ad[4]==0xff && ad[5]==0xff && ad[6]==0xff);
    const uint8_t *found=nullptr;size_t n=0;
    assert(payload(ad,length,found,n) && n==LAUNCH_BYTES && !std::memcmp(found,frame,n));
    // The longest name frame fits too; anything longer is refused whole.
    uint8_t wide[MAX_FRAME+1]={};
    assert(advertisement(wide,MAX_FRAME,ad)==MAX_FRAME+7 && MAX_FRAME+7<=ADVERTISEMENT_MAX);
    assert(advertisement(wide,ADVERTISEMENT_MAX-6,ad)==0);
    // Other advertisements carry no frame: a phone's, another maker's, a cut-off one.
    const uint8_t phone[]={2,0x01,0x06,17,0x07,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    assert(!payload(phone,sizeof(phone),found,n));
    const uint8_t apple[]={2,0x01,0x06,6,0xff,0x4c,0x00,0x4c,0xb1,0x00};
    assert(!payload(apple,sizeof(apple),found,n));           // company 0x004c, not ours
    const uint8_t noMagic[]={5,0xff,0xff,0xff,0x4d,0xb1};
    assert(!payload(noMagic,sizeof(noMagic),found,n));
    assert(!payload(ad,length-1,found,n));                   // the field runs past the data
    const uint8_t zero[]={0,0,0};
    assert(!payload(zero,sizeof(zero),found,n) && !payload(nullptr,0,found,n));
    // Manufacturer data need not come second.
    uint8_t late[ADVERTISEMENT_MAX]={3,0x03,0xaa,0xfe};
    late[4]=uint8_t(NAME_HEAD+1+3);late[5]=0xff;late[6]=0xff;late[7]=0xff;
    NameFrame who;who.id=9;std::strcpy(who.name,"Z");who.encode(late+8);
    assert(payload(late,8+NAME_HEAD+1,found,n) && n==NAME_HEAD+1 && found==late+8);
  }
  std::cout<<"battle beacon ok\n";
}
