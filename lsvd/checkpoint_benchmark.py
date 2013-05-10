import subprocess
import signal
import time
import json

data = []

#for length in range(20, 220, 20):
length = 100
subprocess.call('rm /scratch/test_disk -rf', shell=True)
p = subprocess.Popen(['/u/c/a/canadi/CS838/FreEBS/lsvd/write'])
time.sleep(length)
p.send_signal(signal.SIGINT)
a = subprocess.check_output(['/u/c/a/canadi/CS838/FreEBS/lsvd/open'])
t = float(a.split('\n')[1]) / 2.4e9
data.append((length, t))
print 'second run: ', float(subprocess.check_output(['/u/c/a/canadi/CS838/FreEBS/lsvd/open']).split('\n')[1]) / 2.4e9

print json.dumps(data)


