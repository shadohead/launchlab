#include "controls.h"
#include <cassert>
#include <iostream>
using namespace Controls;
int main() {
  State s;assert(s.autoRecap() && s.tapA()==Effect::Recap);
  assert(s.holdB(true)==Effect::Recap && s.holdB()==Effect::None);
  s.tapB();assert(s.page==Page::Menu && s.menu==0 && !s.autoRecap());
  s.tapA();assert(s.page==Page::HistoryMenu);
  for(unsigned view=0;view<3;++view) {
    s.history=view;s.tapA();assert(s.page==Page::History && s.tapB()==Effect::Older);
    s.holdB();assert(s.page==Page::HistoryMenu && s.history==view);
  }
  s.history=3;s.tapA();assert(s.page==Page::Menu);
  s.menu=1;s.tapA();assert(s.page==Page::NewSession && !s.startSession);
  assert(s.tapA()==Effect::None && s.page==Page::Menu);
  s.tapA();s.tapB();assert(s.tapA()==Effect::NewSession && s.page==Page::Menu);
  s.menu=3;s.tapA();assert(s.page==Page::Settings);
  for(unsigned row=0;row<EDITABLE_SETTING_COUNT;++row) {
    s.setting=row;assert(s.tapA()==Effect::BeginEdit && s.page==Page::Edit);
    assert(s.tapA()==Effect::Apply && s.page==Page::Edit); // Backend must confirm success.
    assert(s.holdA()==Effect::None);assert(s.holdB()==Effect::CancelEdit && s.page==Page::Settings);
  }
  s.page=Page::Settings;s.setting=unsigned(Setting::WipeAll);
  assert(s.tapA()==Effect::BeginWipe && s.page==Page::Wipe);
  assert(s.tapA()==Effect::ConfirmWipe && s.holdA()==Effect::None);
  assert(s.tapB()==Effect::CancelWipe && s.holdB()==Effect::CancelWipe);
  s.page=Page::Settings; // The sketch consumes CancelWipe before navigating.
  s.setting=SETTING_COUNT;s.tapA();assert(s.page==Page::Menu);
  s.holdB();assert(s.page==Page::Main);
  assert(s.holdA(true)==Effect::None && s.page==Page::Main);
  assert(s.holdA()==Effect::Tournament && s.page==Page::Tournament && !s.autoRecap());
  assert(s.tapA()==Effect::None && s.holdB()==Effect::None && s.page==Page::Tournament);
  assert(s.tapB()==Effect::Tournament && s.page==Page::Tournament);
  assert(s.holdA()==Effect::Tournament && s.page==Page::Main);
  // Best effect cycles Random, the offered effects (the default set: the 16
  // of 0.16) and Off.
  for(unsigned row=0;row<EDITABLE_SETTING_COUNT;++row) {
    s.page=Page::Edit;s.setting=row;s.draft=row==0?3:row==2?40:0;
    const unsigned original=s.draft,cycle=row==0 || row==2?10:row==5?Appearance::THEMES:row==6?EffectSet::count(s.offeredEffects)+2:row==7 || row==8?3:2;
    for(unsigned i=0;i<cycle;++i)assert(s.tapB()==Effect::Preview);
    assert(s.draft==original);
  }
  // Every theme is reachable exactly once from every saved starting choice.
  for(unsigned start=0;start<Appearance::THEMES;++start) {
    s.page=Page::Edit;s.setting=unsigned(Setting::Theme);s.draft=start;
    unsigned visited=0;
    for(unsigned i=0;i<Appearance::THEMES;++i){visited|=1u<<s.draft;assert(s.tapB()==Effect::Preview);}
    assert(s.draft==start && visited==(1u<<Appearance::THEMES)-1);
  }
  // Hold/release must never execute both actions, including a synthetic
  // library release click and a new short press immediately afterwards.
  Gesture gesture;assert(!gesture.click(true,false,false));
  assert(gesture.click(false,false,true));
  assert(!gesture.click(true,false,false));assert(!gesture.click(false,true,false));
  assert(!gesture.click(false,false,true));assert(!gesture.click(true,false,false));
  assert(gesture.click(false,false,true));
  for(unsigned page=0;page<=unsigned(Page::Wipe);++page)
    for(unsigned menu=0;menu<6;++menu)for(unsigned setting=0;setting<=SETTING_COUNT;++setting)for(uint8_t view=0;view<2;++view) {
      State before;before.page=Page(page);before.menu=menu;before.setting=setting;before.history=2;
      State after;uint8_t saved=99;
      assert(decodeWake(encodeWake(before,view),after,saved));
      assert(after.page==((before.page==Page::Edit || before.page==Page::Wipe)?Page::Settings:before.page));
      assert(after.menu==menu && after.setting==setting && after.history==2 && saved==view);
    }
  State out;uint8_t view=0;
  assert(decodeWake(LEGACY_WAKE_MAGIC|uint32_t(Page::Settings)|(5u<<9),out,view) && out.setting==SETTING_COUNT);
  // 0.11 layout: seven settings, Back at row 7, view at bit 12.
  assert(decodeWake(WAKE7_MAGIC|uint32_t(Page::Settings)|(7u<<9)|(1u<<12),out,view) && out.setting==SETTING_COUNT && view==1);
  assert(decodeWake(WAKE7_MAGIC|uint32_t(Page::Settings)|(6u<<9),out,view) && out.setting==6 && view==0);
  // 0.12 Back must stay Back after inserting Mount at row 8.
  assert(decodeWake(WAKE8_MAGIC|uint32_t(Page::Settings)|(8u<<9)|(1u<<13),out,view) && out.setting==SETTING_COUNT && view==1);
  // Rows from Sound on moved down one when Celebrate was inserted after Best effect.
  assert(decodeWake(WAKE8_MAGIC|uint32_t(Page::Settings)|(7u<<9),out,view) && out.setting==unsigned(Setting::Sound) && view==0);
  assert(decodeWake(WAKE8_MAGIC|uint32_t(Page::Settings)|(6u<<9),out,view) && out.setting==unsigned(Setting::BestEffect));
  assert(decodeWake(WAKE_MAGIC|uint32_t(Page::Settings)|(9u<<9),out,view) && out.setting==unsigned(Setting::Mount));
  assert(decodeWake(WAKE_MAGIC|uint32_t(Page::Settings)|(7u<<9),out,view) && out.setting==unsigned(Setting::Celebrate));
  assert(decodeWake(WAKE9_MAGIC|uint32_t(Page::Settings)|(9u<<9)|(1u<<13),out,view) && out.setting==SETTING_COUNT && view==1);
  assert(decodeWake(WAKE9_MAGIC|uint32_t(Page::Settings)|(8u<<9),out,view) && out.setting==unsigned(Setting::Mount));
  // 0.13.9-0.14: Wipe all at row 9 and Back at row 10.
  assert(decodeWake(WAKE10_MAGIC|uint32_t(Page::Settings)|(7u<<9),out,view) && out.setting==unsigned(Setting::Sound));
  assert(decodeWake(WAKE10_MAGIC|uint32_t(Page::Settings)|(8u<<9),out,view) && out.setting==unsigned(Setting::Mount));
  assert(decodeWake(WAKE10_MAGIC|uint32_t(Page::Settings)|(9u<<9),out,view) && out.setting==unsigned(Setting::WipeAll));
  assert(decodeWake(WAKE10_MAGIC|uint32_t(Page::Settings)|(10u<<9)|(1u<<13),out,view) && out.setting==SETTING_COUNT && view==1);
  assert(decodeWake(WAKE10_MAGIC|uint32_t(Page::Settings)|(5u<<9),out,view) && out.setting==unsigned(Setting::Theme));
  for(uint32_t corrupt:{0u,WAKE_MAGIC|15u,WAKE_MAGIC|uint32_t(Page::Edit),WAKE_MAGIC|(7u<<4),LEGACY_WAKE_MAGIC|(7u<<9),
                        WAKE_MAGIC|0x4000u,WAKE_MAGIC|(13u<<9),WAKE11_MAGIC|(12u<<9),WAKE10_MAGIC|(11u<<9),WAKE8_MAGIC|(9u<<9),WAKE9_MAGIC|(10u<<9),
                        WAKE_MAGIC|uint32_t(Page::Wipe),WAKE9_MAGIC|uint32_t(Page::Wipe),WAKE7_MAGIC|0x2000u})
    assert(!decodeWake(corrupt,out,view));
  std::cout<<"PASS: consistent actions, visible menu/back, deliberate sessions, staged settings, tournament lock, hold release, wake round trips and corruption\n";
}
