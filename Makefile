#
# comment this line if you don't want the emulation-code
#

MATH_EMULATION = -DKERNEL_MATH_EMULATION

#
# uncomment the correct keyboard:
#

# KEYBOARD = -DKBD_FINNISH
KEYBOARD = -DKBD_US
# KEYBOARD = -DKBD_GR
# KEYBOARD = -DKBD_FR
# KEYBOARD = -DKBD_UK
# KEYBOARD = -DKBD_DK

#
# uncomment this line if you are using gcc-1.40
#
#GCC_OPT = -fcombine-regs -fstrength-reduce

#
# standard CFLAGS
#

CFLAGS =-Wall -O6 -fomit-frame-pointer $(GCC_OPT)

#
# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, /dev/xxxx or empty, in which case the
# default of FLOPPY is used by 'build'.
#

# ROOT_DEV = /dev/hdb1

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

ARCHIVES	=kernel/kernel.o mm/mm.o fs/fs.o
FILESYSTEMS	=fs/minix/minix.o
DRIVERS		=kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a
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
	@echo \#define UTS_RELEASE \"0.95c-`cat .version`\" > include/linux/config_rel.h
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
	$(CC) $(CFLAGS) \
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

kernel/chr_drv/chr_drv.a: dummy
	(cd kernel/chr_drv; $(MAKE) KEYBOARD="$(KEYBOARD)")

kernel/kernel.o: dummy
	(cd kernel; $(MAKE))

mm/mm.o: dummy
	(cd mm; $(MAKE))

fs/fs.o: dummy
	(cd fs; $(MAKE))

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

dummy:

### Dependencies:
init/main.o : init/main.c include/unistd.h include/sys/stat.h \
  include/sys/types.h include/sys/time.h include/time.h include/sys/times.h \
  include/sys/utsname.h include/sys/param.h include/sys/resource.h \
  include/utime.h include/linux/tty.h include/termios.h include/linux/sched.h \
  include/linux/head.h include/linux/fs.h include/sys/dirent.h \
  include/limits.h include/linux/mm.h include/linux/kernel.h include/signal.h \
  include/asm/system.h include/asm/io.h include/stddef.h include/stdarg.h \
  include/fcntl.h include/string.h 
