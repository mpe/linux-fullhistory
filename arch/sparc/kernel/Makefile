# $Id: Makefile,v 1.48 1998/09/21 05:04:46 jj Exp $
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.s:
	$(CPP) -D__ASSEMBLY__ $(AFLAGS) -ansi $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c $< -o $*.o

all: kernel.o head.o init_task.o

O_TARGET := kernel.o
IRQ_OBJS := irq.o sun4m_irq.o sun4c_irq.o sun4d_irq.o
O_OBJS   := entry.o wof.o wuf.o etrap.o rtrap.o traps.o ${IRQ_OBJS} \
	    process.o signal.o ioport.o setup.o idprom.o \
	    sys_sparc.o sunos_asm.o sparc-stub.o systbls.o sys_sunos.o  \
	    sunos_ioctl.o time.o windows.o cpu.o devices.o \
	    sclow.o solaris.o tadpole.o tick14.o ptrace.o sys_solaris.o \
	    unaligned.o muldiv.o pcic.o

OX_OBJS  := sparc_ksyms.o

ifdef CONFIG_SUN4
O_OBJS += sun4setup.o
endif

ifdef CONFIG_SMP
O_OBJS += trampoline.o smp.o sun4m_smp.o sun4d_smp.o
endif

ifdef CONFIG_SUN_AUXIO
O_OBJS += auxio.o
endif

ifdef CONFIG_PCI
O_OBJS += ebus.o
endif

head.o: head.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c $*.S -o $*.o

check_asm: dummy
	@echo "/* Automatically generated. Do not edit. */" > asm_offsets.h
	@echo "#ifndef __ASM_OFFSETS_H__" >> asm_offsets.h
	@echo "#define __ASM_OFFSETS_H__" >> asm_offsets.h
	@echo "" >> asm_offsets.h
	@echo "#include <linux/config.h>" >> asm_offsets.h
	@echo "" >> asm_offsets.h
	@echo "#ifndef CONFIG_SMP" >> asm_offsets.h
	@echo "" >> asm_offsets.h
	@echo "#include <linux/config.h>" > tmp.c
	@echo "#undef CONFIG_SMP" >> tmp.c
	@echo "#include <linux/sched.h>" >> tmp.c
	$(CC) -E tmp.c -o tmp.i
	@echo "/* Automatically generated. Do not edit. */" > check_asm.c
	@echo "#include <linux/config.h>" >> check_asm.c
	@echo "#undef CONFIG_SMP" >> check_asm.c
	@echo "#include <linux/sched.h>" >> check_asm.c
	@echo 'struct task_struct _task;' >> check_asm.c
	@echo 'struct mm_struct _mm;' >> check_asm.c
	@echo 'struct thread_struct _thread;' >> check_asm.c
	@echo 'int main(void) {' >> check_asm.c
	$(SH) ./check_asm.sh task tmp.i check_asm.c
	$(SH) ./check_asm.sh mm tmp.i check_asm.c
	$(SH) ./check_asm.sh thread tmp.i check_asm.c
	@echo 'return 0; }' >> check_asm.c
	@rm -f tmp.[ci]
	$(CC) -o check_asm check_asm.c
	./check_asm >> asm_offsets.h
	@rm -f check_asm check_asm.c
	@echo "" >> asm_offsets.h
	@echo "#else /* CONFIG_SMP */" >> asm_offsets.h
	@echo "" >> asm_offsets.h
	@echo "#include <linux/config.h>" > tmp.c
	@echo "#undef CONFIG_SMP" >> tmp.c
	@echo "#define CONFIG_SMP 1" >> tmp.c
	@echo "#include <linux/sched.h>" >> tmp.c
	$(CC) -D__SMP__ -E tmp.c -o tmp.i
	@echo "/* Automatically generated. Do not edit. */" > check_asm.c
	@echo "#include <linux/config.h>" >> check_asm.c
	@echo "#undef CONFIG_SMP" >> check_asm.c
	@echo "#define CONFIG_SMP 1" >> check_asm.c
	@echo "#include <linux/sched.h>" >> check_asm.c
	@echo 'struct task_struct _task;' >> check_asm.c
	@echo 'struct mm_struct _mm;' >> check_asm.c
	@echo 'struct thread_struct _thread;' >> check_asm.c
	@echo 'int main(void) {' >> check_asm.c
	$(SH) ./check_asm.sh task tmp.i check_asm.c
	$(SH) ./check_asm.sh mm tmp.i check_asm.c
	$(SH) ./check_asm.sh thread tmp.i check_asm.c
	@echo 'return 0; }' >> check_asm.c
	@rm -f tmp.[ci]
	$(CC) -D__SMP__ -o check_asm check_asm.c
	./check_asm >> asm_offsets.h
	@rm -f check_asm check_asm.c
	@echo "" >> asm_offsets.h
	@echo "#endif /* CONFIG_SMP */" >> asm_offsets.h
	@echo "" >> asm_offsets.h
	@echo "#endif /* __ASM_OFFSETS_H__ */" >> asm_offsets.h
	@if test -r $(HPATH)/asm/asm_offsets.h; then \
	  if cmp -s asm_offsets.h $(HPATH)/asm/asm_offsets.h; then \
	    echo $(HPATH)/asm/asm_offsets.h is unchanged; \
	    rm -f asm_offsets.h; \
	  else \
	    mv -f asm_offsets.h $(HPATH)/asm/asm_offsets.h; \
	  fi; \
	else \
	  mv -f asm_offsets.h $(HPATH)/asm/asm_offsets.h; \
	fi


include $(TOPDIR)/Rules.make
