#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <Preferences.h>
#include <esp_random.h>
#include <host/ble_hs.h>
#include <host/ble_store.h>
#include <host/util/util.h>
#include <memory>
#include <services/gap/ble_svc_gap.h>
#include "ble_battle_pass.h"
#include "ble_security.h"
#include "name_setting.h"

// The stick's Bluetooth link. One stack and one link at a time carry profiles,
// each a GATT service with its own contract:
//   - Battle Pass (ble_battle_pass.h, docs/BLE_BATTLE_PASS.md): what apps
//     written for the BEYBATTLE PASS read. Open, as the real device is, so it
//     reports launches and changes nothing. The stick advertises under its name.
//   - LaunchLab (docs/BLE_PROTOCOL.md): commands, replies and typed events for
//     everything else, on a paired and encrypted link only.
// A new kind of data is a new event type or command of the LaunchLab profile, or
// a new profile here; neither touches the other.
// Callbacks run on the NimBLE host task and only store what arrived: commands
// run in the loop, which owns all settings and history. Nothing is sent while
// a pull is being measured, apart from what `busy` lets through afterwards.
namespace BleLink {
static constexpr uint32_t START_AFTER_MS=1500,ADVERTISE_RETRY_MS=2000,FRAME_GAP_MS=15,BANNER_MS=1500;
static constexpr size_t REPLY_BYTES=4096,COMMAND_BYTES=17,MAX_FRAME_BYTES=180,FRAME_HEAD=3;
static constexpr uint8_t FLAG_LAST=1,FLAG_ERROR=2;
static constexpr char SERVICE[]="4c4c0001-6265-7977-6174-63686c61626c";
static constexpr char EVENTS[]="4c4c0002-6265-7977-6174-63686c61626c";
static constexpr char COMMAND[]="4c4c0003-6265-7977-6174-63686c61626c";
static constexpr char REPLY[]="4c4c0004-6265-7977-6174-63686c61626c";
enum class State:uint8_t {Off,Ready,Connected};
// A HIL build's LaunchLab profile needs no pairing, so the rig can read it (its
// tap below, or its host as the phone with tools/ble_check); it is never a
// release, and the pairing itself is checked by hand on a production build.
#if LAUNCHLAB_HIL
static constexpr bool OPEN_FOR_RIG=true;
#else
static constexpr bool OPEN_FOR_RIG=false;
#endif

// Fills `out`; returns its length, or minus a protocol status for an error frame.
using Handler=int32_t(*)(uint8_t op,const uint8_t *args,size_t length,uint8_t *out,size_t capacity);

static BLEServer *server=nullptr;
static BLECharacteristic *events=nullptr,*reply=nullptr,*passNotify=nullptr;
// The Battle Pass profile: what an app asked (host task), and packets on their way (loop).
static BattlePass::Requests passRequests;
static BattlePass::Outbox passOutbox;
static volatile bool passSubscribed=false;
static uint32_t passPackets=0;
static Handler handler=nullptr;
static uint8_t *replyBuffer=nullptr;
static volatile bool linkUp=false,started=false,initFailed=false,commandWaiting=false;
static volatile uint32_t connects=0,disconnects=0,commandsDropped=0;
static uint8_t commandBytes[1+COMMAND_BYTES];
static volatile size_t commandLength=0;
static size_t replyLength=0,replySent=0;
static uint8_t replyOp=0,replySeq=0,replyFlags=0;
static bool replying=false,advertising=false;
static uint32_t lastAdvertiseTry=0,lastFrame=0,notifies=0,startedAt=0;
static size_t heapBefore=0,heapAfter=0;
static State shown=State::Off;
static bool bannerShown=false,bannerConnected=false;
static uint32_t bannerUntil=0;
// Security state: written by the host task, read by the loop, under the mux.
static BlePairing::Link securityLink;
static portMUX_TYPE securityMux=portMUX_INITIALIZER_UNLOCKED;
static volatile bool promptBusy=false;
// Live orientation, owned by the loop: asked for by the paired phone, and forgotten with its link.
static bool liveOrientation=false;
static uint32_t disconnectsSeen=0;
static bool keyWasShown=false;
// The stack's bond store is RAM only here (no CONFIG_BT_NIMBLE_NVS_PERSIST): keep
// its code, and copy its security records to Preferences through the settings
// save, inside a checkpoint, never in a pull. These notice a change on the host task.
using BondImage=BleBonds::Image<ble_store_value_sec>;
static ble_store_write_fn *storeWrite=nullptr;
static ble_store_delete_fn *storeDelete=nullptr;
static volatile bool bondsChanged=false,bondsRestoring=false;
static bool bondsRestored=false;
static Preferences *bondPrefs=nullptr;
static constexpr const char *BONDS_KEY="ble_bonds";
static bool isBondRecord(int type){return type==BLE_STORE_OBJ_TYPE_OUR_SEC || type==BLE_STORE_OBJ_TYPE_PEER_SEC;}
static int bondStoreWrite(int type,const ble_store_value *value) {
  const int rc=storeWrite(type,value);
  if(!rc && isBondRecord(type) && !bondsRestoring)bondsChanged=true;
  return rc;
}
static int bondStoreDelete(int type,const ble_store_key *key) {
  const int rc=storeDelete(type,key);
  if(!rc && isBondRecord(type) && !bondsRestoring)bondsChanged=true;
  return rc;
}
// The chosen name (empty: the default) and what it advertises as.
static char chosenName[BleName::MAX+1]="";
static char advertisedName[BleName::FULL]="";
static volatile bool nameChanged=false;

// The library answers a numeric comparison inside its own event handler, so it
// could not wait for a button. Its handler is private: an explicit instantiation
// may name it. Each link's events come here first, the rest pass to the library.
using GapHandler=int(*)(ble_gap_event *,void *);
GapHandler libraryGapHandler();
template<GapHandler H> struct ExposeGapHandler {friend GapHandler libraryGapHandler(){return H;}};
template struct ExposeGapHandler<&BLEServer::handleGATTServerEvent>;
static int gapEvents(ble_gap_event *event,void *arg) {
  if(event->type!=BLE_GAP_EVENT_PASSKEY_ACTION)return libraryGapHandler()(event,arg);
  // Asked only of a new phone; a bonded one restores encryption. Answered from the loop.
  const bool compare=event->passkey.params.action==BLE_SM_IOACT_NUMCMP;
  const uint32_t code=event->passkey.params.numcmp,now=millis();
  int bonds=0;ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC,&bonds);
  portENTER_CRITICAL(&securityMux);
  if(compare)securityLink.comparison(now,promptBusy,code,bonds>0);else securityLink.refuse();
  portEXIT_CRITICAL(&securityMux);
  return 0;
}
class ServerEvents:public BLEServerCallbacks {
  void onConnect(BLEServer *)override{
    linkUp=true;++connects;
    portENTER_CRITICAL(&securityMux);securityLink.connected(millis());portEXIT_CRITICAL(&securityMux);
  }
  void onConnect(BLEServer *owner,ble_gap_conn_desc *desc)override{ble_gap_set_event_cb(desc->conn_handle,gapEvents,owner);}
  void onDisconnect(BLEServer *)override{
    linkUp=false;commandWaiting=false;passSubscribed=false;passRequests.clear();++disconnects;
    const uint32_t now=millis();
    portENTER_CRITICAL(&securityMux);securityLink.disconnected(now);portEXIT_CRITICAL(&securityMux);
  }
  void onAuthenticationComplete(ble_gap_conn_desc *desc)override{
    const bool encrypted=desc->sec_state.encrypted,authenticated=desc->sec_state.authenticated;
    const uint32_t now=millis();
    // A peer that could not compare (Just Works) keeps no bond here.
    if(encrypted && !authenticated)ble_store_util_delete_peer(&desc->peer_id_addr);
    portENTER_CRITICAL(&securityMux);securityLink.encryption(now,encrypted,authenticated);portEXIT_CRITICAL(&securityMux);
  }
};
class CommandEvents:public BLECharacteristicCallbacks {
  // The library calls both onWrite overloads for one write, and this one's
  // default calls the other: overriding the plain one would run commands twice.
  void onWrite(BLECharacteristic *c,ble_gap_conn_desc *)override{
    const String value=c->getValue();
    const size_t length=value.length();
    // The characteristic's permissions already require the secured link.
    if(!length || length>sizeof(commandBytes) || commandWaiting){++commandsDropped;return;}
    memcpy(commandBytes,value.c_str(),length);commandLength=length;commandWaiting=true;
  }
};
// The Battle Pass profile's two characteristics: one-byte commands on either
// (apps differ on which), and a subscription to the notifying one.
class PassEvents:public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c,ble_gap_conn_desc *)override{
    const String value=c->getValue();
    if(value.length()==1)passRequests.command(uint8_t(value[0]));
  }
  void onSubscribe(BLECharacteristic *c,ble_gap_conn_desc *,uint16_t subscribed)override{
    if(c!=passNotify)return;
    passSubscribed=subscribed&1;
    if(passSubscribed)passRequests.subscribed(millis());
  }
};

