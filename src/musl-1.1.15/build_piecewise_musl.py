#!/usr/bin/env python

import os
import subprocess

os.system('make -j')
ret = subprocess.check_output('objdump -h lib/libc.so | grep dep', shell=True)
info = filter(lambda x:len(x)==8, ret.strip('\n').split(' '))

os.system("sed -i -e 's/#define DEPOFF.*/#define DEPOFF 0x" + info[1] + "/' $PWHOME/musl-1.1.15/ldso/dynlink.c")
os.system("sed -i -e 's/#define DEPSZ.*/#define DEPSZ 0x" + info[0] + "/' $PWHOME/musl-1.1.15/ldso/dynlink.c")
os.system("sed -i 's/#define DEPOFF.*/#define DEPOFF 0x" + info[1] + "/' $PWHOME/musl-1.1.15/ldso/dlstart.c")
os.system("sed -i 's/#define DEPSZ.*/#define DEPSZ 0x" + info[0] + "/' $PWHOME/musl-1.1.15/ldso/dlstart.c")

os.system('make -j')
