#!/usr/bin/env python

import socket
import struct
import mmap

size = 1024 * 1024

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
            command = client.recv(14)
            c, l, o, sn = struct.unpack('!HIII', command)
            #print "SEQ %d" % sn
            if c == 1: # write
                #print "Writing %d bytes at %d offset" % (l, o)
                data = ''
                while len(data) < l:
                    data = data + client.recv(l - len(data))
                for i in range(0, l):
                    mm[o*512 + i] = data[i]
                client.send(struct.pack('!HI', 0, sn))
            else: # read
                #print "Reading %d bytes at %d offset" % (l, o)
                client.send(struct.pack('!HI', 0, sn))
                client.send(''.join(mm[o*512 : o*512 + l]))
        client.close()
