#pragma once
#ifndef LAUNCHLAB_CPU_PM
#define LAUNCHLAB_CPU_PM 0
#endif

#if LAUNCHLAB_CPU_PM
#include "sdkconfig.h"
#if !CONFIG_PM_ENABLE
#error "LAUNCHLAB_CPU_PM requires a fully rebuilt CONFIG_PM_ENABLE SDK"
#endif
#include "esp_pm.h"
#include <cstdio>
#include <cstdlib>
#endif

// Each owner has its own handle: IDF PM lock operations on one handle are not
// thread-safe. Locks cover peripheral work, never the ordinary task idle delay.
namespace CpuPower {
enum Owner {Ui,Imu};
#if LAUNCHLAB_CPU_PM
inline esp_pm_lock_handle_t locks[2]={};
inline bool enabled=false;
inline esp_err_t error=ESP_OK;
inline void checked(esp_err_t result){if(result!=ESP_OK)std::abort();}
inline bool begin() {
  if(enabled)return true;
  for(unsigned i=0;i<2;++i) {
    error=esp_pm_lock_create(ESP_PM_APB_FREQ_MAX,0,i?"ll_imu":"ll_ui",&locks[i]);
    if(error!=ESP_OK)break;
  }
  if(error==ESP_OK) {
    const esp_pm_config_t requested={80,40,false};
    error=esp_pm_configure(&requested);
    esp_pm_config_t actual={};
    if(error==ESP_OK)error=esp_pm_get_configuration(&actual);
    if(error==ESP_OK && (actual.max_freq_mhz!=80 || actual.min_freq_mhz!=40 || actual.light_sleep_enable))
      error=ESP_ERR_INVALID_STATE;
  }
  if(error==ESP_OK){enabled=true;return true;}
  // A partial initialization must leave the old fixed 80 MHz policy in force.
  const esp_pm_config_t fallback={80,80,false};
  checked(esp_pm_configure(&fallback));
  for(auto &lock:locks)if(lock){checked(esp_pm_lock_delete(lock));lock=nullptr;}
  return false;
}
class Guard {
  esp_pm_lock_handle_t lock_=nullptr;
public:
  explicit Guard(Owner owner){if(enabled){lock_=locks[owner];checked(esp_pm_lock_acquire(lock_));}}
  Guard(const Guard&)=delete;
  Guard& operator=(const Guard&)=delete;
  void release(){if(lock_){checked(esp_pm_lock_release(lock_));lock_=nullptr;}}
  ~Guard(){release();}
};
inline bool active(){return enabled;}
inline const char *errorName(){return esp_err_to_name(error);}
inline int dump(FILE *stream){return esp_pm_dump_locks(stream);}
#else
inline bool begin(){return false;}
class Guard {
public:
  explicit Guard(Owner){}
  void release(){}
};
inline bool active(){return false;}
inline const char *errorName(){return "disabled";}
#endif
}
