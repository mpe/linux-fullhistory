#
# Makefile for MIPS-specific library files..
#
# Many of these routines are just left over debugging trash of ancient
# times when I just make my Tyne beep and so ...
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

L_TARGET = lib.a
L_OBJS	= beep.o checksum.o csum.o dump_tlb.o tinycon.o watch.o

include $(TOPDIR)/Rules.make
