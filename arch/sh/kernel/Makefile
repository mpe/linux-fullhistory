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
O_OBJS   := process.o signal.o entry.o traps.o irq.o irq_onchip.o \
            ptrace.o setup.o time.o sys_sh.o semaphore.o pci-sh.o \
	    irq_imask.o
OX_OBJS  := sh_ksyms.o
MX_OBJS  :=

ifdef CONFIG_CF_ENABLER
O_OBJS += cf-enabler.o
endif

ifdef CONFIG_CPU_SH4
O_OBJS += fpu.o
endif

all: kernel.o head.o init_task.o

entry.o: entry.S

head.o: head.S
	$(CC) $(AFLAGS) -traditional -c $*.S -o $*.o

clean:

include $(TOPDIR)/Rules.make
