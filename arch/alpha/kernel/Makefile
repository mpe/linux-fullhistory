#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.s:
	$(CPP) -D__ASSEMBLY__ -traditional $< -o $*.s
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o

OBJS  = entry.o traps.o process.o osf_sys.o irq.o signal.o setup.o \
	bios32.o ptrace.o time.o apecs.o lca.o

all: kernel.o head.o

head.o: head.s

head.s: head.S $(TOPDIR)/include/asm-alpha/system.h
	$(CPP) -traditional -o $*.s $<

kernel.o: $(OBJS)
	$(LD) -r -o kernel.o $(OBJS)

dep:
	$(CPP) -M *.c > .depend

include $(TOPDIR)/Rules.make
