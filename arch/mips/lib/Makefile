#
# Makefile for MIPS-specific library files..
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

L_TARGET = lib.a
L_OBJS	= checksum.o copy_user.o csum.o dump_tlb.o io.o \
	  memset.o memcpy.o strlen_user.o strncpy_user.o tags.o watch.o

ifdef CONFIG_DECSTATION
L_OBJS += pmaxcon.o pmaxio.o
else
L_OBJS += tinycon.o
endif

include $(TOPDIR)/Rules.make
