import numpy as np
import matplotlib.pyplot as plt

FONT_SIZE = 17          

def get_data(filename):
    lines = open(filename, 'r').read().split('\n')[:-1]
    data = {}

    for l in lines:
        a, b = l.split()
        data[tuple(a.split('.'))] = float(b)

    relative_data = {}

    for d in data:
        if d[1] == 'freebs':
            relative_data['.'.join(d)] = data[d] / data[(d[0], 'local', d[2])]

    xlabs = [("iometer.freebs.read", "IOmeter read"),
            ("iometer.freebs.write", "IOmeter write"),
            ("read.freebs.read", "Sequential reads"), 
            ("write.freebs.write", "Sequential writes"),
            ("randr.freebs.read", "Random reads"),
            ("randw.freebs.write", "Random writes"),]
            #("kernel.freebs.result", "Linux kernel build")]

    return relative_data, xlabs

relative_throughputs, tlabs = get_data('throughput.dat')
relative_latencies, llabs = get_data('latency.dat')
ind = np.arange(len(relative_throughputs))
width = 0.2
fig = plt.figure()
ax = fig.add_subplot(111)
ax.tick_params(axis='both', labelsize=FONT_SIZE)
plt.subplots_adjust(bottom=0.3)
rects = ax.bar(ind + 0.25, 
    map(lambda x: relative_throughputs[x[0]], tlabs), 
    width, 
    color='b', 
    label='throughput')
rects = ax.bar(ind + 0.45, 
    map(lambda x: relative_latencies[x[0]], llabs), 
    width, 
    color='r', 
    label='latency')

ax.set_ylabel('Relative speedup over local', fontsize=FONT_SIZE)
ax.set_xticks(ind + 0.45)
ax.set_xticklabels(map(lambda x: x[1], tlabs), rotation=90, fontsize=FONT_SIZE)
ax.legend(loc='upper left')

#plt.show()
fig.savefig('../paper/figures/throughput.pdf', format='pdf', bbox_inches='tight')
