# libmseed + FLAC / STEIM2 hybrid

Current implementation: compact FLAC21.

Reusable tests, fixtures and validation documentation are included. Tests may
create logs or output files when run; these are not build inputs.

This experimental C fork integrates compact FLAC frame support into
libmseed 3.5.4. Normal libmseed packing and reading APIs now handle FLAC
without application calls to `msflac_pack_record` or `msflac_unpack_record`.
STEIM2 and the other upstream encodings remain available. A file may mix
STEIM2 and FLAC records and multiple channels. Each record is independent.

## Authorship, status and format

**Andrei Akimov — project author and maintainer.** Developed with assistance
from OpenAI ChatGPT/Codex. Upstream libmseed and FLAC authors retain their
credit; see [AUTHORS.md](AUTHORS.md) and [CITATION.cff](CITATION.cff).

This is an experimental public-release candidate, not a standard encoding.
**21 is not an FDSN assignment or private-use reservation. Ordinary miniSEED
readers cannot decode these FLAC records without this extension.**
Read [FORMAT.md](FORMAT.md), [COMPATIBILITY.md](COMPATIBILITY.md),
[LICENSE](LICENSE), [LICENSE_NOTES.md](LICENSE_NOTES.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[CHANGELOG.md](CHANGELOG.md) before redistributing or exchanging files.

## Build and test

When updating an existing extracted copy, run `make clean` before rebuilding.

From this directory on Linux with a C compiler, GNU make, tar and xz:

```sh
make -j4
make test
make upstream-tests
```

The archive includes FLAC 1.5.0 source, including its configure script.
No Python, CMake, autoconf or network access is needed for the C build.
Use this top-level Makefile; the upstream CMake/Windows build files have
not been adapted. Both outputs, `libmseed_flac_hybrid.a` and
`libmseed_flac_hybrid.so`, contain libFLAC. No separate FLAC library is needed when linking
with the static archive or running with the shared library. The build compiles
libFLAC as position-independent static code (`--with-pic`).

Example static application link:

```sh
cc -Ilibmseed app.c libmseed_flac_hybrid.a \
  -lm -o app
```

Example shared application link (place the hybrid `.so` beside the executable):

```sh
cc -Ilibmseed app.c -L. -lmseed_flac_hybrid -lm \
  -Wl,-rpath,'$ORIGIN' -o app
```

These libraries still use standard system libraries such as libc and libm.
The `repack` application also uses POSIX threads. libFLAC remains unmodified;
its license and attribution notices are included in the distribution.

## Normal API use

Prepare an `MS3Record` as usual, with SID, start time, sample rate and data:

```c
msr->formatversion = 2;
msr->reclen = 4096;             /* 512 also supported */
msr->encoding = DE_FLAC_EXPERIMENTAL;
msr->sampletype = 'i';
msr->datasamples = samples;     /* int32_t array */
msr->numsamples = sample_count;
msr->samplecnt = sample_count;
int64_t packed;
int records = msr3_pack(msr, record_handler, context, &packed,
                       MSF_FLUSHDATA, 0);
```

Set `encoding = DE_STEIM2` for STEIM2 output. Normal `msr3_parse` and
`ms3_readmsr_r` with `MSF_UNPACKDATA` automatically decode both encodings.
`msr3_pack_init/next/free` and trace-list packing also support FLAC.
Pass `DE_FLAC_EXPERIMENTAL` (21) directly to record and trace-list APIs.
It fits their signed `int8_t` encoding parameters. No negative alias or
API type change is needed.

For buffering without `MSF_FLUSHDATA`, FLAC waits until at least 16,384
samples are available before starting another packing session. Flush the
remaining samples at the end. Each record consumes up to 16,384 samples;
the sample count is reduced until its compressed bytes fit, then a bounded
binary search tests larger prefixes and probes eight nearby sample counts.
Only actual encodings that fit are accepted. FLAC sizes are not strictly
monotone, so this does not guarantee a globally optimal sample count.
Multiple trial encodings increase packing cost.

Measure header, FLAC stream, trailer and unused padding, grouped by BH/HH:

```sh
python3 record_space.py flac21.mseed --record-length 512
```

This reports byte occupancy, not sample equivalence or CRC validation; run
`compare_samples` separately. End-of-segment records may remain partly empty.

## Conversion example

```sh
./repack old_steim2.mseed flac21.mseed 21 4096
./repack flac21.mseed restored_steim2.mseed 11 4096
```

The converter uses normal read/pack APIs and dynamically allocates a separate
sample buffer for each new channel SID. It joins contiguous records of that
channel even when other channels are interleaved in the input. Each channel's
input order is preserved; output interleaving can differ.

Before appending each input record it checks the expected next sample time,
rate, record flags, publication version and mapped extra headers. Gaps,
overlaps, out-of-order records, or metadata changes flush that channel's
pending samples and start a new segment. There is no sorting, interpolation,
deduplication or filling of missing samples. miniSEED2's timestamp rounding
is allowed up to 500 nanoseconds. The continuity check is conservative and
may split a segment when unusual rates accumulate more rounding noise.

FLAC uses 32,768 int32 sample slots (128 KiB) per discovered channel, plus
metadata. Samples are emitted through libmseed without flushing partial
records during ordinary buffering. Tail samples are retained; only segment
boundaries and EOF flush the remaining partial record. The last record is
still padded to the requested fixed length. STEIM2 buffers are sized using
the record geometry and never exceed 131,070 int32 slots per channel.
Memory therefore grows with the number of channel IDs, not file duration.

The converter accepts local regular input files and verifies that all input
bytes belong to complete records. It refuses to overwrite existing output and removes its new
output on failure. It preserves sample sequences, SID, timing, rates and
mapped metadata; it does not preserve the original file bytes or unsupported
original blockettes. Only nonempty int32 waveform records are accepted.
STEIM2 cannot encode every full-range int32 jump, so conversion back to
STEIM2 reports an error for such data.

Verify samples, timing, rates and flags independently of output interleaving:

```sh
./compare_samples old_steim2.mseed flac21.mseed
```

`compare_samples` loads the original int32 samples and their record metadata
into RAM, then compares the second file as it reads it. It keeps independent
sample cursors for each channel, so input record boundaries and interleaving
may differ. Completed original records are freed during comparison. It
creates no temporary files and does not keep file handles per channel.

RAM usage is four bytes per original sample plus record/channel metadata
and allocation overhead. For 129,606,810 samples, the sample arrays alone
occupy 518,427,240 bytes (about 494 MiB). Allow additional RAM for metadata.
If memory allocation fails, the tool reports an operational error explicitly.
It prints loading/comparison phases immediately and progress approximately
every five seconds during long reads. Timing checks allow 500 nanoseconds
of rounding; rates, flags and publication versions are compared as well.

`make test-repack` verifies boundaries and fixed-size output for both codecs.
`make test-many-channels` verifies 4,000 channels under a 32-file-descriptor
limit, different channel interleaving and genuine sample mismatch detection.

Buffering removes partial records at old input boundaries. FLAC at 512 bytes
can still be larger than STEIM2 depending on the waveform and segment boundaries;
this change does not guarantee a smaller archive.

## Format and limits

* Real miniSEED2 fixed header plus blockettes 1000/1001 and normal metadata.
* Experimental encoding **21**, with a compact frame-only payload profile.
* One mono FLAC frame per record; decoded samples are exact signed int32.
  Stored precision may be 8, 12, 16, 20, 24 or 32 bits. STREAMINFO is reconstructed
  in memory; MD5 is zero/unavailable. No stream metadata is stored.
* Zero padding and a 4-byte CRC32 IEEE trailer over the complete record,
  with checksum bytes treated as zero. Frame boundaries come from libFLAC.
* The decoder accepts one compact FLAC frame, zero padding and the record CRC.
* Fixed power-of-two records, 512 through 1,048,576 bytes.
* Integer sample rates from 1 through 655,350 Hz; fractional rates rejected.
* FLAC output is miniSEED2 only; miniSEED3 requests rejected.
* Headers may use any valid miniSEED2 16-bit data offset, provided the
  chosen fixed record has room for the FLAC frame and 4-byte trailer.
* No channel cross-prediction, automatic codec competition, new index or
  header compression. Choose STEIM2 or FLAC explicitly for each pack request.
* `ms_decode_data` (the standalone raw-payload decoder) does not decode this
  record-level profile. Use `msr3_unpack_data`, parse or file-reader APIs.

Encoding 21 is our experimental identifier, **not an FDSN assignment**.
Stock libmseed/ObsPy will not unpack these FLAC records. Readers must use
this fork or another implementation of the compact profile documented in FORMAT.md. Small FLAC records
can compress worse than STEIM2 due to repeated codec metadata.

## Validation

`test_hybrid.c` verifies exact int32 reconstruction, three component SIDs,
microsecond timestamps, 512/4096-byte records, 70,000-sample input buffers,
full-range int32 values in FLAC, CRC rejection, callback/stateful/trace-list
packing and ordinary file reading of mixed STEIM2/FLAC records. The
upstream libmseed suite is also run against the combined static library.
See [VALIDATION.md](VALIDATION.md) for the recorded results. Test source files below are reusable checks.

### Test source files and commands

Run commands from this top-level directory. First run `make -j4`; the Python
codec tests load `libmseed_flac_hybrid.so` and do not build it themselves.

| File | Purpose | Build/run command |
| --- | --- | --- |
| `test_hybrid.c` | Main normal-API integration test: callback, stateful and trace-list packing, mixed STEIM2/FLAC reading, exact int32 samples, timestamps, CRC rejection and supported limits. Generates `hybrid_test.mseed`. | `make test` |
| `test_repack.c` | Generates and verifies three interleaved channels, continuous streams and deliberate gap, overlap, rate, flag and timing-quality boundaries. The Makefile also checks FLAC/STEIM2 conversion, truncated input, output cleanup and overwrite refusal. | `make test-repack` (also part of `make test`) |
| `test_large_headers.c` | Checks 244-byte headers with three calibration blockettes and exact decoding; rejects invalid offsets and headers leaving insufficient payload space. Can generate input for the repack pipeline. | `make test` |
| `test_many_channels.c` | Generates original, reverse-interleaved and deliberately changed data for 4,000 channels. The Makefile checks exact comparison under a 32-file-descriptor limit and detection of the changed sample. | `make test-many-channels` (also part of `make test`) |
| `test_compact.py` | Rejects bad CRCs, padding, sample counts, rates and overlapping blockettes; checks null arguments and exact adaptive sample widths 8/12/16/20/24/32. Uses `hybrid_test.mseed` from the main C test. | `make test-codec` |
| `test_codec_io.py` | Exercises decoder reads, constant/random data, counts through 16,384, padding and truncation; checks whole-record IEEE CRC32 against Python's `zlib`. | `make test-codec` |
| `test_threaded.py` | Compares threads 1/2/4/8 for exact per-channel record bytes/order, values and metadata; checks boundaries, 4,000 channels, invalid thread options and cleanup after read/packing/write failures. | `make test-threaded` |
| `test_mutations.c` | Optional robustness harness: mutates 3,000 copies of a 512-byte record and recomputes its CRC, exercising structural decoding beyond the checksum gate. Not included in the default test targets. | Manual command below |

Recommended complete routine checks, after building:

```sh
make test-codec
make test-threaded
make upstream-tests
```

`make test-codec` builds the required shared library and runs `make test`;
it can also be invoked directly after `make clean`. Python checks require Python 3
(3.8 or later for `test_threaded.py`) and standard-library modules only;
no ObsPy, NumPy or pip installation is needed. Threaded checks use Linux/Unix
resource limits. Upstream regression sources and fixtures are in
`libmseed/test/`; `make upstream-tests` runs those against the hybrid library.

The optional bundled mutation harness can be built explicitly:

```sh
cc -Ilibmseed -Iflac-1.5.0/include test_mutations.c \
  libmseed_flac_hybrid.a -lm \
  -o test_mutations
./test_mutations testdata/flac21.mseed
```

The mutation harness reads the first 512-byte record from the supplied file.
Some mutations can remain valid; rejection of every mutation is not expected.
Memory-safety instrumentation requires a separate sanitizer build of the
harness and library. An ordinary run is a decoder robustness exercise, not
evidence of sanitizer coverage or exhaustive safety.

Test sources and `testdata/` should remain in a published source release.
They are not called by production applications. `test_hybrid.c` is currently
built by the default `make` target; the other test sources support the targets
listed above. Removing them can break builds or regression checks.
Generated executables, `hybrid_test.mseed`, `c_test512.mseed`,
`c_test4096.mseed` and routine `.log` files need not be distributed. Do not
remove the checked-in `testdata/flac21.mseed` fixture. See
[VALIDATION.md](VALIDATION.md) for coverage limits.

## Licenses and source

Upstream libmseed: Apache-2.0, commit
`d6e6ad306de5b7bdde072e5a5e3d58ddcc11abd8`, included under `libmseed`.
libFLAC library: BSD license (`COPYING.Xiph`); its bundled command-line
programs have their own GPL terms. FLAC source archive: version 1.5.0.
Adapter, tests and integration additions: GPL-2.0-or-later (`COPYING.GPL`).
The combined work is distributed under GPLv3, selecting the adapter's
existing later-version permission, while retaining upstream notices. See
LICENSE, LICENSE_NOTES.md, COPYING.GPL3 and THIRD_PARTY_NOTICES.md. This is an experimental fork, not an upstream release.

## Multithreaded conversion

```sh
./repack input.mseed output.mseed 21 512 --threads 4
./repack --version
```

The default is one worker; `--threads` accepts 1 through 64. Each channel has
at most one active packing job, preserving its sample and record order.
Workers pack separate channels; output writes are serialized. The queue is
bounded to twice the worker count. Parsing, reading and boundary detection
are serial. Single-channel or channel-grouped files and short segments may
see little speedup. Cross-channel output interleaving can vary.

## Diagnostics and comparison status

On failure, `repack` reports channel, UTC start time, rate, encoding, record
size, buffered samples and packing status, followed by a codec diagnostic.
It removes incomplete output and refuses to overwrite an existing file.
If a header leaves insufficient frame space, choose a larger record size.
Valid mapped blockettes are retained; a 244-byte header works in 512 bytes.

`compare_samples` exit codes:

- `0`: samples, channel timing, rates, flags and publication versions match.
- `1`: a mismatch was found, with channel/sample context.
- `2`: the comparison could not complete because of an input, read or RAM error.

## Compression implementation

The encoder selects 8/12/16/20/24/32-bit stored precision without changing
sample values. Nonconstant narrow blocks also try a 32-bit candidate; the
shorter frame is retained. Constant blocks skip the redundant wider trial.
The chosen frame is decoded and compared with the source before writing.

Channel lookup uses 8192 hash buckets. Whole-record IEEE CRC32 uses a small
constant lookup table. The decoder exposes a synthetic 42-byte STREAMINFO
prefix and the original frame through callbacks without copying the entire
padded payload. Bundled libFLAC is unmodified and uses public APIs only.
The exact layout and decoder rules are in [FORMAT.md](FORMAT.md).
