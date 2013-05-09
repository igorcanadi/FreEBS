iometer: (g=0): rw=randrw, bs=512-64K/512-64K, ioengine=libaio, iodepth=64
2.0.8
Starting 1 process
iometer: Laying out IO file(s) (1 file(s) / 400MB)

iometer: (groupid=0, jobs=1): err= 0: pid=3167
  Description  : [Emulation of Intel IOmeter File Server Access Pattern]
  read : io=328445KB, bw=2423.6KB/s, iops=783 , runt=135550msec
    slat (usec): min=0 , max=64636 , avg=74.90, stdev=616.48
    clat (usec): min=598 , max=2862.7K, avg=64572.44, stdev=79343.14
     lat (usec): min=730 , max=2862.8K, avg=64655.72, stdev=79338.15
    clat percentiles (msec):
     |  1.00th=[   10],  5.00th=[   21], 10.00th=[   35], 20.00th=[   43],
     | 30.00th=[   52], 40.00th=[   57], 50.00th=[   60], 60.00th=[   62],
     | 70.00th=[   65], 80.00th=[   71], 90.00th=[   95], 95.00th=[  116],
     | 99.00th=[  169], 99.50th=[  241], 99.90th=[ 1680], 99.95th=[ 1893],
     | 99.99th=[ 2114]
    bw (KB/s)  : min=   32, max=16549, per=100.00%, avg=2537.22, stdev=1982.17
  write: io=81156KB, bw=613081 B/s, iops=196 , runt=135550msec
    slat (usec): min=0 , max=66910 , avg=112.03, stdev=1076.94
    clat (msec): min=3 , max=2845 , avg=67.79, stdev=88.61
     lat (msec): min=4 , max=2845 , avg=67.91, stdev=88.60
    clat percentiles (msec):
     |  1.00th=[   12],  5.00th=[   23], 10.00th=[   36], 20.00th=[   43],
     | 30.00th=[   52], 40.00th=[   58], 50.00th=[   60], 60.00th=[   63],
     | 70.00th=[   67], 80.00th=[   73], 90.00th=[  101], 95.00th=[  119],
     | 99.00th=[  239], 99.50th=[  383], 99.90th=[ 1680], 99.95th=[ 2311],
     | 99.99th=[ 2343]
    bw (KB/s)  : min=    5, max= 3192, per=100.00%, avg=625.49, stdev=480.06
    lat (usec) : 750=0.01%, 1000=0.01%
    lat (msec) : 2=0.02%, 4=0.06%, 10=0.88%, 20=3.74%, 50=23.94%
    lat (msec) : 100=62.33%, 250=8.45%, 500=0.36%, 750=0.02%, 1000=0.05%
    lat (msec) : 2000=0.09%, >=2000=0.05%
  cpu          : usr=0.14%, sys=8.27%, ctx=38839, majf=0, minf=22
  IO depths    : 1=0.1%, 2=0.1%, 4=0.1%, 8=0.1%, 16=0.1%, 32=0.1%, >=64=100.0%
     submit    : 0=0.0%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=0.0%
     complete  : 0=0.0%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.1%, >=64=0.0%
     issued    : total=r=106136/w=26650/d=0, short=r=0/w=0/d=0

Run status group 0 (all jobs):
   READ: io=328444KB, aggrb=2423KB/s, minb=2423KB/s, maxb=2423KB/s, mint=135550msec, maxt=135550msec
  WRITE: io=81155KB, aggrb=598KB/s, minb=598KB/s, maxb=598KB/s, mint=135550msec, maxt=135550msec

Disk stats (read/write):
  freebs: ios=106067/26696, merge=0/31, ticks=6602032/1748400, in_queue=8351844, util=100.00%
