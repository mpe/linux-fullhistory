#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<
.S.s:
	$(CPP) -D__ASSEMBLY__ -ansi $< -o $*.s
.S.o:
	$(CC) -D__ASSEMBLY__ -ansi -c $< -o $*.o

OBJS  = entry.o traps.o irq.o process.o promops.o signal.o ioport.o setup.o \
	idprom.o probe.o

all: kernel.o head.o

head.o: head.s

head.s: head.S $(TOPDIR)/include/asm-sparc/head.h
	$(CPP) -D__ASSEMBLY__ -ansi -o $*.s $<

kernel.o: $(OBJS)
	$(LD) -r -o kernel.o $(OBJS)
	sync

dep:
	$(CPP) -M *.c > .depend

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif


