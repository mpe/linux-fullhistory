#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.o:
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -c $< -o $*.o

O_TARGET := kernel.o
OX_OBJS := ppc_ksyms.o


O_OBJS := traps.o irq.o idle.o time.o process.o signal.o syscalls.o misc.o \
	  bitops.o ppc_htab.o setup.o ptrace.o align.o

ifdef CONFIG_PCI
O_OBJS += pci.o
endif
ifdef CONFIG_KGDB
O_OBJS += ppc-stub.o
endif

ifeq ($(CONFIG_MBX),y)
O_OBJS += mbx_setup.o mbx_pci.o softemu8xx.o
else
ifeq ($(CONFIG_APUS),y)
O_OBJS += prom.o openpic.o
else
O_OBJS += prep_time.o pmac_time.o chrp_time.o \
	  prep_setup.o pmac_setup.o pmac_support.o chrp_setup.o \
	  prep_pci.o pmac_pci.o chrp_pci.o \
	  residual.o prom.o openpic.o
endif
endif

ifdef SMP
O_OBJS += smp.o
endif

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

find_name : find_name.c
	$(HOSTCC) -o find_name find_name.c

checks: checks.c
	$(HOSTCC) -fno-builtin -I$(TOPDIR)/include -D__KERNEL__ -o checks checks.c
	./checks

include $(TOPDIR)/Rules.make
