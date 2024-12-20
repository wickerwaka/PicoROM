import csv
from collections import namedtuple
from typing import List
import sys

import matplotlib.pyplot as plt

State = namedtuple('State', ['time', 'address', 'data'])

def read_csv(name) -> List[State]:
    result = []
    header = None
    addr_cols = []
    data_cols = []
    with open(name) as fp:
        reader = csv.reader(fp)
        for row in reader:
            if row[0].startswith(';'):
                continue
            if not header:
                header = [x.strip() for x in row]
                addr_cols = [0 for _ in header]
                data_cols = [0 for _ in header]
                for idx, name in enumerate(header[1:]):
                    if name[0] == 'D':
                        data_cols[idx] = 2 ** int(name[1:])
                    elif name[0] == 'A':
                        addr_cols[idx] = 2 ** int(name[1:])
                continue
            
            addr = 0
            data = 0
            t = float(row[0]) * 1000000000
            for idx, val in enumerate(row[1:]):
                i = int(val)
                addr += i * addr_cols[idx]
                data += i * data_cols[idx]

            result.append( State(t, addr, data) )

    return result        


states = read_csv(sys.argv[1])

wanted_addr = 0
wanted_data = 0
addr_time = 0
time_delta = []
for state in states:
    if state.address == wanted_addr:
        addr_time = state.time
        wanted_data = (( wanted_addr >> 8 ) ^ wanted_addr) & 0xff
        wanted_addr += 1
        wanted_data = wanted_data & 0x0f
        wanted_addr = wanted_addr & 0x0f
    elif state.data == wanted_data:
        time_delta.append(int(state.time - addr_time))
        wanted_data = -1

delta_min = min(time_delta)
delta_max = max(time_delta)
print(f"Min: {delta_min}, Max: {delta_max}")

bins = {}
for x in time_delta:
    c = bins.get(x, 0)
    bins[x] = c + 1

print(bins)

plt.hist(time_delta, bins=[x for x in range(30, 80, 2)], align="left")
plt.xlabel("Access Time (ns)")
plt.ylabel("Occurances")
plt.show()
