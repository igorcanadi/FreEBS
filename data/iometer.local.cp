iometer: (g=0): rw=randrw, bs=512-64K/512-64K/512-64K, ioengine=libaio, iodepth=64
fio-2.0.15
Starting 1 process
iometer: Laying out IO file(s) (1 file(s) / 400MB)
Jobs: 1 (f=1): [m] [99.4% done] [1796K/520K/0K /s] [419 /109 /0  iops] [eta 00m:02s]
iometer: (groupid=0, jobs=1): err= 0: pid=21423: Thu May  9 16:33:14 2013
  Description  : [Emulation of Intel IOmeter File Server Access Pattern]
  read : io=326081KB, bw=1023.3KB/s, iops=170 , runt=318686msec
    slat (usec): min=3 , max=249858 , avg=53.47, stdev=1982.29
    clat (usec): min=22 , max=1805.8K, avg=300904.66, stdev=281965.67
     lat (usec): min=293 , max=1805.8K, avg=300958.41, stdev=282008.08
    clat percentiles (msec):
     |  1.00th=[    8],  5.00th=[   21], 10.00th=[   38], 20.00th=[   73],
     | 30.00th=[  111], 40.00th=[  159], 50.00th=[  219], 60.00th=[  289],
     | 70.00th=[  375], 80.00th=[  490], 90.00th=[  652], 95.00th=[  881],
     | 99.00th=[ 1319], 99.50th=[ 1434], 99.90th=[ 1614], 99.95th=[ 1696],
     | 99.99th=[ 1762]
    bw (KB/s)  : min=    9, max= 3016, per=100.00%, avg=1073.19, stdev=421.76
  write: io=83749KB, bw=269100 B/s, iops=43 , runt=318686msec
    slat (usec): min=6 , max=104934 , avg=109.46, stdev=2621.41
    clat (usec): min=464 , max=1653.3K, avg=296085.13, stdev=281466.03
     lat (usec): min=478 , max=1653.3K, avg=296194.91, stdev=281660.61
    clat percentiles (msec):
     |  1.00th=[    5],  5.00th=[   18], 10.00th=[   34], 20.00th=[   68],
     | 30.00th=[  106], 40.00th=[  157], 50.00th=[  215], 60.00th=[  285],
     | 70.00th=[  371], 80.00th=[  482], 90.00th=[  652], 95.00th=[  873],
     | 99.00th=[ 1319], 99.50th=[ 1418], 99.90th=[ 1549], 99.95th=[ 1598],
     | 99.99th=[ 1647]
    bw (KB/s)  : min=    5, max=  890, per=100.00%, avg=277.22, stdev=141.93
    lat (usec) : 50=0.01%, 100=0.01%, 250=0.01%, 500=0.04%, 750=0.13%
    lat (usec) : 1000=0.06%
    lat (msec) : 2=0.14%, 4=0.23%, 10=1.12%, 20=3.27%, 50=9.14%
    lat (msec) : 100=13.59%, 250=27.13%, 500=26.01%, 750=12.14%, 1000=3.39%
    lat (msec) : 2000=3.61%
  cpu          : usr=0.32%, sys=1.04%, ctx=64119, majf=0, minf=29
  IO depths    : 1=0.1%, 2=0.1%, 4=0.1%, 8=0.1%, 16=0.1%, 32=0.1%, >=64=99.9%
     submit    : 0=0.0%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=0.0%
     complete  : 0=0.0%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.1%, >=64=0.0%
     issued    : total=r=54217/w=13770/d=0, short=r=0/w=0/d=0

Run status group 0 (all jobs):
   READ: io=326080KB, aggrb=1023KB/s, minb=1023KB/s, maxb=1023KB/s, mint=318686msec, maxt=318686msec
  WRITE: io=83748KB, aggrb=262KB/s, minb=262KB/s, maxb=262KB/s, mint=318686msec, maxt=318686msec

Disk stats (read/write):
  sda: ios=51222/14005, merge=2989/1003, ticks=15723516/4140971, in_queue=19864810, util=100.00%

