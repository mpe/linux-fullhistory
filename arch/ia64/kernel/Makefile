#
# Makefile for the linux kernel.
#

.S.s:
	$(CPP) $(AFLAGS) -o $*.s $<
.S.o:
	$(CC) $(AFLAGS) -c -o $*.o $<

all: kernel.o head.o init_task.o

O_TARGET := kernel.o
O_OBJS	 := acpi.o entry.o gate.o efi.o efi_stub.o irq.o irq_ia64.o irq_sapic.o ivt.o	\
	    pal.o pci-dma.o process.o perfmon.o ptrace.o sal.o semaphore.o setup.o	\
	    signal.o sys_ia64.o traps.o time.o unaligned.o unwind.o
OX_OBJS  := ia64_ksyms.o

ifdef CONFIG_IA64_GENERIC
O_OBJS	+= machvec.o
endif

ifdef CONFIG_IA64_PALINFO
O_OBJS	+= palinfo.o
endif

ifdef CONFIG_PCI
O_OBJS	+= pci.o
endif

ifdef CONFIG_SMP
O_OBJS	+= smp.o
endif

ifdef CONFIG_IA64_MCA
O_OBJS	+= mca.o mca_asm.o
endif

ifdef CONFIG_IA64_BRL_EMU
O_OBJS  += brl_emu.o
endif

clean::

include $(TOPDIR)/Rules.make
