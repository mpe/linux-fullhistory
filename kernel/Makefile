#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.s:
	$(CPP) -traditional $< -o $*.s
.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

SUBDIRS	= chr_drv blk_drv math

OBJS  = sched.o sys_call.o traps.o asm.o fork.o \
	panic.o printk.o vsprintf.o sys.o exit.o \
	signal.o mktime.o ptrace.o ioport.o itimer.o

all: kernel.o subdirs

kernel.o: $(OBJS)
	$(LD) -r -o kernel.o $(OBJS)
	sync

subdirs: dummy
	for i in $(SUBDIRS); do (cd $$i; $(MAKE)); done

sys_call.s: sys_call.S

sys_call.o: sys_call.s

sched.o: sched.c
	$(CC) $(CFLAGS) -fno-omit-frame-pointer -c $<

clean:
	rm -f core *.o *.a tmp_make sys_call.s
	for i in *.c;do rm -f `basename $$i .c`.s;done
	for i in $(SUBDIRS); do (cd $$i; $(MAKE) clean); done

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	for i in *.c;do $(CPP) -M $$i;done >> tmp_make
	cp tmp_make Makefile
	for i in $(SUBDIRS); do (cd $$i; $(MAKE) dep); done

dummy:

### Dependencies:
exit.o : exit.c /usr/src/linux/include/errno.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h /usr/src/linux/include/sys/wait.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/sys/param.h \
  /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h \
  /usr/src/linux/include/linux/tty.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/termios.h \
  /usr/src/linux/include/asm/segment.h 
fork.o : fork.c /usr/src/linux/include/errno.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/sys/types.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h \
  /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h /usr/src/linux/include/asm/segment.h \
  /usr/src/linux/include/asm/system.h 
ioport.o : ioport.c /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/errno.h 
itimer.o : itimer.c /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/string.h \
  /usr/src/linux/include/asm/segment.h /usr/src/linux/include/errno.h 
mktime.o : mktime.c /usr/src/linux/include/time.h 
panic.o : panic.c /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h 
printk.o : printk.c /usr/src/linux/include/stdarg.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/errno.h /usr/src/linux/include/asm/segment.h /usr/src/linux/include/asm/system.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/sys/types.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h \
  /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h 
ptrace.o : ptrace.c /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/asm/segment.h /usr/src/linux/include/asm/system.h \
  /usr/src/linux/include/errno.h /usr/src/linux/include/sys/ptrace.h 
sched.o : sched.c /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/timer.h /usr/src/linux/include/linux/sys.h \
  /usr/src/linux/include/linux/fdreg.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/asm/io.h \
  /usr/src/linux/include/asm/segment.h /usr/src/linux/include/errno.h 
signal.o : signal.c /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h \
  /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h /usr/src/linux/include/sys/vfs.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/signal.h \
  /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/sys/resource.h /usr/src/linux/include/asm/segment.h /usr/src/linux/include/sys/wait.h \
  /usr/src/linux/include/sys/ptrace.h /usr/src/linux/include/errno.h 
sys.o : sys.c /usr/src/linux/include/errno.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h \
  /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h /usr/src/linux/include/linux/tty.h \
  /usr/src/linux/include/asm/system.h /usr/src/linux/include/termios.h /usr/src/linux/include/linux/config.h \
  /usr/src/linux/include/linux/config_rel.h /usr/src/linux/include/linux/config_ver.h \
  /usr/src/linux/include/linux/config.dist.h /usr/src/linux/include/asm/segment.h \
  /usr/src/linux/include/sys/times.h /usr/src/linux/include/linux/utsname.h /usr/src/linux/include/linux/string.h 
traps.o : traps.c /usr/src/linux/include/linux/string.h /usr/src/linux/include/sys/types.h \
  /usr/src/linux/include/stddef.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/sys/dirent.h /usr/src/linux/include/limits.h \
  /usr/src/linux/include/sys/vfs.h /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/signal.h /usr/src/linux/include/sys/param.h /usr/src/linux/include/sys/time.h \
  /usr/src/linux/include/time.h /usr/src/linux/include/sys/resource.h /usr/src/linux/include/asm/system.h \
  /usr/src/linux/include/asm/segment.h /usr/src/linux/include/asm/io.h /usr/src/linux/include/errno.h 
vsprintf.o : vsprintf.c /usr/src/linux/include/stdarg.h /usr/src/linux/include/linux/string.h \
  /usr/src/linux/include/sys/types.h /usr/src/linux/include/stddef.h 
