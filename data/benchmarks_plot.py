import numpy as np
import matplotlib.pyplot as plt

lines = open('throughput.dat', 'r').read().split('\n')[:-1]
data = {}

for l in lines:
    a, b = l.split()
    data[tuple(a.split('.'))] = float(b)

relative_data = {}

for d in data:
    if d[1] == 'freebs':
        relative_data['.'.join(d)] = data[d] / data[(d[0], 'local', d[2])]

ind = np.arange(len(data)/2)
width = 0.4

xlabs = [("iometer.freebs.read", "IOmeter read"),
        ("iometer.freebs.write", "IOmeter write"),
        ("read.freebs.read", "Sequential reads"), 
        ("write.freebs.write", "Sequential writes"),
        ("randr.freebs.read", "Random reads"),
        ("randw.freebs.write", "Random writes")]

fig = plt.figure()
ax = fig.add_subplot(111)
plt.subplots_adjust(bottom=0.3)
rects = ax.bar(ind, map(lambda x: relative_data[x[0]], xlabs), width, color='b')

ax.set_xticks(ind + width/2)
ax.set_xticklabels(map(lambda x: x[1], xlabs), rotation=90)

#plt.show()
fig.savefig('../paper/figures/benchmarks.pdf', format='pdf')
