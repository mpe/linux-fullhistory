#
# Makefile for the Jazz family specific parts of the kernel
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

all: jazz.o
O_TARGET := jazz.o
O_OBJS	 := hw-access.o int-handler.o jazzdma.o reset.o rtc-jazz.o setup.o

int-handler.o: int-handler.S

clean:

include $(TOPDIR)/Rules.make
