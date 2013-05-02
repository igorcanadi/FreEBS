#!/usr/bin/env python

import socket
import struct
import mmap

size = 1024 * 1024 * 1024
sector_size = 512

host = ''
port = 9000
backlog = 5
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.bind((host, port))
s.listen(backlog)

with open('replica.dsk', 'r+b') as f:
    mm = mmap.mmap(f.fileno(), 0)
    while True:
        client, address = s.accept()
        while True:
            command = client.recv(18)
            c, l, o, sn, rn = struct.unpack('!HIIII', command)
            #print "SEQ %d" % sn
            if c == 1: # write
                #print "Writing %d bytes at %d offset" % (l, o)
                data = ''
                while len(data) < l:
                    data = data + client.recv(l - len(data))
                for i in range(0, l):
                    mm[o*sector_size + i] = data[i]
                mm.flush()
                client.send(struct.pack('!HI', 0, rn))
            else: # read
                #print "Reading %d bytes at %d offset" % (l, o)
                client.send(struct.pack('!HI', 0, rn))
                client.send(''.join(mm[o*sector_size : o*sector_size + l]))
        client.close()
