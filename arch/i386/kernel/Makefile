#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

#.S.s:
#	$(CPP) -D__ASSEMBLY__ -traditional $< -o $*.s
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o

all: kernel.o head.o
O_TARGET := kernel.o
O_OBJS   := process.o signal.o entry.o traps.o irq.o vm86.o bios32.o \
            ptrace.o ioport.o ldt.o setup.o time.o sys_i386.o


#head.o: head.s

head.o: head.S $(TOPDIR)/include/linux/tasks.h
	$(CC) -D__ASSEMBLY__ -traditional -c $*.S -o $*.o
#	$(CPP) -traditional -o $*.s $<

include $(TOPDIR)/Rules.make
