#pragma once
#include "decay_record.h"
#include "forecast_shadow.h"

// Little-endian calibration archives. Raw observations consume sample storage;
// their time deltas preserve quiet intervals without padding. LLC9 preserves
// LLC8 pitch provenance and distinguishes interrupted runs from timeouts.
// LLCA stores the active stamina forecast in compact checkpoint slots.
namespace Calibration {
constexpr size_t HEADER = 96 + Decay::Record::BYTES, MAX_BYTES = HEADER + 5 * Decay::MAX_SAMPLES + 4;
struct Preview {
  uint32_t atMs = 0, count = 0, model = 0, kind = 0;
  float lo = 0, hi = 0, stops[4] = {};
};
inline void put(uint8_t *&p, uint32_t x) {
  for (unsigned i = 0; i < 4; ++i)
    *p++ = uint8_t(x >> (8 * i));
}
inline void number(uint8_t *&p, float value) {
  uint32_t v;
  std::memcpy(&v, &value, 4);
  put(p, v);
}
// LLC2 commits the raw prefix independently. A later fit attaches its own
// checksummed record and one exclusion bit per raw sample.
constexpr size_t RAW_HEADER = 96, RAW_MIN = RAW_HEADER + 4;
inline bool isRawPacket(const uint8_t *data) {
  return !std::memcmp(data, "LLC", 3) && ((data[3] >= '2' && data[3] <= '9') || data[3] == 'A');
}
constexpr size_t RAW_OVERHEAD = RAW_MIN + 26 + 12 * ForecastShadow::COUNT;
// Adjacent relative-pitch readings cost five bytes each. Longer deltas use
// empty 100 ms slots, so the five-minute trace still fits this bound. Sparse
// traces reserve six bytes per reading plus two possible four-byte deltas.
constexpr size_t MAX_RAW_BYTES = RAW_OVERHEAD + 5 * Decay::MAX_SAMPLES;
constexpr size_t rawCapacity(unsigned count) {
  return std::min(RAW_OVERHEAD + 6 * count + 2, MAX_RAW_BYTES);
}
constexpr size_t fitBytes(unsigned count) {
  return 4 + Decay::Record::BYTES + (count + 7) / 8 + 4;
}
inline void checksum(uint8_t *start, uint8_t *end) {
  put(end, Decay::Record::checksum(start, end - start));
}
inline uint32_t word(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline bool validPacket(const uint8_t *data, size_t n) {
  if (!data || n < RAW_MIN || n > MAX_BYTES || word(data + 20) > Decay::MAX_SAMPLES)
    return false;
  if (!isRawPacket(data) && (std::memcmp(data, "LLC1", 4) || n < HEADER + 4))
    return false;
  return word(data + n - 4) == Decay::Record::checksum(data, n - 4);
}
inline bool validFit(const uint8_t *data, size_t n, unsigned count) {
  return data && count <= Decay::MAX_SAMPLES && n == fitBytes(count) && !std::memcmp(data, "LLF2", 4) &&
         word(data + 44) == count &&
         word(data + 4 + Decay::Record::BYTES - 4) ==
             Decay::Record::checksum(data + 4, Decay::Record::BYTES - 4) &&
         word(data + n - 4) == Decay::Record::checksum(data, n - 4);
}

template <class Trace>
size_t packRaw(uint8_t *data, uint32_t launch, float rpm, uint32_t duration, unsigned stopReason,
               const uint8_t *build, const Preview &v, const Trace *trace,
               const ForecastShadow::Log &shadow) {
  const unsigned count = trace ? trace->rawCount() : 0;
  uint8_t *p = data;
  // LLC9 endpoint field: 0 timeout, 1 manual observation, 2 interrupted.
  put(p, 0x41434c4c);
  put(p, launch);
  number(p, rpm);
  put(p, duration);
  put(p, stopReason);
  put(p, count);
  put(p, v.atMs);
  put(p, v.count);
  put(p, v.model);
  put(p, v.kind);
  number(p, v.lo);
  number(p, v.hi);
  for (float stop : v.stops)
    number(p, stop);
  std::memcpy(p, build, 32);
  p += 32;
  uint32_t previous = 0;
  for (unsigned i = 0; trace && i < trace->count(); ++i) {
    const uint16_t raw = trace->rawRpm(i);
    if (!raw)
      continue;
    const auto ms = uint32_t(std::lround(Trace::seconds(i) * 1000));
    // LLC8 flags: live rejection, final exclusion (in the attachment), generic
    // recovery, relative pitch. Confidence follows RPM only when bit 3 is set.
    const uint8_t confidence = trace->pitchConfidence(i);
    uint32_t delta = ((ms - previous) << 4) | (unsigned(confidence != 0) << 3) |
                     (unsigned(trace->recovered(i) || confidence) << 2) |
                     unsigned(trace->rejectedRpm(i) != 0);
    previous = ms;
    while (delta >= 128) {
      *p++ = uint8_t(delta) | 128;
      delta >>= 7;
    }
    *p++ = uint8_t(delta);
    *p++ = uint8_t(raw);
    *p++ = uint8_t(raw >> 8);
    if (confidence)
      *p++ = confidence;
  }
  for (unsigned i = 0; i < 9; ++i) {
    const unsigned n = trace ? trace->reasonCount(i) : 0;
    *p++ = uint8_t(n);
    *p++ = uint8_t(n >> 8);
  }
  put(p, trace ? trace->skippedMs() : 0);
  put(p, trace ? trace->lostFrames() : 0);
  for (const auto &row : shadow.rows) {
    put(p, row.observedMs);
    put(p, unsigned(row.displayed.source));
    number(p, row.displayed.seconds);

  }
  checksum(data, p);
  return size_t(p - data) + 4;
}
inline void packFit(uint8_t *data, const Decay::Session &input, const Decay::Results &fits) {
  uint8_t *p = data;
  put(p, 0x32464c4c); // LLF2
  Decay::Record::Image record;
  Decay::Record::encode(fits, unsigned(std::max(0, fits.best)), record);
  std::memcpy(p, record.data(), record.size());
  p += record.size();
  const unsigned n = (input.count + 7) / 8;
  std::memset(p, 0, n);
  for (unsigned i = 0; i < input.count; ++i)
    if (input.samples[i].excludedFromFit)
      p[i / 8] |= 1u << (i % 8);
  checksum(data, p + n);
}
} // namespace Calibration
