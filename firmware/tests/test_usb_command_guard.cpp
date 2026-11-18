// Stray USB byte streams must never run a state-changing command, while the
// beywatch updater (K, R), status polling and the repo's tools keep working.
#include "../LaunchLabMini/usb_command_guard.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
struct Run {
  UsbCommandGuard guard;uint32_t now=1000;std::vector<std::string> ran;
  // Bytes `gapMs` apart (0 = one USB burst); the caller polls due() first.
  void send(const std::string &bytes,uint32_t gapMs=0) {
    char out[UsbCommandGuard::MAX+1];
    for(char c:bytes) {
      now+=gapMs;
      if(gapMs){guard.idle(now);if(guard.due(now,out))ran.push_back(out);} // keys typed apart: the queue was empty, and the loop polled
      if(guard.drop(c,now))continue;
      if(guard.due(now,out))ran.push_back(out);
      if(guard.feed(c,now,out))ran.push_back(out);
    }
  }
  void idle(uint32_t ms) {char out[UsbCommandGuard::MAX+1];now+=ms;guard.idle(now);if(guard.due(now,out))ran.push_back(out);}
  unsigned guardedRan()const{unsigned n=0;for(auto &r:ran)if(!UsbCommandGuard::immediate(r[0]))++n;return n;}
};
// The device loop: bytes queue at USB speed; each pass reads up to 32 of
// them, observes an empty queue (idle), and takes 5 ms plus, with `slow`, a
// redraw of up to 30 ms; a status/backup/power-log export adds 60 ms. Quiet
// must mean the queue was empty, never a slow pass (a stray 'n' ran after a
// long power-log export on a HIL M5, 2026-10-17).
struct Loop {
  UsbCommandGuard guard;uint32_t now=1000,rng=7,extra=0;bool slow=false;
  std::vector<std::pair<uint32_t,char>> wire;size_t head=0;std::string rx;std::vector<std::string> ran;
  void arrive(const std::string &bytes){for(char c:bytes)wire.push_back({now,c});}
  void run(const char *command){ran.push_back(command);if(strchr("pPCK",command[0]))extra+=60;}
  void pass() {
    char out[UsbCommandGuard::MAX+1];
    while(head<wire.size() && wire[head].first<=now)rx+=wire[head++].second;
#ifndef OLD_GUARD
    if(rx.empty())guard.idle(now);
#endif
    if(guard.due(now,out))run(out);
    for(unsigned n=0;n<32 && !rx.empty();++n) {
      const char c=rx[0];rx.erase(0,1);
      if(guard.drop(c,now))continue;
      if(guard.due(now,out))run(out);
      if(guard.feed(c,now,out))run(out);
    }
#ifndef OLD_GUARD
    if(rx.empty() && head==wire.size())guard.idle(now);
#endif
    rng=rng*1664525u+1013904223u;
    now+=5+(slow?(rng>>24)%31:0)+extra;extra=0;
  }
  void drain(){while(head<wire.size() || !rx.empty())pass();}
  void idle(uint32_t ms){const uint32_t end=now+ms;while(now<end)pass();}
  unsigned guardedRan()const{unsigned n=0;for(auto &r:ran)if(!UsbCommandGuard::immediate(r[0]))++n;return n;}
};
int main() {
  // Random bursts as the loop sees them, with slow passes and long exports:
  // letters, newlines, the immediate letters that print for tens of ms, runs
  // of dangerous letters, bursts that start or end on one. No guarded command
  // may run. (A burst that is exactly "<letter>\n" after quiet is a command.)
  uint32_t state=2026101701u;auto next=[&]{state=state*1664525u+1013904223u;return uint8_t(state>>24);};
  const std::string dangerous="OSxgiaodhceQEqbnutyzvwjsLTMUFXmAYWVJ";
  unsigned leaked=0;
  for(unsigned trial=0;trial<64;++trial) {
    Loop loop;loop.slow=true;loop.idle(100);
    for(unsigned burst=0;burst<16;++burst) {
      std::string bytes;
      if(burst%4==1)bytes+=dangerous[next()%dangerous.size()];
      bytes+=char(0x80|next());
      for(unsigned i=0;i<4096;++i) {
        const uint8_t r=next();
        if(r<8)bytes+="OOOOSSSSxxxx"+std::string(r%2?"\nS\n":"\nO\n");
        else if(r<16)bytes+="\ng5\n";
        else if(r<24)bytes+=std::string(1,"PpKC"[r%4])+dangerous[next()%dangerous.size()]+"\n";
        else bytes+=char(next());
      }
      if(burst%4==2)bytes+=std::string("\n")+dangerous[next()%dangerous.size()];
      if(burst%4==3)bytes+=dangerous[next()%dangerous.size()];
      loop.arrive(bytes);loop.drain();loop.idle(60+next());
    }
    leaked+=loop.guardedRan();
    assert(loop.guard.rejected>0);
  }
  if(leaked)std::cout<<"leaked "<<leaked<<" guarded command(s) from random bursts\n";
  assert(leaked==0);
  {
    // Burst edges through slow passes: a burst ending "...\nn" or "...n", or
    // starting "n" followed by garbage, runs nothing. A lone "n\n" or "n"
    // after quiet is the command itself.
    for(const char *burst:{"\x91\x02junk\nn","\x91\x02junkn","n\x91\x02junk","Pn\n\x91","\x91p\nn\n"}) {
      Loop loop;loop.slow=true;loop.idle(100);loop.arrive(burst);loop.drain();loop.idle(200);
      if(loop.guardedRan())std::cout<<"burst edge leaked: "<<loop.ran.back()<<"\n";
      assert(loop.guardedRan()==0);
    }
    for(const char *command:{"n\n","n"}) {
      Loop loop;loop.slow=true;loop.idle(100);loop.arrive(command);loop.drain();loop.idle(200);
      assert((loop.ran==std::vector<std::string>{"n"}));
    }
  }
  // The repo's tools and the updater.
  {
    Run run;run.idle(100);run.send("K");run.idle(500);run.send("R");run.send("QUJD\n.\n");
    assert((run.ran==std::vector<std::string>{"K","R"})); // restore lines belong to restoreRx
  }
  {
    // HIL rig status poll: 'p' and the "!hil" line immediately after.
    Run run;run.idle(100);run.send("p!hil\n");assert((run.ran==std::vector<std::string>{"p"}));
  }
  {
    // A bare letter after a pause runs once the input is quiet again.
    Run run;run.idle(100);run.send("w");assert(run.ran.empty());run.idle(19);assert(run.ran.empty());
    run.idle(1);assert((run.ran==std::vector<std::string>{"w"}));
  }
  {
    // Scripted lines run back to back; CRLF and the effect/pin forms work.
    Run run;run.idle(100);run.send("h\nh\nq\r\nE3\nE\ng5\nn\n");
    assert((run.ran==std::vector<std::string>{"h","h","q","E3","E","g5","n"}));
    run.idle(100);run.send("E3");run.idle(25);assert(run.ran.back()=="E3");
    run.idle(100);run.send("g5");run.idle(25);assert(run.ran.back()=="E3"); // a pin needs its newline
  }
  {
    // Effect ids from 10 have two digits and loaded effects (96-127) three,
    // sent together; a fourth is a burst.
    Run run;run.idle(100);run.send("E12\nE17");run.idle(25);
    assert((run.ran==std::vector<std::string>{"E12","E17"}));
    run.idle(100);run.send("E123\n");run.idle(25);assert(run.ran.size()==3 && run.ran.back()=="E123");
    run.idle(100);run.send("E1234\n");run.idle(25);assert(run.ran.size()==3);
    // Typed key by key, a second digit after QUIET_MS is a new burst: E1 runs alone.
    run.idle(100);run.send("E1");run.idle(25);assert(run.ran.back()=="E1");
  }
  {
    // Letters glued together are a burst: "qp" runs only the status letter.
    Run run;run.idle(100);run.send("qp");run.idle(30);assert((run.ran==std::vector<std::string>{"p"}));
    run.idle(100);run.send("hh");run.idle(30);assert((run.ran==std::vector<std::string>{"p"}));
    run.idle(100);run.send("xO\n");run.idle(30);assert((run.ran==std::vector<std::string>{"p"}));
  }
  {
    // Typed by hand, one key every 150 ms then Enter.
    Run run;run.idle(100);run.send("g",0);run.send("7\n",150);run.send("O\n",400);
    assert((run.ran==std::vector<std::string>{"g7","O"}));
  }
  {
    // A HIL line consumed by its own parser; a command may follow at once.
    Run run;run.idle(100);
    for(char c:std::string("!home\n"))run.guard.consumed(c,run.now);
    run.send("v");run.idle(30);assert((run.ran==std::vector<std::string>{"v"}));
    // A '!' inside a burst starts no accepted line (HIL builds parse it).
    run.send("\x93");for(char c:std::string("!x\n"))run.guard.consumed(c,run.now);
    run.send("O\n");run.idle(30);assert((run.ran==std::vector<std::string>{"v"}));
  }
  {
    // The rest of a line a transfer refused runs nothing, not even K or R:
    // an R in a backup line 0.18 refused started another restore, which took
    // the commands after it. A lone letter left before the newline neither.
    // (A refused byte is always inside a line: refused(now,true).)
    Run run;run.idle(100);run.guard.refused(run.now,true);run.send("QUJDRK3p\n");run.idle(30);
    assert(run.ran.empty());
    run.guard.refused(run.now,true);run.send("X\n");run.idle(30);assert(run.ran.empty());
    // The next line, once the host waited for the error, is a command again.
    run.send("L\n");assert((run.ran==std::vector<std::string>{"L"}));
    // A host that paused mid-line, even past the quiet window (the site
    // writes 64-byte pieces 30 ms apart): the rest of the line still runs
    // nothing. Its newline ends it.
    run.idle(100);run.guard.refused(run.now,true);run.send("QU");run.idle(25);run.send("pKR\n");run.idle(30);
    assert((run.ran==std::vector<std::string>{"L"}));
    run.send("L\n");assert((run.ran==std::vector<std::string>{"L","L"}));
  }
  {
    // Through slow passes: base64-like lines refused at a random character,
    // the rest arriving at USB speed. Nothing runs; then "L" does.
    uint32_t state=2026102301u;auto next=[&]{state=state*1664525u+1013904223u;return uint8_t(state>>24);};
    const std::string base64="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for(unsigned trial=0;trial<256;++trial) {
      Loop loop;loop.slow=trial%2;loop.idle(100);
      std::string rest;const unsigned n=next()%97;
      for(unsigned i=0;i<n;++i)rest+=next()%8?base64[next()%64]:"RKXpL"[next()%5];
      loop.guard.refused(loop.now,true);loop.arrive(rest+"\n");loop.drain();loop.idle(60);
      if(!loop.ran.empty())std::cout<<"refused line ran: "<<loop.ran[0]<<" from "<<rest<<"\n";
      assert(loop.ran.empty());
      loop.arrive("L\n");loop.drain();loop.idle(30);assert((loop.ran==std::vector<std::string>{"L"}));
    }
  }
  {
    // Through slow passes: what a host sends after its transfer was refused
    // or timed out, in the site's 64-byte pieces 30-60 ms apart: the rest of
    // the line it was in (any length, cut anywhere when it follows a refusal
    // at once) and then up to three more body lines (multiples of 4
    // characters, as base64 lines are), sometimes with the "." end line, all
    // after a stall of up to 2 s. Nothing runs; then "L" does.
    uint32_t state=2026111301u;auto next=[&]{state=state*1664525u+1013904223u;return uint8_t(state>>24);};
    const std::string base64="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    auto chars=[&](unsigned n){std::string t;for(unsigned i=0;i<n;++i)t+=next()%4?base64[next()%64]:"RKCpPLhwEg"[next()%10];return t;};
    for(unsigned trial=0;trial<512;++trial) {
      Loop loop;loop.slow=trial%2;loop.idle(100);
      const bool midLine=trial%4<2;
      std::string sent=midLine?chars(next()%100)+"\n":"";
      for(unsigned lines=next()%4;lines;--lines)sent+=chars(4*(1+next()%24))+(next()%8?"\n":"\r\n");
      if(next()%2)sent+=".\n";
      const uint32_t stall=trial%8<4?0:uint32_t(next())*8;
      const unsigned first=(stall || !midLine)?64:next()%65;
      loop.guard.refused(loop.now,midLine);loop.idle(stall);
      for(size_t at=0;at<sent.size();) {
        const size_t take=at?64:(first?first:64);
        loop.arrive(sent.substr(at,take));at+=take;loop.drain();loop.idle(30+next()%31);
      }
      loop.idle(60);
      if(!loop.ran.empty())std::cout<<"after an abandoned transfer ran: "<<loop.ran[0]<<" from "<<sent;
      assert(loop.ran.empty());
      loop.arrive("L\n");loop.drain();loop.idle(30);assert((loop.ran==std::vector<std::string>{"L"}));
    }
  }
  {
    // A transfer that timed out: the host stalled 15 s or more mid-transfer
    // (a throttled background tab), then sent the rest. On 0.20.10 the timeout
    // left the guard reading commands, so each K in the host's next base64
    // line exported a full backup and each R started a restore.
    const std::string line=std::string("KRCpPQUJDRA0KRCpPQUJDRA0KRCpPQUJDRA0KRCpPQUJDRA0")+"KRCpPQUJDRA0KRCpPQUJDRA0KRCpPQUJDRA0KRCpPQUJDRA0";
    assert(line.size()==96);
    for(const uint32_t stall:{uint32_t(30),uint32_t(1000),uint32_t(60000)}) {
      // Between lines (the site waits for each line's acknowledgement): the
      // next whole line, plain or in pieces of 64 and 33, runs nothing.
      Run run;run.idle(100);run.idle(15000);run.guard.refused(run.now,false);  // RESTORE_ERROR reason=timeout
      run.idle(stall);run.send(line+"\n");run.idle(100);
      if(!run.ran.empty())std::cout<<"timed-out transfer ran "<<run.ran.size()<<" command(s), first "<<run.ran[0]<<", after "<<stall<<" ms\n";
      assert(run.ran.empty());
      run.idle(15000);run.guard.refused(run.now,false);
      run.idle(stall);run.send(line.substr(0,64));run.idle(30);run.send(line.substr(64)+"\n");run.idle(100);
      assert(run.ran.empty());
      // A 65-character last line: its second piece is "K\n".
      run.idle(15000);run.guard.refused(run.now,false);
      run.idle(stall);run.send(line.substr(0,64));run.idle(30);run.send("K\n");run.idle(100);
      assert(run.ran.empty());
      // A host that does not wait per line sends several, then ".": nothing runs.
      run.idle(15000);run.guard.refused(run.now,false);
      run.idle(stall);run.send(line+"\n"+line+"\r\n"+line.substr(0,40)+"\n.\n");run.idle(100);
      assert(run.ran.empty());
      // Several lines without the "." line, in pieces: nothing; a short last
      // line that reads like E<id> ("E123") is body too.
      run.idle(15000);run.guard.refused(run.now,false);
      run.idle(stall);run.send(line.substr(0,64));run.idle(30);run.send(line.substr(64)+"\n"+line.substr(0,31));
      run.idle(30);run.send(line.substr(31)+"\nE123\n");run.idle(100);
      assert(run.ran.empty());
      // Stalled mid-line: the rest of it, a one-character tail included, and
      // the lines after it.
      for(const char *rest:{"RKpCPQUJD\n","K\n","R\r\n","p\n","h\n","\n","K\nKRCp\n.\n"}) {
        run.idle(15000);run.guard.refused(run.now,true);run.idle(stall);run.send(rest);run.idle(100);
        if(!run.ran.empty())std::cout<<"the rest of a timed-out line ("<<rest<<") ran "<<run.ran[0]<<"\n";
        assert(run.ran.empty());
      }
      // Then the host's next request runs: the updater's bare K, then R and its lines.
      run.send("K");run.idle(25);run.send("R");run.send("QUJD\n.\n");run.idle(30);
      run.send("L\n");run.idle(30);
      assert((run.ran==std::vector<std::string>{"K","R","L"}));
    }
    // Between lines, a newline-ended command runs at once, the first thing
    // after the timeout or after resumed lines: no body line is one character.
    {
      Run run;run.idle(100);
      for(const char *command:{"L\n","T\n","p\n","M\r\n","K\n","R\n"}) {
        run.idle(15000);run.guard.refused(run.now,false);run.idle(500);run.send(command);run.idle(30);
        assert(!run.ran.empty() && run.ran.back()==std::string(1,command[0]));
      }
      run.idle(15000);run.guard.refused(run.now,false);run.idle(500);run.send(line+"\n");run.idle(30);run.send("L\n");run.idle(30);
      assert(run.ran.size()==7 && run.ran.back()=="L");
      // Glued to resumed lines in the same write it is a burst, not a
      // command (as in the normal guard); sent after quiet it runs.
      run.idle(15000);run.guard.refused(run.now,false);run.idle(500);run.send(line+"\n"+line+"\np\n");run.idle(30);
      assert(run.ran.size()==7);
      run.send("p\n");run.idle(30);assert(run.ran.size()==8 && run.ran.back()=="p");
      // A line that is not body ('!', a HIL line) ends the state; the normal
      // guard judges it and what follows.
      run.idle(15000);run.guard.refused(run.now,false);run.idle(500);run.send("!hil\n");run.idle(30);run.send("w\n");
      assert(run.ran.size()==9 && run.ran.back()=="w");
      // A one-letter line glued to the body line before it is dropped; the next, after quiet, runs.
      run.idle(15000);run.guard.refused(run.now,false);run.idle(500);run.send(line.substr(0,4)+"\nh\n");run.idle(30);
      assert(run.ran.size()==9);
      run.send("h\n");run.idle(30);assert(run.ran.size()==10 && run.ran.back()=="h");
      // E<id> and g<pin> are not taken while the state lasts ("E123" is a
      // valid last base64 line); a single letter first ends it.
      run.idle(15000);run.guard.refused(run.now,false);run.idle(500);run.send("E12\n");run.idle(30);run.send("g5\n");run.idle(30);
      assert(run.ran.size()==10);
      run.send("E\n");run.idle(30);run.send("E12\n");run.idle(30);
      assert(run.ran.size()==12 && (std::vector<std::string>(run.ran.end()-2,run.ran.end())==std::vector<std::string>{"E","E12"}));
      // A letter at a line start right after the previous line with quiet only after it is body.
      run.idle(15000);run.guard.refused(run.now,false);run.idle(500);run.send(line.substr(0,4)+"\nK");run.idle(30);
      run.send("RCp\n");run.idle(30);assert(run.ran.size()==12);
    }
    // A host that stopped mid-line and never sent its newline: a letter that
    // stands alone (quiet before and after it) is a command again, so the
    // updater's bare K and R still work. A newline-ended command right after
    // is taken as the end of that line; the next one runs.
    {
      Run run;run.idle(100);run.guard.refused(run.now,true);run.idle(500);
      run.send("K");run.idle(19);assert(run.ran.empty());run.idle(1);assert((run.ran==std::vector<std::string>{"K"}));
      run.idle(15000);run.guard.refused(run.now,true);run.idle(500);run.send("R");run.idle(25);
      assert((run.ran==std::vector<std::string>{"K","R"}));
      run.idle(15000);run.guard.refused(run.now,true);run.idle(500);run.send("p");run.idle(25);
      assert((run.ran==std::vector<std::string>{"K","R","p"}));
      run.idle(15000);run.guard.refused(run.now,true);run.idle(500);run.send("w");run.idle(25);
      assert((run.ran==std::vector<std::string>{"K","R","p","w"}));
      run.idle(15000);run.guard.refused(run.now,true);run.idle(500);run.send("L\n");run.idle(30);run.send("L\n");
      assert((run.ran==std::vector<std::string>{"K","R","p","w","L"}));
      // A pin typed key by key still works after it.
      run.idle(15000);run.guard.refused(run.now,true);run.idle(500);run.send("g",0);run.send("7\n",150);
      assert((run.ran==std::vector<std::string>{"K","R","p","w","L","g7"}));
      // The CRLF of the line ends it; the next command runs.
      run.idle(15000);run.guard.refused(run.now,true);run.idle(500);run.send("QUJD\r\n");run.idle(30);run.send("h\r\n");
      assert(!run.ran.empty() && run.ran.back()=="h");
      // Bare letters end it from any point: a line start, a body line, a "." line.
      for(const char *before:{"","QUJD\n","QUJD\nQU","QUJD\n."}) {
        const size_t n=run.ran.size();
        run.idle(15000);run.guard.refused(run.now,false);run.send(before);run.idle(500);run.send("K");run.idle(25);
        assert(run.ran.size()==n+1 && run.ran.back()=="K");
      }
    }
  }
  {
    // The hosts' own sequences, unchanged: a lone K; a lone R and its restore
    // body; the updater's K then R; commands separated by quiet; CRLF.
    Run run;run.idle(100);run.send("K");assert((run.ran==std::vector<std::string>{"K"}));
    run.idle(500);run.send("R");run.send("QUJD\n.\n");run.idle(30);
    assert((run.ran==std::vector<std::string>{"K","R"}));
    run.idle(100);run.send("p");run.idle(100);run.send("p\r\n");run.idle(100);run.send("L\r\n");run.idle(100);run.send("K\r\n");
    assert((run.ran==std::vector<std::string>{"K","R","p","p","L","K"}));
  }
  std::cout<<"PASS: 64 x 16 random 4 KiB bursts with dangerous-letter runs, slow passes and long exports run no guarded command; burst edges neither; K/R, status polling, bare, scripted, CRLF, typed, E<id>/g<pin> and post-HIL commands still run; the rest of a line whose transfer was refused or timed out runs nothing, K and R included, and so do further body lines until a \".\" line or a one-letter command, after any stall and in 64-byte pieces; a one-letter line or a bare K or R then runs\n";
}
