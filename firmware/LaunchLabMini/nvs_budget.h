#pragma once

// Entry accounting for the 20 KB ESP-IDF NVS partition (630 32-byte entries,
// one 126-entry page kept free for garbage collection). A blob costs one index
// entry, one header entry per chunk and one entry per 32 bytes of data; it
// splits into a chunk per page it spans, so budgets allow two chunks.
// Rewriting a key writes the new copy before releasing the old one.
namespace NvsBudget {
constexpr unsigned ENTRY_BYTES=32;
constexpr unsigned blobEntries(unsigned bytes){return (bytes+ENTRY_BYTES-1)/ENTRY_BYTES+3;}
// An alternating two-slot store at its peak: both slots plus the in-flight
// copy of the slot being rewritten.
constexpr unsigned alternatingStoreEntries(unsigned bytes){return 3*blobEntries(bytes);}
// Available entries left after rewriting one record of the given size.
constexpr int headroom(unsigned available,unsigned bytes){return int(available)-int(blobEntries(bytes));}
// Free entries kept beyond a history rewrite whenever an optional record
// (custom themes, the milestone table, the effect set, later loaded effects)
// would take more room. History and settings saves never use it.
// On 83201 with a full history (2026-10-21, 299 saves), a history save took
// about 130 ms, 295 ms with a page erase or 460-480 ms with two, past the
// 400 ms an automatic checkpoint may stop the ADC. Two-erase saves were 49%
// of saves at 85-94 free entries, none of 19 at 95-99 and 13% at 100-119:
// they happen at any fill level, most below about 95. 10 beyond the
// 87-entry rewrite keeps at least 97 free, out of that band, and still lets
// the fullest unit seen (110 free with a full history, a table and effect
// set in both slots and 0.15's record, no custom theme) add its first theme.
constexpr unsigned RESERVE_ENTRIES=10;
// What an optional save needs free: a rewrite in place (grows 0) needs its
// own `own` entries beside a history rewrite (`history`), as before 0.18; a
// save that takes `grows` more entries needs them plus a history rewrite that
// still leaves the reserve.
constexpr unsigned optionalNeeded(unsigned own,unsigned grows,unsigned history) {
  return grows?grows+history+RESERVE_ENTRIES:own+history;
}
}
