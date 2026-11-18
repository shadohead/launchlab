#pragma once
#include <cstdlib>
#include "rival_book.h"

// Battles (docs/FIRMWARE_BATTLES.md): one stick's side of finding its opponent
// and agreeing on the result. Host-testable: it is told about launches, heard
// frames, presses and time, and says what the radio and the screen should do.
//
// A launch opens a short window (Detect) in which the stick tells its launch
// and listens. A stick whose pull began within TOGETHER_MS of this one is a
// candidate. Once every such stick has had time to finish its pull and be
// heard (NAME_AFTER_MS), each names the candidate it prefers in its own
// frames: the nearest, by how close together the pulls began and how loud the
// stick is. Once the one it names has named it back the two are in a battle.
// Nobody presses anything for that. Waiting before naming is what keeps a
// stranger who pulled a moment earlier from taking the opponent's place. The battle then stays open for the result: either
// player enters it, the frames carry it to the other stick, and it can be
// corrected until the next launch.
//
// Everything here ends on its own: no state renews a timer, and none keeps
// the stick awake.
namespace Battle {
enum class Button:uint8_t {A,B,HoldB};

class Session {
public:
  static constexpr uint32_t DETECT_MS=8000;     // telling a launch and listening for one made with it
  static constexpr int32_t TOGETHER_MS=2000;    // pulls that began this close together are one battle
  // From the pull's start: a pull begun TOGETHER_MS later has ended, been
  // accepted and been heard by then (a pull and its acceptance take about a second).
  static constexpr uint32_t NAME_AFTER_MS=TOGETHER_MS+1500;
  // A stick heard no stronger than this is no opponent: it is not across the
  // same stadium. Two sticks as far apart as players get across a stadium
  // (about 28 cm, 2026-11-16) heard each other's strongest frame at -41 to
  // -44 dBm in 40 of 40 launches, and side by side at -33 dBm; single frames
  // fade far lower (-81), so the strongest one is what counts (near()). This
  // leaves about 6 dB for hands and launchers in the way, which in open air
  // is about twice that distance.
  static constexpr int MIN_RSSI=-50;
  // Still telling after the match, so the other stick has it too: to the end
  // of this stick's own window, and this long at least. The other stick needs
  // one of these frames to be in the battle at all, so a few seconds of them
  // lost (a hand or a body in the way) must not leave it out.
  static constexpr uint32_t LINGER_MS=2000;
  static constexpr uint32_t OPEN_MS=300000;     // a battle waits this long for its result
  static constexpr uint32_t SETTLED_MS=60000;   // and this long after one, for a correction
  static constexpr uint32_t TELL_MS=15000;      // telling a result the other stick has not shown back
  static constexpr uint32_t ECHO_MS=1500;       // showing a result back once both agree
  static constexpr uint32_t PICK_MS=12000;      // a picker left alone closes with nothing entered
  static constexpr uint32_t RESULT_MS=8000;     // the result card, from when it is first on screen
  static constexpr unsigned CANDIDATES=4;
  // How hard the radio listens: all the time it can, or a tenth of it.
  enum class Scan:uint8_t {Off,Detect,Listen};
  enum class View:uint8_t {None,Versus,Pick,Result};
  enum Event:uint8_t {Matched=1,Settled=2,Synced=4,Changed=8,Redraw=16};

  // This stick's ID, never 0. Without one nothing is told or matched.
  void begin(uint32_t id){id_=id;}
  uint32_t id()const{return id_;}
  // The match length this stick's player has chosen, told with every launch.
  // A match takes the longer of the two players' choices when it begins.
  void matchLength(uint8_t to){matchTo_=validMatch(to)?to:MATCH_SHORT;}
  uint8_t matchLength()const{return matchTo_;}
  // Ends whatever was going on: tournament mode, Battles turned off, a wipe.
  void stop(){if(phase_!=Phase::Idle || view_!=View::None)events_|=Redraw;phase_=Phase::Idle;view_=View::None;}

