#include "name_setting.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace BlePairing;
struct Value {uint8_t bytes[37];};
static Value value(uint8_t fill){Value v;std::memset(v.bytes,fill,sizeof v.bytes);return v;}
int main() {
  // Name: 1-16 of letters, digits, - and _.
  auto ok=[](const std::string &n){return BleName::valid(n.data(),n.size());};
  assert(ok("a") && ok("Max-Stick_01") && ok("ABCDEFGHIJKLMNOP"));
  assert(!ok("") && !ok("ABCDEFGHIJKLMNOPQ") && !ok("has space") && !ok("umla\xc3\xbct") && !ok("a.b") && !ok("a/b") && !ok(std::string("a\0b",3)));
  const uint8_t mac[6]={1,2,3,4,0xab,0x0c};char full[BleName::FULL];
  BleName::full(full,sizeof full,"",mac);assert(std::string(full)=="LaunchLab-AB0C");
  BleName::full(full,sizeof full,"ABCDEFGHIJKLMNOP",mac);assert(std::string(full)=="LaunchLab-ABCDEFGHIJKLMNOP" && BleName::FULL==27);
  // Its setting: default without a record, survives a restart, nothing invalid.
  {
    Preferences prefs;prefs.begin("launchlab-mini",false);char name[BleName::MAX+1]="x";
    assert(NameSetting::load(prefs,name) && !name[0] && Preferences::data.empty());
    assert(NameSetting::save(prefs,"Stick_1"));
    Preferences again;again.begin("launchlab-mini",false);
    assert(NameSetting::load(again,name) && std::string(name)=="Stick_1");
    const auto good=Preferences::data;
    assert(!NameSetting::save(prefs,"bad name") && Preferences::data==good);
    Preferences::failWrite=true;assert(!NameSetting::save(prefs,"Other"));Preferences::failWrite=false;
    assert(NameSetting::load(prefs,name) && std::string(name)=="Stick_1");
    const auto key=std::string("launchlab-mini")+NameSetting::KEY;
    for(const std::vector<unsigned char> bad:std::initializer_list<std::vector<unsigned char>>{{'a',' '},std::vector<unsigned char>(17,'a')}) {
      Preferences::data[key]=bad;const auto kept=Preferences::data;
      assert(!NameSetting::load(prefs,name) && !name[0] && Preferences::data==kept);
    }
    Preferences::data.clear();
  }
  {
    // Confirmed: the number shows until A, and nothing is sent until encrypted.
    Link l;l.connected(1000);assert(!l.secured() && !l.comparing() && !l.takeDrop(1000));
    l.comparison(2000,false,123,true);assert(l.comparing() && l.number==123 && !l.secured() && !l.takeDrop(3000));
    assert(l.answer(3000,true) && !l.comparing() && !l.number && !l.secured());
    l.encryption(3500,true,true);assert(l.secured() && !l.takeDrop(100000));
    assert(!l.answer(4000,true));
    l.disconnected(5000);assert(!l.secured() && l.phase==Phase::None && !l.failures);
  }
  {
    // B: the link ends once, and it counts.
    Link l;l.connected(0);l.comparison(10,false,5,true);assert(l.answer(20,false));
    assert(!l.comparing() && !l.secured() && l.failures==1 && l.takeDrop(21) && !l.takeDrop(22));
  }
  {
    // Refused while busy: not queued, no number, ends once, and does not count.
    Link l;l.connected(0);l.comparison(10,true,123,false);
    assert(!l.comparing() && !l.number && !l.secured() && l.takeDrop(11) && !l.takeDrop(12) && !l.failures);
    // Another pairing already open.
    Link o;o.connected(0);o.comparison(10,false,1,true);o.comparison(20,false,2,true);assert(!o.comparing() && o.takeDrop(21));
  }
  {
    // Any other association: a passkey or OOB action, or encryption without the
    // comparison (Just Works), never secures.
    Link p;p.connected(0);p.refuse();assert(!p.secured() && p.takeDrop(1));
    Link j;j.connected(0);j.encryption(5,true,false);assert(!j.secured() && j.takeDrop(6));
    Link f;f.connected(0);f.comparison(1,false,2,true);f.answer(2,true);f.encryption(3,false,false);assert(!f.secured() && f.takeDrop(4));
  }
  {
    // A bonded phone: encryption straight from the connection, no prompt.
    Link l;l.connected(0);l.encryption(1,true,true);assert(l.secured() && !l.comparing());
  }
  {
    // Bounded: a number nobody answers rejects. A link that never pairs stays:
    // a Battle Pass app does not pair, and reaches only that open profile.
    Link l;l.connected(100);assert(!l.takeDrop(100+60000) && !l.takeDrop(100+3600000) && !l.secured() && l.phase==Phase::Unsecured);
    Link p;p.connected(0);p.comparison(50000,false,9,true);assert(!p.takeDrop(50000+CONFIRM_MS-1) && p.comparing());
    assert(p.takeDrop(50000+CONFIRM_MS) && !p.comparing() && p.failures==1);
    // After A, the phone still has the rest of its time.
    Link q;q.connected(0);q.comparison(10,false,9,true);q.answer(29000,true);assert(!q.takeDrop(29000+60000) && !q.secured() && !q.comparing());
    // Late host events after the link has gone change nothing.
    Link g;g.encryption(0,true,true);g.comparison(0,false,1,false);g.refuse();assert(g.phase==Phase::None && !g.secured() && !g.takeDrop(1));
  }
  {
    // No bond yet: the number shows for a moment, B can still reject, then the
    // stick accepts by itself, once. Any bond means A: never by itself.
    Link l;l.connected(0);l.comparison(100,false,42,false);
    assert(l.comparing() && l.automatic && !l.takeAutoAccept(100+AUTO_ACCEPT_MS-1) && l.comparing());
    assert(l.takeAutoAccept(100+AUTO_ACCEPT_MS) && !l.comparing() && !l.takeAutoAccept(100+AUTO_ACCEPT_MS+1) && !l.failures);
    l.encryption(5000,true,true);assert(l.secured());
    Link b;b.connected(0);b.comparison(100,false,42,false);assert(b.answer(200,false) && !b.takeAutoAccept(100000) && b.failures==1 && b.takeDrop(201));
    Link a;a.connected(0);a.comparison(100,false,42,true);assert(!a.automatic && !a.takeAutoAccept(100+AUTO_ACCEPT_MS) && !a.takeAutoAccept(100+CONFIRM_MS-1) && a.comparing());
    // Busy and the spam hold refuse it just the same.
    Link u;u.connected(0);u.comparison(1,true,42,false);assert(!u.comparing() && !u.takeAutoAccept(100000) && u.takeDrop(2));
    Link h;h.holding=true;h.holdFrom=0;h.connected(1);h.comparison(2,false,42,false);assert(!h.comparing() && h.takeDrop(3));
  }
  {
    // Spam: 3 rejected or timed-out requests across connections, then 60 s of
    // refusals. A phone that leaves mid-prompt counts. Refusals do not extend it.
    Link l;uint32_t t=1000;
    auto ask=[&](){l.connected(t);l.comparison(t,false,7,true);return l.comparing();};
    assert(ask());l.answer(t,false);l.takeDrop(t);l.disconnected(t);t+=100;
    assert(ask());l.takeDrop(t+CONFIRM_MS);l.disconnected(t+CONFIRM_MS);t+=CONFIRM_MS+100;
    assert(ask());l.disconnected(t);assert(l.holding);
    const uint32_t from=t;
    t=from+1;assert(!ask() && l.takeDrop(t));l.disconnected(t);
    t=from+HOLD_MS-1;assert(!ask());l.disconnected(t);
    t=from+HOLD_MS;assert(ask());l.answer(t,true);l.encryption(t,true,true);assert(l.secured() && !l.failures);
    // A confirmed pairing clears the count.
    Link c;c.failures=2;c.connected(0);c.comparison(1,false,1,true);c.answer(2,true);c.encryption(3,true,true);assert(!c.failures && !c.holding);
    // The hold works across the millis wrap.
    Link w;w.holding=true;w.holdFrom=0xfffffff0u;assert(w.holdingAt(5) && !w.holdingAt(0xfffffff0u+HOLD_MS));
  }
  // The bond record: round trip, sized to what is held, and refused whole if damaged.
  {
    using Image=BleBonds::Image<Value>;
    Image a;assert(a.add(true,value(1)) && a.add(false,value(2)) && a.add(false,value(3)));
    std::vector<uint8_t> bytes(a.bytes());
    assert(bytes.size()==4+3*sizeof(Value)+4 && a.encode(bytes.data(),bytes.size())==bytes.size());
    assert(!a.encode(bytes.data(),bytes.size()-1));
    Image b;assert(b.decode(bytes.data(),bytes.size()) && b.ours==1 && b.peers==2);
    assert(b.our[0].bytes[5]==1 && b.peer[0].bytes[5]==2 && b.peer[1].bytes[36]==3);
    for(size_t i=0;i<bytes.size();++i) {
      auto bad=bytes;bad[i]^=0x40;Image c;assert(!c.decode(bad.data(),bad.size()) && !c.ours && !c.peers);
    }
    Image d;assert(!d.decode(bytes.data(),bytes.size()-1) && !d.decode(bytes.data(),3));
    // Another SDK's struct size is not this one's.
    BleBonds::Image<uint8_t> other;assert(!other.decode(bytes.data(),bytes.size()));
    // Only three of each are kept, as the stack does.
    Image full2;for(uint8_t i=0;i<3;++i){assert(full2.add(true,value(i)) && full2.add(false,value(i)));}
    assert(!full2.add(true,value(9)) && !full2.add(false,value(9)));
    Image none;std::vector<uint8_t> empty(none.bytes());assert(none.encode(empty.data(),empty.size())==8);
    Image e;assert(e.decode(empty.data(),8) && !e.ours && !e.peers);
  }
  std::cout<<"PASS: name validation and setting, numeric comparison confirmed, by itself with no bond, rejected, timed out or refused, spam hold, failures drop the link, bond record round trip and damage\n";
}
