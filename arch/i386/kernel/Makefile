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
	$(CPP) -D__ASSEMBLY__ -traditional $< -o $*.s
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o

OBJS  = process.o signal.o entry.o traps.o irq.o vm86.o bios32.o ptrace.o ioport.o ldt.o setup.o

all: kernel.o head.o

head.o: head.s

head.s: head.S $(TOPDIR)/include/linux/tasks.h
	$(CPP) -traditional -o $*.s $<

kernel.o: $(OBJS)
	$(LD) -r -o kernel.o $(OBJS)
	sync

dep:
	$(CPP) -M *.c > .depend

modules:

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif


