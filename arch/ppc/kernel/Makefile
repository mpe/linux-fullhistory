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
OX_OBJS := ppc_ksyms.o setup.o


ifeq ($(CONFIG_4xx),y)
  KHEAD := head_4xx.o
else
  ifeq ($(CONFIG_8xx),y)
    KHEAD := head_8xx.o
  else
    KHEAD := head.o
  endif
endif

ifdef CONFIG_ALL_PPC
CONFIG_PMAC=y
CONFIG_PREP=y
CONFIG_CHRP=y
endif

O_OBJS := entry.o traps.o irq.o idle.o time.o process.o signal.o syscalls.o \
	  misc.o bitops.o ptrace.o align.o ppc_htab.o semaphore.o

ifndef CONFIG_8xx
O_OBJS += hashtable.o
endif

ifdef CONFIG_PCI
O_OBJS += pci.o
endif

ifdef CONFIG_KGDB
O_OBJS += ppc-stub.o
endif

ifdef CONFIG_TOTALMP
O_OBJS += totalmp.o
endif

ifdef CONFIG_PMAC_PBOOK
O_OBJS += sleep.o
endif

ifdef CONFIG_SMP
O_OBJS += smp.o
endif

ifeq ($(CONFIG_OAK),y)
  O_OBJS += oak_setup.o
endif

ifeq ($(CONFIG_8xx),y)
  O_OBJS += m8xx_setup.o ppc8xx_pic.o
  ifndef CONFIG_MATH_EMULATION
    O_OBJS += softemu8xx.o
  endif
  ifdef CONFIG_PCI
    O_OBJS += qspan_pci.c
  endif
  ifdef CONFIG_MBX
    O_OBJS += i8259.o
  endif
endif

ifeq ($(CONFIG_NVRAM),y)
  O_OBJS += pmac_nvram.o
endif
ifeq ($(CONFIG_6xx),y)
  O_OBJS += open_pic.o indirect_pci.o
endif
ifeq ($(CONFIG_APUS),y)
  O_OBJS += apus_setup.o
endif
ifeq ($(CONFIG_PMAC),y)
  O_OBJS += pmac_pic.o pmac_setup.o pmac_time.o feature.o pmac_pci.o prom.o
endif
ifeq ($(CONFIG_CHRP),y)
  O_OBJS += chrp_pci.o pmac_pci.o chrp_setup.o i8259.o \
	chrp_time.o pmac_time.o prom.o
endif
ifeq ($(CONFIG_PREP),y)
  O_OBJS += prep_pci.o i8259.o prep_setup.o prep_nvram.o prep_time.o residual.o
endif
ifeq ($(CONFIG_GEMINI),y)
  O_OBJS += gemini_prom.o gemini_pci.o gemini_setup.o
endif

all: $(KHEAD) kernel.o

head.o: head.S ppc_defs.h
head_4xx.o: head_4xx.S ppc_defs.h
head_8xx.o: head_8xx.S ppc_defs.h

ppc_defs.h: mk_defs.c ppc_defs.head \
		$(TOPDIR)/include/asm/mmu.h \
		$(TOPDIR)/include/asm/processor.h \
		$(TOPDIR)/include/asm/pgtable.h \
		$(TOPDIR)/include/asm/ptrace.h
	$(CC) $(CFLAGS) -S mk_defs.c
	cp ppc_defs.head ppc_defs.h
	grep '^#define' mk_defs.s >> ppc_defs.h
	rm mk_defs.s

find_name : find_name.c
	$(HOSTCC) $(HOSTCFLAGS) -o find_name find_name.c

checks: checks.c
	$(HOSTCC) -I$(HPATH) $(HOSTCFLAGS) -D__KERNEL__ -fno-builtin -o checks checks.c
	./checks

include $(TOPDIR)/Rules.make
