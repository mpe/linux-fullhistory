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

ifdef SMP

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $< -o $*.o
else
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o
endif

all: kernel.o head.o

O_TARGET := kernel.o
O_OBJS   := process.o signal.o entry.o traps.o irq.o vm86.o bios32.o \
            ptrace.o ioport.o ldt.o setup.o time.o sys_i386.o ksyms.o

ifdef SMP

O_OBJS += smp.o

head.o: head.S $(TOPDIR)/include/linux/tasks.h
	$(CC) -D__ASSEMBLY__ -D__SMP__ -traditional -c $*.S -o $*.o

else

head.o: head.S $(TOPDIR)/include/linux/tasks.h
	$(CC) -D__ASSEMBLY__ -traditional -c $*.S -o $*.o

endif

hexify:
	$(HOSTCC) hexify.c -o hexify

smp.c: trampoline.hex

trampoline.hex: trampoline hexify
	(dd if=trampoline bs=1 skip=32 | ./hexify >trampoline.hex )

trampoline: trampoline.o trampoline32.o
	$(LD86) -s -o $@ trampoline.o trampoline32.o

trampoline.o: trampoline.s
	$(AS86) -o $@ $<

trampoline32.o: trampoline32.s
	$(AS386) -o $@ $<

trampoline.s: trampoline.S $(CONFIGURE) $(TOPDIR)/include/linux/config.h Makefile
	$(CPP) -D__SMP__ -traditional $< -o $@

trampoline32.s: trampoline32.S $(CONFIGURE) $(TOPDIR)/include/linux/config.h Makefile
	$(CPP) -D__SMP__ -traditional $< -o $@

clean:
	rm -f trampoline hexify

include $(TOPDIR)/Rules.make
