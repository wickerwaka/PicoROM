#! /usr/bin/env python3


data = bytearray([])
for x in range(4 * 64 * 1024):
    v = (( x >> 8 ) ^ x) & 0xff
    data.append(v)

with open("2mbit_xor_rom.bin", "wb") as fp:
    fp.write(data)

