#
# Makefile for the Linux/SuperH kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.o:
	$(CC) $(AFLAGS) -traditional -c $< -o $*.o

O_TARGET := kernel.o
O_OBJS   := process.o signal.o entry.o traps.o irq.o irq_ipr.o \
            ptrace.o setup.o time.o sys_sh.o semaphore.o \
            irq_imask.o io.o
OX_OBJS  := sh_ksyms.o
MX_OBJS  :=

ifdef CONFIG_CF_ENABLER
O_OBJS += cf-enabler.o
endif

ifdef CONFIG_SH_GENERIC

O_OBJS += mach_se.o setup_se.o setup_cqreek.o io_se.o led_se.o \
	  mach_hp600.o io_hd64461.o \
	  mach_unknown.o io_unknown.o \
	  io_generic.o

else

ifdef CONFIG_SH_HP600
O_OBJS += mach_hp600.o io_hd64461.o io_generic.o
endif

ifdef CONFIG_SH_OVERDRIVE
O_OBJS += io_generic.o
endif

ifdef CONFIG_SH_SOLUTION_ENGINE
O_OBJS += mach_se.o setup_se.o io_se.o io_generic.o led_se.o
endif

ifdef CONFIG_SH_CQREEK
O_OBJS += setup_cqreek.o
endif

ifdef CONFIG_SH_UNKNOWN
O_OBJS += mach_unknown.o io_unknown.o io_generic.o
endif

endif

ifdef CONFIG_CPU_SH4
O_OBJS += fpu.o
endif

ifdef CONFIG_PCI
O_OBJS += pci-sh.o 
endif

ifneq ($(CONFIG_SH_GENERIC)$(CONFIG_HD64461),)
O_OBJS += setup_hd64461.o
endif

ifdef CONFIG_SH_STANDARD_BIOS
O_OBJS += sh_bios.o
endif

all: kernel.o head.o init_task.o

entry.o: entry.S

head.o: head.S

clean:

include $(TOPDIR)/Rules.make
