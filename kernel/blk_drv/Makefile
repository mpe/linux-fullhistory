#
# Makefile for the kernel block device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now inherited from the
# parent makefile.
#

AR	=ar
AS	=as
LD	=ld
LDFLAGS	=-s -x
CC	=gcc -nostdinc -I../../include
CPP	=cpp -nostdinc -I../../include

.c.s:
	$(CC) $(CFLAGS) \
	-S -o $*.s $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) \
	-c -o $*.o $<

OBJS = hd.o ll_rw_blk.o floppy.o ramdisk.o

all: blk_drv.a

blk_drv.a: $(OBJS)
	rm -f blk_drv.a
	$(AR) rcs blk_drv.a $(OBJS)
	sync

clean:
	rm -f core *.o *.a tmp_make
	for i in *.c;do rm -f `basename $$i .c`.s;done
	(cd scsi; make clean)

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in *.c;do echo -n `echo $$i | sed 's,\.c,\.s,'`" "; \
		$(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile

### Dependencies:
floppy.s floppy.o : floppy.c ../../include/linux/sched.h ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/sys/dirent.h ../../include/limits.h \
  ../../include/sys/vfs.h ../../include/linux/mm.h ../../include/linux/kernel.h \
  ../../include/signal.h ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h \
  ../../include/sys/resource.h ../../include/linux/timer.h ../../include/linux/fdreg.h \
  ../../include/linux/fd.h ../../include/asm/system.h ../../include/asm/io.h ../../include/asm/segment.h \
  ../../include/errno.h blk.h 
hd.s hd.o : hd.c ../../include/errno.h ../../include/linux/config.h ../../include/linux/config_rel.h \
  ../../include/linux/config_ver.h ../../include/linux/config.dist.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h ../../include/sys/types.h \
  ../../include/sys/dirent.h ../../include/limits.h ../../include/sys/vfs.h ../../include/linux/mm.h \
  ../../include/linux/kernel.h ../../include/signal.h ../../include/sys/param.h \
  ../../include/sys/time.h ../../include/time.h ../../include/sys/resource.h ../../include/linux/timer.h \
  ../../include/linux/hdreg.h ../../include/asm/system.h ../../include/asm/io.h \
  ../../include/asm/segment.h blk.h 
ll_rw_blk.s ll_rw_blk.o : ll_rw_blk.c ../../include/errno.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h ../../include/sys/types.h \
  ../../include/sys/dirent.h ../../include/limits.h ../../include/sys/vfs.h ../../include/linux/mm.h \
  ../../include/linux/kernel.h ../../include/signal.h ../../include/sys/param.h \
  ../../include/sys/time.h ../../include/time.h ../../include/sys/resource.h ../../include/asm/system.h \
  blk.h 
ramdisk.s ramdisk.o : ramdisk.c ../../include/linux/string.h ../../include/linux/config.h \
  ../../include/linux/config_rel.h ../../include/linux/config_ver.h ../../include/linux/config.dist.h \
  ../../include/linux/sched.h ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/sys/dirent.h ../../include/limits.h \
  ../../include/sys/vfs.h ../../include/linux/mm.h ../../include/linux/kernel.h \
  ../../include/signal.h ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h \
  ../../include/sys/resource.h ../../include/linux/minix_fs.h ../../include/asm/system.h \
  ../../include/asm/segment.h ../../include/asm/memory.h blk.h 
