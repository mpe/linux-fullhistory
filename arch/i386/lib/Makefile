#
# Makefile for i386-specific library files..
#

.S.o:
	$(CC) $(AFLAGS) -traditional -c $< -o $*.o

L_TARGET = lib.a
L_OBJS  = checksum.o old-checksum.o delay.o \
	usercopy.o getuser.o putuser.o iodebug.o

ifdef CONFIG_X86_USE_3DNOW
L_OBJS += mmx.o
endif

include $(TOPDIR)/Rules.make
