# $Id: Makefile,v 1.15 1997/04/10 07:53:30 davem Exp $
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.s:
	$(CPP) -D__ASSEMBLY__ -ansi $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ -ansi -c $< -o $*.o

all: kernel.o head.o

O_TARGET := kernel.o
O_OBJS   := etrap.o rtrap.o hack.o process.o setup.o cpu.o idprom.o \
	    systbls.o traps.o entry.o devices.o auxio.o ioport.o \
	    irq.o time.o sys_sparc.o sys_sparc32.o signal32.o
OX_OBJS  := sparc64_ksyms.o

ifdef CONFIG_BINFMT_ELF32
  O_OBJS += sparcelf32.o
endif

head.o: head.S ttable.S itlb_miss.S dtlb_miss.S dtlb_prot.S
	$(CC) -D__ASSEMBLY__ -ansi -c $*.S -o $*.o

check_asm: dummy
	@echo "#include <linux/sched.h>" > tmp.c
	$(CC) -E tmp.c -o tmp.i
	@echo "/* Automatically generated. Do not edit. */" > check_asm.c; echo "#include <linux/sched.h>" >> check_asm.c; echo 'struct task_struct _task; struct mm_struct _mm; struct thread_struct _thread; int main(void) { printf ("/* Automatically generated. Do not edit. */\n#ifndef __ASM_OFFSETS_H__\n#define __ASM_OFFSETS_H__\n\n");' >> check_asm.c
	$(SH) ./check_asm.sh task tmp.i check_asm.c
	$(SH) ./check_asm.sh mm tmp.i check_asm.c
	$(SH) ./check_asm.sh thread tmp.i check_asm.c
	@echo 'printf ("\n#endif /* __ASM_OFFSETS_H__ */\n"); return 0; }' >> check_asm.c
	@rm -f tmp.[ci]
	#$(CC) -o check_asm check_asm.c
	# <hack> Until we can do this natively, a hack has to take place
	$(CC) -mmedlow -S -o check_asm.s check_asm.c
	$(HOSTCC) -o check_asm check_asm.s
	@rm -f check_asm.s
	# </hack>
	./check_asm > asm_offsets.h
	@if test -r $(HPATH)/asm/asm_offsets.h; then if cmp -s asm_offsets.h $(HPATH)/asm/asm_offsets.h; then echo $(HPATH)/asm/asm_offsets.h is unchanged; rm -f asm_offsets.h; else mv -f asm_offsets.h $(HPATH)/asm/asm_offsets.h; fi; else mv -f asm_offsets.h $(HPATH)/asm/asm_offsets.h; fi
	@rm -f check_asm check_asm.c


include $(TOPDIR)/Rules.make
