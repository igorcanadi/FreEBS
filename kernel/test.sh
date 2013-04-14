#!/usr/bin/env bash

rmmod freebs
insmod ./freebs.ko
mkfs.ext2 /dev/freebs
mount /dev/freebs /mnt
ls /mnt
echo hi > /mnt/bye
cat /mnt/bye
umount /mnt
mount /dev/freebs /mnt
cat /mnt/bye
umount /mnt
