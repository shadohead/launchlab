// Battles (docs/FIRMWARE_BATTLES.md): sticks finding their opponent and
// agreeing on a result, as whole sticks. Each has its own clock and its own
// record, and all they share is the frames one tells while another listens,
// passed here as the bytes that go on the air.
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "battle_session.h"

using namespace Battle;
using View=Session::View;using Scan=Session::Scan;

struct Stick {
  Session session;Book book;
  uint32_t boot=0;      // its clock reads world time plus this
  const char *name="";
  uint8_t events=0;     // everything it reported since the last look
  bool everTold=false,everListened=false;
  Stick(uint32_t id,uint32_t bootAt,const char *called):boot(bootAt),name(called){session.begin(id);}
};

struct World {
  static constexpr uint32_t STEP=50;
  std::vector<Stick *> sticks;
  uint32_t t=1000;
  // rssi[tx][rx]; 0 means out of range.
  int rssi[4][4]={};
  World(std::initializer_list<Stick *> all):sticks(all){for(auto &row:rssi)for(int &v:row)v=-43;}
  uint32_t at(const Stick &s)const{return t+s.boot;}
  void link(unsigned a,unsigned b,int level){rssi[a][b]=rssi[b][a]=level;}
  // A pull that began `pullMs` ago is accepted now.
  void launch(Stick &s,uint16_t rpm,uint16_t tag,uint32_t pullMs=600){s.session.launch(at(s),at(s)-pullMs,rpm,tag,s.book);}
  void step() {
    t+=STEP;
    for(Stick *s:sticks)s->session.tick(at(*s));
    for(unsigned i=0;i<sticks.size();++i) {
      Stick &tx=*sticks[i];
      if(!tx.session.beacon(at(tx)))continue;
      tx.everTold=true;
      // The bytes as advertised: three launch frames, then the name.
      uint8_t frame[MAX_FRAME],ad[ADVERTISEMENT_MAX];size_t n=LAUNCH_BYTES;
      const bool naming=(t/STEP)%4==3;
      if(naming){NameFrame who;who.id=tx.session.id();snprintf(who.name,sizeof(who.name),"%s",tx.name);n=who.encode(frame);}
      else tx.session.frame(at(tx),tx.book).encode(frame);
      const size_t length=advertisement(frame,n,ad);assert(length);
      for(unsigned k=0;k<sticks.size();++k) {
        Stick &rx=*sticks[k];
        const Scan scan=rx.session.scan(at(rx));
        if(k==i || !rssi[i][k] || scan==Scan::Off)continue;
        // Listening a tenth of the time catches one frame in six here.
        if(scan==Scan::Listen && (t/STEP)%6)continue;
        const uint8_t *found=nullptr;size_t size=0;
        assert(payload(ad,length,found,size));
        LaunchFrame f;NameFrame who;
        if(f.decode(found,size))rx.session.heard(at(rx),f,rssi[i][k],rx.book);
        else if(who.decode(found,size))rx.session.heard(who,rx.book);
        else assert(false);
      }
    }
    for(Stick *s:sticks) {
      s->events|=s->session.take();
      if(s->session.scan(at(*s))!=Scan::Off)s->everListened=true;
    }
  }
  void run(uint32_t ms){for(uint32_t done=0;done<ms;done+=STEP)step();}
  void press(Stick &s,Button b){assert(s.session.press(at(s),b,s.book));s.events|=s.session.take();}
  bool quiet()const{for(const Stick *s:sticks)if(s->session.beacon(t+s->boot) || s->session.scan(t+s->boot)!=Scan::Off)return false;return true;}
};

static const uint32_t KAI=0x11110001,MAX=0x22220002,ZOE=0x33330003;
// From the later launch: both sticks have named each other by then.
static const uint32_t MATCHED=4500;
static bool took(Stick &s,uint8_t event){const bool out=s.events&event;s.events&=uint8_t(~event);return out;}
// Both sticks hold the same result for their last battle, each from its side.
static bool agree(const Stick &a,const Stick &b) {
  const Rival *ra=a.book.find(b.session.id()),*rb=b.book.find(a.session.id());
  return ra && rb && ra->claim==rb->claim.mirrored() && ra->myTag==rb->theirTag && ra->theirTag==rb->myTag &&
    ra->wins==rb->losses && ra->losses==rb->wins && ra->pointsFor==rb->pointsAgainst && ra->pointsAgainst==rb->pointsFor &&
    ra->matchMe==rb->matchThem && ra->matchThem==rb->matchMe && ra->matchWins==rb->matchLosses && ra->matchLosses==rb->matchWins;
}

