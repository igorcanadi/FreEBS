iometer: (g=0): rw=randrw, bs=512-64K/512-64K, ioengine=libaio, iodepth=64
2.0.8
Starting 1 process

iometer: (groupid=0, jobs=1): err= 0: pid=2619
  Description  : [Emulation of Intel IOmeter File Server Access Pattern]
  read : io=327521KB, bw=2273.7KB/s, iops=746 , runt=144052msec
    slat (usec): min=0 , max=12114 , avg=64.02, stdev=194.44
    clat (usec): min=636 , max=2125.2K, avg=67824.56, stdev=94271.52
     lat (usec): min=712 , max=2125.3K, avg=67896.48, stdev=94271.92
    clat percentiles (msec):
     |  1.00th=[   11],  5.00th=[   17], 10.00th=[   25], 20.00th=[   39],
     | 30.00th=[   48], 40.00th=[   57], 50.00th=[   60], 60.00th=[   63],
     | 70.00th=[   67], 80.00th=[   76], 90.00th=[  104], 95.00th=[  125],
     | 99.00th=[  253], 99.50th=[  359], 99.90th=[ 1598], 99.95th=[ 1631],
     | 99.99th=[ 2008]
    bw (KB/s)  : min=   67, max=16136, per=100.00%, avg=2397.77, stdev=1870.33
  write: io=82079KB, bw=583462 B/s, iops=185 , runt=144052msec
    slat (usec): min=0 , max=12527 , avg=82.12, stdev=262.69
    clat (msec): min=3 , max=2122 , avg=71.69, stdev=98.85
     lat (msec): min=4 , max=2122 , avg=71.79, stdev=98.85
    clat percentiles (msec):
     |  1.00th=[   11],  5.00th=[   18], 10.00th=[   27], 20.00th=[   40],
     | 30.00th=[   49], 40.00th=[   57], 50.00th=[   61], 60.00th=[   64],
     | 70.00th=[   68], 80.00th=[   81], 90.00th=[  111], 95.00th=[  133],
     | 99.00th=[  355], 99.50th=[  482], 99.90th=[ 1614], 99.95th=[ 1631],
     | 99.99th=[ 1991]
    bw (KB/s)  : min=    7, max= 4088, per=100.00%, avg=597.48, stdev=492.71
    lat (usec) : 750=0.01%, 1000=0.01%
    lat (msec) : 2=0.02%, 4=0.04%, 10=0.76%, 20=5.79%, 50=25.43%
    lat (msec) : 100=56.44%, 250=10.33%, 500=0.81%, 750=0.08%, 2000=0.27%
    lat (msec) : >=2000=0.02%
  cpu          : usr=0.11%, sys=7.88%, ctx=40929, majf=0, minf=24
  IO depths    : 1=0.1%, 2=0.1%, 4=0.1%, 8=0.1%, 16=0.1%, 32=0.1%, >=64=100.0%
     submit    : 0=0.0%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=0.0%
     complete  : 0=0.0%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.1%, >=64=0.0%
     issued    : total=r=107489/w=26720/d=0, short=r=0/w=0/d=0

Run status group 0 (all jobs):
   READ: io=327521KB, aggrb=2273KB/s, minb=2273KB/s, maxb=2273KB/s, mint=144052msec, maxt=144052msec
  WRITE: io=82079KB, aggrb=569KB/s, minb=569KB/s, maxb=569KB/s, mint=144052msec, maxt=144052msec

Disk stats (read/write):
  freebs: ios=107455/26711, merge=106/51, ticks=7087344/1865660, in_queue=8954816, util=100.00%
