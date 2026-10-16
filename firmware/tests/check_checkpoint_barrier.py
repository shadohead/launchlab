#!/usr/bin/env python3
"""Exercise exact request/send/preflight bodies with a fake transport and IMU.

The timing test separately executes the real IMU barrier. Here a preflight veto
must never reach the command queue or a guarded flash-writing caller. This is
control-flow verification, not an ESP32 queue/scheduler or NVS hardware test.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
owner = (root / 'LaunchLabMini/acquisition.h').read_text()
sketch = (root / 'LaunchLabMini/LaunchLabMini.ino').read_text()


def body(text, signature):
    start = text.index(signature)
    cursor = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    return text[start:cursor]


prefix = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
using TickType_t=uint32_t;
static constexpr int pdTRUE=1;
static TickType_t now=0;
static unsigned enqueues=0,replyReads=0;
static uint32_t pendingId=0;
static bool queueOk=true,replyOk=true,replyAvailable=true;
static TickType_t pdMS_TO_TICKS(uint32_t ms){return ms;}
static TickType_t xTaskGetTickCount(){return now;}
template<class Command> int xQueueSend(void*,const Command* command,TickType_t){
  ++enqueues;pendingId=command->id;return queueOk?pdTRUE:0;
}
template<class Reply> int xQueueReceive(void*,Reply* reply,TickType_t wait){
  ++replyReads;now+=wait;
  if(!replyAvailable)return 0;
  reply->id=pendingId;reply->ok=replyOk;return pdTRUE;
}
class Owner {
public:
'''
declarations = '\n'.join(re.search(pattern, owner)[0] for pattern in [
    r'enum class Action:uint8_t \{[^}]+\};',
    r'using RequestHook=[^;]+;',
    r'struct Command \{[^}]+\};',
    r'struct Reply \{[^}]+\};',
])
methods = '\n'.join(body(owner, signature) for signature in [
    'void setRequestHook(', 'bool request(', 'bool send(',
])
state = r'''
  RequestHook hook_=nullptr;
  void* task_=this;
  void* commands_=nullptr;
  void* replies_=nullptr;
  uint32_t requestId_=0;
};
using Action=Owner::Action;
struct FakeImu {
  bool available=true,active=false;
  unsigned quietCalls=0,endCalls=0;
  bool quiet(){++quietCalls;if(!available)return false;active=true;return true;}
  void endQuiet(){++endCalls;active=false;}
} imuAcquisition;
'''
tests = r'''
static void reset(){
  imuAcquisition={};now=0;enqueues=replyReads=0;
  queueOk=replyOk=replyAvailable=true;
}
static unsigned completions=0;
static bool veto(Action,bool done,bool){if(done)++completions;return false;}
int main(){
  reset();Owner plain;assert(plain.request(Action::Reset));assert(enqueues==1 && plain.requestId_==1);
  reset();Owner blocked;blocked.setRequestHook(veto);
  assert(!blocked.request(Action::CheckpointBegin));
  assert(!enqueues && !replyReads && !blocked.requestId_ && !completions);
  // Every flash-capable begin action must be vetoed before enqueue/ADC pause,
  // and failure must preserve an earlier quiet window rather than end it.
  for(Action action:{Action::CheckpointBegin,Action::IdleCheckpointBegin,
      Action::HistoryIdleCheckpointBegin,Action::ShutdownBegin}){
    reset();Owner owner;owner.setRequestHook(quietImuForFlash);
    imuAcquisition.available=false;imuAcquisition.active=true;
    unsigned flashWrites=0;if(owner.request(action,5))++flashWrites;
    assert(!flashWrites && !enqueues && !replyReads && !owner.requestId_);
    assert(imuAcquisition.quietCalls==1 && !imuAcquisition.endCalls && imuAcquisition.active);
    imuAcquisition.available=true;assert(owner.request(action,5));
    assert(enqueues==1 && owner.requestId_==1 && imuAcquisition.active);
    assert(owner.request(Action::CheckpointEnd));
    assert(!imuAcquisition.active && imuAcquisition.endCalls==1);
  }
  // If preflight succeeds, all send-failure paths still run completion cleanup.
  for(unsigned failure=0;failure<4;++failure){
    reset();Owner owner;owner.setRequestHook(quietImuForFlash);
    if(failure==0)owner.task_=nullptr;
    if(failure==1)queueOk=false;
    if(failure==2)replyOk=false;
    if(failure==3)replyAvailable=false;
    assert(!owner.request(Action::CheckpointBegin));
    assert(imuAcquisition.quietCalls==1 && imuAcquisition.endCalls==1 && !imuAcquisition.active);
    assert(now<=2000);
  }
  // Sensor-idle and ordinary commands do not require a quiet-capable IMU.
  reset();Owner owner;owner.setRequestHook(quietImuForFlash);imuAcquisition.available=false;
  for(Action action:{Action::Reset,Action::IdlePause,Action::IdleResume,Action::Pin})assert(owner.request(action));
  assert(!imuAcquisition.quietCalls && !imuAcquisition.endCalls && enqueues==4);
  std::cout<<"PASS: exact checkpoint request/send/preflight; veto skips queue, request ID, completion and guarded flash; success/end and all send-failure cleanup; ordinary commands unaffected\n";
}
'''
with tempfile.TemporaryDirectory(prefix='launchlab-checkpoint-barrier-') as directory:
    build = Path(directory)
    source = build / 'test.cpp'
    source.write_text(prefix + declarations + '\n' + methods + state +
                      body(sketch, 'static bool quietImuForFlash(') + tests)
    executable = build / 'test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(source), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
