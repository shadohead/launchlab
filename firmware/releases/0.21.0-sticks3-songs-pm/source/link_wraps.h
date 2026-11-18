#pragma once
// What the linker's wrappers (src/link_wraps/link_wraps.cpp) ask the sketch. The sketch
// defines each where the answer lives; link_wraps.cpp only forwards, and is
// its own object file for the reason given there.
#include <cstddef>

namespace LinkWraps {
// LAUNCHLAB_RADIO_CAL_WRAP (radio_calibration.h): this nvs_open is refused, and counted.
bool nvsOpenRefused(const char *name);
#if LAUNCHLAB_HIL
// Test builds (hil.h): the NVS faults a test armed.
bool hilNvsWriteFails(const char *key,std::size_t bytes);
void hilNvsWroteBlob(const char *key);
bool hilNvsReadFails(const char *key);
#endif
}
