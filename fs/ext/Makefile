#
# Makefile for the linux ext-filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.c.s:
	$(CC) $(CFLAGS) -S $<
.c.o:
	$(CC) $(CFLAGS) -c $<
.s.o:
	$(AS) -o $*.o $<

OBJS=	bitmap.o freelists.o truncate.o namei.o inode.o \
	file.o dir.o symlink.o blkdev.o chrdev.o fifo.o

ext.o: $(OBJS)
	$(LD) -r -o ext.o $(OBJS)

clean:
	rm -f core *.o *.a tmp_make
	for i in *.c;do rm -f `basename $$i .c`.s;done

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	for i in *.c;do $(CPP) -M $$i;done >> tmp_make
	cp tmp_make Makefile

### Dependencies:
bitmap.o : bitmap.c /usr/src/linux/include/linux/string.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h \
  /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/ext_fs.h 
blkdev.o : blkdev.c /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/ext_fs.h \
  /usr/src/linux/include/linux/tty.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/termios.h \
  /usr/src/linux/include/linux/stat.h /usr/src/linux/include/linux/fcntl.h /usr/src/linux/include/errno.h 
chrdev.o : chrdev.c /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/ext_fs.h \
  /usr/src/linux/include/linux/tty.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/termios.h \
  /usr/src/linux/include/linux/stat.h /usr/src/linux/include/linux/fcntl.h /usr/src/linux/include/errno.h 
dir.o : dir.c /usr/src/linux/include/errno.h /usr/src/linux/include/asm/segment.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/ext_fs.h /usr/src/linux/include/linux/stat.h 
fifo.o : fifo.c /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/ext_fs.h 
file.o : file.c /usr/src/linux/include/errno.h /usr/src/linux/include/sys/dirent.h \
  /usr/src/linux/include/limits.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/asm/segment.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/linux/fcntl.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h \
  /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/ext_fs.h \
  /usr/src/linux/include/linux/stat.h 
freelists.o : freelists.c /usr/src/linux/include/linux/string.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h \
  /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/ext_fs.h 
inode.o : inode.c /usr/src/linux/include/linux/string.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/linux/stat.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/dirent.h \
  /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h \
  /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h \
  /usr/src/linux/include/linux/ext_fs.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/asm/segment.h 
namei.o : namei.c /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/ext_fs.h \
  /usr/src/linux/include/linux/string.h /usr/src/linux/include/linux/stat.h /usr/src/linux/include/linux/fcntl.h \
  /usr/src/linux/include/asm/segment.h /usr/src/linux/include/errno.h /usr/src/linux/include/const.h 
symlink.o : symlink.c /usr/src/linux/include/errno.h /usr/src/linux/include/asm/segment.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h /usr/src/linux/include/sys/dirent.h \
  /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h \
  /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h \
  /usr/src/linux/include/linux/ext_fs.h /usr/src/linux/include/linux/stat.h 
truncate.o : truncate.c /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/ext_fs.h \
  /usr/src/linux/include/linux/tty.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/termios.h \
  /usr/src/linux/include/linux/stat.h /usr/src/linux/include/linux/fcntl.h /usr/src/linux/include/errno.h 
