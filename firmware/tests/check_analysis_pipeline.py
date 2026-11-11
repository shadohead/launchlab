#!/usr/bin/env python3
"""Exercise the actual microphone ownership and exit helpers for Analysis."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'LaunchLabMini/LaunchLabMini.ino').read_text()
def function(name):
    start=source.index('static void '+name+'(')
    end=source.index('{',start)+1; depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
prefix=r'''
#include "controls.h"
#include "analysis_mode.h"
#include "mic_session.h"
#include <cassert>
#include <iostream>
static Controls::State controls;static AnalysisMode::Session analysis;
static uint32_t now=1000,lastDraw=0,micPageChecked=0;
static uint32_t millis(){return now;}
static constexpr uint32_t MIC_PAGE_FRAME_MS=100;
static bool dirty=false,micPageWarm=false,micPageFresh=false,celebrationSound=true,soundPreview=true;
static unsigned navigation=0;
static void syncNavigation(){++navigation;}
struct Mic {bool analysisMode=false;void setAnalysisMode(bool enabled){analysisMode=enabled;}bool active=false;unsigned ticks=0;
 bool off()const{return !active;}bool on()const{return active;}bool warm(uint32_t)const{return active;}
 bool muted()const{return active;}bool fresh(){return false;}
 MicSession::State state()const{return active?MicSession::State::On:MicSession::State::Off;}
 void tick(bool wanted,bool,uint32_t){active=wanted;++ticks;}
}micListener;
struct Sound {bool muted=false,playing_=true;unsigned stops=0;
 bool powered(){return false;}bool playing(){return playing_;}void stop(){playing_=false;++stops;}
}soundPlayer;
static struct {bool active=false;}soundRequest;
'''
tests=r'''
int main(){
  tickMicPage();assert(!micListener.ticks);
  controls.page=Controls::Page::Analysis;soundRequest.active=true;tickMicPage();
  assert(micListener.analysisMode && micListener.on() && soundPlayer.muted && soundPlayer.stops==1 && !soundRequest.active);
  assert(!celebrationSound && !soundPreview);
  analysis.begin(1,true,6000,1000000);analysis.stop(2000000);tickMicPage();assert(micListener.on());
  analysis.toggleReplay(2000000);dirty=false;micPageFresh=true;now+=1000;tickMicPage();assert(!dirty && !micPageFresh);
  analysis.toggleReplay(3000000);micPageFresh=true;now+=1000;tickMicPage();assert(dirty);
  leaveMicPage();assert(controls.page==Controls::Page::Main && !analysis.number() && navigation==1);
  tickMicPage();assert(micListener.off() && !soundPlayer.muted);
  controls.page=Controls::Page::MicRpm;tickMicPage();assert(micListener.on() && !micListener.analysisMode);
  leaveMicPage();assert(controls.page==Controls::Page::Menu);tickMicPage();assert(micListener.off());
  controls.page=Controls::Page::Analysis;soundRequest.active=true;tickMicPage();controls.holdB();tickMicPage();assert(micListener.off());
  std::cout<<"PASS: production Analysis mic ownership, stop readiness, audio mute, B exit and power-off cleanup\n";
}
'''
with tempfile.TemporaryDirectory(prefix='launchlab-analysis-') as directory:
    cpp=Path(directory)/'test.cpp';exe=Path(directory)/'test'
    cpp.write_text(prefix+function('tickMicPage')+function('leaveMicPage')+tests)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined',
      '-I'+str(root/'LaunchLabMini'),'-I'+str(root/'LaunchLabRpm'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
