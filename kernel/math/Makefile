#
# Makefile for the kernel math emulation routines
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.c.s:
	$(CC) $(CFLAGS) $(MATH_EMULATION) -S $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) $(MATH_EMULATION) -c $<

OBJS  = emulate.o convert.o ea.o get_put.o \
	add.o mul.o div.o compare.o sqrt.o

math.a: $(OBJS)
	$(AR) rcs math.a $(OBJS)
	sync

clean:
	rm -f core *.o *.a tmp_make
	for i in *.c;do rm -f `basename $$i .c`.s;done

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	for i in *.c;do $(CPP) -M $$i;done >> tmp_make
	cp tmp_make Makefile

### Dependencies:
add.o : add.c /usr/src/linux/include/linux/math_emu.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h \
  /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h \
  /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/signal.h \
  /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h 
compare.o : compare.c /usr/src/linux/include/linux/math_emu.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h \
  /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h \
  /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/signal.h \
  /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h 
convert.o : convert.c /usr/src/linux/include/linux/math_emu.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h \
  /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h \
  /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/signal.h \
  /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h 
div.o : div.c /usr/src/linux/include/linux/math_emu.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h \
  /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h \
  /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/signal.h \
  /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h 
ea.o : ea.c /usr/src/linux/include/linux/stddef.h /usr/src/linux/include/linux/math_emu.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/linux/limits.h /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h \
  /usr/src/linux/include/linux/dirent.h /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/signal.h \
  /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h \
  /usr/src/linux/include/asm/segment.h 
emulate.o : emulate.c /usr/src/linux/include/linux/signal.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h \
  /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h \
  /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/time.h \
  /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h 
get_put.o : get_put.c /usr/src/linux/include/linux/signal.h /usr/src/linux/include/linux/math_emu.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/linux/limits.h /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h \
  /usr/src/linux/include/linux/dirent.h /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/time.h \
  /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h \
  /usr/src/linux/include/asm/segment.h 
mul.o : mul.c /usr/src/linux/include/linux/math_emu.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h \
  /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h \
  /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/signal.h \
  /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h 
sqrt.o : sqrt.c /usr/src/linux/include/linux/math_emu.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h \
  /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h \
  /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/signal.h \
  /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h 
