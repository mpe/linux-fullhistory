#
# Makefile for the Linux/SuperH kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $< -o $*.o

O_TARGET := kernel.o
O_OBJS   := process.o signal.o entry.o traps.o irq.o irq_onchip.o \
            ptrace.o setup.o time.o sys_sh.o test-img.o semaphore.o
OX_OBJS  := sh_ksyms.o
MX_OBJS  :=

all: kernel.o head.o init_task.o

entry.o: entry.S

head.o: head.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $*.S -o $*.o

clean:

include $(TOPDIR)/Rules.make
