#!/usr/bin/env python3
"""Exercise the production DFS setup, per-owner guards and safe rollback."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
header=root/'firmware/LaunchLabMini'
fake=r'''
#pragma once
#include <cassert>
#include <cstdio>
#include <map>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_ERR_INVALID_STATE=1,ESP_ERR_NO_MEM=2,ESP_ERR_NOT_SUPPORTED=3;
static const char *esp_err_to_name(int e){return e?"error":"ESP_OK";}
struct esp_pm_config_t {int max_freq_mhz,min_freq_mhz;bool light_sleep_enable;};
struct Lock {int owner,depth=0;};using esp_pm_lock_handle_t=Lock*;
constexpr int ESP_PM_APB_FREQ_MAX=1;
static int failCreate=0,creates=0,deletes=0,failConfig=0,failRead=0;
static bool badRead=false;
static esp_pm_config_t configuration={80,80,false};
static int esp_pm_lock_create(int type,int arg,const char *,Lock **out){
 assert(type==ESP_PM_APB_FREQ_MAX && arg==0);++creates;
 if(failCreate==creates)return ESP_ERR_NO_MEM;
 *out=new Lock{creates};return ESP_OK;
}
static int esp_pm_lock_delete(Lock *p){assert(p && p->depth==0);delete p;++deletes;return ESP_OK;}
static int esp_pm_lock_acquire(Lock *p){assert(p);++p->depth;return ESP_OK;}
static int esp_pm_lock_release(Lock *p){assert(p && p->depth>0);--p->depth;return ESP_OK;}
static int esp_pm_configure(const void *p){auto c=*static_cast<const esp_pm_config_t*>(p);
 assert(c.max_freq_mhz==80 && (c.min_freq_mhz==40 || c.min_freq_mhz==80) && !c.light_sleep_enable);
 if(c.min_freq_mhz==40 && failConfig)return ESP_ERR_NOT_SUPPORTED;
 configuration=c;return ESP_OK;
}
static int esp_pm_get_configuration(void *p){if(failRead)return ESP_ERR_INVALID_STATE;
 *static_cast<esp_pm_config_t*>(p)=configuration;if(badRead)static_cast<esp_pm_config_t*>(p)->light_sleep_enable=true;return ESP_OK;}
static int esp_pm_dump_locks(FILE *){return ESP_OK;}
'''
program=r'''
#include "esp_pm.h"
#include "cpu_power.h"
#include <cstdlib>
int main(int argc,char **argv){
 int scenario=argc>1?std::atoi(argv[1]):0;
 if(scenario==1)failCreate=1;
 if(scenario==2)failCreate=2;
 if(scenario==3)failConfig=1;
 if(scenario==4)failRead=1;
 if(scenario==5)badRead=true;
 bool enabled=CpuPower::begin();
 if(scenario){
  assert(!enabled && !CpuPower::active());
  assert(configuration.max_freq_mhz==80 && configuration.min_freq_mhz==80 && !configuration.light_sleep_enable);
  assert(!CpuPower::locks[0] && !CpuPower::locks[1]);
  assert(deletes==(scenario==1?0:scenario==2?1:2));
  CpuPower::Guard fallback(CpuPower::Ui);fallback.release();
 }else{
  assert(enabled && CpuPower::active() && creates==2);
  assert(configuration.min_freq_mhz==40 && !configuration.light_sleep_enable);
  assert(CpuPower::begin() && creates==2);
  {CpuPower::Guard ui(CpuPower::Ui);assert(CpuPower::locks[0]->depth==1);
   {CpuPower::Guard imu(CpuPower::Imu);assert(CpuPower::locks[1]->depth==1);
    assert(CpuPower::locks[0]!=CpuPower::locks[1]);}
   assert(CpuPower::locks[1]->depth==0);
   ui.release();ui.release();assert(CpuPower::locks[0]->depth==0);
  }
  assert(CpuPower::locks[0]->depth==0 && CpuPower::locks[1]->depth==0);
  for(auto &lock:CpuPower::locks){esp_pm_lock_delete(lock);lock=nullptr;}
 }
}
'''
with tempfile.TemporaryDirectory(prefix='launchlab-cpu-pm-test-') as tmp:
 p=Path(tmp);(p/'esp_pm.h').write_text(fake);(p/'sdkconfig.h').write_text('#define CONFIG_PM_ENABLE 1\n')
 (p/'test.cpp').write_text(program)
 flags=[os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(p),'-I'+str(header)]
 subprocess.run(flags+['-DLAUNCHLAB_CPU_PM=1',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 for scenario in range(6):subprocess.run([str(p/'test'),str(scenario)],check=True)
 (p/'off.cpp').write_text('#include "cpu_power.h"\n#include <cassert>\nint main(){assert(!CpuPower::begin());CpuPower::Guard g(CpuPower::Ui);g.release();}\n')
 subprocess.run(flags+[str(p/'off.cpp'),'-o',str(p/'off')],check=True);subprocess.run([str(p/'off')],check=True)
 (p/'sdkconfig.h').write_text('#define CONFIG_PM_ENABLE 0\n')
 rejected=subprocess.run(flags+['-DLAUNCHLAB_CPU_PM=1',str(p/'test.cpp'),'-o',str(p/'invalid')],capture_output=True,text=True)
 assert rejected.returncode and 'fully rebuilt CONFIG_PM_ENABLE SDK' in rejected.stderr
source=(header/'LaunchLabMini.ino').read_text()
loop=source[source.index('void loop() {'):]
assert 'CpuPower::Guard clock(CpuPower::Ui);' in loop
assert loop.count('clock.release();delay(IdlePowerPolicy::UI_POLL_MS)')==2
imu=(header/'motion_acquisition.h').read_text()
assert 'CpuPower::Guard clock(CpuPower::Imu);' in imu
assert imu.index('CpuPower::Guard clock(CpuPower::Imu);')<imu.index('readFifo(s,sampledUs);unlock();')
assert source.index('CpuPower::begin();')<source.index('M5.begin(cfg);')
assert "c!='C'" in source
print('PASS: CPU PM setup/readback, separate owner locks, early release, fixed80 rollback, default-off and SDK mismatch rejection')
