# Validation and testing

The library is built on Linux with GCC, libmseed 3.5.4 and unmodified libFLAC
1.5.0. [README.md](README.md#test-source-files-and-commands) explains the test
source files and individual commands. [FORMAT.md](FORMAT.md) defines the
record profile; [COMPATIBILITY.md](COMPATIBILITY.md) defines reader requirements.

## Reproduce the checks

From the project directory:

```sh
make clean
make -j4
make test-codec
make test-threaded
make upstream-tests
```

`make test-codec` includes the C integration suite (`make test`). Python tests
need Python 3 and its standard library only. Thread/resource-limit tests use
Linux/Unix facilities. The default C build does not need Python or downloads.

## Test coverage

| Source | Checks |
| --- | --- |
| `test_hybrid.c` | Exact int32 values, three components, timestamps, 512/4096-byte records, large input arrays, normal callback/stateful/trace-list APIs, mixed-codec file reading, CRC and unsupported-input rejection. |
| `test_repack.c` | Three interleaved channels, 121,200 samples, continuous packing and gap/overlap/rate/flag/timing-quality boundaries. Makefile pipelines compare FLAC and STEIM2 outputs, validate fixed sizes and test truncation, cleanup and overwrite refusal. |
| `test_large_headers.c` | Exact samples with a 244-byte header and calibration blockettes; invalid offsets and insufficient payload space rejected. |
| `test_many_channels.c` | 4,000-channel fixtures, reversed interleaving, sample mismatch detection and a 32-file-descriptor limit. |
| `test_compact.py` | Compact encoding identifier, frame-only payload, CRCs, padding, count/rate/precision, overlapping blockettes, null arguments and exact adaptive-width boundary values. |
| `test_codec_io.py` | Decoder read boundaries, constant/random data, counts through 16,384, truncation, padding through 1 MiB and IEEE record CRC agreement with Python zlib. |
| `test_threaded.py` | Threads 1/2/4/8, exact per-channel record bytes/order, sample/metadata comparisons, 4,000 channels, invalid options and failure cleanup. |
| `test_mutations.c` | Optional deterministic 3,000-record mutation harness with CRC recomputation. Build/run instructions are in README.md. It is not part of default test targets. |
| `libmseed/test/` | Upstream regression suites run against the combined static library. |

`testdata/flac21.mseed` is one 512-byte compact FLAC21 record, for standalone
robustness checks. Keep it and test sources in the source distribution.
Generated logs and executables are not required release inputs.

An ordinary mutation-harness run does not enable ASan/UBSan. Instrumented
coverage requires a sanitizer build of the harness and library; its printed
PASS wording alone does not establish that instrumentation was enabled.
Valid mutations may be accepted. These tests do not prove absence of bugs.

## Scope and limits

`compare_samples` checks integer samples, channel identity, timing (allowing
500 ns rounding), rates, flags and publication versions. It does not compare
arbitrary extra headers. Repacking does not preserve original record bytes,
record boundaries or cross-channel interleaving. Specific fixture tests check
mapped calibration and timing-quality metadata independently.

Compression measurements must use complete files, matching output record
sizes and comparable segment/metadata handling. Short segments may leave
padding. Fixed-size FLAC records need not be smaller than STEIM2 for every
input. The library does not automatically choose the smaller codec.

## Current source verification

Checked on 2026-10-16: clean build, `make test-codec`, `make test-threaded`
and `make upstream-tests` passed. The upstream runner reported 89/89 suites.
The codec tests reject every non-profile encoding identifier even with a
recomputed CRC and reject a full-stream payload at the data offset.

A 121,200-sample, three-channel repack produced 42 fixed 512-byte records
(21,504 bytes); all samples, timing, rates and flags passed comparison.
Per-channel compact record bytes were checked for consistency; multithreaded
cross-channel interleaving may differ. The shared-library symbol table was
checked. The integration
patch was reconstructed from the exact upstream base and reapplied successfully.

A full build emits three existing upstream signedness warnings; the checks
above completed successfully.

Direct `make -j4 test-codec` with no prebuilt project outputs also passed;
the target builds the shared library required by the Python tests.
AddressSanitizer and UndefinedBehaviorSanitizer checks passed for `make test`,
`make test-threaded`, and the 3,000-record mutation harness. Instrumentation
covered the adapter, libmseed and C test/application code; the linked libFLAC
build was not instrumented. Leak detection was disabled because the execution
environment prevents LeakSanitizer's process inspection. No leak-check result
is claimed.

Both hybrid library outputs embed the position-independent static libFLAC
objects. The codec, threading and 89 upstream suites pass with applications
linked only to the combined hybrid archive and standard system libraries.
Standalone copies of the static and shared hybrid libraries were separately
linked to the C integration test and passed its round-trip checks. ELF
dependency inspection confirms that the hybrid shared library, `repack`, and
`compare_samples` have no dynamic libFLAC dependency. The combined static
archive contains 48 uniquely named object members.
