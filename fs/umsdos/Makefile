#
# Makefile for the umsdos Unix-like filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (not a .c file).
#
# Note 2:  the CFLAGS definitions are now in the main makefile.
O_TARGET := umsdos.o

O_OBJS   := dir.o  file.o inode.o ioctl.o mangle.o namei.o \
		rdir.o symlink.o emd.o check.o

M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s

p:
	proto *.c >/usr/include/linux/umsdos_fs.p

doc:
	nadoc -i -p umsdos.doc - /tmp/umsdos.mpg
