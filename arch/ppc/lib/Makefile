#
# Makefile for ppc-specific library files..
#

.S.o:
	$(CC) -D__ASSEMBLY__ -c $< -o $*.o

O_TARGET = lib.o
O_OBJS  = checksum.o string.o strcase.o

include $(TOPDIR)/Rules.make
