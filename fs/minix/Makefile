#
# Makefile for the linux minix-filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

AR	=ar
AS	=as
LD	=ld
CC	=gcc -nostdinc -I../../include
CPP	=cpp -nostdinc -I../../include

.c.s:
	$(CC) $(CFLAGS) \
	-S -o $*.s $<
.c.o:
	$(CC) $(CFLAGS) \
	-c -o $*.o $<
.s.o:
	$(AS) -o $*.o $<

OBJS=	minix_op.o bitmap.o truncate.o namei.o inode.o file_dev.o

minix.o: $(OBJS)
	$(LD) -r -o minix.o $(OBJS)

clean:
	rm -f core *.o *.a tmp_make
	for i in *.c;do rm -f `basename $$i .c`.s;done

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in *.c;do $(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile

### Dependencies:
bitmap.o : bitmap.c ../../include/string.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/sys/dirent.h ../../include/limits.h \
  ../../include/linux/mm.h ../../include/linux/kernel.h \
  ../../include/signal.h ../../include/sys/param.h ../../include/sys/time.h \
  ../../include/time.h ../../include/sys/resource.h \
  ../../include/linux/minix_fs.h 
file_dev.o : file_dev.c ../../include/errno.h ../../include/fcntl.h \
  ../../include/sys/types.h ../../include/sys/dirent.h ../../include/limits.h \
  ../../include/sys/stat.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/linux/mm.h ../../include/linux/kernel.h \
  ../../include/signal.h ../../include/sys/param.h ../../include/sys/time.h \
  ../../include/time.h ../../include/sys/resource.h \
  ../../include/linux/minix_fs.h ../../include/asm/segment.h 
inode.o : inode.c ../../include/string.h ../../include/sys/stat.h \
  ../../include/sys/types.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/dirent.h ../../include/limits.h ../../include/linux/mm.h \
  ../../include/linux/kernel.h ../../include/signal.h \
  ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h \
  ../../include/sys/resource.h ../../include/linux/minix_fs.h \
  ../../include/asm/system.h 
minix_op.o : minix_op.c ../../include/linux/fs.h ../../include/sys/types.h \
  ../../include/sys/dirent.h ../../include/limits.h \
  ../../include/linux/minix_fs.h 
namei.o : namei.c ../../include/linux/sched.h ../../include/linux/head.h \
  ../../include/linux/fs.h ../../include/sys/types.h \
  ../../include/sys/dirent.h ../../include/limits.h ../../include/linux/mm.h \
  ../../include/linux/kernel.h ../../include/signal.h \
  ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h \
  ../../include/sys/resource.h ../../include/linux/minix_fs.h \
  ../../include/asm/segment.h ../../include/string.h ../../include/fcntl.h \
  ../../include/errno.h ../../include/const.h ../../include/sys/stat.h 
truncate.o : truncate.c ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/sys/dirent.h ../../include/limits.h \
  ../../include/linux/mm.h ../../include/linux/kernel.h \
  ../../include/signal.h ../../include/sys/param.h ../../include/sys/time.h \
  ../../include/time.h ../../include/sys/resource.h \
  ../../include/linux/minix_fs.h ../../include/linux/tty.h \
  ../../include/termios.h ../../include/errno.h ../../include/fcntl.h \
  ../../include/sys/stat.h 
