#
# Makefile for ppc-specific library files..
#

.S.o:
	$(CC) -D__ASSEMBLY__ -c $< -o $*.o

O_TARGET = lib.o
O_OBJS  = checksum.o string.o strcase.o

ifdef CONFIG_SMP
O_OBJS += locks.o
endif


include $(TOPDIR)/Rules.make
