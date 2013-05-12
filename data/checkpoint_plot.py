import numpy as np
import matplotlib.pyplot as plt
import json

data = json.loads(open('checkpoint_benchmark_2MB.dat').read())

FONT_SIZE = 20

fig = plt.figure()
ax = fig.add_subplot(111)
ax.tick_params(axis='both', labelsize=FONT_SIZE)
ln1, = ax.plot(map(lambda x: x[0], data), map(lambda x: x[1], data), 'o-', color='blue')
ax.set_ylabel('Time to recover (s)', fontsize=FONT_SIZE)
data_written = np.array(map(lambda x: x[2], data)) * ((40.0*1024) / (1024*1024*1024))
ax.set_xlabel('Runtime before SIGKILL (s)', fontsize=FONT_SIZE)

ax2 = ax.twinx()
ax2.tick_params(axis='both', labelsize=FONT_SIZE)
ax2.set_ylabel('Total data written (GB)', fontsize=FONT_SIZE)
ln2, = ax2.plot(map(lambda x: x[0], data), data_written, 'o-', color='red')
ax2.set_ylim(0, 40)
ax.set_xlim(0, 220)

ax.legend((ln1, ln2), ('Time to recover', 'Total data written'), loc = 'upper left')

#plt.show()
fig.savefig('../paper/figures/checkpointing.pdf', format='pdf')

