# $Id: Makefile,v 1.39 1997/04/01 02:21:44 davem Exp $
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

CHECKASM_CC	=	$(CC) -D__SMP__

else

.S.s:
	$(CPP) -D__ASSEMBLY__ -ansi $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ -ansi -c $< -o $*.o

CHECKASM_CC	=	$(CC)
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

check_asm: dummy
	@echo "#include <linux/sched.h>" > tmp.c
	$(CHECKASM_CC) -E tmp.c -o tmp.i
	@echo "/* Automatically generated. Do not edit. */" > check_asm.c; echo "#include <linux/sched.h>" >> check_asm.c; echo 'struct task_struct _task; struct mm_struct _mm; struct thread_struct _thread; int main(void) { printf ("/* Automatically generated. Do not edit. */\n#ifndef __ASM_OFFSETS_H__\n#define __ASM_OFFSETS_H__\n\n");' >> check_asm.c
	$(SH) ./check_asm.sh task tmp.i check_asm.c
	$(SH) ./check_asm.sh mm tmp.i check_asm.c
	$(SH) ./check_asm.sh thread tmp.i check_asm.c
	@echo 'printf ("\n#endif /* __ASM_OFFSETS_H__ */\n"); return 0; }' >> check_asm.c
	@rm -f tmp.[ci]
	$(CHECKASM_CC) -o check_asm check_asm.c
	./check_asm > asm_offsets.h
	@if test -r $(HPATH)/asm/asm_offsets.h; then if cmp -s asm_offsets.h $(HPATH)/asm/asm_offsets.h; then echo $(HPATH)/asm/asm_offsets.h is unchanged; rm -f asm_offsets.h; else mv -f asm_offsets.h $(HPATH)/asm/asm_offsets.h; fi; else mv -f asm_offsets.h $(HPATH)/asm/asm_offsets.h; fi
	@rm -f check_asm check_asm.c


include $(TOPDIR)/Rules.make