int main() {
  // Two players launch on "go shoot", a third of a second apart, on sticks
  // whose clocks have nothing in common. Nobody presses anything, and each
  // shows the other a few seconds into the battle.
  {
    Stick kai(KAI,5000,"Kai"),max(MAX,90000000,"Max");World w{&kai,&max};
    w.launch(kai,6420,0x1001);
    w.run(300);
    assert(kai.session.detecting() && kai.session.view()==View::None && !kai.book.count());
    w.launch(max,5980,0x2002);
    // Nobody is named while a third stick could still turn up.
    w.run(2000);
    assert(kai.session.detecting() && max.session.detecting() && kai.session.candidates()==1 && !kai.session.frame(w.at(kai),kai.book).peer);
    w.run(MATCHED-2000);
    assert(took(kai,Session::Matched) && took(max,Session::Matched) && took(kai,Session::Changed));
    assert(kai.session.inBattle() && max.session.inBattle() && kai.session.view()==View::Versus && max.session.view()==View::Versus);
    assert(kai.session.rivalId()==MAX && max.session.rivalId()==KAI);
    // How each heard the other: Max pulled 300 ms after Kai, give or take the frame's 10 ms steps.
    assert(kai.session.rivalRssi()==-43 && std::abs(kai.session.rivalOffset()-300)<=60 && std::abs(max.session.rivalOffset()+300)<=60);
    const Rival *k=kai.session.rival(kai.book),*m=max.session.rival(max.book);
    assert(k && m && k->myRpm==6420 && k->theirRpm==5980 && m->myRpm==5980 && m->theirRpm==6420);
    assert(k->duels==1 && k->duelWins==1 && m->duels==1 && !m->duelWins && k->myTag==0x1001 && k->theirTag==0x2002 && agree(kai,max));
    // Each learns the other's name from the same frames.
    w.run(1500);
    assert(std::string(k->name)=="Max" && std::string(m->name)=="Kai");
    // Each keeps telling to the end of its own 8 s window: the other stick
    // needs one of those frames to be in the battle at all.
    assert(kai.session.beacon(w.at(kai)) && max.session.beacon(w.at(max)));
    w.run(2400);
    // The radio then only listens, a tenth of the time, and tells nothing.
    assert(!kai.session.beacon(w.at(kai)) && !max.session.beacon(w.at(max)));
    assert(kai.session.scan(w.at(kai))==Scan::Listen && max.session.scan(w.at(max))==Scan::Listen);

    // Kai won by Xtreme finish: A (won), B three times (spin, over, burst, xtreme), A.
    w.press(kai,Button::A);
    assert(kai.session.view()==View::Pick && kai.session.pickOutcome()==Outcome::Won && kai.session.pickFinish()==Finish::Spin);
    w.press(kai,Button::B);w.press(kai,Button::B);w.press(kai,Button::B);
    assert(kai.session.pickFinish()==Finish::Xtreme && !k->claim.rev);
    w.press(kai,Button::A);
    assert(took(kai,Session::Settled) && kai.session.view()==View::Result && !kai.session.synced());
    assert(k->claim.outcome==Outcome::Won && k->wins==1 && k->pointsFor==3 && k->matchMe==3);
    // Max's stick shows the loss by itself, and both go quiet again.
    w.run(1000);
    assert(took(max,Session::Settled) && max.session.view()==View::Result && m->claim.outcome==Outcome::Lost && m->claim.finish==Finish::Xtreme);
    assert(m->losses==1 && m->pointsAgainst==3 && m->matchThem==3 && agree(kai,max));
    assert(took(kai,Session::Synced) && kai.session.synced() && max.session.synced());
    w.run(2500);
    assert(!kai.session.beacon(w.at(kai)) && !max.session.beacon(w.at(max)));
    // The card stays until it has been on screen for a while.
    w.run(20000);assert(max.session.view()==View::Result);
    max.session.presented(w.at(max));w.run(Session::RESULT_MS-500);assert(max.session.view()==View::Result);
    w.run(1000);assert(max.session.view()==View::None);

    // Max objects: it was an Over finish. B on the card (change), A is not
    // right, so back and B (lost), B (over), A.
    kai.session.presented(w.at(kai));
    w.press(kai,Button::B);assert(kai.session.view()==View::Versus);
    w.press(kai,Button::HoldB);assert(kai.session.view()==View::None && k->claim.rev==1);  // nothing changed after all
    // From Max's side instead, some seconds later: its card is gone, so it waits for the next battle.
    assert(!max.session.press(w.at(max),Button::A,max.book));

    // Next battle. Max wins by burst and enters it himself (B would be lost, A is won).
    w.run(3000);
    w.launch(max,7000,0x2003);w.run(100);w.launch(kai,6100,0x1002);
    w.run(MATCHED);
    assert(took(kai,Session::Matched) && took(max,Session::Matched) && k->duels==2 && k->duelWins==1 && k->preMe==3);
    w.press(max,Button::A);w.press(max,Button::B);w.press(max,Button::B);w.press(max,Button::A);
    assert(m->claim.outcome==Outcome::Won && m->claim.finish==Finish::Burst);
    w.run(1500);
    assert(k->claim.outcome==Outcome::Lost && k->matchMe==3 && k->matchThem==2 && agree(kai,max));
    // Kai corrects it on his card: it was a spin finish. B (change), B (lost), A.
    assert(kai.session.view()==View::Result);
    w.press(kai,Button::B);w.press(kai,Button::B);
    assert(kai.session.view()==View::Pick && kai.session.pickOutcome()==Outcome::Lost);
    w.press(kai,Button::A);
    w.run(1500);
    assert(k->claim.rev==2 && m->claim.rev==2 && m->claim.finish==Finish::Spin && m->pointsFor==1 && k->matchThem==1 && agree(kai,max));
    assert(max.session.view()==View::Result);
    // Totals so far: one win each.
    assert(k->wins==1 && k->losses==1 && m->wins==1 && m->losses==1 && kai.book.totalPoints()==3 && max.book.totalPoints()==1);
  }

  // A pull alone, and pulls too far apart, are not battles; the stick stops
  // telling and listening by itself.
  {
    Stick kai(KAI,0,"Kai"),max(MAX,777,"Max");World w{&kai,&max};
    w.launch(kai,6000,1);
    w.run(Session::DETECT_MS-200);assert(kai.session.detecting() && kai.session.beacon(w.at(kai)));
    w.run(400);
    assert(!kai.session.active() && w.quiet() && !kai.book.count() && !max.everListened);
    // 2.6 s apart.
    w.launch(kai,6000,2);w.run(2600);w.launch(max,6000,3);
    w.run(Session::DETECT_MS+200);
    assert(w.quiet() && !kai.book.count() && !max.book.count() && !took(kai,Session::Matched) && !took(max,Session::Matched));
    // 1.5 s apart is still one battle.
    w.launch(kai,6000,4);w.run(1500);w.launch(max,6000,5);w.run(MATCHED);
    assert(kai.session.inBattle() && max.session.inBattle());
    // A stick that hears its own frame (or one made to look like it) ignores it.
    LaunchFrame self=kai.session.frame(w.at(kai),kai.book);
    const uint32_t generation=kai.book.generation();
    kai.session.heard(w.at(kai),self,-30,kai.book);assert(kai.book.generation()==generation);
  }

  // Two battles side by side: four sticks pulled at the same moment pair up
  // with the one nearest.
  {
    Stick a(KAI,1,"A"),b(MAX,2,"B"),c(ZOE,3,"C"),d(0x44440004,4,"D");World w{&a,&b,&c,&d};
    w.link(0,1,-42);w.link(2,3,-44);
    for(unsigned i:{0u,1u})for(unsigned k:{2u,3u})w.link(i,k,-49);
    w.launch(a,6000,10);w.launch(c,6000,30);w.run(50);w.launch(b,6000,20);w.launch(d,6000,40);
    w.run(MATCHED);
    assert(a.session.rivalId()==MAX && b.session.rivalId()==KAI && c.session.rivalId()==0x44440004 && d.session.rivalId()==ZOE);
    assert(a.book.count()==1 && c.book.count()==1);
    // A result reaches the opponent only.
    w.press(a,Button::A);w.press(a,Button::A);w.run(1500);
    assert(b.book.find(KAI)->losses==1 && !c.book.find(0x44440004)->claim.rev && !d.book.find(ZOE)->claim.rev);
  }
  // Each stick tells its player's match length; a match takes the longer, and
  // a change of mind inside the match waits for the next one.
  {
    Stick kai(KAI,1,"Kai"),max(MAX,2,"Max");World w{&kai,&max};
    kai.session.matchLength(MATCH_LONG);assert(kai.session.matchLength()==MATCH_LONG && max.session.matchLength()==MATCH_SHORT);
    w.launch(kai,6000,10);w.launch(max,6000,20);w.run(MATCHED);
    assert(kai.session.rivalId()==MAX && max.session.rivalId()==KAI);
    assert(kai.session.frame(w.at(kai),kai.book).matchTo==MATCH_LONG && max.session.frame(w.at(max),max.book).matchTo==MATCH_SHORT);
    assert(kai.book.find(MAX)->matchTo==MATCH_LONG && max.book.find(KAI)->matchTo==MATCH_LONG);
    w.press(kai,Button::A);w.press(kai,Button::A);w.run(1500);
    assert(kai.book.find(MAX)->matchMe==1 && max.book.find(KAI)->matchThem==1);
    kai.session.matchLength(MATCH_SHORT);
    w.run(Session::DETECT_MS);w.launch(kai,6000,11);w.launch(max,6000,21);w.run(MATCHED);
    assert(kai.session.rivalId()==MAX && kai.book.find(MAX)->preMe==1 && kai.book.find(MAX)->matchTo==MATCH_LONG && max.book.find(KAI)->matchTo==MATCH_LONG);
    // A length nobody offers is taken as first to 4.
    max.session.matchLength(0);assert(max.session.matchLength()==MATCH_SHORT);
  }
  // Three sticks pulled together: two pair up and the third stays out.
  {
    Stick a(KAI,1,"A"),b(MAX,2,"B"),c(ZOE,3,"C");World w{&a,&b,&c};
    w.link(0,1,-42);w.link(0,2,-48);w.link(1,2,-49);
    w.launch(a,6000,10);w.launch(b,6000,20);w.launch(c,6000,30);
    w.run(Session::DETECT_MS+500);
    assert(a.session.rivalId()==MAX && b.session.rivalId()==KAI && !c.session.active() && !c.book.count());
  }
  // Two sticks farther apart than across a stadium never pair; across one,
  // they pair though every other frame fades far below the floor.
  for(bool far:{true,false}) {
    Stick a(KAI,1,"A"),b(MAX,2,"B");World w{&a,&b};
    w.launch(a,6000,10);w.launch(b,6000,20);
    for(uint32_t ran=0;ran<MATCHED;ran+=World::STEP) {
      w.link(0,1,far?Session::MIN_RSSI-6:(ran/World::STEP)%2?-90:-43);
      w.step();
    }
    assert(a.session.rivalId()==(far?0u:MAX) && b.session.rivalId()==(far?0u:KAI) && a.book.count()==(far?0u:1u));
  }

  // One stick hears nothing of the other for four seconds, just when the
  // other has paired: it still pairs, since the other tells to the end of its
  // window. (With only 2 s of telling after a match, the battle would have
  // been on one stick alone.)
  {
    Stick kai(KAI,0,"Kai"),max(MAX,777,"Max");World w{&kai,&max};
    w.launch(kai,6000,1);w.launch(max,6100,2);
    w.run(2800);assert(kai.session.detecting() && max.session.detecting() && kai.session.candidates()==1 && max.session.candidates()==1);
    w.rssi[0][1]=0;  // Max goes deaf to Kai; Kai still hears Max
    w.run(4000);
    assert(kai.session.inBattle() && max.session.detecting() && kai.session.beacon(w.at(kai)));
    w.rssi[0][1]=-43;w.run(300);
    assert(max.session.inBattle() && max.session.rivalId()==KAI && kai.session.rivalId()==MAX && agree(kai,max));
  }
  // Both press at once, out of each other's hearing.
  {
    Stick kai(KAI,0,"Kai"),max(MAX,31337,"Max");World w{&kai,&max};
    w.launch(kai,6000,1);w.launch(max,6100,2);w.run(MATCHED);
    const Rival *k=kai.book.find(MAX),*m=max.book.find(KAI);
    // The same winner, another finish: the winner's own press stands.
    w.link(0,1,0);
    w.press(kai,Button::A);w.press(kai,Button::A);                        // Kai: won by spin
    w.press(max,Button::B);w.press(max,Button::B);w.press(max,Button::A); // Max: lost by over
    w.link(0,1,-43);w.run(1500);
    assert(k->claim.outcome==Outcome::Won && k->claim.finish==Finish::Spin && k->claim.rev==1 && agree(kai,max) && m->pointsAgainst==1);
    assert(kai.session.synced() && max.session.synced());
    // Both claim the win of the next one: void on both, nothing counted.
    w.launch(kai,6000,3);w.launch(max,6100,4);w.run(MATCHED);
    w.link(0,1,0);
    w.press(kai,Button::A);w.press(kai,Button::A);
    w.press(max,Button::A);w.press(max,Button::A);
    assert(k->wins==2 && m->wins==1);
    w.link(0,1,-43);w.run(2000);
    assert(k->claim.outcome==Outcome::Void && m->claim.outcome==Outcome::Void && k->wins==1 && m->wins==0 && agree(kai,max));
    assert(kai.session.view()==View::Result && max.session.view()==View::Result && kai.session.synced() && max.session.synced());
    // Max gives in: B (change), B (lost), A.
    w.press(max,Button::B);w.press(max,Button::B);w.press(max,Button::A);w.run(1500);
    assert(k->claim.outcome==Outcome::Won && k->claim.rev==2 && k->wins==2 && m->losses==2 && agree(kai,max));
  }

  // A result the other stick never heard reaches it with the next battle,
  // before that battle takes the record's place.
  {
    Stick kai(KAI,0,"Kai"),max(MAX,424242,"Max");World w{&kai,&max};
    w.launch(kai,6000,1);w.launch(max,6100,2);w.run(MATCHED);
    const Rival *k=kai.book.find(MAX),*m=max.book.find(KAI);
    w.link(0,1,0);  // Max walks off
    w.press(kai,Button::A);w.press(kai,Button::B);w.press(kai,Button::A);  // Kai: won by over
    // Kai's stick tells it for a while, then gives up telling, and says so on its card.
    w.run(Session::TELL_MS-1000);assert(kai.session.beacon(w.at(kai)) && !kai.session.synced());
    kai.events=0;
    w.run(2000);assert(!kai.session.beacon(w.at(kai)) && !kai.session.synced() && !m->claim.rev && k->wins==1);
    assert(kai.session.view()==View::Result && took(kai,Session::Redraw));
    kai.session.presented(w.at(kai));
    // Both battles end by themselves, radios off.
    w.run(Session::OPEN_MS);
    assert(w.quiet() && !kai.session.active() && !max.session.active() && max.session.view()==View::None);
    // The next day they battle again.
    w.link(0,1,-43);
    w.launch(kai,6000,3);w.launch(max,6100,4);w.run(MATCHED);
    assert(kai.session.inBattle() && max.session.inBattle());
    assert(m->losses==1 && m->pointsAgainst==2 && m->preThem==2 && k->preMe==2 && m->matchThem==2 && !m->claim.rev && agree(kai,max));
    // And the other way round, with a third stick battled in between: Max's
    // result for this battle reaches Kai two battles later.
    Stick zoe(ZOE,99,"Zoe");w.sticks.push_back(&zoe);
    w.link(0,1,0);
    w.press(max,Button::A);w.press(max,Button::A);  // Max: won by spin
    w.run(Session::OPEN_MS+Session::TELL_MS);
    assert(m->wins==1 && !k->claim.rev);
    w.link(0,2,-50);w.link(1,2,0);
    w.launch(kai,6000,5);w.launch(zoe,5000,6);w.run(MATCHED);
    assert(kai.session.rivalId()==ZOE && kai.book.count()==2);
    w.run(Session::OPEN_MS);
    w.link(0,1,-50);w.link(0,2,0);
    w.launch(kai,6000,7);w.launch(max,6100,8);w.run(MATCHED);
    assert(kai.session.rivalId()==MAX && k->losses==1 && k->pointsAgainst==1 && k->matchMe==2 && k->matchThem==1 && agree(kai,max));
  }

  // Not scoring: hold B puts the card away. The other player's press still
  // arrives and shows.
  {
    Stick kai(KAI,0,"Kai"),max(MAX,5,"Max");World w{&kai,&max};
    w.launch(kai,6000,1);w.launch(max,6100,2);w.run(MATCHED);
    w.press(kai,Button::HoldB);
    assert(kai.session.view()==View::None && kai.session.inBattle() && !kai.session.press(w.at(kai),Button::A,kai.book));
    w.press(max,Button::A);w.press(max,Button::A);w.run(2000);
    assert(kai.session.view()==View::Result && kai.book.find(MAX)->losses==1 && agree(kai,max));
    // A (OK) puts the result away.
    w.press(kai,Button::A);assert(kai.session.view()==View::None);
  }

  // A picker left alone closes with nothing entered; hold B leaves it at once.
  {
    Stick kai(KAI,0,"Kai"),max(MAX,5,"Max");World w{&kai,&max};
    w.launch(kai,6000,1);w.launch(max,6100,2);w.run(MATCHED);
    w.press(kai,Button::B);assert(kai.session.view()==View::Pick && kai.session.pickOutcome()==Outcome::Lost);
    w.run(Session::PICK_MS-500);assert(kai.session.view()==View::Pick);
    w.run(1000);assert(kai.session.view()==View::Versus && !kai.book.find(MAX)->claim.rev);
    w.press(kai,Button::A);w.press(kai,Button::HoldB);assert(kai.session.view()==View::Versus && !kai.book.find(MAX)->claim.rev);
    // B in the picker restarts its time.
    w.press(kai,Button::A);w.run(Session::PICK_MS-500);w.press(kai,Button::B);w.run(Session::PICK_MS-500);
    assert(kai.session.view()==View::Pick && kai.session.pickFinish()==Finish::Over);
  }

  // Everything ends by itself. A battle nobody scores closes after OPEN_MS,
  // and a new launch closes it at once.
  {
    Stick kai(KAI,0xfffff000u,"Kai"),max(MAX,5,"Max");World w{&kai,&max};  // Kai's clock wraps on the way
    w.launch(kai,6000,1);w.launch(max,6100,2);w.run(MATCHED);
    w.run(Session::OPEN_MS-4000);
    assert(kai.session.inBattle() && kai.session.view()==View::Versus && kai.session.scan(w.at(kai))==Scan::Listen);
    w.run(4000);
    assert(!kai.session.active() && kai.session.view()==View::None && w.quiet() && took(kai,Session::Redraw));
    assert(kai.book.find(MAX)->duels==1 && !kai.book.find(MAX)->battles());
    // A result keeps the battle open a little longer, for a correction.
    w.launch(kai,6000,3);w.launch(max,6100,4);w.run(Session::OPEN_MS-5000);
    w.press(kai,Button::A);w.press(kai,Button::A);w.run(10000);
    assert(kai.session.inBattle() && kai.session.scan(w.at(kai))==Scan::Listen);
    kai.session.presented(w.at(kai));max.session.presented(w.at(max));
    w.run(Session::SETTLED_MS);
    assert(w.quiet() && !kai.session.active() && !max.session.active());
    // A result nobody has looked at stays on the card, with the radio off.
    w.launch(kai,6000,5);w.launch(max,6100,6);w.run(MATCHED);
    w.press(max,Button::A);w.press(max,Button::A);w.run(Session::OPEN_MS+5000);
    assert(kai.session.view()==View::Result && w.quiet());
    kai.session.presented(w.at(kai));w.run(Session::RESULT_MS+100);
    assert(kai.session.view()==View::None && !kai.session.active());
    // A new launch alone ends an open battle: the card goes with it.
    w.launch(kai,6000,7);w.launch(max,6100,8);w.run(MATCHED);
    assert(kai.session.view()==View::Versus);
    w.launch(kai,6000,9);
    assert(kai.session.view()==View::None && kai.session.detecting() && !kai.session.inBattle());
    // Stopped (tournament mode, Battles off): nothing told, nothing heard.
    w.run(1000);kai.session.stop();
    assert(!kai.session.active() && !kai.session.beacon(w.at(kai)) && kai.session.scan(w.at(kai))==Scan::Off);
    // A stick with no ID never starts.
    Session blank;Book none;blank.launch(100,50,6000,1,none);
    assert(!blank.active() && !blank.beacon(100));
  }

  // The frame says how long ago the pull began, and stops counting rather
  // than wrapping.
  {
    Session s;Book book;s.begin(KAI);
    s.launch(10000,9400,6420,0,book);
    LaunchFrame f=s.frame(10000,book);
    assert(f.id==KAI && f.tag==1 && f.age==60 && f.rpm==6420 && !f.peer && !f.peerTag && !f.claim.rev && !f.prevTag);
    assert(s.frame(10250,book).age==85 && s.frame(9400+655349,book).age==65534 && s.frame(9400+655350,book).age==AGE_OLD && s.frame(9400+3000000,book).age==AGE_OLD);
    uint8_t bytes[LAUNCH_BYTES];f.encode(bytes);LaunchFrame back;assert(back.decode(bytes,sizeof(bytes)));
    // A frame that old is no candidate.
    Session other;Book otherBook;other.begin(MAX);other.launch(1600,1000,6000,7,otherBook);
    LaunchFrame old=f;old.age=AGE_OLD;other.heard(4600,old,-40,otherBook);assert(!other.candidates());
    old.age=360;other.heard(4600,old,-40,otherBook);assert(other.candidates()==1 && other.frame(4600,otherBook).peerTag==1);
    // The same stick launching again replaces its first launch.
    old.tag=99;old.age=370;other.heard(4700,old,-40,otherBook);assert(other.candidates()==1 && other.frame(4700,otherBook).peerTag==99);
    // And one that turns out not to have launched together is dropped.
    old.age=900;other.heard(4800,old,-40,otherBook);assert(!other.candidates() && !other.frame(4800,otherBook).peer);
    // A stick too faint to be across the stadium is no candidate, however well timed.
    for(int rssi:{-100,-56,-51,-50,-40}) {
      Session one;Book oneBook;one.begin(MAX);one.launch(10000,9000,6000,7,oneBook);
      LaunchFrame peer;peer.id=KAI;peer.tag=3;peer.age=300;
      one.heard(12000,peer,rssi,oneBook);assert(one.candidates()==(rssi>=Session::MIN_RSSI?1u:0u));
    }
    // Near is judged by a stick's strongest frame: one frame faded by a hand in
    // the way does not undo the ones before it, and a faint first frame counts
    // once a near one follows.
    for(bool fadedFirst:{false,true}) {
      Session one;Book oneBook;one.begin(MAX);one.launch(10000,9000,6000,7,oneBook);
      LaunchFrame peer;peer.id=KAI;peer.tag=3;peer.age=300;
      one.heard(12000,peer,fadedFirst?Session::MIN_RSSI-30:Session::MIN_RSSI,oneBook);
      assert(one.candidates()==(fadedFirst?0u:1u));
      peer.age=310;one.heard(12100,peer,fadedFirst?Session::MIN_RSSI:Session::MIN_RSSI-30,oneBook);
      assert(one.candidates()==1);
      one.tick(14000);assert(one.frame(14000,oneBook).peer==KAI);
    }
    // Pulls 2.0 s apart are together, 2.05 s are not, either way round.
    for(int apart:{-2050,-2000,0,2000,2050}) {
      Session one;Book oneBook;one.begin(MAX);one.launch(10000,9000,6000,7,oneBook);
      LaunchFrame peer;peer.id=KAI;peer.tag=3;peer.age=uint16_t((12000-(9000+apart))/10);
      one.heard(12000,peer,-40,oneBook);assert(one.candidates()==(std::abs(apart)<=2000?1u:0u));
    }
  }
  std::cout<<"battle session ok\n";
}
