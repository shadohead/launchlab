// Prints a device backup export exactly as `K` does over USB, for the web
// updater's parser tests (site tests/launchlab/fixtures/backup-export.txt).
// Build like the native suites, then: ./backup_fixture > backup-export.txt
// With `themes`, two custom themes, one selected: a format 2 backup
// (backup-export-themes.txt).
#include "../LaunchLabMini/device_backup.h"
#include <cstdio>

#include <cstring>

int main(int argc,char **argv) {
  const bool themes=argc>1 && !std::strcmp(argv[1],"themes");
  DeviceBackup::Contents c;
  for(uint32_t i=1;i<=40;++i)c.practice.accept(i,true,5000+i*37,i*20000);
  c.practice.newSession();c.practice.accept(41,true,9100,900000);
  c.look.select(Appearance::Theme::Amber,Appearance::Effect::Lightning);
  c.bests.observe(0,0,true,8800);c.bests.observe(1,1,true,7700);
  c.hasMotion=true;auto &t=c.motion;
  t.fused=true;t.quality=LaunchMotion::Quality::Valid;t.count=48;t.number=41;t.rpm=9100;t.durationMs=400;t.gravity[2]=1;
  for(unsigned i=0;i<t.count;++i){t.points[i].ms=-500+int(i*1150/47);t.points[i].q[0]=16384;}
  t.startLevel={StickS3Level::State::Valid,6,.2f,0};t.endLevel={StickS3Level::State::Valid,15,0,-.5f};
  c.settings={7,60,RpmEstimator::Mode::SingleTurn,StickS3SensorProfile::Mode::Tcrt,true};
  if(themes) {
    auto custom=[](const char *name,uint16_t accent){Palette::Custom t;std::strncpy(t.name,name,Palette::Custom::MAX_NAME);
      const uint16_t roles[]={0x0006,0xffff,0x5aeb,accent,accent,0xfda0,0x07e0,0xf800};std::memcpy(t.role,roles,sizeof(roles));return t;};
    c.hasThemes=c.themes.put(0,custom("Dragoon Storm",0x2d7f)) && c.themes.put(2,custom("Ember Night",0xfb20));
    c.look.select(Themes::slotTheme(2),Appearance::Effect::Lightning);
  }
  DeviceBackup::Buffer buffer;const size_t n=DeviceBackup::encode(c,"0.11.0-sticks3-themes-backup",buffer);
  // Unrelated log lines interleave with a real export; the parser must skip them.
  std::printf("RPM_STATE state=ready t_us=1 contrast=900.0 noise=4.0\n");
  std::printf("BACKUP_BEGIN format=%lu bytes=%u records=%u sessions=%u motion=%d themes=%d runtime=%s\n",(unsigned long)c.format(),
    unsigned(n),c.practice.size(),c.practice.sessions(),c.hasMotion,c.hasThemes,"0.11.0-sticks3-themes-backup");
  DeviceBackup::base64Lines(buffer.data(),n,96,[](const char *line){std::printf("BACKUP_DATA %s\n",line);});
  std::printf("BATTERY_STATUS percent=80\nBACKUP_END\n");
}
