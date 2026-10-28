#pragma once
#include <cstdint>

namespace PracticeUI {
// Values 4/5 belonged to the removed Motion/Recreate pages. Keep Settings=6
// so an existing wake marker still restores Settings after an update.
enum class View:uint8_t { Recent=0, Pulls=1, Sessions=2, Battery=3, Settings=6 };
inline View restoredView(uint8_t value) {
  switch(value) {
    case 0:return View::Recent;
    case 1:return View::Pulls;
    case 2:return View::Sessions;
    case 3:return View::Battery;
    case 6:return View::Settings;
    default:return View::Recent;
  }
}
inline View next(View view) {
  switch(view) {
    case View::Recent:return View::Pulls;
    case View::Pulls:return View::Sessions;
    case View::Sessions:return View::Battery;
    case View::Battery:return View::Settings;
    default:return View::Recent;
  }
}
}
