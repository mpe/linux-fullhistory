#
# Makefile for some libs needed in the kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

OBJS  = ctype.o _exit.o open.o close.o errno.o write.o dup.o setsid.o \
	execve.o wait.o string.o malloc.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)
	sync

clean:
	rm -f core *.o *.a tmp_make
	for i in *.c;do rm -f `basename $$i .c`.s;done

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	for i in *.c;do $(CPP) -M $$i;done >> tmp_make
	cp tmp_make Makefile

### Dependencies:
_exit.o : _exit.c /usr/src/linux/include/linux/unistd.h 
close.o : close.c /usr/src/linux/include/linux/unistd.h 
ctype.o : ctype.c /usr/src/linux/include/linux/ctype.h 
dup.o : dup.c /usr/src/linux/include/linux/unistd.h 
errno.o : errno.c 
execve.o : execve.c /usr/src/linux/include/linux/unistd.h 
malloc.o : malloc.c /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h /usr/src/linux/include/linux/wait.h \
  /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h /usr/src/linux/include/linux/vfs.h \
  /usr/src/linux/include/linux/minix_fs_sb.h /usr/src/linux/include/linux/ext_fs_sb.h \
  /usr/src/linux/include/linux/msdos_fs_sb.h /usr/src/linux/include/linux/signal.h \
  /usr/src/linux/include/asm/system.h 
open.o : open.c /usr/src/linux/include/linux/unistd.h /usr/src/linux/include/stdarg.h 
setsid.o : setsid.c /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/unistd.h 
string.o : string.c /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/string.h 
wait.o : wait.c /usr/src/linux/include/linux/unistd.h /usr/src/linux/include/linux/types.h 
write.o : write.c /usr/src/linux/include/linux/unistd.h /usr/src/linux/include/linux/types.h 
