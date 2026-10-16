# Compact FLAC21 miniSEED2 record profile

Specification revision: 1, 2026-10-15. Implemented by this FLAC21 library.
Experimental format design and integration: **Andrei Akimov**.
This describes the current C implementation; revision 1 is a documentation
revision and is not a newly inserted on-disk field. See COMPATIBILITY.md.

## Scope and record layout

Each record represents one component/channel and decodes independently to
signed int32 samples. No inter-record state or cross-component prediction is
required. New output is miniSEED2 only, encoding 21 in blockette 1000.
Record length R is a power of two from 512 through 1,048,576 bytes.
Sample rate is an exactly integral 1..655350 Hz, using miniSEED header rate
fields/blockette mapping; unsupported/fractional rates are rejected.
Writer sample count N is 1..16384 and is stored as a big-endian uint16 in
the miniSEED2 fixed header. The compact decoder enforces the same count cap.

Offsets below are zero-based; ranges are half-open.

| Record bytes | Meaning |
| --- | --- |
| [0, 48) | miniSEED2 fixed section of data header, in big-endian form |
| [48, D) | Blockette chain and any header padding/mapped metadata |
| [D, D+L) | Exactly one mono FLAC frame of L bytes |
| [D+L, R-4) | Zero padding |
| [R-4, R) | Big-endian IEEE CRC32 over the whole record with this field zeroed |

The fixed header holds N at bytes [30,32), D (data offset) at [44,46),
and the first blockette offset at [46,48). Byte 39 gives blockette count.
D is a valid 16-bit miniSEED2 offset, at least 48; the implementation requires
D+20 <= R to leave room for a frame and CRC. Real usable minimum is determined
by the generated blockettes, their lengths and the encoded frame.

Blockette 1000 is required exactly once. Relative bytes +4,+5,+6 contain
encoding **21**, data word-order **1**, and log2(R), respectively.
The reader validates the known blockette lengths, chain count and ordering;
headers/blockette bodies may not overlap the payload or each other. Unknown
blockette lengths are rejected. Blockette 1001 and additional mapped metadata
may be present. The usual writer header is 64 bytes, leaving 444 bytes for
the frame in a 512-byte record. The 448 bytes after that header include the
four-byte CRC; they are not all available to FLAC.

## FLAC payload

The first bytes are a normal FLAC frame sync (FF followed by F8/F9 under a
FE mask), not `fLaC`. Frame syntax, predictors, residual coding, frame header
CRC8 and frame CRC16 are the standard FLAC structures described by RFC 9639:
https://www.rfc-editor.org/rfc/rfc9639.html

There is no stored STREAMINFO, MD5, vendor comment, payload-length field,
additional compact-profile magic/version byte. The decoded
frame block size must equal N, channel count must be one, and frame sample
rate must agree with the miniSEED rate. A decoder must locate the actual frame
end and require every remaining byte before the record CRC to be zero.

The FLAC sample-size code is bits 3..1 of frame byte 3 (relative to D):

| Code | Signed stored precision |
| ---: | ---: |
| 0 | 32 bits, profile default |
| 1 | 8 bits |
| 2 | 12 bits |
| 3 | Reserved; reject |
| 4 | 16 bits |
| 5 | 20 bits |
| 6 | 24 bits |
| 7 | 32 bits |

All samples must fit the selected signed precision. Decoding returns int32;
this is lossless representation, with no scaling, filtering or quantization.
Code zero obtains its 32-bit meaning from this profile's synthetic STREAMINFO,
not from a universal FLAC default. Rejecting code 3 is intentional.

## Reconstructing STREAMINFO for libFLAC

To use an ordinary libFLAC stream decoder, prepend a synthetic 42-byte prefix
in memory: `fLaC`, four-byte metadata header 80 00 00 22 (last block, type 0,
length 34), and 34-byte STREAMINFO initialized to zero.

Set minimum/maximum block size both to max(N,32); leave minimum/maximum frame
sizes zero (unknown). In the 64-bit STREAMINFO packed field at prefix bytes
[18,26), set sample rate to the header rate, channels-minus-one to zero,
bits-per-sample-minus-one to the decoded precision minus one, total samples
to N. Write this field big-endian. The 16-byte MD5 at [26,42) remains zero,
meaning unavailable. Synthetic block-size fields are compatibility hints;
actual decoded frame count must still equal N.

Process metadata, decode exactly one frame, verify the decoder result, and
obtain consumed frame bytes using libFLAC's decode position minus 42.
Require 0 < L <= R-D-4 and verify all padding. Do not scan blindly for the
next sync or strip trailing zero bytes to infer L: valid frame bytes can
contain zeros. Our callback exposes prefix and original frame without
copying the full padded payload.

## Whole-record checksum

Use reflected CRC-32/ISO-HDLC (IEEE): polynomial 0xEDB88320, initial state
0xFFFFFFFF, final XOR 0xFFFFFFFF. Include every record byte, including all
headers and padding, treating [R-4,R) as four zero bytes. Store the result
big-endian. This is IEEE CRC32, not CRC32C used by miniSEED3.

Equivalent reference calculation:

```python
import struct
import zlib
expected = zlib.crc32(record[:-4] + bytes(4)) & 0xffffffff
stored = struct.unpack('>I', record[-4:])[0]
assert expected == stored
```

The current reader checks CRC before decoding; it also checks FLAC frame
errors, channel count, sample precision, sample count, rate and zero padding.
The writer decodes its selected frame and compares every sample before
writing the record. CRCs provide error detection, not authentication.

## Writer choices versus format requirements

The writer uses libFLAC 1.5.0 compression level 8, mono, non-subset mode,
block size max(N,32), maximum LPC order min(N-1,32). It tries the narrowest
supported precision containing all samples, and a 32-bit fallback for
nonconstant narrower blocks, keeping the smaller encoded result. Constant
blocks skip the redundant wider trial. These are encoder choices; a
compatible decoder must follow the payload, not assume these settings.

Prefix fitting uses shrink estimates, binary search and eight local probes,
with rejected-count caching. Frame sizes are not monotone; this does not
promise globally optimal record boundaries or better compression on all data.
Thread scheduling may change cross-channel interleaving without changing
independent per-channel record content/order.

