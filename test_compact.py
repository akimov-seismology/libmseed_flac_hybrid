#!/usr/bin/env python3
"""Check compact frame boundaries and reject inconsistent metadata."""
import ctypes
from pathlib import Path
import struct
import zlib

root = Path(__file__).resolve().parent
lib = ctypes.CDLL(str(root / 'libmseed_flac_hybrid.so'))
lib.msflac_unpack_record.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p)]
lib.msr3_free.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
raw = (root / 'hybrid_test.mseed').read_bytes()
pos = 0
while pos < len(raw):
    blockette = struct.unpack_from('>H',raw,pos+46)[0]
    encoding, exponent = raw[pos+blockette+4], raw[pos+blockette+6]
    length = 1 << exponent
    if encoding == 21 and length == 512:
        record = bytearray(raw[pos:pos+length])
        break
    pos += length
else:
    raise AssertionError('No compact fixture; run make test first')


def valid(data):
    output = ctypes.c_void_p()
    result = lib.msflac_unpack_record(bytes(data),len(data),ctypes.byref(output))
    lib.msr3_free(ctypes.byref(output))
    return result == 0


def checksum(data):
    data[-4:] = bytes(4)
    data[-4:] = struct.pack('>I',zlib.crc32(data))
    return data


assert valid(record)
# The record adapter accepts only its own encoding identifier, even when the
# checksum is correct. Other libmseed codecs use their normal decoding paths.
blockette = struct.unpack_from('>H', record, 46)[0]
for code in range(256):
    if code != 21:
        bad = record.copy(); bad[blockette+4] = code
        assert not valid(checksum(bad))
bad = record.copy()
data_offset = struct.unpack_from('>H', record, 44)[0]
bad[data_offset:data_offset+4] = b'fLaC'
assert not valid(checksum(bad))
print('PASS compact record identifier and frame-only payload checks')
bad = record.copy(); bad[10] ^= 1
assert not valid(bad)
offset = struct.unpack_from('>H',record,44)[0]
count = struct.unpack_from('>H',record,30)[0]
used = ctypes.c_int(); samples = (ctypes.c_int32 * count)()
assert lib.flac_decode_frame(ctypes.c_char_p(bytes(record[offset:-4])),508-offset,
                            samples,count,40,ctypes.byref(used)) == count
# Recompute the record checksum to exercise decoder checks independently.
bad = record.copy(); bad[offset+used.value-1] ^= 1
assert not valid(checksum(bad))
if offset+used.value < 508:
    bad = record.copy(); bad[offset+used.value] = 1
    assert not valid(checksum(bad))
bad = record.copy(); struct.pack_into('>H',bad,30,count+1)
assert not valid(checksum(bad))
bad = record.copy(); struct.pack_into('>hh',bad,32,41,1)
assert not valid(checksum(bad))
# Changing the final timing blockette into a 60-byte calibration blockette
# makes its body overlap the frame, even though its chain header still fits.
pointer = struct.unpack_from('>H',record,46)[0]
while struct.unpack_from('>H',record,pointer+2)[0]:
    pointer = struct.unpack_from('>H',record,pointer+2)[0]
bad = record.copy(); struct.pack_into('>H',bad,pointer,300)
assert not valid(checksum(bad))
# Invalid calls must fail cleanly instead of dereferencing null pointers.
lib.flac_decode_frame.argtypes = [ctypes.c_char_p,ctypes.c_int,ctypes.POINTER(ctypes.c_int32),ctypes.c_int,ctypes.c_int,ctypes.POINTER(ctypes.c_int)]
assert lib.flac_decode_frame(None,1,samples,count,40,ctypes.byref(used)) < 0
assert lib.flac_decode_frame(b'\xff',1,None,count,40,ctypes.byref(used)) < 0
assert lib.flac_decode_frame(b'\xff',1,samples,count,40,None) < 0
print('PASS compact: CRCs, padding, sample count, rate, overlapping blockettes and null pointers')

# Every supported stored precision must reconstruct the original int32 values,
# including signed boundaries and tiny final blocks. No quantization is used.
lib.flac_encode_frame.argtypes=[ctypes.POINTER(ctypes.c_int32),ctypes.c_int,
    ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.POINTER(ctypes.c_ubyte),ctypes.c_int]
width_codes={0:32,1:8,2:12,4:16,5:20,6:24,7:32}
selected=set()
for bits in (8,12,16,20,24,32):
    bound=1 << (bits-1)
    for count in (1,2,31,32,257):
        values=[-bound if i%2==0 else bound-1 for i in range(count)]
        source=(ctypes.c_int32*count)(*values)
        packed=(ctypes.c_ubyte*100000)()
        length=lib.flac_encode_frame(source,count,max(count,32),min(count-1,32),40,packed,len(packed))
        assert length>0
        selected.add(width_codes[(packed[3]>>1)&7])
        restored=(ctypes.c_int32*count)();used=ctypes.c_int()
        assert lib.flac_decode_frame(bytes(packed[:length]),length,restored,count,40,ctypes.byref(used))==count
        assert used.value==length and list(restored)==values
assert selected=={8,12,16,20,24,32},selected
print('PASS adaptive widths 8/12/16/20/24/32: exact signed endpoints and counts 1/2/31/32/257')
