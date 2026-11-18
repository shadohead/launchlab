#pragma once
#include "analysis_calibration.h"
#include "flash_log.h"

// The newest 32 completed traces plus a spare slot for a power-safe overwrite.
// Register the region in the existing layout before programming any slot.
namespace Calibration {
namespace Archive {
constexpr unsigned KEEP = 32, SLOTS = KEEP + 1, SLOT_BYTES = 4 * FlashLayout::SECTOR;
constexpr unsigned BASE = FlashLayout::PULLS_OFFSET, BYTES = SLOTS * SLOT_BYTES;
constexpr unsigned KIND = 4, HEADER_BYTES = 16;
constexpr size_t MAX_SLOT_USED =
    HEADER_BYTES + ((MAX_RAW_BYTES + 3) & ~size_t(3)) + fitBytes(Decay::MAX_SAMPLES);
static_assert(BYTES <= FlashLayout::PULLS_BYTES, "Analysis archive exceeds the pull region");
static_assert(MAX_SLOT_USED <= SLOT_BYTES, "Raw trace and fit exceed a flash slot");
struct Entry {
  uint32_t sequence, size, offset;
};
struct Index {
  std::array<Entry, SLOTS> entries{};
  unsigned count = 0;
};

// Verify a write in bounded chunks before publishing its commit marker. This
// also makes retries safe if the driver reported failure after programming.
template <class Flash> bool compare(Flash &flash, uint32_t at, const uint8_t *bytes, size_t n, bool &equal) {
  uint8_t chunk[256];
  equal = true;
  while (n) {
    const size_t size = std::min(n, sizeof(chunk));
    if (!flash.read(at, chunk, size))
      return false;
    if (std::memcmp(chunk, bytes, size)) {
      equal = false;
      return true;
    }
    at += size;
    bytes += size;
    n -= size;
  }
  return true;
}
template <class Flash> bool matches(Flash &flash, uint32_t at, const uint8_t *bytes, size_t n) {
  bool equal = false;
  return compare(flash, at, bytes, n, equal) && equal;
}

template <class Flash>
bool openRegion(Flash &flash, uint16_t kind, uint32_t base, uint32_t length, bool create, bool &registered) {
  using namespace FlashLayout;
  Superblock table;
  unsigned copy = 2;
  registered = false;
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

  for (unsigned i = 0; i < table.count; ++i) {
    const auto &r = table.regions[i];
    if (r.kind == kind) {
      if (r.offset != base || r.size != length || registered)
        return false;
      registered = true;
    } else if (r.offset < base + length && base < r.offset + r.size)
      return false;
  }
  if (!registered) {
    if (!create)
      return true;
    if (table.count == MAX_REGIONS)
      return false;
    table.regions[table.count++] = {kind, 0, base, length};
    ++table.sequence;
    Superblock::Image bytes;
    table.encode(bytes);
    const auto at = SUPER_OFFSET[1 - copy];
    if (!flash.erase(at, SECTOR) || !flash.write(at, bytes.data(), bytes.size()) ||
        !matches(flash, at, bytes.data(), bytes.size()))
      return false;
  }
  registered = true;
  return true;
}

template <class Flash> bool scan(Flash &flash, Index &index, bool create = false) {
  using namespace FlashLayout;
  index.count = 0;
  bool registered = false;
  if (!openRegion(flash, KIND, BASE, BYTES, create, registered))
    return false;
  if (!registered && !create)
    return true;
  for (unsigned slot = 0; slot < SLOTS; ++slot) {
    const uint32_t at = BASE + slot * SLOT_BYTES;
    uint8_t h[HEADER_BYTES];
    if (!flash.read(at, h, sizeof(h)))
      return false;
    const uint32_t size = get32(h + 8);
    if (get32(h) == 0x3141434c && get32(h + 12) == crc32(h, 12) && size >= RAW_MIN && size <= MAX_BYTES)
      index.entries[index.count++] = {get32(h + 4), size, at};
  }
  std::sort(index.entries.begin(), index.entries.begin() + index.count,
            [](const Entry &a, const Entry &b) { return int32_t(a.sequence - b.sequence) < 0; });
  return true;
}
template <class Flash>
bool append(Flash &flash, const uint8_t *bytes, size_t size, Entry *committed = nullptr) {
  Index index;
  if (!validPacket(bytes, size) || !scan(flash, index, true))
    return false;
  const Entry newest =
      index.count ? index.entries[index.count - 1] : Entry{0, 0, BASE + (SLOTS - 1) * SLOT_BYTES};
  if (index.count && newest.size == size) {
    bool equal = false;
    if (!compare(flash, newest.offset + HEADER_BYTES, bytes, size, equal))
      return false;
    if (equal) {
      if (committed)
        *committed = newest;
      return true;
    }
  }
  const uint32_t at = BASE + (newest.offset - BASE + SLOT_BYTES) % BYTES;
  uint8_t h[HEADER_BYTES];
  FlashLayout::put32(h, 0x3141434c);
  FlashLayout::put32(h + 4, newest.sequence + 1);
  FlashLayout::put32(h + 8, size);
  FlashLayout::put32(h + 12, FlashLayout::crc32(h, 12));
  // Only erase sectors the new packet needs. The header commits the packet
  // last.
  const bool raw = isRawPacket(bytes);
  const unsigned count = raw ? FlashLayout::get32(bytes + 20) : 0;
  if (count > Decay::MAX_SAMPLES)
    return false;
  // Reserve erased space for an optional fit, even when reusing an old slot.
  const size_t reserved = raw ? ((size + 3) & ~size_t(3)) + fitBytes(count) : size;
  if (HEADER_BYTES + reserved > SLOT_BYTES)
    return false;
  const size_t sector = FlashLayout::SECTOR,
               eraseBytes = ((sizeof(h) + reserved + sector - 1) / sector) * sector;
  const bool ok = flash.erase(at, eraseBytes) && flash.write(at + sizeof(h), bytes, size) &&
                  matches(flash, at + sizeof(h), bytes, size) && flash.write(at, h, sizeof(h)) &&
                  matches(flash, at, h, sizeof(h));
  if (ok && committed)
    *committed = {newest.sequence + 1, uint32_t(size), at};
  return ok;
}
inline uint32_t fitOffset(const Entry &e) {
  return e.offset + HEADER_BYTES + ((e.size + 3) & ~3u);
}
template <class Flash> bool attach(Flash &flash, const Entry &e, const uint8_t *fit, size_t n) {
  using namespace FlashLayout;
  if (e.offset < BASE || e.offset >= BASE + BYTES || (e.offset - BASE) % SLOT_BYTES || e.size < RAW_MIN ||
      e.size > MAX_BYTES)
    return false;
  uint8_t h[HEADER_BYTES], raw[24];
  if (!flash.read(e.offset, h, sizeof(h)) || get32(h) != 0x3141434c || get32(h + 12) != crc32(h, 12) ||
      get32(h + 4) != e.sequence || get32(h + 8) != e.size ||
      !flash.read(e.offset + HEADER_BYTES, raw, sizeof(raw)) || !isRawPacket(raw) ||
      !validFit(fit, n, get32(raw + 20)))
    return false;
  const auto at = fitOffset(e);
  // A power cut leaves a readable raw packet. Program the marker last.
  return at + n <= e.offset + SLOT_BYTES &&
         (matches(flash, at, fit, n) ||
          (flash.write(at + 4, fit + 4, n - 4) && matches(flash, at + 4, fit + 4, n - 4) &&
           flash.write(at, fit, 4) && matches(flash, at, fit, n)));
}
} // namespace Archive
} // namespace Calibration
