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
// can qualify. The rest of a line a transfer refused runs nothing either, not
// even K or R: an R in a refused backup line started another restore, which
// took the commands after it.
class UsbCommandGuard {
public:
  static constexpr uint32_t QUIET_MS=20,TYPING_MS=2000;
  static constexpr unsigned MAX=7;
  static bool immediate(char c){return c=='p' || c=='P' || c=='C' || c=='K' || c=='R';}
  static bool guarded(char c){return c && strchr("OSxgiaodhceQEqbnutyzvwjsLTMU",c);}
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
  // A transfer (restore, theme, milestone or effect request) refused the
  // line it was reading: drop() takes the rest of that line.
  void refused(uint32_t now){dropping_=true;dropMs_=now;}
  // Before feed() and any other parser: true when `c` belongs to a refused
  // line. The line ends with its newline, or with QUIET_MS of quiet if the
  // host stopped in the middle of it.
  bool drop(char c,uint32_t now) {
    if(!dropping_)return false;
    if(emptySeen_ && int32_t(emptyMs_-dropMs_)>=int32_t(QUIET_MS)){dropping_=false;return false;}
    dropMs_=now;
    if(c=='\n' || c=='\r'){dropping_=false;lineStart_=true;chain_=chainNext_=false;}
    return true;
  }
  // Call whenever the receive queue is seen empty (each pass).
  void idle(uint32_t now){emptyMs_=now;emptySeen_=true;}
  void cancel(){used_=0;pending_[0]=0;chain_=chainNext_=otherLine_=dropping_=false;}
private:
  char pending_[MAX+1]={};unsigned used_=0;
  bool seen_=false,lineStart_=true,chain_=false,chainNext_=false,otherLine_=false,otherBoundary_=false,dropping_=false;
  uint32_t lastMs_=0,emptyMs_=0,dropMs_=0;bool emptySeen_=false;
  // The queue was seen empty at least `ms` after the last byte.
  bool quietFor(uint32_t ms)const {return !seen_ || (emptySeen_ && int32_t(emptyMs_-lastMs_)>=int32_t(ms));}
  // How long a pending command may wait for its next key.
  uint32_t window()const {return pending_[0]=='g' || (pending_[0]=='E' && used_==1)?TYPING_MS:QUIET_MS;}
  bool extends(char c)const {
    const bool digit=c>='0' && c<='9';
    if(pending_[0]=='g')return digit && used_<MAX;
    if(pending_[0]=='E')return digit && used_<=2;
    return false;
  }
  bool complete(bool newline)const {
    if(pending_[0]=='g')return newline && used_>1;
    return true;
  }
  void take(char *out){memcpy(out,pending_,used_+1);used_=0;pending_[0]=0;}
  void reject(){used_=0;pending_[0]=0;++rejected;chain_=false;}
};
