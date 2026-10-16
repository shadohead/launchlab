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
}
