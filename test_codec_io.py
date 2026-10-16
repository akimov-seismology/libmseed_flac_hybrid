#!/usr/bin/env python3
"""Exercise compact decoder reads, flat blocks and CRC.

Run after `make test`; uses Python standard library only.
"""
import ctypes as C
from pathlib import Path
import random
import struct
import zlib

root = Path(__file__).resolve().parent
lib = C.CDLL(str(root / 'libmseed_flac_hybrid.so'))
sample_pointer = C.POINTER(C.c_int32)
encode_args = [sample_pointer, C.c_int, C.c_int, C.c_int, C.c_int,
               C.c_char_p, C.c_int]
lib.flac_encode_frame.argtypes = encode_args
lib.flac_decode_frame.argtypes = [C.c_char_p, C.c_int, sample_pointer,
                                  C.c_int, C.c_int, C.POINTER(C.c_int)]
lib.msr3_parse.argtypes = [C.c_char_p, C.c_uint64, C.POINTER(C.c_void_p),
                          C.c_uint32, C.c_int8]
lib.msr3_free.argtypes = [C.POINTER(C.c_void_p)]
lib.msflac_pack_record.argtypes = [C.c_void_p, sample_pointer, C.c_int,
                                  C.c_char_p, C.c_uint32]
lib.msflac_unpack_record.argtypes = [C.c_char_p, C.c_uint32,
                                    C.POINTER(C.c_void_p)]

random_source = random.Random(417)
packed = C.create_string_buffer(200000)
for bits in (8, 12, 16, 20, 24, 32):
    bound = 1 << (bits - 1)
    for count in (1, 2, 31, 32, 257, 16384):
        for flat in (True, False):
            values = ([bound - 1] * count if flat else
                      [random_source.randrange(-bound, bound) for _ in range(count)])
            source = (C.c_int32 * count)(*values)
            length = lib.flac_encode_frame(source, count, max(count, 32),
                                          min(count - 1, 32), 100, packed, len(packed))
            assert length > 0, (bits, count, flat)
            frame = packed.raw[:length]
            for padding in (0, 7, 8192, 1048576):
                stream = frame + bytes(padding)
                output = (C.c_int32 * count)()
                consumed = C.c_int()
                assert lib.flac_decode_frame(stream, len(stream), output, count,
                                             100, C.byref(consumed)) == count
                assert list(output) == values and consumed.value == length
            assert lib.flac_decode_frame(frame[:-1], length - 1, output, count,
                                         100, C.byref(consumed)) < 0
print('PASS decoder reads: all widths, flat/random blocks, counts 1..16384, padding up to 1 MiB, truncation')

values = [-2147483648, 2147483647, 0, -1, 1] * 101
source = (C.c_int32 * len(values))(*values)

# Independent zlib comparison at every supported record length. The final
# four stored CRC bytes are logically zero during whole-record calculation.
metadata = C.c_void_p()
fixture = (root / 'hybrid_test.mseed').read_bytes()[:512]
assert lib.msr3_parse(fixture, 512, C.byref(metadata), 0, 0) == 0
try:
    for exponent in range(9, 21):
        record_length = 1 << exponent
        record = C.create_string_buffer(record_length)
        count = lib.msflac_pack_record(metadata, source, len(values), record, record_length)
        assert 0 < count <= len(values)
        data = record.raw
        expected = zlib.crc32(data[:-4] + bytes(4))
        assert struct.unpack('>I', data[-4:])[0] == expected
        decoded = C.c_void_p()
        try:
            assert lib.msflac_unpack_record(data, record_length, C.byref(decoded)) == 0
        finally:
            lib.msr3_free(C.byref(decoded))
finally:
    lib.msr3_free(C.byref(metadata))
print('PASS whole-record IEEE CRC32 against zlib: all power-of-two lengths 512..1048576')
