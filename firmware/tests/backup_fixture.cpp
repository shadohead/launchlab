// Prints a device backup export exactly as `K` does over USB, for the web
// updater's parser tests (site tests/launchlab/fixtures/backup-export.txt).
// Build like the native suites, then: ./backup_fixture > backup-export.txt
// With `themes`, two custom themes, one selected: a format 2 backup
// (backup-export-themes.txt). With `milestones`, Celebrate Both and a
// three-row table: a format 3 backup (backup-export-milestones.txt). With
// `pools`, a four-row table with an effect pool: a format 4 backup
// (backup-export-pools.txt).
// With `effect-set`, four offered effects and the pooled table: a format 5
// backup (backup-export-effect-set.txt). With `wide`, the same with a pool
// holding effects past value 63: a format 6 backup (backup-export-wide.txt).
// With `loaded`, the wide backup plus two added effects (a comet with its
// packed source, a spark without), Best effect and a milestone pool naming
// them: a format 7 backup (backup-export-loaded.txt).
#include "../LaunchLabMini/device_backup.h"
#include "../tools/fx/fx_share.h"
#include <cstdio>
#include <vector>

#include <cstring>

int main(int argc,char **argv) {
  const bool loaded=argc>1 && !std::strcmp(argv[1],"loaded");
  const bool wide=loaded || (argc>1 && !std::strcmp(argv[1],"wide")),effectSet=wide || (argc>1 && !std::strcmp(argv[1],"effect-set"));
  const bool themes=argc>1 && !std::strcmp(argv[1],"themes"),pools=effectSet || (argc>1 && !std::strcmp(argv[1],"pools")),milestones=pools || (argc>1 && !std::strcmp(argv[1],"milestones"));
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
  if(milestones && !pools) {
    const Milestones::Row rows[]={{9000,Appearance::Effect::Sparkles},{12500,Appearance::Effect::Lightning},{15000,Appearance::Effect::Slash}};
    c.hasMilestones=c.milestones.set(Milestones::Mode::Both,rows,3)==Milestones::Problem::None;
  }
  if(pools) {
    const uint64_t pool=uint64_t(1)<<unsigned(Appearance::Effect::Tidal)|uint64_t(1)<<unsigned(Appearance::Effect::Frost)|uint64_t(1)<<unsigned(Appearance::Effect::Bloom);
    const Milestones::Pool widePool=EffectSet::flag(Appearance::Effect::Haymaker)|EffectSet::flag(Appearance::Effect::Starfall)|EffectSet::flag(Appearance::Effect::Blink);
    const Milestones::Row rows[]={{9000,Appearance::Effect::Sparkles},{10500,pool},{12000,wide?widePool:Milestones::Pool(EffectSet::flag(Appearance::Effect::Lightning))},{15000,Appearance::Effect::Slash}};
    c.hasMilestones=c.milestones.set(Milestones::Mode::Both,rows,4)==Milestones::Problem::None;
  }
  if(effectSet) {
    using Appearance::Effect;using EffectSet::flag;
    c.hasEffectSet=c.effectSet.set(flag(Effect::Slash)|flag(Effect::Tidal)|flag(Effect::Crimson)|flag(Effect::Ravage)|(wide?flag(Effect::Sharkfin):EffectSet::Mask()))==EffectSet::Problem::None;
  }
  const std::string comet="effect \"Comet\"\nuid 0xc0ffee\nduration 2000\nfallback \"Tidal\"\nframe {\n  dot(acx, ay1 + 20, accent, 1 - t/2)\n}\n";
  const auto cometFile=FxCompiler::compile(comet).bytes,sparkFile=FxCompiler::compile(
    "effect \"Spark\"\nuid 0x5a\nduration 1500\nfallback \"Sparkles\"\nframe {\n  spark(acx, acy, accent, 1 - t/1.5)\n}\n").bytes;
  const std::string cometSource=FxShare::pack(comet).bytes;
  if(loaded) {
    // As on a unit with the two installed: they play, so records may name them.
    auto &hooks=Appearance::loadedHooks();
    hooks.playable=[](unsigned e){return e==96 || e==97;};
    hooks.name=[](unsigned e){return e==96?"Comet":"Spark";};
    hooks.duration=[](unsigned e){return e==96?2000u:1500u;};
    hooks.fallback=[](unsigned e){return unsigned(e==96?Appearance::Effect::Tidal:Appearance::Effect::Sparkles);};
    c.loadedCount=2;
    c.loaded[0]={0,cometFile.data(),reinterpret_cast<const uint8_t *>(cometSource.data()),uint16_t(cometFile.size()),uint16_t(cometSource.size())};
    c.loaded[1]={1,sparkFile.data(),nullptr,uint16_t(sparkFile.size()),0};
    c.look.select(Appearance::Theme::Amber,Appearance::Effect(96));
    const Milestones::Row rows[]={{9000,Appearance::Effect::Sparkles},{12000,Milestones::Pool(EffectSet::flag(Appearance::Effect(96))|EffectSet::flag(Appearance::Effect(97)))}};
    c.hasMilestones=c.milestones.set(Milestones::Mode::Both,rows,2)==Milestones::Problem::None;
  }
  const char *runtime=loaded?"0.19.0-sticks3-pm":wide?"0.18.0-sticks3-anime-pm":effectSet?"0.17.0-sticks3-anime-pm":pools?"0.16.0-sticks3-effects-pm":milestones?"0.15.0-sticks3-milestone-pm":"0.11.0-sticks3-themes-backup";
  std::vector<uint8_t> buffer(DeviceBackup::encodedBound(c));const size_t n=DeviceBackup::encode(c,runtime,buffer.data(),buffer.size());
  if(!n){std::fprintf(stderr,"backup does not encode\n");return 1;}
  // Unrelated log lines interleave with a real export; the parser must skip them.
  std::printf("RPM_STATE state=ready t_us=1 contrast=900.0 noise=4.0\n");
  if(loaded)std::printf("BACKUP_BEGIN format=%lu bytes=%u records=%u sessions=%u motion=%d themes=%d milestones=%d effect_set=%d effects=%u runtime=%s\n",(unsigned long)c.format(),
    unsigned(n),c.practice.size(),c.practice.sessions(),c.hasMotion,c.hasThemes,c.hasMilestones,c.hasEffectSet,c.loadedCount,runtime);
  else if(effectSet)std::printf("BACKUP_BEGIN format=%lu bytes=%u records=%u sessions=%u motion=%d themes=%d milestones=%d effect_set=%d runtime=%s\n",(unsigned long)c.format(),
    unsigned(n),c.practice.size(),c.practice.sessions(),c.hasMotion,c.hasThemes,c.hasMilestones,c.hasEffectSet,runtime);
  else if(milestones)std::printf("BACKUP_BEGIN format=%lu bytes=%u records=%u sessions=%u motion=%d themes=%d milestones=%d runtime=%s\n",(unsigned long)c.format(),
    unsigned(n),c.practice.size(),c.practice.sessions(),c.hasMotion,c.hasThemes,c.hasMilestones,runtime);
  else std::printf("BACKUP_BEGIN format=%lu bytes=%u records=%u sessions=%u motion=%d themes=%d runtime=%s\n",(unsigned long)c.format(),
    unsigned(n),c.practice.size(),c.practice.sessions(),c.hasMotion,c.hasThemes,runtime);
  DeviceBackup::base64Lines(buffer.data(),n,96,[](const char *line){std::printf("BACKUP_DATA %s\n",line);});
  std::printf("BATTERY_STATUS percent=80\nBACKUP_END\n");
}
