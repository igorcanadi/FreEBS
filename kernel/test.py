#!/usr/bin/env python

"""
test.py

A test script for freebs.
"""

# To get sh on Igor's testbed, run:
# sudo apt-get install python-pip
# sudo pip install sh
import sh
import sys

if len(sys.argv) < 2:
    print "Usage: %s <name of block device>" % sys.argv[0]
    sys.exit(-1)

block_device = sys.argv[1]
sh.mkfs('-t', 'ext2', block_device)
sh.mount(block_device, '/mnt')
sh.echo('hi', _out='/mnt/bye')
