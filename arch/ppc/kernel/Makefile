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
	$(CPP) $(CFLAGS) -D__ASSEMBLY__ $< -o $*.s
.S.o:
	$(CPP) $(CFLAGS) -D__ASSEMBLY__ $< -o $*.s
	$(AS) $(ASFLAGS) -o $*.o $*.s
	rm $*.s

HOST_CC = gcc

OBJS  = misc.o port_io.o pci.o traps.o process.o \
	signal.o syscalls.o ptrace.o ksyms.o irq.o bitops.o strcase.o ppc_htab.o

all: head.o kernel.o
head.o: head.S $(TOPDIR)/include/linux/tasks.h ppc_defs.h

ifeq ($(CONFIG_PREP),y)
OBJS += prep_setup.o prep_time.o
endif

ifeq ($(CONFIG_PMAC),y)
OBJS += pmac_setup.o pmac_support.o align.o pmac_time.o
endif

ifeq ($(CONFIG_MODULES),y)
OBJS = ksyms.o
endif


ppc_defs.h: mk_defs.c ppc_defs.head \
		$(TOPDIR)/include/asm/mmu.h \
		$(TOPDIR)/include/asm/processor.h \
		$(TOPDIR)/include/asm/pgtable.h \
		$(TOPDIR)/include/asm/ptrace.h
	$(CC) ${CFLAGS} -S mk_defs.c
	cp ppc_defs.head ppc_defs.h
	grep '^#define' mk_defs.s >>ppc_defs.h
	rm mk_defs.s

checks: checks.c
	$(HOSTCC) ${CFLAGS} -o checks checks.c
	checks

kernel.o: $(OBJS)
	$(LD) -r -o kernel.o $(OBJS)

fastdep:
	$(TOPDIR)/scripts/mkdep *.[Sch] > .depend

dep:
	$(CPP) -M *.S *.c > .depend

modules: 

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
