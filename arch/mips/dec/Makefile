#
# Makefile for the DECstation family specific parts of the kernel
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

all: dec.o
O_TARGET := dec.o
O_OBJS	 := boot.o int-handler.o decstation.o hw-access.o reset.o setup.o

boot.o:	boot.S

int-handler.o:	int-handler.S

clean:

include $(TOPDIR)/Rules.make
