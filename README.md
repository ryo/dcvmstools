# dcvmstools - Dreamcast Visual Memory Storage manipulation tools

## device file
On NetBSD/dreamcast, The [Visual Memory Unit (VMU)](https://en.wikipedia.org/wiki/VMU) can be treated as a /dev/mmem* device.
This unit has 128kbyte of storage space, but the NetBSD OS cannot mount it as a file system.
This tool can be used to handle each file in the visual memory storage.

## usage

### device (or image) file
The "-f" option can be used to specify any device file, such as "dcvmstools -f /dev/mmem0.0c" or "dcvmstools -f image".

### dcvmstools dir
It can display the list of files in the storage, consisting of 512 bytes per block, and user files can (normally) use up to 200 blocks (100kbyte).
The file name can be a maximum of 12 characters.
The only characters that can be used in the file name are period, underscore, numbers, and alphabet.
There are two types of files: GAME files and DATA files, with or without a copy-prohibit flag.

```
# dcvmstools -f /dev/mmem0.0c dir
2021-03-18 08:21:03 PROHIBIT DATA  44 blocks ASUKA____001
2020-03-04 19:59:21          DATA  38 blocks ASUKA____RNK
2019-03-16 04:22:09          DATA   9 blocks REZ_________
2019-03-16 04:32:16          DATA   4 blocks KAROUS___SYS
2019-03-16 18:03:41 PROHIBIT DATA   4 blocks VOORATAN.SYS
2019-03-16 18:28:33          DATA  18 blocks SONIC2___S01
2019-03-16 18:28:51          DATA  52 blocks SONIC2___ALF
2019-03-16 18:29:58          DATA  10 blocks SONICADV_SYS
1a20-03-03 01:39:57          DATA   2 blocks GDMENU.SYS
2020-11-29 00:03:08          DATA   7 blocks DEADORALIVE2
                        10 files, 188/200 user blocks used
                   12 user blocks +  41 system blocks free
```

### dcvmstools get
The specified file can be extracted from the storage.
The timestamp will also be copied.

### dcvmstools cat
It is similar to "GET", but outputs the contents of a file to the stdout.

### dcvmstools put
The specified file can be stored to the storage.
The timestamp will also be copied.

### dcvmstools del
Deletes the specified file in the storage.

### dcvmstools dump
Outputs information about the system area of the visual memory.

### dcvmstools fat
Outputs the FAT mapping information.

## license
dcvmstools is distributed under BSD license.

## reference
[NetBSD/dreamcast](http://wiki.netbsd.org/ports/dreamcast/)

[Dreamcast Programming](http://mc.pp.se/dc/)