static void initTask(void *) {
  heapBefore=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  replyBuffer=static_cast<uint8_t *>(heap_caps_malloc(REPLY_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!replyBuffer){initFailed=true;vTaskDelete(nullptr);return;}
  uint8_t mac[6]={};esp_read_mac(mac,ESP_MAC_BT);
  BleName::full(advertisedName,sizeof(advertisedName),chosenName,mac);
  // Battle Pass apps look for this name, exactly, and phones remember a
  // device's own name over its advertisement: the stick is this to every scan.
  // Its LaunchLab name is in Get info.
  BLEDevice::init(BattlePass::NAME);
  BLEDevice::setMTU(MAX_FRAME_BYTES+3);
  // LE Secure Connections, numeric comparison only: the stick shows the number and A
  // confirms. MITM and Secure Connections only leave no Just Works or legacy way in.
  // Directly: on NimBLE, BLESecurity's setters use statics the core never defines.
  ble_hs_cfg.sm_io_cap=BLE_HS_IO_DISPLAY_YESNO;
  ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_mitm=1;ble_hs_cfg.sm_sc=1;ble_hs_cfg.sm_sc_only=1;
  ble_hs_cfg.sm_our_key_dist=ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
  storeWrite=ble_hs_cfg.store_write_cb;storeDelete=ble_hs_cfg.store_delete_cb;
  ble_hs_cfg.store_write_cb=bondStoreWrite;ble_hs_cfg.store_delete_cb=bondStoreDelete;
  server=BLEDevice::createServer();
  server->setCallbacks(new ServerEvents());
  auto *service=server->createService(SERVICE);
  // The stack takes these flags as they are. Subscribing has no permission of its
  // own, so these reads carry it, and the library then notifies encrypted links only;
  // the loop also sends nothing until the link is secured.
  const uint32_t protectedRead=BLECharacteristic::PROPERTY_READ|(OPEN_FOR_RIG?0:BLECharacteristic::PROPERTY_READ_ENC|BLECharacteristic::PROPERTY_READ_AUTHEN);
  events=service->createCharacteristic(EVENTS,BLECharacteristic::PROPERTY_NOTIFY|protectedRead);
  reply=service->createCharacteristic(REPLY,BLECharacteristic::PROPERTY_NOTIFY|protectedRead);
  auto *command=service->createCharacteristic(COMMAND,BLECharacteristic::PROPERTY_WRITE|(OPEN_FOR_RIG?0:BLECharacteristic::PROPERTY_WRITE_ENC|BLECharacteristic::PROPERTY_WRITE_AUTHEN));
  command->setCallbacks(new CommandEvents());
  service->start();
  // The Battle Pass profile: no pairing, as its apps expect. Read-only data.
  auto *pass=server->createService(BattlePass::SERVICE);
  auto *passEvents=new PassEvents();
  const uint32_t openWrite=BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_WRITE_NR;
  pass->createCharacteristic(BattlePass::WRITE,openWrite)->setCallbacks(passEvents);
  passNotify=pass->createCharacteristic(BattlePass::NOTIFY,BLECharacteristic::PROPERTY_NOTIFY|openWrite);
  passNotify->setCallbacks(passEvents);
  pass->start();
  // Its service is the one advertised (one 128-bit UUID fits beside the name);
  // a LaunchLab app finds its own service on the link.
  auto *ad=BLEDevice::getAdvertising();
  ad->addServiceUUID(BattlePass::SERVICE);
  ad->setScanResponse(true);
  heapAfter=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  started=true;
  vTaskDelete(nullptr);
}

// The rig's tap (HIL builds only, `!bletap 1`): a link without the radio. Both
// profiles then run as for a subscribed, paired phone, and every packet they
// would notify is printed over USB instead (BLE_TAP pass|event|reply HEX), so
// the rig reads exactly what an app would get for the pulls and poses it
// injects. Commands come in with `!blepass` and `!blell`. Everything but the
// stack's own transport is the production code.
#if LAUNCHLAB_HIL
static bool tap=false;
inline bool tapped(){return tap;}
#else
constexpr bool tapped(){return false;}
#endif
enum class Channel:uint8_t {Pass,Event,Reply};
static void deliver(Channel channel,const uint8_t *data,size_t length) {
#if LAUNCHLAB_HIL
  if(tap) {
    static const char *const names[]={"pass","event","reply"};
    char text[2*MAX_FRAME_BYTES+1];size_t at=0;
    for(size_t i=0;i<length && i<MAX_FRAME_BYTES;++i)at+=snprintf(text+at,sizeof(text)-at,"%02x",data[i]);
    text[at]=0;Serial.printf("BLE_TAP %s %s\n",names[unsigned(channel)],text);
    return;
  }
#endif
  BLECharacteristic *c=channel==Channel::Pass?passNotify:channel==Channel::Event?events:reply;
  if(!c)return;
  c->setValue(const_cast<uint8_t *>(data),length);c->notify();++notifies;
}
inline bool connected(){return (started && linkUp) || tapped();}
inline bool secured() {
  portENTER_CRITICAL(&securityMux);const bool out=securityLink.secured();portEXIT_CRITICAL(&securityMux);
  return (out || OPEN_FOR_RIG) && connected();
}
// The 6-digit number while a pairing waits for A or B.
inline bool comparison(uint32_t &number,bool &automatic) {
  portENTER_CRITICAL(&securityMux);const bool out=securityLink.comparing();number=securityLink.number;automatic=securityLink.automatic;portEXIT_CRITICAL(&securityMux);
  return out && connected();
}
// From the loop, never the host task: the stack waits for this. A refusal ends the link.
inline void answerComparison(bool accept) {
  const uint32_t now=millis();
  portENTER_CRITICAL(&securityMux);const bool asked=securityLink.answer(now,accept);portEXIT_CRITICAL(&securityMux);
  if(!asked)return;
  ble_sm_io io{};io.action=BLE_SM_IOACT_NUMCMP;io.numcmp_accept=accept;
  ble_sm_inject_io(server->getConnId(),&io);
}
// A reply's last frame has had time to leave: a restart may follow it.
inline bool replyFlushed(uint32_t now){return !replying && now-lastFrame>=300;}
// No prompt during a pull, an editor, Wipe or tournament: refused, not queued.
inline void setPromptBusy(bool busy){promptBusy=busy;}
inline int bondCount() {
  int bonds=0;
  if(started)ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC,&bonds);
  else if(bondPrefs && bondPrefs->isKey(BONDS_KEY)) {
    // Not started yet: the saved record's own count, without decoding its keys.
    uint8_t head[BleBonds::HEADER];
    if(bondPrefs->getBytesLength(BONDS_KEY)>=sizeof(head)) {
      auto whole=std::unique_ptr<uint8_t[]>(new(std::nothrow) uint8_t[bondPrefs->getBytesLength(BONDS_KEY)]);
      if(whole && bondPrefs->getBytes(BONDS_KEY,whole.get(),bondPrefs->getBytesLength(BONDS_KEY))>=sizeof(head)){std::memcpy(head,whole.get(),sizeof(head));bonds=head[3]&15;}
    }
  }
  return bonds;
}
inline bool takeBondsChanged(){const bool out=bondsChanged;bondsChanged=false;return out;}
// Forget phones: ends the link and empties the store; the caller queues the save.
inline void forget() {
  if(!started)return;
  if(linkUp)server->disconnect(server->getConnId());
  ble_store_clear();
}
// The record, from the loop inside a checkpoint. Empty removes it.
static int bondCollect(int type,ble_store_value *value,void *image) {
  static_cast<BondImage *>(image)->add(type==BLE_STORE_OBJ_TYPE_OUR_SEC,value->sec);return 0;
}
inline bool saveBonds(Preferences &prefs) {
  if(!started)return !prefs.isKey(BONDS_KEY) || prefs.remove(BONDS_KEY);
  auto image=std::unique_ptr<BondImage>(new(std::nothrow) BondImage);
  if(!image)return false;
  ble_store_iterate(BLE_STORE_OBJ_TYPE_OUR_SEC,bondCollect,image.get());
  ble_store_iterate(BLE_STORE_OBJ_TYPE_PEER_SEC,bondCollect,image.get());
  if(!image->ours && !image->peers)return !prefs.isKey(BONDS_KEY) || prefs.remove(BONDS_KEY);
  const size_t capacity=image->bytes();
  auto bytes=std::unique_ptr<uint8_t[]>(new(std::nothrow) uint8_t[capacity]);
  if(!bytes)return false;
  const size_t n=image->encode(bytes.get(),capacity);
  return n && prefs.putBytes(BONDS_KEY,bytes.get(),n)==n;
}
// Before the first advertising, so no phone can arrive first. Reading NVS is safe in a pull.
static void restoreBonds() {
  if(!bondPrefs || !bondPrefs->isKey(BONDS_KEY))return;
  const size_t n=bondPrefs->getBytesLength(BONDS_KEY);
  auto bytes=std::unique_ptr<uint8_t[]>(new(std::nothrow) uint8_t[n?n:1]);
  auto image=std::unique_ptr<BondImage>(new(std::nothrow) BondImage);
  if(!bytes || !image || bondPrefs->getBytes(BONDS_KEY,bytes.get(),n)!=n || !image->decode(bytes.get(),n)) {
    Serial.println("BLE_BONDS_NOTICE unreadable_record_retained");return;
  }
  bondsRestoring=true;
  for(unsigned i=0;i<image->ours;++i)ble_store_write_our_sec(&image->our[i]);
  for(unsigned i=0;i<image->peers;++i)ble_store_write_peer_sec(&image->peer[i]);
  bondsRestoring=false;
}
// Set name: saved by the caller, advertised from the next advertising.
inline void setName(const char *name,size_t length) {
  std::memcpy(chosenName,name,length);chosenName[length]=0;nameChanged=true;
}
inline bool saveName(Preferences &prefs){return NameSetting::save(prefs,chosenName);}
inline void loadName(Preferences &prefs){if(!NameSetting::load(prefs,chosenName))Serial.println("BLE_NAME_NOTICE invalid_saved_name_retained_using_default");}
inline const char *fullName() {
  if(!advertisedName[0] || nameChanged) {
    uint8_t mac[6]={};esp_read_mac(mac,ESP_MAC_BT);
    BleName::full(advertisedName,sizeof(advertisedName),chosenName,mac);
  }
  return advertisedName;
}
inline State state(bool enabled) {
  if(!enabled || !started)return State::Off;
  return linkUp?State::Connected:State::Ready;
}

