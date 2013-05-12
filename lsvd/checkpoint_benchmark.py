import subprocess
import sys
import signal
import time
import json

data = []

for length in range(10, 220, 10):
    print >> sys.stderr, 'running %d' % length
    subprocess.call('rm /scratch/test_disk -rf', shell=True)
    p = subprocess.Popen(['/u/c/a/canadi/CS838/FreEBS/lsvd/write'])
    time.sleep(length)
    p.send_signal(signal.SIGKILL)
    p.wait()
    a = subprocess.check_output(['/u/c/a/canadi/CS838/FreEBS/lsvd/open'])
    t = a.split('\n')[1]
    s = a.split('\n')[0]
    print >> sys.stderr, length, t, s
    data.append((length, t, s))
    # breathe
    time.sleep(10)

print json.dumps(data)


