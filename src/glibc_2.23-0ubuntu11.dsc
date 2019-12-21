-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA512

Format: 3.0 (quilt)
Source: glibc
Binary: libc-bin, libc-dev-bin, libc-l10n, glibc-doc, glibc-source, locales, locales-all, nscd, multiarch-support, libc6, libc6-dev, libc6-dbg, libc6-pic, libc6-udeb, libc6.1, libc6.1-dev, libc6.1-dbg, libc6.1-pic, libc6.1-udeb, libc0.3, libc0.3-dev, libc0.3-dbg, libc0.3-pic, libc0.3-udeb, libc0.1, libc0.1-dev, libc0.1-dbg, libc0.1-pic, libc0.1-udeb, libc6-i386, libc6-dev-i386, libc6-sparc, libc6-dev-sparc, libc6-sparc64, libc6-dev-sparc64, libc6-s390, libc6-dev-s390, libc6-amd64, libc6-dev-amd64, libc6-powerpc, libc6-dev-powerpc, libc6-ppc64, libc6-dev-ppc64, libc6-mips32, libc6-dev-mips32, libc6-mipsn32, libc6-dev-mipsn32, libc6-mips64, libc6-dev-mips64, libc6-armhf, libc6-dev-armhf, libc6-armel, libc6-dev-armel, libc0.1-i386, libc0.1-dev-i386, libc6-x32, libc6-dev-x32, libc6-i686, libc6-xen, libc0.1-i686, libc6.1-alphaev67
Architecture: any all
Version: 2.23-0ubuntu11
Maintainer: Ubuntu Developers <ubuntu-devel-discuss@lists.ubuntu.com>
Uploaders: Clint Adams <clint@debian.org>, Aurelien Jarno <aurel32@debian.org>, Adam Conrad <adconrad@0c3.net>
Homepage: http://www.gnu.org/software/libc/libc.html
Standards-Version: 3.9.7
Vcs-Browser: https://anonscm.debian.org/cgit/pkg-glibc/glibc.git
Vcs-Git: https://anonscm.debian.org/git/pkg-glibc/glibc.git
Testsuite: autopkgtest
Testsuite-Triggers: fakeroot
Build-Depends: gettext, dpkg-dev (>= 1.17.14), xz-utils, file, quilt, autoconf, gawk, debhelper (>= 9.20141010), rdfind, symlinks, netbase, linux-libc-dev (>= 3.9) [linux-any], systemtap-sdt-dev [linux-any], libaudit-dev [linux-any], libcap-dev [linux-any], libselinux-dev [linux-any], mig (>= 1.5-3) [hurd-i386], gnumach-dev (>= 2:1.4+git20141109~) [hurd-i386], hurd-dev (>= 1:0.5.git20140320~) [hurd-i386] | hurd-headers-dev [hurd-i386], hurd-dev (>= 1:0.5.git20140320~) [hurd-i386] <!stage1> | libihash-dev [hurd-i386] <!stage1>, kfreebsd-kernel-headers [kfreebsd-any], binutils (>= 2.21), g++-5, g++-5-multilib [amd64 armhf armel i386 kfreebsd-amd64 mips mipsel mipsn32 mipsn32el mips64 mips64el powerpc ppc64 s390x sparc sparc64 x32], libc-bin (>= 2.23) <cross>
Build-Depends-Indep: perl, po-debconf (>= 1.0)
Package-List:
 glibc-doc deb doc optional arch=all profile=!stage1
 glibc-source deb devel optional arch=all profile=!stage1
 libc-bin deb libs required arch=any profile=!stage1 essential=yes
 libc-dev-bin deb libdevel optional arch=any profile=!stage1
 libc-l10n deb localization standard arch=all profile=!stage1
 libc0.1 deb libs required arch=kfreebsd-amd64,kfreebsd-i386 profile=!stage1
 libc0.1-dbg deb debug extra arch=kfreebsd-amd64,kfreebsd-i386 profile=!stage1
 libc0.1-dev deb libdevel optional arch=kfreebsd-amd64,kfreebsd-i386
 libc0.1-dev-i386 deb libdevel optional arch=kfreebsd-amd64 profile=!nobiarch
 libc0.1-i386 deb libs optional arch=kfreebsd-amd64 profile=!stage1,!nobiarch
 libc0.1-i686 deb libs extra arch=kfreebsd-i386 profile=!stage1
 libc0.1-pic deb libdevel optional arch=kfreebsd-amd64,kfreebsd-i386 profile=!stage1
 libc0.1-udeb udeb debian-installer extra arch=kfreebsd-amd64,kfreebsd-i386 profile=!stage1
 libc0.3 deb libs required arch=hurd-i386 profile=!stage1
 libc0.3-dbg deb debug extra arch=hurd-i386 profile=!stage1
 libc0.3-dev deb libdevel optional arch=hurd-i386
 libc0.3-pic deb libdevel optional arch=hurd-i386 profile=!stage1
 libc0.3-udeb udeb debian-installer extra arch=hurd-i386 profile=!stage1
 libc6 deb libs required arch=amd64,arm64,armel,armhf,hppa,i386,m68k,mips,mipsel,mipsn32,mipsn32el,mips64,mips64el,nios2,powerpc,powerpcspe,ppc64,ppc64el,sparc,sparc64,s390x,sh4,x32 profile=!stage1
 libc6-amd64 deb libs optional arch=i386,x32 profile=!stage1,!nobiarch
 libc6-armel deb libs optional arch=armhf profile=!stage1,!nobiarch
 libc6-armhf deb libs optional arch=armel profile=!stage1,!nobiarch
 libc6-dbg deb debug extra arch=amd64,arm64,armel,armhf,hppa,i386,m68k,mips,mipsel,mipsn32,mipsn32el,mips64,mips64el,nios2,powerpc,powerpcspe,ppc64,ppc64el,sparc,sparc64,s390x,sh4,x32 profile=!stage1
 libc6-dev deb libdevel optional arch=amd64,arm64,armel,armhf,hppa,i386,m68k,mips,mipsel,mipsn32,mipsn32el,mips64,mips64el,nios2,powerpc,powerpcspe,ppc64,ppc64el,sparc,sparc64,s390x,sh4,x32
 libc6-dev-amd64 deb libdevel optional arch=i386,x32 profile=!nobiarch
 libc6-dev-armel deb libdevel optional arch=armhf profile=!nobiarch
 libc6-dev-armhf deb libdevel optional arch=armel profile=!nobiarch
 libc6-dev-i386 deb libdevel optional arch=amd64,x32 profile=!nobiarch
 libc6-dev-mips32 deb libdevel optional arch=mipsn32,mipsn32el,mips64,mips64el profile=!nobiarch
 libc6-dev-mips64 deb libdevel optional arch=mips,mipsel,mipsn32,mipsn32el profile=!nobiarch
 libc6-dev-mipsn32 deb libdevel optional arch=mips,mipsel,mips64,mips64el profile=!nobiarch
 libc6-dev-powerpc deb libdevel optional arch=ppc64 profile=!nobiarch
 libc6-dev-ppc64 deb libdevel optional arch=powerpc profile=!nobiarch
 libc6-dev-s390 deb libdevel optional arch=s390x profile=!nobiarch
 libc6-dev-sparc deb libdevel optional arch=sparc64 profile=!nobiarch
 libc6-dev-sparc64 deb libdevel optional arch=sparc profile=!nobiarch
 libc6-dev-x32 deb libdevel optional arch=amd64,i386 profile=!nobiarch
 libc6-i386 deb libs optional arch=amd64,x32 profile=!stage1,!nobiarch
 libc6-i686 deb libs extra arch=i386 profile=!stage1
 libc6-mips32 deb libs optional arch=mipsn32,mipsn32el,mips64,mips64el profile=!stage1,!nobiarch
 libc6-mips64 deb libs optional arch=mips,mipsel,mipsn32,mipsn32el profile=!stage1,!nobiarch
 libc6-mipsn32 deb libs optional arch=mips,mipsel,mips64,mips64el profile=!stage1,!nobiarch
 libc6-pic deb libdevel optional arch=amd64,arm64,armel,armhf,hppa,i386,m68k,mips,mipsel,mipsn32,mipsn32el,mips64,mips64el,nios2,powerpc,powerpcspe,ppc64,ppc64el,sparc,sparc64,s390x,sh4,x32 profile=!stage1
 libc6-powerpc deb libs optional arch=ppc64 profile=!stage1,!nobiarch
 libc6-ppc64 deb libs optional arch=powerpc profile=!stage1,!nobiarch
 libc6-s390 deb libs optional arch=s390x profile=!stage1,!nobiarch
 libc6-sparc deb libs optional arch=sparc64 profile=!stage1,!nobiarch
 libc6-sparc64 deb libs optional arch=sparc profile=!stage1,!nobiarch
 libc6-udeb udeb debian-installer extra arch=amd64,arm64,armel,armhf,hppa,i386,m68k,mips,mipsel,mipsn32,mipsn32el,mips64,mips64el,nios2,powerpc,powerpcspe,ppc64,ppc64el,sparc,sparc64,s390x,sh4,x32 profile=!stage1
 libc6-x32 deb libs optional arch=amd64,i386 profile=!stage1,!nobiarch
 libc6-xen deb libs extra arch=i386 profile=!stage1
 libc6.1 deb libs required arch=alpha profile=!stage1
 libc6.1-alphaev67 deb libs extra arch=alpha profile=!stage1
 libc6.1-dbg deb debug extra arch=alpha profile=!stage1
 libc6.1-dev deb libdevel optional arch=alpha
 libc6.1-pic deb libdevel optional arch=alpha profile=!stage1
 libc6.1-udeb udeb debian-installer extra arch=alpha profile=!stage1
 locales deb localization standard arch=all profile=!stage1
 locales-all deb localization extra arch=any profile=!stage1
 multiarch-support deb libs required arch=any profile=!stage1
 nscd deb admin optional arch=any profile=!stage1