// The Battle Pass profile, for the loop: whether an app listens, what it asked
// for, and packets for it. A launch's set replaces whatever still waits.
inline bool passListening(){return connected() && passSubscribed;}
inline bool takePassStatus(uint32_t now){return passRequests.takeStatus(now) && passListening();}
inline bool takePassShots(){return passRequests.takeShots() && passListening();}
inline void passClear(){passOutbox.clear();}
inline void passSend(const BattlePass::Packet &packet){if(passListening())passOutbox.push(packet);}

// A typed event of the LaunchLab profile (docs/BLE_PROTOCOL.md, Events): the
// way a new kind of live data reaches a paired phone. Byte 0 is the type.
inline void publishEvent(const uint8_t *payload,size_t length) {
  if(!secured())return;
  deliver(Channel::Event,payload,length);
}
// Live orientation (event type 2): on only while the phone that asked is linked.
inline void setLiveOrientation(bool on){liveOrientation=on && secured();}
inline bool liveOrientationOn(){return liveOrientation && secured();}
// Called with each accepted pull, after it has ended.
inline void publishLaunch(uint32_t number,uint32_t session,float rpm) {
  uint8_t payload[13];payload[0]=1;
  memcpy(payload+1,&number,4);memcpy(payload+5,&session,4);memcpy(payload+9,&rpm,4);
  publishEvent(payload,sizeof(payload));
}

