#
# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, /dev/xxxx or empty, in which case the
# default of FLOPPY is used by 'build'.
#

ROOT_DEV = /dev/hdb1

#
# uncomment the correct keyboard:
#
# The value of KBDFLAGS should be or'ed together from the following
# bits, depending on which features you want enabled.
# 0x80 - Off: the Alt key will set bit 7 if pressed together with
#             another key.
#        On:  the Alt key will NOT set the high bit; an escape
#             character is prepended instead.
# The least significant bits control if the following keys are "dead".
# The key is dead by default if the bit is on.
# 0x01 - backquote (`)
# 0x02 - accent acute
# 0x04 - circumflex (^)
# 0x08 - tilde (~)
# 0x10 - dieresis (umlaut)

KEYBOARD = -DKBD_FINNISH -DKBDFLAGS=0
# KEYBOARD = -DKBD_FINNISH_LATIN1 -DKBDFLAGS=0x9F
# KEYBOARD = -DKBD_US -DKBDFLAGS=0
# KEYBOARD = -DKBD_GR -DKBDFLAGS=0
# KEYBOARD = -DKBD_GR_LATIN1 -DKBDFLAGS=0x9F
# KEYBOARD = -DKBD_FR -DKBDFLAGS=0
# KEYBOARD = -DKBD_FR_LATIN1 -DKBDFLAGS=0x9F
# KEYBOARD = -DKBD_UK -DKBDFLAGS=0
# KEYBOARD = -DKBD_DK -DKBDFLAGS=0
# KEYBOARD = -DKBD_DK_LATIN1 -DKBDFLAGS=0x9F
# KEYBOARD = -DKBD_DVORAK -DKBDFLAGS=0

#
# comment this line if you don't want the emulation-code
#

MATH_EMULATION = -DKERNEL_MATH_EMULATION

#
# uncomment this line if you are using gcc-1.40
#
#GCC_OPT = -fcombine-regs -fstrength-reduce

#
# standard CFLAGS
#

CFLAGS =-Wall -O6 -fomit-frame-pointer $(GCC_OPT)

#
# if you want the ram-disk device, define this to be the
# size in blocks.
#

#RAMDISK = -DRAMDISK=512

AS86	=as86 -0 -a
LD86	=ld86 -0

AS	=as
LD	=ld
#LDFLAGS	=-s -x -M
LDFLAGS	= -M
CC	=gcc $(RAMDISK)
MAKE	=make CFLAGS="$(CFLAGS)"
CPP	=cpp -nostdinc -Iinclude

ARCHIVES	=kernel/kernel.o mm/mm.o fs/fs.o net/net.o
FILESYSTEMS	=fs/minix/minix.o
DRIVERS		=kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a \
		 kernel/blk_drv/scsi/scsi.a
MATH		=kernel/math/math.a
LIBS		=lib/lib.a

.c.s:
	$(CC) $(CFLAGS) \
	-nostdinc -Iinclude -S -o $*.s $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) \
	-nostdinc -Iinclude -c -o $*.o $<

all:	Version Image

Version:
	@./makever.sh
	@echo \#define UTS_RELEASE \"0.96b.pl2-`cat .version`\" > include/linux/config_rel.h
	@echo \#define UTS_VERSION \"`date +%D`\" > include/linux/config_ver.h
	touch include/linux/config.h

Image: boot/bootsect boot/setup tools/system tools/build
	cp tools/system system.tmp
	strip system.tmp
	tools/build boot/bootsect boot/setup system.tmp $(ROOT_DEV) > Image
	rm system.tmp
	sync

disk: Image
	dd bs=8192 if=Image of=/dev/PS0

tools/build: tools/build.c
	$(CC) -static $(CFLAGS) \
	-o tools/build tools/build.c

boot/head.o: boot/head.s

tools/system:	boot/head.o init/main.o \
		$(ARCHIVES) $(FILESYSTEMS) $(DRIVERS) $(MATH) $(LIBS)
	$(LD) $(LDFLAGS) boot/head.o init/main.o \
	$(ARCHIVES) \
	$(FILESYSTEMS) \
	$(DRIVERS) \
	$(MATH) \
	$(LIBS) \
	-o tools/system > System.map

kernel/math/math.a: dummy
	(cd kernel/math; $(MAKE) MATH_EMULATION="$(MATH_EMULATION)")

kernel/blk_drv/blk_drv.a: dummy
	(cd kernel/blk_drv; $(MAKE))

kernel/blk_drv/scsi/scsi.a: dummy
	(cd kernel/blk_drv/scsi; $(MAKE))

kernel/chr_drv/chr_drv.a: dummy
	(cd kernel/chr_drv; $(MAKE) KEYBOARD="$(KEYBOARD)")

kernel/kernel.o: dummy
	(cd kernel; $(MAKE))

mm/mm.o: dummy
	(cd mm; $(MAKE))

fs/fs.o: dummy
	(cd fs; $(MAKE))

net/net.o: dummy
	(cd net; $(MAKE))

fs/minix/minix.o: dummy
	(cd fs/minix; $(MAKE))

lib/lib.a: dummy
	(cd lib; $(MAKE))

boot/setup: boot/setup.s
	$(AS86) -o boot/setup.o boot/setup.s
	$(LD86) -s -o boot/setup boot/setup.o

boot/setup.s:	boot/setup.S include/linux/config.h
	$(CPP) -traditional boot/setup.S -o boot/setup.s

boot/bootsect.s:	boot/bootsect.S include/linux/config.h
	$(CPP) -traditional boot/bootsect.S -o boot/bootsect.s

boot/bootsect:	boot/bootsect.s
	$(AS86) -o boot/bootsect.o boot/bootsect.s
	$(LD86) -s -o boot/bootsect boot/bootsect.o

clean:
	rm -f Image System.map tmp_make core boot/bootsect boot/setup \
		boot/bootsect.s boot/setup.s init/main.s
	rm -f init/*.o tools/system tools/build boot/*.o
	(cd mm;make clean)
	(cd fs;make clean)
	(cd kernel;make clean)
	(cd lib;make clean)
	(cd net;make clean)

backup: clean
	(cd .. ; tar cf - linux | compress - > backup.Z)
	sync

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in init/*.c;do echo -n "init/";$(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile
	(cd fs; make dep)
	(cd kernel; make dep)
	(cd mm; make dep)
	(cd net;make dep)
	(cd lib; make dep)

dummy:

### Dependencies:
init/main.o : init/main.c include/stddef.h include/stdarg.h include/fcntl.h include/sys/types.h \
  include/time.h include/asm/system.h include/asm/io.h include/linux/config.h \
  include/linux/config_rel.h include/linux/config_ver.h include/linux/config.dist.h \
  include/linux/sched.h include/linux/head.h include/linux/fs.h include/sys/dirent.h \
  include/limits.h include/sys/vfs.h include/linux/mm.h include/linux/kernel.h \
  include/signal.h include/sys/param.h include/sys/time.h include/sys/resource.h \
  include/linux/tty.h include/termios.h include/linux/unistd.h 
