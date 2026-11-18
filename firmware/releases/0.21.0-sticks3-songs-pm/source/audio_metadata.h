#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

// Analysis-only acoustic evidence. Frequencies remain Hz, including candidates
// that never became RPM. No accepted-reading counts or filter state change here.
namespace AudioMetadata {
constexpr unsigned BANDS = 8, RECORD_BYTES = 19, HEADER = 32;
constexpr unsigned DENSE_END = 30000, DENSE_MS = 200, SPARSE_MS = 1000, LIMIT_MS = 300000;
constexpr unsigned DENSE_RECORDS = DENSE_END / DENSE_MS;
constexpr unsigned MAX_RECORDS = DENSE_RECORDS + (LIMIT_MS - DENSE_END) / SPARSE_MS;
constexpr unsigned MAX_BYTES = HEADER + MAX_RECORDS * RECORD_BYTES + 4;
constexpr float EDGES[BANDS + 1] = {40, 80, 160, 320, 500, 800, 1200, 1600, 2000};
struct Frame {
  uint8_t bands[BANDS] = {};
};
inline uint8_t db(float value) {
  return std::isfinite(value) ? uint8_t(std::lround(std::clamp(value, -127.f, 0.f)) + 128) : 0;
}
inline unsigned index(unsigned endMs) {
  return endMs <= DENSE_END ? (endMs ? endMs - 1 : 0) / DENSE_MS
                            : DENSE_RECORDS + (endMs - DENSE_END - 1) / SPARSE_MS;
}
inline unsigned begin(unsigned i) {
  return i < DENSE_RECORDS ? i * DENSE_MS : DENSE_END + (i - DENSE_RECORDS) * SPARSE_MS;
}
inline unsigned end(unsigned i) {
  return begin(i) + (i < DENSE_RECORDS ? DENSE_MS : SPARSE_MS);
}
inline unsigned records(unsigned duration) {
  return duration ? index(std::min(duration, LIMIT_MS)) + 1 : 0;
}
inline size_t packedBytes(unsigned duration) {
  return HEADER + records(duration) * RECORD_BYTES + 4;
}
inline void put16(uint8_t *p, unsigned v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
}
inline void put32(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = uint8_t(v >> (8 * i));
}
inline uint32_t get32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline uint32_t crc(const uint8_t *p, size_t n) {
  uint32_t c = ~0u;
  while (n--) {
    c ^= *p++;
    for (unsigned i = 0; i < 8; ++i)
      c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
  }
  return ~c;
}

using Measurement = std::array<uint8_t, 16>;
template <class Reading> Measurement measure(const Reading &r) {
  Measurement row{};
  put16(row.data(), unsigned(std::lround(std::clamp(r.candidateHz * 2, 0.f, 65535.f))));
  row[2] = db(r.level);
  row[3] = r.audio.bands[0] ? uint8_t(std::lround(std::clamp(r.flatness, 0.f, 1.f) * 254)) : 255;
  row[4] = r.candidateHz > 0 ? uint8_t(std::lround(std::clamp(r.score, 0.f, 254.f))) : 255;
  row[5] = r.candidateHz > 0 ? uint8_t(std::lround(std::clamp(r.margin, -128.f, 126.f)) + 128) : 255;
  row[6] = uint8_t(unsigned(r.reason) | (unsigned(r.search) << 4) |
                   (unsigned(r.valid ? r.cycles : r.candidateCycles) << 6));
  row[7] = r.support;
  std::copy(r.audio.bands, r.audio.bands + BANDS, row.begin() + 8);
  return row;
}
inline size_t pack(uint8_t *out, const uint8_t *rows, unsigned duration, unsigned skipped, unsigned drops,
                   bool available) {
  duration = std::min(duration, LIMIT_MS);
  const unsigned count = records(duration);
  std::memset(out, 0, HEADER);
  std::memcpy(out, "LAF1", 4);
  put32(out + 4, duration);
  put16(out + 8, count);
  out[10] = RECORD_BYTES;
  out[11] = BANDS;
  put32(out + 12, skipped);
  put32(out + 16, drops);
  put16(out + 20, 400);
  put16(out + 22, DENSE_MS);
  put32(out + 24, DENSE_END);
  put16(out + 28, SPARSE_MS);
  put16(out + 30, available ? 1 : 0);
  if (rows)
    std::memcpy(out + HEADER, rows, count * RECORD_BYTES);
  else
    std::memset(out + HEADER, 0, count * RECORD_BYTES);
  const size_t bytes = HEADER + count * RECORD_BYTES;
  put32(out + bytes, crc(out, bytes));
  return bytes + 4;
}
class Recorder {
  using Row = std::array<uint8_t, RECORD_BYTES>;
  std::array<Row, MAX_RECORDS> rows_{};
  uint32_t launch_ = 0;
  bool frozen_ = false;
  unsigned flatCount_ = 0, flatSum_ = 0, current_ = MAX_RECORDS, lastEnd_ = 0;

public:
  void reset(uint32_t launch) {
    std::memset(rows_.data(), 0, sizeof(rows_));
    launch_ = launch;
    frozen_ = false;
    flatCount_ = flatSum_ = lastEnd_ = 0;
    current_ = MAX_RECORDS;
  }
  uint32_t launch() const { return launch_; }
  const uint8_t *row(unsigned i) const { return i < rows_.size() ? rows_[i].data() : nullptr; }
  void add(unsigned endMs, unsigned observedMs, const Measurement &m) {
    if (frozen_ || !endMs || endMs > LIMIT_MS)
      return;
    const unsigned at = index(endMs);
    if (endMs < lastEnd_)
      return;
    lastEnd_ = endMs;
    if (at != current_) {
      flatCount_ = flatSum_ = 0;
      current_ = at;
    }
    auto &row = rows_[at];
    if (!row[16])
      row[3] = row[4] = row[5] = 255;
    row[16] = uint8_t(std::min(255u, unsigned(row[16]) + 1));
    row[2] = std::max(row[2], m[2]);
    if (m[3] != 255) {
      flatSum_ += m[3];
      ++flatCount_;
      row[3] = uint8_t((flatSum_ + flatCount_ / 2) / flatCount_);
    }
    for (unsigned i = 8; i < 16; ++i)
      row[i] = std::max(row[i], m[i]);
    const unsigned reason = m[6] & 15;
    if (reason == 3)
      row[17] = uint8_t((row[17] & 0xf0) | std::min(15u, unsigned(row[17] & 15) + 1));
    if (reason == 2)
      row[17] = uint8_t((row[17] & 15) | (std::min(15u, unsigned(row[17] >> 4) + 1) << 4));
    const unsigned lag = observedMs > endMs ? observedMs - endMs : 0;
    row[18] = std::max(row[18], uint8_t(std::min(255u, (lag + 19) / 20)));
    // Keep the best-supported candidate; band/level peaks are independent.
    if ((m[0] || m[1]) && ((!row[0] && !row[1]) || m[7] > row[7] || (m[7] == row[7] && m[4] > row[4]))) {
      row[0] = m[0];
      row[1] = m[1];
      for (unsigned i = 4; i < 8; ++i)
        row[i] = m[i];
    } else if (!row[0] && !row[1])
      row[6] = m[6];
  }
  void freeze() { frozen_ = true; }
  size_t pack(uint8_t *out, unsigned duration, unsigned skipped, unsigned drops) const {
    const size_t n = AudioMetadata::pack(out, rows_[0].data(), duration, skipped, drops, true);
    // A delayed stop can trim a partially accumulated interval. Its summary
    // cannot be split back into frames, so mark that interval missing.
    if (duration && lastEnd_ > duration && duration < end(index(duration))) {
      std::memset(out + HEADER + index(duration) * RECORD_BYTES, 0, RECORD_BYTES);
      put32(out + n - 4, crc(out, n - 4));
    }
    return n;
  }
};
inline bool valid(const uint8_t *p, size_t n) {
  if (!p || n < HEADER + 4 || n > MAX_BYTES || std::memcmp(p, "LAF1", 4))
    return false;
  const unsigned count = p[8] | (unsigned(p[9]) << 8), duration = get32(p + 4);
  return duration <= LIMIT_MS && count == records(duration) && p[10] == RECORD_BYTES && p[11] == BANDS &&
         (p[20] | unsigned(p[21]) << 8) == 400 && (p[22] | unsigned(p[23]) << 8) == DENSE_MS &&
         get32(p + 24) == DENSE_END && (p[28] | unsigned(p[29]) << 8) == SPARSE_MS && p[30] <= 1 && !p[31] &&
         n == HEADER + count * RECORD_BYTES + 4 && crc(p, n - 4) == get32(p + n - 4);
}
} // namespace AudioMetadata
