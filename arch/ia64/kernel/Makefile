#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.s:
	$(CPP) $(AFLAGS) -o $*.s $<
.S.o:
	$(CC) $(AFLAGS) -c -o $*.o $<

all: kernel.o head.o init_task.o

O_TARGET := kernel.o
O_OBJS	 := acpi.o entry.o gate.o efi.o efi_stub.o irq.o irq_ia64.o irq_internal.o ivt.o \
	    pal.o pci-dma.o process.o perfmon.o ptrace.o sal.o sal_stub.o semaphore.o setup.o signal.o \
	    sys_ia64.o traps.o time.o unaligned.o unwind.o
#O_OBJS   := fpreg.o
#OX_OBJS  := ia64_ksyms.o

ifeq ($(CONFIG_IA64_GENERIC),y)
O_OBJS	+= machvec.o
endif

ifdef CONFIG_PCI
O_OBJS	+= pci.o
endif

ifdef CONFIG_SMP
O_OBJS	+= smp.o irq_lock.o
endif

ifeq ($(CONFIG_MCA),y)
O_OBJS	+= mca.o mca_asm.o
endif

clean::

include $(TOPDIR)/Rules.make
