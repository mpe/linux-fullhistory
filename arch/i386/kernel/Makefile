#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $< -o $*.o

all: kernel.o head.o init_task.o

O_TARGET := kernel.o
O_OBJS   := process.o signal.o entry.o traps.o irq.o vm86.o \
            ptrace.o ioport.o ldt.o setup.o time.o sys_i386.o
OX_OBJS  := i386_ksyms.o
MX_OBJS  :=

ifdef CONFIG_PCI
O_OBJS += bios32.o
endif

ifdef CONFIG_MCA
O_OBJS += mca.o
endif

ifeq ($(CONFIG_MTRR),y)
OX_OBJS += mtrr.o
else
  ifeq ($(CONFIG_MTRR),m)
  MX_OBJS += mtrr.o
  endif
endif

ifdef CONFIG_APM
OX_OBJS += apm.o
endif

ifdef CONFIG_SMP
O_OBJS += smp.o trampoline.o
endif

ifdef CONFIG_X86_IO_APIC
O_OBJS += io_apic.o
endif

ifdef CONFIG_X86_VISWS_APIC
O_OBJS += visws_apic.o
endif

head.o: head.S $(TOPDIR)/include/linux/tasks.h
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $*.S -o $*.o

include $(TOPDIR)/Rules.make
