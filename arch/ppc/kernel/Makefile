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
	$(AS) $(ASFLAGS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<
.S.s:
#	$(CPP) $(CFLAGS) -D__ASSEMBLY__ -traditional $< -o $*.s
	$(CPP) $(CFLAGS) -D__ASSEMBLY__ $< -o $*.s
.S.o:
#	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o
	$(CPP) $(CFLAGS) -D__ASSEMBLY__ $< -o $*.s
	$(AS) $(ASFLAGS) -o $*.o $*.s
	rm $*.s

HOST_CC = gcc

OBJS  = misc.o setup.o port_io.o irq.o pci.o traps.o stubs.o process.o \
	signal.o ksyms.o time.o syscalls.o \
	support.o ptrace.o # ramdisk_drvr.o

all: head.o kernel.o #no_ramdisk.o ramdisk.o

head.o: head.s
head.s: head.S $(TOPDIR)/include/linux/tasks.h ppc_defs.h

ppc_defs.h: mk_defs
#	simppc mk_defs -- $@
	mk_defs $@

#no_ramdisk.o: no_ramdisk.S

#ramdisk.o: ramdisk.image mk_ramdisk
#	mk_ramdisk ramdisk.image $*.s
#	$(AS) -o $@ $*.s
#	rm $*.s

#mk_ramdisk: mk_ramdisk.c
#	${HOST_CC} -o mk_ramdisk mk_ramdisk.c	

mk_defs: mk_defs.c $(TOPDIR)/include/asm/mmu.h $(TOPDIR)/include/asm/processor.h $(TOPDIR)/include/asm/pgtable.h $(TOPDIR)/include/asm/ptrace.h
#	cc.ppc ${CFLAGS} -o mk_defs -T ld.script-user -Ttext 0x1000 mk_defs.c
	gcc.ppc ${CFLAGS} -o mk_defs mk_defs.c


kernel.o: $(OBJS)
	$(LD) -r -o kernel.o $(OBJS)
	sync

#mkboot: mkboot.c
#	${HOST_CC} -o $@ -Iinclude mkboot.c

dep:
	$(CPP) -M *.c > .depend

fastdep:

modules:

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
