#!/usr/bin/env python

import os
import subprocess

os.system('make -j8')
ret = subprocess.check_output('objdump -h lib/libc.so | grep dep', shell=True)
info = filter(lambda x:len(x)==8, ret.strip('\n').split(' '))
print info
#info = map(lambda hex: int(hex, 16), info)

#print "sed -i 'Ns/#define DEPOFF.*/#define DEPOFF 0x" + info[1] + "/' /home/anhquach/research/bingseclab/PiecewiseCompile/musl-1.1.15/ldso/dynlink.c"

os.system("sed -i -e 's/#define DEPOFF.*/#define DEPOFF 0x" + info[1] + "/' /home/anh/work/PiecewiseCompile/musl-1.1.15/ldso/dynlink.c")
os.system("sed -i -e 's/#define DEPSZ.*/#define DEPSZ 0x" + info[0] + "/' /home/anh/work/PiecewiseCompile/musl-1.1.15/ldso/dynlink.c")
os.system("sed -i 's/#define DEPOFF.*/#define DEPOFF 0x" + info[1] + "/' /home/anh/work/PiecewiseCompile/musl-1.1.15/ldso/dlstart.c")
os.system("sed -i 's/#define DEPSZ.*/#define DEPSZ 0x" + info[0] + "/' /home/anh/work/PiecewiseCompile/musl-1.1.15/ldso/dlstart.c")

os.system('make -j8')
os.system('rm ~/opt/lib/libc.so')
os.system('make install')
