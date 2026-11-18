#pragma once
#include <cstdint>
#include <cstdio>

// Min RPM (0.21.0): the slowest launch a unit records (StickS3Acquisition::
// setLaunchFloor). One optical channel cannot tell a pull from the string fed
// back into the launcher, which the owner's captures (2026-10-27) measured at
// 3,515-3,991 rpm, so by default a launch must read 4,000 rpm or more.
// 1,000-10,000 rpm in 500 rpm steps; 1,000 is the detector's own floor
// (AnalogTachometer::MIN_LAUNCH_RPM), the same as no setting. A setting's
// value is the step count (rpm / 500): the editor, the phone and HIL use it.
namespace MinRpm {
static constexpr uint16_t STEP_RPM=500;
static constexpr uint8_t MIN_STEPS=2,MAX_STEPS=20,DEFAULT_STEPS=8;
constexpr bool valid(uint8_t steps){return steps>=MIN_STEPS && steps<=MAX_STEPS;}
constexpr uint16_t rpm(uint8_t steps){return uint16_t(steps*STEP_RPM);}
// The editor's next value: 500 rpm faster, and back to 1,000 after 10,000.
constexpr uint8_t next(uint8_t steps){return valid(steps) && steps<MAX_STEPS?uint8_t(steps+1):MIN_STEPS;}
// The row's title says RPM: the value is the number alone, which fits the
// editor's large font up to 10000.
inline void label(char *out,unsigned length,uint8_t steps){snprintf(out,length,"%u",unsigned(rpm(steps)));}
}
