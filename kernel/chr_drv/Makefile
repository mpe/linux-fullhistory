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

OBJS  = tty_io.o console.o keyboard.o serial.o rs_io.o \
	tty_ioctl.o pty.o lp.o

chr_drv.a: $(OBJS)
	$(AR) rcs chr_drv.a $(OBJS)
	sync

keyboard.s: keyboard.S
	$(CPP) $(KEYBOARD) -traditional keyboard.S -o keyboard.s

clean:
	rm -f core *.o *.a tmp_make keyboard.s
	for i in *.c;do rm -f `basename $$i .c`.s;done

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in *.c;do echo -n `echo $$i | sed 's,\.c,\.s,'`" "; \
		$(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile

### Dependencies:
console.s console.o : console.c ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/sys/dirent.h ../../include/limits.h \
  ../../include/linux/mm.h ../../include/linux/kernel.h \
  ../../include/signal.h ../../include/sys/param.h ../../include/sys/time.h \
  ../../include/time.h ../../include/sys/resource.h \
  ../../include/linux/timer.h ../../include/linux/tty.h \
  ../../include/termios.h ../../include/linux/config.h \
  ../../include/linux/config_rel.h ../../include/linux/config_ver.h \
  ../../include/asm/io.h ../../include/asm/system.h \
  ../../include/asm/segment.h ../../include/string.h ../../include/errno.h 
lp.s lp.o : lp.c ../../include/linux/lp.h ../../include/errno.h \
  ../../include/linux/kernel.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/types.h ../../include/sys/dirent.h ../../include/limits.h \
  ../../include/linux/mm.h ../../include/signal.h ../../include/sys/param.h \
  ../../include/sys/time.h ../../include/time.h ../../include/sys/resource.h \
  ../../include/asm/io.h ../../include/asm/segment.h \
  ../../include/checkpoint.h 
pty.s pty.o : pty.c ../../include/linux/tty.h ../../include/termios.h \
  ../../include/sys/types.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/dirent.h ../../include/limits.h ../../include/linux/mm.h \
  ../../include/linux/kernel.h ../../include/signal.h \
  ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h \
  ../../include/sys/resource.h ../../include/asm/system.h \
  ../../include/asm/io.h 
serial.s serial.o : serial.c ../../include/linux/tty.h ../../include/termios.h \
  ../../include/sys/types.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/dirent.h ../../include/limits.h ../../include/linux/mm.h \
  ../../include/linux/kernel.h ../../include/signal.h \
  ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h \
  ../../include/sys/resource.h ../../include/linux/timer.h \
  ../../include/asm/system.h ../../include/asm/io.h 
tty_io.s tty_io.o : tty_io.c ../../include/ctype.h ../../include/errno.h \
  ../../include/signal.h ../../include/sys/types.h ../../include/unistd.h \
  ../../include/sys/stat.h ../../include/sys/time.h ../../include/time.h \
  ../../include/sys/times.h ../../include/sys/utsname.h \
  ../../include/sys/param.h ../../include/sys/resource.h \
  ../../include/utime.h ../../include/fcntl.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/dirent.h ../../include/limits.h ../../include/linux/mm.h \
  ../../include/linux/kernel.h ../../include/linux/tty.h \
  ../../include/termios.h ../../include/asm/segment.h \
  ../../include/asm/system.h 
tty_ioctl.s tty_ioctl.o : tty_ioctl.c ../../include/errno.h ../../include/termios.h \
  ../../include/sys/types.h ../../include/linux/sched.h \
  ../../include/linux/head.h ../../include/linux/fs.h \
  ../../include/sys/dirent.h ../../include/limits.h ../../include/linux/mm.h \
  ../../include/linux/kernel.h ../../include/signal.h \
  ../../include/sys/param.h ../../include/sys/time.h ../../include/time.h \
  ../../include/sys/resource.h ../../include/linux/tty.h \
  ../../include/asm/io.h ../../include/asm/segment.h \
  ../../include/asm/system.h 
