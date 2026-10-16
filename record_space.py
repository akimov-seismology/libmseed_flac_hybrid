#!/usr/bin/env python3
"""Measure compact FLAC21 record space, including headers, frame and padding.

Usage: python3 record_space.py output.mseed [--record-length 512]
Inspection requires the built libmseed_flac_hybrid.so beside this script.
"""
import argparse
import collections
import ctypes
from pathlib import Path
import json
import struct


def measure(path, record_length):
    groups = collections.defaultdict(lambda: collections.Counter())
    with open(path, 'rb') as source:
        while record := source.read(record_length):
            if len(record) != record_length:
                raise ValueError('Incomplete record')
            offset = struct.unpack_from('>H', record, 44)[0]
            blockette = struct.unpack_from('>H', record, 46)[0]
            seen = set()
            while blockette:
                if blockette in seen or blockette < 48 or blockette + 4 > offset:
                    raise ValueError('Invalid blockette chain')
                seen.add(blockette)
                kind, following = struct.unpack_from('>HH', record, blockette)
                if kind == 1000:
                    if blockette + 8 > offset or record[blockette+4] != 21:
                        raise ValueError('Expected compact FLAC21')
                    break
                blockette = following
            else:
                raise ValueError('Missing blockette 1000')
            trailer = 4
            library = ctypes.CDLL(str(Path(__file__).resolve().parent / 'libmseed_flac_hybrid.so'))
            count = struct.unpack_from('>H', record, 30)[0]
            factor, multiplier = struct.unpack_from('>hh', record, 32)
            if not factor or not multiplier:
                raise ValueError('Missing sample rate')
            rate = (factor if factor>0 else -1/factor) * (multiplier if multiplier>0 else -1/multiplier)
            if rate != int(rate):
                raise ValueError('Fractional sample rate')
            data = record[offset:-4]
            samples = (ctypes.c_int32 * count)()
            used = ctypes.c_int()
            if library.flac_decode_frame(ctypes.c_char_p(data),len(data),samples,count,int(rate),ctypes.byref(used)) != count:
                raise ValueError('Invalid compact frame')
            size = used.value
            if offset < 48 or size < 1 or offset + size > record_length - trailer:
                raise ValueError('Invalid FLAC payload bounds')
            padding = record_length - offset - size - trailer
            if any(record[offset + size:-trailer]):
                raise ValueError('Nonzero padding')
            channel = record[15:18].decode('ascii')
            for name in ('ALL', channel[:2]):
                stats = groups[name]
                stats.update(records=1, total_bytes=record_length,
                             header_bytes=offset, trailer_bytes=trailer,
                             flac_bytes=size, available_bytes=record_length-offset-trailer,
                             padding_bytes=padding,
                             samples=struct.unpack_from('>H', record, 30)[0])
    for stats in groups.values():
        stats['average_padding_bytes'] = stats['padding_bytes'] / stats['records']
        stats['payload_utilization_percent'] = 100 * stats['flac_bytes'] / stats['available_bytes']
    return dict(groups)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file')
    parser.add_argument('--record-length', type=int, default=512)
    args = parser.parse_args()
    if args.record_length < 512 or args.record_length & (args.record_length-1):
        parser.error('record length must be a power of two, at least 512')
    print(json.dumps(measure(args.file, args.record_length), indent=2))
