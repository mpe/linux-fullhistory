#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.o:
	$(CC) -D__ASSEMBLY__ -c $< -o $*.o

O_TARGET := kernel.o
O_OBJS := misc.o traps.o process.o signal.o syscalls.o \
	  align.o ptrace.o irq.o openpic.o bitops.o ppc_htab.o idle.o \
	  time.o prep_time.o pmac_time.o chrp_time.o \
	  setup.o prep_setup.o pmac_setup.o pmac_support.o chrp_setup.o \
	  pci.o prep_pci.o pmac_pci.o chrp_pci.o \
	  residual.o prom.o
OX_OBJS := ppc_ksyms.o

all: head.o kernel.o

head.o: head.S $(TOPDIR)/include/linux/tasks.h ppc_defs.h

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
	$(HOSTCC) -fno-builtin -I$(TOPDIR)/include -D__KERNEL__ -o checks checks.c
	./checks

include $(TOPDIR)/Rules.make
