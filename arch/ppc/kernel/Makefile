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

HOST_CC = gcc

OBJS  = misc.o setup.o port_io.o irq.o pci.o traps.o stubs.o process.o \
	signal.o raw_printf.o ramdisk.o 

all: head.o kernel.o cortstrip mkboot

head.o: head.s
head.s: head.S $(TOPDIR)/include/linux/tasks.h ppc_defs.h

ppc_defs.h: mk_defs $(TOPDIR)/include/asm/mmu.h $(TOPDIR)/include/asm/processor.h $(TOPDIR)/include/asm/pgtable.h $(TOPDIR)/include/asm/ptrace.h
#	simppc mk_defs -- $@
	mk_defs ppc_defs.h

ramdisk.o: ramdisk.s

ramdisk.s: ramdisk.image mk_ramdisk
	mk_ramdisk ramdisk.image ramdisk.s

mk_ramdisk: mk_ramdisk.c
	$(HOST_CC) -o mk_ramdisk mk_ramdisk.c	



cortstrip : cortstrip.c
	${HOST_CC} -o cortstrip cortstrip.c

mkboot : mkboot.c
	${HOST_CC} -o mkboot mkboot.c

mk_defs: mk_defs.c $(TOPDIR)/include/asm/mmu.h $(TOPDIR)/include/asm/processor.h $(TOPDIR)/include/asm/pgtable.h $(TOPDIR)/include/asm/ptrace.h
#	$(CC) ${CFLAGS} -c mk_defs -T ld.script-user -Ttext 0x1000 mk_defs.c
#	$(LD) -T ld.script-user -Ttext 0x1000  -o mk_defs mk_defs.o
	$(HOST_CC) -DMKDEFS ${CFLAGS} -o mk_defs mk_defs.c


kernel.o: $(OBJS)
	$(LD) -r -o kernel.o $(OBJS)
	sync


dep:
	$(CPP) -M *.c > .depend

fastdep:
	$(CPP) -M *.c > .depend


modules:

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
