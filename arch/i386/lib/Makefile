#
# Makefile for i386-specific library files..
#

.S.o:
	$(CC) $(AFLAGS) -c $< -o $*.o

L_TARGET = lib.a
L_OBJS  = checksum.o old-checksum.o delay.o \
	usercopy.o getuser.o putuser.o iodebug.o \
	memcpy.o

ifdef CONFIG_X86_USE_3DNOW
L_OBJS += mmx.o
endif

ifdef CONFIG_HAVE_DEC_LOCK
L_OBJS += dec_and_lock.o
endif

include $(TOPDIR)/Rules.make
