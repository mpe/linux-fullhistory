#
# Makefile for the ia32 kernel emulation subsystem.
#

.S.s:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -E -o $*.s $<
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -c -o $*.o $<

all: ia32.o

O_TARGET := ia32.o
O_OBJS	 := ia32_entry.o ia32_signal.o sys_ia32.o ia32_support.o binfmt_elf32.o

clean::

include $(TOPDIR)/Rules.make