Checksums-Sha1:
 fcd58cc91038a54faa5dac68ea000c6dd92184ae 13849968 glibc_2.23.orig.tar.xz
 5626b0dbaac62b5ef4309cc11e9fd6b9f6ece398 1246096 glibc_2.23-0ubuntu11.debian.tar.xz
Checksums-Sha256:
 bf6c528eeebefcacc295270068b79330c1fb2b22458ff66285b4175d23442c96 13849968 glibc_2.23.orig.tar.xz
 fcaea11240f3eb24c28420f0bb7a48409da99939be6e1a5ed1479ed4ad1273db 1246096 glibc_2.23-0ubuntu11.debian.tar.xz
Files:
 7945832c8f9ddfec9841582ad9109f36 13849968 glibc_2.23.orig.tar.xz
 07c4de0e26d450939b6044d6f6d77a6c 1246096 glibc_2.23-0ubuntu11.debian.tar.xz
Original-Maintainer: GNU Libc Maintainers <debian-glibc@lists.debian.org>

-----BEGIN PGP SIGNATURE-----

iQIzBAEBCgAdFiEE+9+R2RGx//KpzAJ3/obg6qOzlBUFAlxPa3sACgkQ/obg6qOz
lBUqVg//ao07dcHwkr5/e/3tHQ6D8DpQKVzWEysK4lk58gKclCST/uFFg4u5ql+A
IAA6hC5Mc7aZgU+X9/0JdARU/tIHSD/H28UEMhOmbn2SceBVLY34abxO+iAbigoc
WvFEHU1etrQ5wixC0BgbYhAmXooZT6ZaI3jxt3+Mto+l9Q1t6g9/YgGAWqjUs+0w
G9LvlIVdI3utZjQztTJKtGL9RWzRJT7/3G8e18yYrVnJ3KQF9fELY+u02/hdJ2in
b0EpFdb8UFCI38XF4ohJbRF6nzUsDt+4SZvZQ96Y1O2cvJTDKIi+wSqEPVK1Fafs
q2qJ9KykBqy07K0gd7WF9Cx83hIiDc6H2AcvCli5ZAleUl8Y1r4McZFspZ6eF3qy
oSmangB1oFwuqsuWW2J0et7VcerHNYtDAUo/F7RULq/1N4LwRgEVynUnzwllipok
BDmLhlG0RiDxAVOawqJsKEvETynsHRKqeGLMNTgElG3ma5k+R62RU9H41l6ObJG1
zHoQfFfzwjf1HMjYKXxR66YDTvRqkbDCHObBFEf7QeVU4bSY30B/Hu7iZfzbv2As
LL0RmEg8l9OoR8CGPQkJk8kzLsYb4Ok0Orbi0BH6umwO/T6/3BvITzHbXNE2BjAa
buztD1npMXHkCazsm+eVOor9iyhY8p3C+lF8dKRqQwmCi+lz8F4=
=fGpj
-----END PGP SIGNATURE-----
