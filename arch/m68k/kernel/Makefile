#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -Wa,-m68030 -c $< -o $*.o

all: kernel.o head.o
O_TARGET := kernel.o
O_OBJS := entry.o process.o traps.o ints.o signal.o ptrace.o \
	  setup.o bios32.o sys_m68k.o console.o time.o
OX_OBJS = ksyms.o

head.o: head.S

include $(TOPDIR)/Rules.make
