#pragma once
#include <cstdint>

// Flip is the saved On/Off choice, not a physical rotation flag. The mounted
// default (Off) is 180 degrees from native portrait; On selects native portrait.
// Keep sensor/replay coordinates and LCD rotation on this same mapping.
namespace DisplayOrientation {
constexpr bool rotated180(bool flipEnabled=false){return !flipEnabled;}
constexpr uint8_t rotation(bool flipEnabled=false){return rotated180(flipEnabled)?2:0;}
constexpr float axisSign(bool flipEnabled=false){return rotated180(flipEnabled)?-1.0f:1.0f;}
constexpr bool flipForRotation(uint8_t lcdRotation){return lcdRotation==rotation(true);}
}