  // An accepted pull that began at `startedAt`. `tag` is a fresh random number.
  // The battle before it, if any, keeps whatever result it had.
  void launch(uint32_t now,uint32_t startedAt,uint16_t rpm,uint16_t tag,const Book &book) {
    if(!id_)return;
    phase_=Phase::Detect;view_=View::None;tag_=tag?tag:1;rpm_=rpm;startedAt_=startedAt;detectUntil_=now+DETECT_MS;
    // Never later than the window allows the other stick to answer in.
    nameAt_=startedAt+NAME_AFTER_MS;if(int32_t(nameAt_-(now+DETECT_MS/2))>0)nameAt_=now+DETECT_MS/2;
    for(auto &c:candidates_)c=Candidate{};
    chosen_=-1;rival_=Opponent{};synced_=true;tellUntil_=now;beforeTag_=0;beforeClaim_=Claim{};
    // The last battle this stick was in: its opponent may have missed its result.
    lastId_=0;lastTag_=0;lastClaim_=Claim{};
    if(const Rival *r=book.recent(0))if(r->myTag){lastId_=r->id;lastTag_=r->myTag;lastClaim_=r->claim;}
    events_|=Redraw;
  }

  // A launch frame from another stick, as it arrived at `now`.
  void heard(uint32_t now,const LaunchFrame &f,int rssi,Book &book) {
    if(!id_ || f.id==id_)return;
    // What the sender holds of its battles with this stick: the one it is in
    // and the one before it. Either may be news here.
    const bool current=f.peer==id_ && book.merge(f.id,f.tag,f.claim.mirrored(),f.peerTag);
    const bool earlier=f.prevTag && book.merge(f.id,f.prevTag,f.prevClaim.mirrored());
    if(current || earlier)events_|=Changed;
    if(phase_==Phase::Battle && f.id==rival_.id && f.tag==rival_.tag && f.peer==id_ && f.peerTag==tag_) {
      const Rival *r=book.find(rival_.id);
      if(current) {
        // The other player's press: show it here.
        view_=View::Result;resultSeen_=false;synced_=false;tellUntil_=now+TELL_MS;
        if(int32_t(openUntil_-(now+SETTLED_MS))<0)openUntil_=now+SETTLED_MS;
        events_|=Settled|Redraw;
      }
      if(r && f.claim.mirrored()==r->claim && !synced_) {
        // Both hold the same result: say so a little longer, then go quiet.
        synced_=true;tellUntil_=now+ECHO_MS;events_|=Synced|Redraw;
      }
    }
    if(phase_!=Phase::Detect)return;
    const int32_t offset=int32_t(now-uint32_t(f.age)*10-startedAt_);
    note(f,f.age!=AGE_OLD && std::abs(offset)<=TOGETHER_MS,offset,rssi);
    choose(now);
    if(chosen_>=0 && candidates_[chosen_].peer==id_ && candidates_[chosen_].peerTag==tag_)match(now,book);
  }
  // A rival's name, as it gives it itself. Strangers' names are not kept.
  void heard(const NameFrame &n,Book &book){if(book.rename(n.id,n.name))events_|=Changed|Redraw;}

  // A button while a battle card is on screen. True when the press was the battle's.
  bool press(uint32_t now,Button button,Book &book) {
    switch(view_) {
      case View::None:return false;
      case View::Versus:
        if(button==Button::HoldB)view_=View::None;  // not scoring this one; the other player still can
        else {pickOutcome_=button==Button::A?Outcome::Won:Outcome::Lost;pickFinish_=Finish::Spin;pickedAt_=now;view_=View::Pick;}
        break;
      case View::Pick:
        if(button==Button::HoldB)view_=View::Versus;
        else if(button==Button::B){pickFinish_=Finish((unsigned(pickFinish_)+1)%FINISHES);pickedAt_=now;}
        else if(book.claim(rival_.id,pickOutcome_,pickFinish_)) {
          view_=View::Result;resultSeen_=false;synced_=false;tellUntil_=now+TELL_MS;
          if(int32_t(openUntil_-(now+SETTLED_MS))<0)openUntil_=now+SETTLED_MS;
          events_|=Settled|Changed;
        } else view_=View::Versus;  // changed too often to change again
        break;
      case View::Result:view_=button==Button::B?View::Versus:View::None;break;
    }
    events_|=Redraw;return true;
  }
  // The result card is on screen: it stays RESULT_MS from the first time.
  void presented(uint32_t now){if(view_==View::Result && !resultSeen_){resultSeen_=true;resultAt_=now;}}

