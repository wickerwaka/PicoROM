import csv
import matplotlib.pyplot as plt

header = None
prev = []
rise = {}
delays = []
with open('166Mhz.csv') as fp:
	reader = csv.reader(fp)
	for row in reader:
		if row[0].startswith(';'):
			continue
		if not header:
			header = [x.strip() for x in row]
			prev = [ 1 for _ in header[1:] ]
			rise = dict(((x, 0) for x in header[1:]))
			continue
		
		for idx in range(len(prev)):
			v = int(row[idx + 1])
			name = header[idx+1]
			if prev[idx] == 0 and v == 1:
				rise[name] = float(row[0])
				if name == 'D2':
					a_rise = rise['A2']
					delays.append((float(row[0]) - a_rise) * 1000000000)
			prev[idx] = v

print(max(delays))
plt.hist(delays, bins=100)
plt.show()