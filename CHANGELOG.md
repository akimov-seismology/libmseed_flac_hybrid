# Release notes

This distribution provides compact lossless FLAC21 compression in miniSEED2
using libmseed 3.5.4 and unmodified libFLAC 1.5.0.

- Fixed-size independently decodable records, 512 bytes through 1 MiB.
- Exact int32 values with adaptive 8/12/16/20/24/32-bit stored precision.
- Whole-record IEEE CRC32, FLAC frame CRCs and encoder sample verification.
- Normal libmseed record APIs, independent per-channel buffering and packing
  with 1 through 64 worker threads.
- Gap, overlap, rate and metadata boundary handling; RAM sample comparison.
- Build instructions, test-source inventory, format and compatibility guidance.

Encoding 21 is experimental and not FDSN-assigned. See README.md, FORMAT.md,
COMPATIBILITY.md, VALIDATION.md and the licensing notices.
