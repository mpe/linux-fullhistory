# $Id: Makefile,v 1.22 1996/03/01 07:15:52 davem Exp $
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.s:
	$(CPP) -D__ASSEMBLY__ -ansi $< -o $*.s
.S.o:
	$(CC) -D__ASSEMBLY__ -ansi -c $< -o $*.o

all: kernel.o head.o

O_TARGET := kernel.o
O_OBJS   := entry.o wof.o wuf.o etrap.o rtrap.o switch.o traps.o irq.o  \
	    process.o signal.o ioport.o setup.o idprom.o mp.o c_mp.o    \
	    sys_sparc.o sunos_asm.o sparc-stub.o systbls.o sys_sunos.o  \
	    sunos_ioctl.o time.o windows.o cpu.o auxio.o devices.o ksyms.o \
	    sclow.o

all: kernel.o head.o

head.o: head.S
	$(CC) -D__ASSEMBLY__ -ansi -c $*.S -o $*.o

include $(TOPDIR)/Rules.make
