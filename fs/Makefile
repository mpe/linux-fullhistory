#
# Makefile for the linux filesystem.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

SUBDIRS	=minix ext

.c.s:
	$(CC) $(CFLAGS) -S $<
.c.o:
	$(CC) $(CFLAGS) -c $<
.s.o:
	$(AS) -o $*.o $<

OBJS=	open.o read_write.o inode.o file_table.o buffer.o super.o \
	block_dev.o stat.o exec.o pipe.o namei.o fcntl.o ioctl.o \
	select.o fifo.o

all: fs.o subdirs

fs.o: $(OBJS)
	$(LD) -r -o fs.o $(OBJS)

subdirs: dummy
	for i in $(SUBDIRS); do (cd $$i; $(MAKE)); done

clean:
	rm -f core *.o *.a tmp_make
	for i in *.c; do rm -f `basename $$i .c`.s;done
	for i in $(SUBDIRS); do (cd $$i; $(MAKE) clean); done

depend dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	for i in *.c;do $(CPP) -M $$i;done >> tmp_make
	cp tmp_make Makefile
	for i in $(SUBDIRS); do (cd $$i; $(MAKE) dep); done

dummy:

### Dependencies:
block_dev.o : block_dev.c /usr/src/linux/include/errno.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h \
  /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h /usr/src/linux/include/asm/segment.h \
  /usr/src/linux/include/asm/system.h 
buffer.o : buffer.c /usr/src/linux/include/stdarg.h /usr/src/linux/include/linux/config.h \
  /usr/src/linux/include/linux/config_rel.h /usr/src/linux/include/linux/config_ver.h \
  /usr/src/linux/include/linux/config.dist.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h \
  /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h /usr/src/linux/include/asm/system.h \
  /usr/src/linux/include/asm/io.h 
exec.o : exec.c /usr/src/linux/include/signal.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/errno.h /usr/src/linux/include/sys/ptrace.h \
  /usr/src/linux/include/a.out.h /usr/src/linux/include/linux/string.h /usr/src/linux/include/linux/stat.h \
  /usr/src/linux/include/linux/fcntl.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/dirent.h \
  /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/asm/segment.h /usr/src/linux/include/sys/user.h 
fcntl.o : fcntl.c /usr/src/linux/include/errno.h /usr/src/linux/include/asm/segment.h \
  /usr/src/linux/include/linux/stat.h /usr/src/linux/include/linux/fcntl.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/linux/string.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/dirent.h \
  /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h \
  /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h 
fifo.o : fifo.c /usr/src/linux/include/errno.h /usr/src/linux/include/linux/fcntl.h \
  /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/dirent.h \
  /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h \
  /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h 
file_table.o : file_table.c /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h 
inode.o : inode.c /usr/src/linux/include/linux/string.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/linux/stat.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/dirent.h \
  /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h \
  /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h \
  /usr/src/linux/include/asm/system.h 
ioctl.o : ioctl.c /usr/src/linux/include/errno.h /usr/src/linux/include/linux/string.h \
  /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h /usr/src/linux/include/linux/stat.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h 
namei.o : namei.c /usr/src/linux/include/errno.h /usr/src/linux/include/const.h \
  /usr/src/linux/include/asm/segment.h /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/string.h \
  /usr/src/linux/include/linux/fcntl.h /usr/src/linux/include/linux/stat.h 
open.o : open.c /usr/src/linux/include/errno.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/utime.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/fcntl.h /usr/src/linux/include/linux/stat.h /usr/src/linux/include/linux/string.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h \
  /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h \
  /usr/src/linux/include/asm/segment.h 
pipe.o : pipe.c /usr/src/linux/include/signal.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/errno.h /usr/src/linux/include/termios.h \
  /usr/src/linux/include/asm/segment.h /usr/src/linux/include/linux/fcntl.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/dirent.h \
  /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h \
  /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h 
read_write.o : read_write.c /usr/src/linux/include/errno.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/linux/stat.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h \
  /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h \
  /usr/src/linux/include/linux/minix_fs.h /usr/src/linux/include/asm/segment.h 
select.o : select.c /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/tty.h \
  /usr/src/linux/include/asm/system.h /usr/src/linux/include/termios.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/string.h \
  /usr/src/linux/include/linux/stat.h /usr/src/linux/include/asm/segment.h /usr/src/linux/include/const.h \
  /usr/src/linux/include/errno.h 
stat.o : stat.c /usr/src/linux/include/errno.h /usr/src/linux/include/linux/stat.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h \
  /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h \
  /usr/src/linux/include/asm/segment.h 
super.o : super.c /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h /usr/src/linux/include/sys/dirent.h \
  /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h \
  /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h \
  /usr/src/linux/include/linux/minix_fs.h /usr/src/linux/include/linux/ext_fs.h \
  /usr/src/linux/include/linux/stat.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/asm/segment.h \
  /usr/src/linux/include/errno.h 
