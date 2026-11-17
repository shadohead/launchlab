#pragma once
#include "calibration_archive.h"
#include "audio_metadata.h"

namespace AudioMetadataStore {
constexpr unsigned BASE = Calibration::Archive::BASE + Calibration::Archive::BYTES;
constexpr unsigned SLOT = 2 * FlashLayout::SECTOR, KEEP = Calibration::Archive::KEEP, SLOTS = KEEP + 1;
constexpr unsigned BYTES = SLOTS * SLOT, HEADER = 24, KIND = 6;
constexpr unsigned OLD_WAV_BYTES = 72 * FlashLayout::SECTOR;
static_assert(HEADER + AudioMetadata::MAX_BYTES <= SLOT, "Full-session metadata exceeds a flash slot");
static_assert(BASE + BYTES <= FlashLayout::EFFECTS_OFFSET, "Analysis metadata overlaps effects");
struct Entry {
  uint32_t sequence, offset, bytes, checksum, traceCrc;
};
struct Index {
  std::array<Entry, SLOTS> entries{};
  unsigned count = 0;
};

// Replace only the exact legacy WAV descriptor. Commit the new table before
// reusing sectors; raw traces, effects and history never move. A failed table
// write leaves the older superblock and old WAV region readable.
template <class Flash> bool open(Flash &flash, bool create, bool &present) {
  using namespace FlashLayout;
  Superblock table;
  unsigned copy = 2;
  for (unsigned i = 0; i < 2; ++i) {
    Superblock::Image bytes;
    Superblock candidate;
    if (!flash.read(SUPER_OFFSET[i], bytes.data(), bytes.size()))
      return false;
    if (candidate.decode(bytes.data()) && (copy == 2 || int32_t(candidate.sequence - table.sequence) > 0)) {
      table = candidate;
      copy = i;
    }
  }
  if (copy == 2 || table.version != LAYOUT_VERSION)
    return false;
  unsigned legacy = MAX_REGIONS;
  present = false;
  for (unsigned i = 0; i < table.count; ++i) {
    const auto &r = table.regions[i];
    if (r.kind == KIND) {
      if (present || r.offset != BASE || r.size != SLOTS * SLOT)
        return false;
      present = true;
    } else if (r.offset < BASE + SLOTS * SLOT && BASE < r.offset + r.size) {
      if (r.kind != 5 || r.offset != BASE || r.size != OLD_WAV_BYTES || legacy != MAX_REGIONS)
        return false;
      legacy = i;
    }
  }
  if (present)
    return legacy == MAX_REGIONS;
  if (!create)
    return true;
  if (legacy == MAX_REGIONS) {
    if (table.count == MAX_REGIONS)
      return false;
    legacy = table.count++;
  }
  table.regions[legacy] = {KIND, 0, BASE, SLOTS * SLOT};
  ++table.sequence;
  Superblock::Image bytes;
  table.encode(bytes);
  const auto at = SUPER_OFFSET[1 - copy];
  if (!flash.erase(at, SECTOR) || !flash.write(at, bytes.data(), bytes.size()) ||
      !Calibration::Archive::matches(flash, at, bytes.data(), bytes.size()))
    return false;
  present = true;
  return true;
}
template <class Flash> bool scan(Flash &flash, Index &index, bool create = false) {
  using namespace FlashLayout;
  index.count = 0;
  bool present = false;
  if (!open(flash, create, present))
    return false;
  if (!present)
    return true;
  for (unsigned slot = 0; slot < SLOTS; ++slot) {
    uint8_t h[HEADER];
    const auto at = BASE + slot * SLOT;
    if (!flash.read(at, h, HEADER))
      return false;
    if (std::memcmp(h, "LLAM", 4) || crc32(h, 20) != get32(h + 20))
      continue;
    const unsigned bytes = get32(h + 8);
    if (bytes < AudioMetadata::HEADER + 4 || bytes > AudioMetadata::MAX_BYTES)
      continue;
    index.entries[index.count++] = {get32(h + 4), at, bytes, get32(h + 12), get32(h + 16)};
  }
  std::sort(index.entries.begin(), index.entries.begin() + index.count,
            [](const Entry &a, const Entry &b) { return int32_t(a.sequence - b.sequence) < 0; });
  return true;
}
template <class Flash>
bool save(Flash &flash, uint32_t sequence, uint32_t traceCrc, const uint8_t *data, size_t bytes) {
  using namespace FlashLayout;
  if (!AudioMetadata::valid(data, bytes))
    return false;
  Index index;
  if (!scan(flash, index, true))
    return false;
  const uint32_t checksum = crc32(data, bytes);
  for (unsigned i = 0; i < index.count; ++i) {
    const auto &e = index.entries[i];
    if (e.sequence == sequence)
      return e.bytes == bytes && e.checksum == checksum && e.traceCrc == traceCrc &&
             Calibration::Archive::matches(flash, e.offset + HEADER, data, bytes);
  }
  // An older attachment may retry after newer pulls have saved. Use a free
  // slot, or the oldest entry when full; physical order need not match age.
  unsigned at = BASE;
  if (index.count == SLOTS)
    at = index.entries[0].offset;
  else
    for (unsigned slot = 0; slot < SLOTS; ++slot) {
      at = BASE + slot * SLOT;
      if (std::none_of(index.entries.begin(), index.entries.begin() + index.count,
                       [&](const Entry &e) { return e.offset == at; }))
        break;
    }
  uint8_t h[HEADER];
  std::memcpy(h, "LLAM", 4);
  put32(h + 4, sequence);
  put32(h + 8, bytes);
  put32(h + 12, checksum);
  put32(h + 16, traceCrc);
  put32(h + 20, crc32(h, 20));
  return flash.erase(at, SLOT) && flash.write(at + HEADER, data, bytes) &&
         Calibration::Archive::matches(flash, at + HEADER, data, bytes) && flash.write(at, h, HEADER) &&
         Calibration::Archive::matches(flash, at, h, HEADER);
}
// Batch export can reuse one index; every payload is still checked on read.
template <class Flash>
bool read(Flash &flash, const Index &index, uint32_t sequence, uint32_t traceCrc, uint8_t *out,
          size_t &bytes) {
  bytes = 0;
  for (unsigned i = index.count > KEEP ? index.count - KEEP : 0; i < index.count; ++i) {
    const auto &e = index.entries[i];
    if (e.sequence != sequence || e.traceCrc != traceCrc)
      continue;
    if (!flash.read(e.offset + HEADER, out, e.bytes) || FlashLayout::crc32(out, e.bytes) != e.checksum ||
        !AudioMetadata::valid(out, e.bytes))
      return false;
    bytes = e.bytes;
    return true;
  }
  return true; // Older raw traces legitimately have no metadata attachment.
}
template <class Flash>
bool read(Flash &flash, uint32_t sequence, uint32_t traceCrc, uint8_t *out, size_t &bytes) {
  bytes = 0;
  Index index;
  return scan(flash, index) && read(flash, index, sequence, traceCrc, out, bytes);
}
} // namespace AudioMetadataStore
