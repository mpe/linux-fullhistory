# Makefile for the AP1000 files in the Linux kernel
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.s:
	$(CPP) -D__ASSEMBLY__ -ansi $< -o $*.s
.S.o:
	$(CC) -D__ASSEMBLY__ -ansi -c $< -o $*.o

all: ap1000lib.o

O_TARGET := ap1000lib.o
O_OBJS   := bnet.o timer.o util.o dma.o kgdb.o irq.o \
	msc.o hw.o tnet.o sync.o mpp.o \
	apmmu.o aplib.o approm.o

include $(TOPDIR)/Rules.make
