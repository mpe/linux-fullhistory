# $Id: Makefile,v 1.37 1997/01/06 06:52:15 davem Exp $
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

ifdef SMP

.S.s:
	$(CPP) -D__ASSEMBLY__ $(AFLAGS) -ansi $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c $< -o $*.o


else

.S.s:
	$(CPP) -D__ASSEMBLY__ -ansi $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ -ansi -c $< -o $*.o


endif

all: kernel.o head.o

O_TARGET := kernel.o
IRQ_OBJS := irq.o sun4m_irq.o sun4c_irq.o
O_OBJS   := entry.o wof.o wuf.o etrap.o rtrap.o traps.o ${IRQ_OBJS} \
	    process.o signal.o ioport.o setup.o idprom.o \
	    sys_sparc.o sunos_asm.o sparc-stub.o systbls.o sys_sunos.o  \
	    sunos_ioctl.o time.o windows.o cpu.o devices.o \
	    sclow.o solaris.o tadpole.o tick14.o ptrace.o sys_solaris.o \
	    unaligned.o muldiv.o

OX_OBJS  := sparc_ksyms.o

ifdef SMP
O_OBJS += trampoline.o smp.o
endif

ifdef CONFIG_SUN_AUXIO
O_OBJS += auxio.o
endif

ifdef SMP

head.o: head.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c $*.S -o $*.o

else

head.o: head.S
	$(CC) -D__ASSEMBLY__ -ansi -c $*.S -o $*.o

endif

include $(TOPDIR)/Rules.make
