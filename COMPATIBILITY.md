# Encoding 21 and reader compatibility

Encoding 21 is an experimental local identifier, not an FDSN assignment or
an official private-use reservation. The published FDSN registry is:
https://docs.fdsn.org/projects/miniseed3/en/latest/data-encodings.html
This extension writes miniSEED2. Its FLAC records require a reader that
implements [FORMAT.md](FORMAT.md); stock readers do not gain FLAC support
merely because the enclosing miniSEED header is recognized.

| Stored data | This library | Ordinary upstream reader |
| --- | --- | --- |
| STEIM2 (11) | Supported | Supported where STEIM2 is implemented |
| Compact FLAC21 | Supported | Requires a compatible extension |

The compact FLAC record decoder accepts encoding 21 and requires a mono
frame, valid rate/count/precision, zero padding and a whole-record IEEE CRC32.
Supported stored precisions are 8/12/16/20/24/32 bits; output is exact int32.
Preserve the format specification and reader source with archived data.
Other experimental payloads using the same number are not interchangeable.

Use record APIs: `msr3_parse`/`ms3_readmsr_r` with unpacking,
`msr3_unpack_data`, or `msflac_unpack_record`. Standalone raw-payload
`ms_decode_data()` does not implement this record-dependent profile.
The adapter API is declared in `mseed_flac.h`; encoding parameters retain
their upstream types. Use `DE_FLAC_EXPERIMENTAL` for packing.

```sh
./repack steim2.mseed flac21.mseed 21 512 --threads 4
./compare_samples steim2.mseed flac21.mseed
```

Files may mix supported encodings and independent channels. `repack` chooses
one requested output encoding; it does not automatically choose between
STEIM2 and FLAC. Original record boundaries/interleaving can change. Unsupported
original metadata may not survive upstream header mapping. The comparator
checks samples, channel timing, rates, flags and publication versions, not
arbitrary extra-header equivalence.

This is an experimental extension, not an EarthScope/FDSN/Xiph release or
an assertion of archive certification. Linux top-level Makefile builds are
supported; upstream Windows/CMake files are not adapted. Checksums detect
corruption, not authenticity. See LICENSE and THIRD_PARTY_NOTICES.md.