static size_t framePayload() {
  if(tapped())return MAX_FRAME_BYTES-FRAME_HEAD;
  uint16_t mtu=server?server->getPeerMTU(server->getConnId()):23;
  if(mtu<23)mtu=23;
  return std::min<size_t>(mtu-3,MAX_FRAME_BYTES)-FRAME_HEAD;
}

static void sendFrame() {
  const size_t payload=std::min(framePayload(),replyLength-replySent);
  const bool last=replySent+payload>=replyLength;
  uint8_t frame[MAX_FRAME_BYTES];
  frame[0]=replyOp;frame[1]=replySeq++;frame[2]=replyFlags|(last?FLAG_LAST:0);
  memcpy(frame+FRAME_HEAD,replyBuffer+replySent,payload);
  deliver(Channel::Reply,frame,FRAME_HEAD+payload);
  replySent+=payload;lastFrame=millis();
  if(last)replying=false;
}

#if LAUNCHLAB_HIL
// The tap, from the rig's commands (hil.h), in the loop.
inline void setTap(bool on,uint32_t now) {
  tap=on;passSubscribed=on;passRequests.clear();passOutbox.clear();commandWaiting=false;replying=false;liveOrientation=false;
  if(!on)return;
  if(!replyBuffer)replyBuffer=static_cast<uint8_t *>(heap_caps_malloc(REPLY_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  passRequests.subscribed(now);
}
inline bool tapCommand(const uint8_t *bytes,size_t length) {
  if(!tap || !length || length>sizeof(commandBytes) || commandWaiting)return false;
  memcpy(commandBytes,bytes,length);commandLength=length;commandWaiting=true;return true;
}
inline void tapPass(uint8_t byte){if(tap)passRequests.command(byte);}
#endif
// Returns true when what the screen icon shows has changed.
inline bool tick(uint32_t now,bool enabled,bool busy) {
  static bool launched=false;
  if(enabled && !launched && !busy && now>=START_AFTER_MS) {
    launched=true;startedAt=now;
    // BLE init recurses deeply: give it its own stack, not the 8 KB loop task's.
    if(xTaskCreatePinnedToCore(initTask,"bleinit",8192,nullptr,1,nullptr,1)!=pdPASS)initFailed=true;
  }
  if(started) {
    if(disconnects!=disconnectsSeen || !enabled){disconnectsSeen=disconnects;liveOrientation=false;}
    if(!bondsRestored){bondsRestored=true;restoreBonds();}
    if(nameChanged) {
      // The LaunchLab name (Get info); the advertised one is the Battle Pass's.
      nameChanged=false;
      uint8_t mac[6]={};esp_read_mac(mac,ESP_MAC_BT);
      BleName::full(advertisedName,sizeof(advertisedName),chosenName,mac);
    }
    if(linkUp) {
      // The phone asks for security when it first touches the LaunchLab
      // service, whose characteristics need it: a bonded phone restores, a new
      // one compares. A Battle Pass app never does, and stays as it is.
      portENTER_CRITICAL(&securityMux);const bool end=securityLink.takeDrop(now);portEXIT_CRITICAL(&securityMux);
      if(end)server->disconnect(server->getConnId());
      portENTER_CRITICAL(&securityMux);const bool confirm=securityLink.takeAutoAccept(now);portEXIT_CRITICAL(&securityMux);
      if(confirm) {
        ble_sm_io io{};io.action=BLE_SM_IOACT_NUMCMP;io.numcmp_accept=1;
        ble_sm_inject_io(server->getConnId(),&io);
      }
    }
    if(!enabled) {
      if(linkUp)server->disconnect(server->getConnId());
      if(ble_gap_adv_active())BLEDevice::getAdvertising()->stop();
      advertising=false;
      if(!tapped()){commandWaiting=false;replying=false;passOutbox.clear();passRequests.clear();}
    } else if(!linkUp && !ble_gap_adv_active() && now-lastAdvertiseTry>=ADVERTISE_RETRY_MS) {
      lastAdvertiseTry=now;advertising=BLEDevice::getAdvertising()->start();
    }
    // One phone at a time; the stack would otherwise accept a second link.
    if(linkUp && ble_gap_adv_active())BLEDevice::getAdvertising()->stop();
  }
  // What the profiles send, to a phone on the radio link or to the rig's tap.
  const bool link=(started && enabled && linkUp) || tapped();
  if(!link){replying=false;passOutbox.clear();}
  else if(!busy) {
    // The Battle Pass profile's packets: to a subscribed app, never in a pull.
    BattlePass::Packet packet;
    if(!passSubscribed)passOutbox.clear();
    else if(passOutbox.take(now,packet)){deliver(Channel::Pass,packet.data(),packet.size());++passPackets;}
    if(secured() && replyBuffer) {
      if(replying) {
        if(now-lastFrame>=FRAME_GAP_MS)sendFrame();
      } else if(commandWaiting && handler) {
        const uint8_t op=commandBytes[0];
        const int32_t result=handler(op,commandBytes+1,commandLength-1,replyBuffer,REPLY_BYTES);
        commandWaiting=false;
        replyOp=op;replySeq=0;replySent=0;
        if(result<0){replyBuffer[0]=uint8_t(-result);replyLength=1;replyFlags=FLAG_ERROR;}
        else {replyLength=size_t(result);replyFlags=0;}
        replying=true;sendFrame();
      }
    }
  }
  const State now_shown=state(enabled);
  bool changed=now_shown!=shown;
  // The number's screen comes and goes with the pairing.
  uint32_t code=0;bool automatic=false;const bool keyShown=comparison(code,automatic);
  if(keyShown!=keyWasShown){keyWasShown=keyShown;changed=true;}
  // Say so when a phone arrives or leaves; turning Bluetooth on or off is the user's own act.
  if(changed && (now_shown==State::Connected || (shown==State::Connected && now_shown==State::Ready))) {
    bannerConnected=now_shown==State::Connected;bannerUntil=now+BANNER_MS;bannerShown=true;
  }
  if(bannerShown && int32_t(now-bannerUntil)>=0){bannerShown=false;changed=true;}
  shown=now_shown;
  return changed;
}

template<class Canvas>
inline void drawIcon(Canvas &g,uint16_t readyColor,uint16_t connectedColor,uint16_t background,uint16_t text) {
  if(shown==State::Off)return;
  if(bannerShown) {
    g.fillRoundRect(6,2,g.width()-12,22,5,background);
    g.drawRoundRect(6,2,g.width()-12,22,5,bannerConnected?connectedColor:readyColor);
    g.setTextDatum(top_center);g.setTextColor(text,background);
    g.drawString(bannerConnected?"Phone connected":"Phone left",g.width()/2,5,2);
    return;
  }
  const uint16_t c=shown==State::Connected?connectedColor:readyColor;
  // Top left: the top right corner is the battery icon's (0.20.8), and no page draws here.
  const int x=3,y=2;
  // A rune, two pixels wide so it reads from arm's length, over a plate so it stays clear on any page.
  g.fillRect(x-1,y-1,12,17,background);
  for(int d=0;d<2;++d) {
    g.drawLine(x+4+d,y,x+4+d,y+14,c);
    g.drawLine(x+4+d,y,x+8+d,y+4,c);g.drawLine(x+8+d,y+4,x+d,y+10,c);
    g.drawLine(x+4+d,y+14,x+8+d,y+10,c);g.drawLine(x+8+d,y+10,x+d,y+4,c);
  }
}

inline void status() {
  Serial.printf("BLE_STATUS started=%d init_failed=%d state=%u adv_active=%d connects=%lu disconnects=%lu dropped=%lu notifies=%lu mtu=%u heap_before=%u heap_after=%u secured=%d pass_subscribed=%d pass_packets=%lu\n",
    started,initFailed,unsigned(shown),started?ble_gap_adv_active():-1,(unsigned long)connects,(unsigned long)disconnects,(unsigned long)commandsDropped,
    (unsigned long)notifies,unsigned(started && linkUp?server->getPeerMTU(server->getConnId()):0),unsigned(heapBefore),unsigned(heapAfter),
    secured(),bool(passSubscribed),(unsigned long)passPackets);
}
}