  void tick(uint32_t now) {
    if(phase_==Phase::Detect && due(now,detectUntil_)){phase_=Phase::Idle;events_|=Redraw;}
    if(phase_==Phase::Detect)choose(now);
    // The result card says whether the result is still being told: draw it again when that ends.
    const bool telling=phase_==Phase::Battle && !due(now,tellUntil_);
    if(telling!=telling_){telling_=telling;if(view_==View::Result)events_|=Redraw;}
    if(phase_!=Phase::Battle)return;
    if(view_==View::Pick && now-pickedAt_>=PICK_MS){view_=View::Versus;events_|=Redraw;}
    if(view_==View::Result && resultSeen_ && now-resultAt_>=RESULT_MS){view_=View::None;events_|=Redraw;}
    // A result not looked at yet stays up, with the radio already off.
    if(due(now,openUntil_) && due(now,tellUntil_) && view_!=View::Pick && view_!=View::Result) {
      if(view_!=View::None)events_|=Redraw;
      phase_=Phase::Idle;view_=View::None;
    }
  }

  // What the radio should be doing now.
  bool beacon(uint32_t now)const{return phase_==Phase::Detect || (phase_==Phase::Battle && !due(now,tellUntil_));}
  Scan scan(uint32_t now)const {
    if(phase_==Phase::Detect)return Scan::Detect;
    if(phase_!=Phase::Battle)return Scan::Off;
    if(!due(now,tellUntil_))return Scan::Detect;
    return due(now,openUntil_)?Scan::Off:Scan::Listen;
  }
  // The frame to tell while beacon() holds.
  LaunchFrame frame(uint32_t now,const Book &book) {
    LaunchFrame f;f.id=id_;f.tag=tag_;f.rpm=rpm_;f.matchTo=matchTo_;
    const uint32_t age=(now-startedAt_)/10;f.age=uint16_t(age<AGE_OLD?age:AGE_OLD);
    const Rival *before=nullptr;
    if(phase_==Phase::Battle) {
      f.peer=rival_.id;f.peerTag=rival_.tag;
      if(const Rival *r=book.find(rival_.id))f.claim=r->claim;
    } else if(chosen_>=0) {
      f.peer=candidates_[chosen_].id;f.peerTag=candidates_[chosen_].tag;
      before=book.find(f.peer);
    }
    // The battle before this one with the same stick, else the last battle at all.
    if(phase_==Phase::Battle && beforeTag_){f.prevTag=beforeTag_;f.prevClaim=beforeClaim_;}
    else if(before && before->myTag){f.prevTag=before->myTag;f.prevClaim=before->claim;}
    else if(lastTag_) {
      if(const Rival *r=book.find(lastId_))if(r->myTag==lastTag_)lastClaim_=r->claim;
      f.prevTag=lastTag_;f.prevClaim=lastClaim_;
    }
    return f;
  }

