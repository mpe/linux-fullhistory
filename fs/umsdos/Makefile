#
# Makefile for the umsdos unix-like filesystem routines.
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

OBJS=	dir.o emd.o file.o inode.o ioctl.o mangle.o namei.o\
	rdir.o symlink.o #check.o

umsdos.o: $(OBJS)
	$(LD) -r -o umsdos.o $(OBJS)

modules: umsdos.o
	ln -sf ../fs/umsdos/umsdos.o $(TOPDIR)/modules

clean:
	rm -f core *.o *.a *.s

dep:
	$(CPP) -M *.c > .depend

p:
	proto *.c >/usr/include/linux/umsdos_fs.p

doc:
	nadoc -i -p umsdos.doc - /tmp/umsdos.mpg

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
