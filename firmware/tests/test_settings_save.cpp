#include "settings_save.h"
#include <cassert>
#include <cstdio>

int main() {
  SettingsSave save;
  // Upstream fills all 16 low bits. Analysis must survive an unrelated save
  // and must never clear Bluetooth, bonds, name or Battle Pass sync state.
  constexpr uint32_t existing = 0xffff;
  static_assert(SettingsSave::Bluetooth == 4096 && SettingsSave::PassConfirmed == 32768);
  static_assert((SettingsSave::AnalysisResult & existing) == 0);
  save.mark(existing | SettingsSave::AnalysisResult);
  assert(save.pending() == (existing | SettingsSave::AnalysisResult));
  save.attempted(1000);
  save.wrote(existing);
  assert(save.pending() == SettingsSave::AnalysisResult && save.failed());
  assert(!save.due(5999) && save.due(6000));
  save.mark(SettingsSave::Bluetooth | SettingsSave::PassConfirmed);
  save.wrote(SettingsSave::AnalysisResult);
  assert(save.pending() == (SettingsSave::Bluetooth | SettingsSave::PassConfirmed));
  save.wrote(SettingsSave::Bluetooth | SettingsSave::PassConfirmed);
  assert(!save.pending() && !save.failed());
  assert(SettingsSave::item(14) == SettingsSave::Bluetooth);
  std::puts("PASS: Analysis and upstream save flags remain independent through retries");
}
