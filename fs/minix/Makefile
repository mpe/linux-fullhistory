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

OBJS=	bitmap.o truncate.o namei.o inode.o \
	file.o dir.o symlink.o blkdev.o chrdev.o

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
bitmap.o : bitmap.c ../../include/linux/string.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h ../../include/sys/types.h \
  ../../include/sys/dirent.h ../../include/limits.h ../../include/linux/mm.h ../../include/linux/kernel.h \
  ../../include/signal.h ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h \
  ../../include/sys/resource.h ../../include/linux/minix_fs.h 
blkdev.o : blkdev.c ../../include/linux/sched.h ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/sys/dirent.h ../../include/limits.h \
  ../../include/linux/mm.h ../../include/linux/kernel.h ../../include/signal.h \
  ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h ../../include/sys/resource.h \
  ../../include/linux/minix_fs.h ../../include/linux/tty.h ../../include/asm/system.h \
  ../../include/termios.h ../../include/errno.h ../../include/fcntl.h ../../include/sys/stat.h 
chrdev.o : chrdev.c ../../include/linux/sched.h ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/sys/dirent.h ../../include/limits.h \
  ../../include/linux/mm.h ../../include/linux/kernel.h ../../include/signal.h \
  ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h ../../include/sys/resource.h \
  ../../include/linux/minix_fs.h ../../include/linux/tty.h ../../include/asm/system.h \
  ../../include/termios.h ../../include/errno.h ../../include/fcntl.h ../../include/sys/stat.h 
dir.o : dir.c ../../include/errno.h ../../include/sys/stat.h ../../include/sys/types.h \
  ../../include/asm/segment.h ../../include/linux/fs.h ../../include/sys/dirent.h \
  ../../include/limits.h ../../include/linux/minix_fs.h 
file.o : file.c ../../include/errno.h ../../include/fcntl.h ../../include/sys/types.h \
  ../../include/sys/dirent.h ../../include/limits.h ../../include/sys/stat.h ../../include/asm/segment.h \
  ../../include/asm/system.h ../../include/linux/sched.h ../../include/linux/head.h \
  ../../include/linux/fs.h ../../include/linux/mm.h ../../include/linux/kernel.h \
  ../../include/signal.h ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h \
  ../../include/sys/resource.h ../../include/linux/minix_fs.h 
inode.o : inode.c ../../include/linux/string.h ../../include/sys/stat.h ../../include/sys/types.h \
  ../../include/linux/sched.h ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/dirent.h ../../include/limits.h ../../include/linux/mm.h ../../include/linux/kernel.h \
  ../../include/signal.h ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h \
  ../../include/sys/resource.h ../../include/linux/minix_fs.h ../../include/asm/system.h 
namei.o : namei.c ../../include/linux/sched.h ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/sys/dirent.h ../../include/limits.h \
  ../../include/linux/mm.h ../../include/linux/kernel.h ../../include/signal.h \
  ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h ../../include/sys/resource.h \
  ../../include/linux/minix_fs.h ../../include/asm/segment.h ../../include/linux/string.h \
  ../../include/fcntl.h ../../include/errno.h ../../include/const.h ../../include/sys/stat.h 
symlink.o : symlink.c ../../include/errno.h ../../include/sys/stat.h ../../include/sys/types.h \
  ../../include/asm/segment.h ../../include/linux/sched.h ../../include/linux/head.h \
  ../../include/linux/fs.h ../../include/sys/dirent.h ../../include/limits.h ../../include/linux/mm.h \
  ../../include/linux/kernel.h ../../include/signal.h ../../include/sys/param.h \
  ../../include/sys/time.h ../../include/time.h ../../include/sys/resource.h ../../include/linux/minix_fs.h 
truncate.o : truncate.c ../../include/linux/sched.h ../../include/linux/head.h \
  ../../include/linux/fs.h ../../include/sys/types.h ../../include/sys/dirent.h \
  ../../include/limits.h ../../include/linux/mm.h ../../include/linux/kernel.h \
  ../../include/signal.h ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h \
  ../../include/sys/resource.h ../../include/linux/minix_fs.h ../../include/linux/tty.h \
  ../../include/asm/system.h ../../include/termios.h ../../include/errno.h ../../include/fcntl.h \
  ../../include/sys/stat.h 
