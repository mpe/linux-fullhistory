#
# Makefile for the kernel character device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

OBJS  = tty_io.o console.o keyboard.o serial.o \
	tty_ioctl.o pty.o lp.o vt.o mem.o

chr_drv.a: $(OBJS)
	$(AR) rcs chr_drv.a $(OBJS)
	sync

keyboard.o: keyboard.c
	$(CC) $(CFLAGS) $(KEYBOARD) -c -o keyboard.o keyboard.c

clean:
	rm -f core *.o *.a tmp_make keyboard.s
	for i in *.c;do rm -f `basename $$i .c`.s;done

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	for i in *.c;do $(CPP) -M -DKBD_FINNISH $$i;done >> tmp_make
	cp tmp_make Makefile

### Dependencies:
console.o : console.c /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/timer.h /usr/src/linux/include/linux/tty.h \
  /usr/src/linux/include/asm/system.h /usr/src/linux/include/termios.h /usr/src/linux/include/linux/config.h \
  /usr/src/linux/include/linux/config_rel.h /usr/src/linux/include/linux/config_ver.h \
  /usr/src/linux/include/linux/config.dist.h /usr/src/linux/include/asm/io.h /usr/src/linux/include/asm/segment.h \
  /usr/src/linux/include/linux/string.h /usr/src/linux/include/errno.h /usr/src/linux/include/sys/kd.h \
  vt_kern.h 
keyboard.o : keyboard.c /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/ctype.h /usr/src/linux/include/linux/tty.h \
  /usr/src/linux/include/asm/system.h /usr/src/linux/include/termios.h /usr/src/linux/include/asm/io.h 
lp.o : lp.c /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/lp.h /usr/src/linux/include/errno.h \
  /usr/src/linux/include/asm/io.h /usr/src/linux/include/asm/segment.h 
mem.o : mem.c /usr/src/linux/include/errno.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h \
  /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/tty.h \
  /usr/src/linux/include/asm/system.h /usr/src/linux/include/termios.h /usr/src/linux/include/asm/segment.h \
  /usr/src/linux/include/asm/io.h 
pty.o : pty.c /usr/src/linux/include/errno.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h \
  /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/tty.h \
  /usr/src/linux/include/asm/system.h /usr/src/linux/include/termios.h /usr/src/linux/include/linux/fcntl.h \
  /usr/src/linux/include/asm/io.h 
serial.o : serial.c /usr/src/linux/include/signal.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/errno.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/dirent.h \
  /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h \
  /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/timer.h \
  /usr/src/linux/include/linux/tty.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/termios.h \
  /usr/src/linux/include/asm/io.h /usr/src/linux/include/asm/segment.h 
tty_io.o : tty_io.c /usr/src/linux/include/errno.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h /usr/src/linux/include/linux/fcntl.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/sys/param.h \
  /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h \
  /usr/src/linux/include/linux/tty.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/termios.h \
  /usr/src/linux/include/linux/ctype.h /usr/src/linux/include/asm/io.h /usr/src/linux/include/asm/segment.h \
  /usr/src/linux/include/sys/kd.h vt_kern.h 
tty_ioctl.o : tty_ioctl.c /usr/src/linux/include/errno.h /usr/src/linux/include/termios.h \
  /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/dirent.h \
  /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h \
  /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h \
  /usr/src/linux/include/linux/tty.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/asm/io.h \
  /usr/src/linux/include/asm/segment.h 
vt.o : vt.c /usr/src/linux/include/errno.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/sys/kd.h /usr/src/linux/include/sys/vt.h \
  /usr/src/linux/include/asm/io.h /usr/src/linux/include/asm/segment.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/dirent.h \
  /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h \
  /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h \
  /usr/src/linux/include/linux/tty.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/termios.h \
  /usr/src/linux/include/linux/timer.h vt_kern.h 