  View view()const{return view_;}
  bool active()const{return phase_!=Phase::Idle;}
  bool detecting()const{return phase_==Phase::Detect;}
  bool inBattle()const{return phase_==Phase::Battle;}
  // The opponent's record while in a battle, or showing its card.
  const Rival *rival(const Book &book)const{return phase_==Phase::Battle?book.find(rival_.id):nullptr;}
  uint32_t rivalId()const{return phase_==Phase::Battle?rival_.id:0;}
  // How the opponent was heard when the battle began: its strongest frame, in
  // dBm, and how long after this stick's pull its own began (ms, negative for before).
  int rivalRssi()const{return rival_.rssi;}
  int32_t rivalOffset()const{return rival_.offset;}
  Outcome pickOutcome()const{return pickOutcome_;}
  Finish pickFinish()const{return pickFinish_;}
  // Whether the other stick has shown the same result back.
  bool synced()const{return synced_;}
  uint16_t tag()const{return tag_;}
  unsigned candidates()const{unsigned n=0;for(const auto &c:candidates_)n+=near(c);return n;}
  uint8_t take(){const uint8_t out=events_;events_=0;return out;}

private:
  enum class Phase:uint8_t {Idle,Detect,Battle};
  struct Candidate {uint32_t id=0,peer=0;uint16_t tag=0,peerTag=0,rpm=0;int32_t offset=0;int rssi=-127;uint8_t matchTo=MATCH_SHORT;};
  struct Opponent {uint32_t id=0;uint16_t tag=0;int rssi=0;int32_t offset=0;};
  static bool due(uint32_t now,uint32_t at){return int32_t(now-at)>=0;}
  // Near enough to be across the stadium, by the strongest of its frames:
  // one frame faded by a hand or a body does not undo the ones before it.
  static bool near(const Candidate &c){return c.id && c.rssi>=MIN_RSSI;}
  // Lower is better: how far apart the pulls began, and how far away the stick
  // sounds (20 dB weaker counts like half a second later).
  static int32_t score(const Candidate &c){return std::abs(c.offset)+25*std::min(60,std::max(0,-40-c.rssi));}
  // A stick that names this one counts as that much nearer: between two that
  // are about as near, the one that has chosen this stick is the opponent.
  static constexpr int32_t NAMED_BONUS=400;
  void note(const LaunchFrame &f,bool together,int32_t offset,int rssi) {
    Candidate *slot=nullptr;
    for(auto &c:candidates_)if(c.id==f.id)slot=&c;
    if(!together){if(slot)*slot=Candidate{};return;}
    if(!slot)for(auto &c:candidates_)if(!c.id && !slot)slot=&c;
    if(!slot) {
      // Full: the worst one makes room for a better one.
      Candidate fresh;fresh.offset=offset;fresh.rssi=rssi;
      for(auto &c:candidates_)if(!slot || score(c)>score(*slot))slot=&c;
      if(score(fresh)>=score(*slot))return;
      *slot=Candidate{};
    }
    // Another launch from the same stick starts it over.
    if(slot->id!=f.id || slot->tag!=f.tag){*slot=Candidate{};slot->id=f.id;slot->tag=f.tag;slot->offset=offset;}
    slot->rpm=f.rpm;slot->matchTo=f.matchTo;slot->peer=f.peer;slot->peerTag=f.peerTag;slot->rssi=std::max(slot->rssi,rssi);
  }
  // The nearest stick that has no other opponent. Nobody is named before
  // every stick that pulled together with this one can have been heard.
  void choose(uint32_t now) {
    chosen_=-1;int32_t best=0;
    if(!due(now,nameAt_))return;
    for(unsigned i=0;i<CANDIDATES;++i) {
      const Candidate &c=candidates_[i];
      if(!near(c))continue;
      const bool me=c.peer==id_ && c.peerTag==tag_;
      if(!me && c.peer)continue;  // it has an opponent, and it is not this stick
      const int32_t value=score(c)-(me?NAMED_BONUS:0);
      if(chosen_<0 || value<best){chosen_=int(i);best=value;}
    }
  }
  void match(uint32_t now,Book &book) {
    const Candidate c=candidates_[chosen_];
    // The record is about to hold this battle: keep the one before it to tell.
    beforeTag_=0;beforeClaim_=Claim{};
    if(const Rival *r=book.find(c.id))if(r->myTag){beforeTag_=r->myTag;beforeClaim_=r->claim;}
    book.begin(c.id,tag_,c.tag,rpm_,c.rpm,std::max(matchTo_,c.matchTo));
    rival_.id=c.id;rival_.tag=c.tag;rival_.rssi=c.rssi;rival_.offset=c.offset;phase_=Phase::Battle;view_=View::Versus;synced_=true;
    openUntil_=now+OPEN_MS;tellUntil_=now+LINGER_MS;
    if(int32_t(detectUntil_-tellUntil_)>0)tellUntil_=detectUntil_;
    events_|=Matched|Changed|Redraw;
  }
  uint32_t id_=0;
  Phase phase_=Phase::Idle;View view_=View::None;
  uint8_t matchTo_=MATCH_SHORT;
  uint16_t tag_=0,rpm_=0;uint32_t startedAt_=0,detectUntil_=0,nameAt_=0,openUntil_=0,tellUntil_=0;
  Candidate candidates_[CANDIDATES];int chosen_=-1;
  Opponent rival_;
  Outcome pickOutcome_=Outcome::Won;Finish pickFinish_=Finish::Spin;uint32_t pickedAt_=0;
  bool resultSeen_=false,synced_=true,telling_=false;uint32_t resultAt_=0;
  uint32_t lastId_=0;uint16_t lastTag_=0,beforeTag_=0;Claim lastClaim_,beforeClaim_;
  uint8_t events_=0;
};
}
