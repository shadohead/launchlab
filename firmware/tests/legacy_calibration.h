#pragma once
#include "analysis_calibration.h"

// Builds LLC1 fixtures for backward-compatibility tests. Production writes LLC9.
namespace Calibration::Legacy {
inline size_t capacity(unsigned count) { return HEADER + 5 * std::min(count, Decay::MAX_SAMPLES) + 4; }
inline size_t pack(uint8_t *data, size_t room, uint32_t launch, const uint8_t *build,
                   const Decay::Session &input, const Decay::Results &fits, const Preview &preview) {
  if (input.count > Decay::MAX_SAMPLES || room < capacity(input.count))
    return 0;
  uint8_t *p = data;
  put(p, 0x31434c4c);
  put(p, launch);
  number(p, input.launcherRpm);
  put(p, input.stopMs);
  put(p, input.hasStop);
  put(p, input.count);
  put(p, preview.atMs);
  put(p, preview.count);
  put(p, preview.model);
  put(p, preview.kind);
  number(p, preview.lo);
  number(p, preview.hi);
  for (float stop : preview.stops)
    number(p, stop);
  std::memcpy(p, build, 32);
  p += 32;
  Decay::Record::Image record;
  Decay::Record::encode(fits, unsigned(std::max(0, fits.best)), record);
  std::memcpy(p, record.data(), record.size());
  p += record.size();
  uint32_t previous = 0;
  for (unsigned i = 0; i < input.count; ++i) {
    const auto &s = input.samples[i];
    if (s.ms < previous || s.ms > 301000 || !std::isfinite(s.rpm) || s.rpm < 1 || s.rpm > 65535)
      return 0;
    uint32_t value = ((s.ms - previous) << 2) | (unsigned(s.excludedFromFit) << 1) | unsigned(s.rejected);
    previous = s.ms;
    while (value >= 128) {
      *p++ = uint8_t(value) | 128;
      value >>= 7;
    }
    *p++ = uint8_t(value);
    auto rpm = uint16_t(std::lround(s.rpm));
    *p++ = uint8_t(rpm);
    *p++ = uint8_t(rpm >> 8);
  }
  put(p, Decay::Record::checksum(data, size_t(p - data)));
  return size_t(p - data);
}
} // namespace Calibration::Legacy
