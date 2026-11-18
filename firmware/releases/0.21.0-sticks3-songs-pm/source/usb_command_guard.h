#pragma once
#include <stdint.h>
#include <string.h>

// Single-letter USB commands, guarded against stray byte streams.
//
// On 2026-10-17 binary upload data reached the command parser of two HIL M5s
// and ran backup, restore, a setting change, the sleep test and power-off.
// A web updater bug or line noise could do the same. Status letters (p, P, C)
// and the beywatch updater's backup and restore (K, R; restore validates its
// own lines) still run at once. Every other command must stand alone: it
// starts after QUIET_MS of input quiet, or on the line right after an
// accepted command ("h\nh\n" from a script), and ends with a newline or
// QUIET_MS of quiet, with nothing else in between. Quiet means the receive
// queue was seen empty (idle()) for QUIET_MS after the last byte, never that
// the loop was slow: a 30 ms redraw or a long export left a continuous burst
// looking quiet, and a stray 'n' ran on a HIL M5. The two-part forms
// "g<pin>\n" and "E<id>" may be typed key by key (TYPING_MS between keys),
// except that an id's second digit must follow its first within QUIET_MS;
// a pin needs its newline. Inside a continuous burst of other bytes nothing
// can qualify.
//
// A transfer that ends without its "." line, because it refused a byte or
// timed out, leaves its host mid-upload, and what that host still sends runs
// nothing, not even K or R: an R in a refused backup line started another
// restore, which took the commands after it, and in 0.20.10 a host that
// stalled past the 15 s timeout (a throttled tab) sent its next base64 line
// into the parser, where each K exported a full backup. Pauses prove nothing
// here (the site writes 64-byte pieces 30 ms apart, and a stall can last any
// time), so the guard reads the shape of the input:
// - If the transfer ended mid-line, the rest of that line, up to its newline,
//   is dropped, even a one-character tail such as "K\n".
// - Then every body line is dropped (base64, or the "." end line, which ends
//   this state), however many a host that does not wait sends.
// - A line of exactly one command letter begun after quiet ("L\n", "p\r\n")
//   is a command: no body line is one character (base64 lines are multiples
//   of 4). It runs and ends this state, as does a line that is not body at
//   all ("!hil" on a HIL build, which the normal guard then judges). One glued
//   to the bytes before it is a burst and is dropped.
// - A single letter with quiet before and after it and no newline (the
//   updater's bare K or R, the rig's bare p) runs and ends this state, even
//   inside an unfinished line: no host splits a line that way.
// E<id> and g<pin> are not taken here: "E123" is a valid last base64 line.
// Send a single letter first. There is no timeout: any request a host makes
// starts with a single letter or a '!' line, so the state cannot outlast it.
class UsbCommandGuard {
public:
  static constexpr uint32_t QUIET_MS=20,TYPING_MS=2000;
  static constexpr unsigned MAX=7;
  static bool immediate(char c){return c=='p' || c=='P' || c=='C' || c=='K' || c=='R';}
  static bool guarded(char c){return c && strchr("OSxgiaodhceQEqbnutyzvwjsLTMUFXmAYWVJZG",c);}
  uint32_t rejected=0;
  // A byte for the command parser. True when `out` holds a command to run.
  bool feed(char c,uint32_t now,char *out) {
    const uint32_t gap=seen_?uint32_t(now-lastMs_):UINT32_MAX;
    const bool quiet=quietFor(QUIET_MS);
    seen_=true;lastMs_=now;
    const bool newline=c=='\n' || c=='\r';
    if(used_) {
      if(newline) {
        const bool ok=complete(true);
        if(ok)take(out);else reject();
        chain_=ok;lineStart_=true;return ok;
      }
      if(gap<window() && extends(c)){pending_[used_++]=c;pending_[used_]=0;return false;}
      reject(); // Anything else inside a command: a burst. c is judged below.
    }
    if(newline) {
      // CRLF and blank lines keep the state of the line they end.
      if(!lineStart_)chain_=chainNext_;
      chainNext_=false;lineStart_=true;return false;
    }
    const bool boundary=quiet || (lineStart_ && chain_);
    lineStart_=false;chain_=false;chainNext_=false;
    if(immediate(c)){out[0]=c;out[1]=0;chainNext_=boundary;return true;}
    if(guarded(c)) {
      if(!boundary){++rejected;return false;}
      pending_[0]=c;pending_[1]=0;used_=1;return false;
    }
    return false;
  }
  // Call before each feed() and once per pass, after idle(): a command ended
  // by quiet.
  bool due(uint32_t now,char *out) {
    (void)now;
    if(held_ && quietFor(QUIET_MS)) {
      // A letter alone after an abandoned transfer: a command if quiet came before it too.
      const char c=held_;held_=0;
      if(heldQuiet_)admit(c);else orphan_=Orphan::Body;
    }
    if(ready_){out[0]=ready_;out[1]=0;ready_=0;return true;}
    if(!used_ || !quietFor(window()))return false;
    const bool ok=complete(false);
    if(ok)take(out);else reject();
    return ok;
  }
  // A byte another parser consumed (a HIL line). A line that itself started
  // at a boundary counts as an accepted command, so a command may follow it
  // at once; a '!' inside a burst opens nothing.
  void consumed(char c,uint32_t now) {
    const bool quiet=quietFor(QUIET_MS);
    seen_=true;lastMs_=now;if(used_)reject();
    if(!otherLine_){otherBoundary_=quiet || (lineStart_ && chain_);otherLine_=true;}
    const bool newline=c=='\n' || c=='\r';
    if(newline){chain_=otherBoundary_;otherLine_=false;lineStart_=true;}
    else {chain_=false;lineStart_=false;}
    chainNext_=false;
  }
  // A transfer (restore, theme, milestone or effect request) ended without
  // its "." line: it refused a byte or timed out. `midLine`: it ended inside
  // a line (always so for a refused byte), whose rest the host may still send.
  void refused(uint32_t now,bool midLine){cancel();orphan_=midLine?Orphan::Rest:Orphan::Start;seen_=true;lastMs_=now;}
  // Before feed() and any other parser: true when `c` belongs to the
  // abandoned transfer (see the top of this file) and is dropped. A command
  // letter is held to see what follows: a newline right after it at a line
  // start lets the normal path run it (false), and due() runs it if quiet
  // follows it alone.
  bool drop(char c,uint32_t now) {
    if(orphan_==Orphan::None)return false;
    const bool quiet=quietFor(QUIET_MS);
    seen_=true;lastMs_=now;
    const bool newline=c=='\n' || c=='\r';
    if(held_) {
      const char h=held_;held_=0;
      if(orphan_==Orphan::Start) {
        if(newline) {
          if(heldQuiet_){admit(h);return false;}  // a one-letter line begun after quiet
          return true;  // glued to what came before: a burst, not a command
        }
        orphan_=Orphan::Body;                // more of a body line
      }
    }
    if(orphan_==Orphan::Start) {
      if(newline)return true;
      if(c=='.'){orphan_=Orphan::Dot;return true;}
      if(immediate(c) || guarded(c)){held_=c;heldQuiet_=quiet;return true;}
      if(!base64(c)){end();return false;}  // not a body line: the normal guard judges it
      orphan_=Orphan::Body;return true;
    }
    if(orphan_==Orphan::Dot) {
      if(c=='\r')return true;
      if(c=='\n'){end();return true;}
      orphan_=Orphan::Body;  // not the end line after all
    }
    // The rest of a line: up to its newline, a letter alone after quiet held.
    if(newline)orphan_=Orphan::Start;
    else if(quiet && (immediate(c) || guarded(c))){held_=c;heldQuiet_=true;}
    return true;
  }
  // Call whenever the receive queue is seen empty (each pass).
  void idle(uint32_t now){emptyMs_=now;emptySeen_=true;}
  void cancel(){used_=0;pending_[0]=0;held_=ready_=0;orphan_=Orphan::None;chain_=chainNext_=otherLine_=false;}
private:
  // After an abandoned transfer: the rest of its line, a line start, a body
  // line, or a "." line waiting for its newline.
  enum class Orphan:uint8_t{None,Rest,Start,Body,Dot};
  Orphan orphan_=Orphan::None;
  char pending_[MAX+1]={},held_=0,ready_=0;unsigned used_=0;
  bool seen_=false,lineStart_=true,chain_=false,chainNext_=false,otherLine_=false,otherBoundary_=false,heldQuiet_=false;
  uint32_t lastMs_=0,emptyMs_=0;bool emptySeen_=false;
  // The queue was seen empty at least `ms` after the last byte.
  bool quietFor(uint32_t ms)const {return !seen_ || (emptySeen_ && int32_t(emptyMs_-lastMs_)>=int32_t(ms));}
  // How long a pending command may wait for its next key.
  uint32_t window()const {return pending_[0]=='g' || (pending_[0]=='E' && used_==1)?TYPING_MS:QUIET_MS;}
  bool extends(char c)const {
    const bool digit=c>='0' && c<='9';
    if(pending_[0]=='g')return digit && used_<MAX;
    if(pending_[0]=='E')return digit && used_<=3;  // E<id>: up to three digits (loaded effects are 96-127)
    return false;
  }
  bool complete(bool newline)const {
    if(pending_[0]=='g')return newline && used_>1;
    return true;
  }
  static bool base64(char c){return (c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='+' || c=='/' || c=='=';}
  // The state ends at a line start: what follows is judged as usual.
  void end(){orphan_=Orphan::None;lineStart_=true;chain_=chainNext_=false;}
  // A held letter is a command: as if it had just arrived after quiet.
  void admit(char c) {
    orphan_=Orphan::None;lineStart_=chain_=false;chainNext_=true;
    if(immediate(c))ready_=c;else{pending_[0]=c;pending_[1]=0;used_=1;}
  }
  void take(char *out){memcpy(out,pending_,used_+1);used_=0;pending_[0]=0;}
  void reject(){used_=0;pending_[0]=0;++rejected;chain_=false;}
};
