#
# Makefile for the linux networking.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

AS	=as
AR	=ar
LD	=ld
CC	=gcc -nostdinc -I../include -Wall # -DSOCK_DEBUG
CPP	=cpp -nostdinc -I../include

.c.o:
	$(CC) $(CFLAGS) \
	-c -o $*.o $<
.s.o:
	$(AS) -o $*.o $<
.c.s:
	$(CC) $(CFLAGS) \
	-S -o $*.s $<

OBJS	=  socket.o unix.o

net.o: $(OBJS)
	$(LD) -r -o net.o $(OBJS)

clean:
	rm -f core *.o *.a tmp_make
	for i in *.c;do rm -f `basename $$i .c`.s;done

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in *.c;do $(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile

### Dependencies:
socket.o : socket.c ../include/signal.h ../include/sys/types.h ../include/errno.h \
  ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h ../include/sys/dirent.h \
  ../include/limits.h ../include/linux/mm.h ../include/linux/kernel.h ../include/sys/param.h \
  ../include/sys/time.h ../include/time.h ../include/sys/resource.h ../include/asm/system.h \
  ../include/asm/segment.h ../include/sys/socket.h ../include/sys/stat.h ../include/fcntl.h \
  ../include/termios.h kern_sock.h socketcall.h 
unix.o : unix.c ../include/signal.h ../include/sys/types.h ../include/errno.h \
  ../include/linux/string.h ../include/linux/sched.h ../include/linux/head.h ../include/linux/fs.h \
  ../include/sys/dirent.h ../include/limits.h ../include/linux/mm.h ../include/linux/kernel.h \
  ../include/sys/param.h ../include/sys/time.h ../include/time.h ../include/sys/resource.h \
  ../include/asm/system.h ../include/asm/segment.h ../include/sys/socket.h ../include/sys/un.h \
  ../include/sys/stat.h ../include/fcntl.h ../include/termios.h kern_sock.h 
