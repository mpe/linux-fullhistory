#
# Makefile for MIPS-specific library files..
#
# Many of these routines are just left over debugging trash of ancient
# times when I just make my Tyne beep and so ...
#
# ...and for when I need to get the DECStation to use the boot prom to
# do things... Paul M. Antoine.
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

L_TARGET = lib.a
L_OBJS	= beep.o checksum.o copy_user.o csum.o dump_tlb.o memset.o memcpy.o \
          strlen_user.o strncpy_user.o tags.o watch.o

ifdef CONFIG_DECSTATION
L_OBJS += pmaxcon.o pmaxio.o
else
L_OBJS += tinycon.o
endif

include $(TOPDIR)/Rules.make
