# $Id: Makefile,v 1.41 1998/10/11 06:58:14 davem Exp $
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
O_OBJS   := process.o setup.o cpu.o idprom.o \
	    traps.o devices.o auxio.o ioport.o \
	    irq.o ptrace.o time.o sys_sparc.o signal.o \
	    unaligned.o sys_sunos32.o sunos_ioctl32.o \
	    central.o psycho.o
OX_OBJS  := sparc64_ksyms.o

ifdef CONFIG_PCI
  O_OBJS += ebus.o
endif

ifdef CONFIG_SMP
O_OBJS += smp.o trampoline.o
endif

ifdef CONFIG_SPARC32_COMPAT
  O_OBJS += sys32.o sys_sparc32.o signal32.o ioctl32.o
endif

ifdef CONFIG_BINFMT_ELF32
  O_OBJS += binfmt_elf32.o
endif

ifdef CONFIG_BINFMT_AOUT32
  O_OBJS += binfmt_aout32.o
endif

head.o: head.S ttable.S itlb_base.S dtlb_base.S dtlb_backend.S dtlb_prot.S \
	etrap.S rtrap.S winfixup.S entry.S
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -ansi -c $*.S -o $*.o

#
# This is just to get the dependencies...
#
binfmt_elf32.o: $(TOPDIR)/fs/binfmt_elf.c

ifneq ($(IS_EGCS),y)
  CMODEL_CFLAG := -mmedlow
else
  CMODEL_CFLAG := -mcmodel=medlow
endif

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
	#$(CC) -o check_asm check_asm.c
	# <hack> Until we can do this natively, a hack has to take place
	$(CC) $(CMODEL_CFLAG) -ffixed-g4 -S -o check_asm.s check_asm.c
	$(HOSTCC) -Wa,-Av9a -o check_asm check_asm.s
	@rm -f check_asm.s
	# </hack>
	./check_asm >> asm_offsets.h
	@rm -f check_asm check_asm.c
	@echo "" >> asm_offsets.h
	@echo "#else /* CONFIG_SMP */" >> asm_offsets.h
	@echo "" >> asm_offsets.h
	@echo "#ifndef SPIN_LOCK_DEBUG" >>asm_offsets.h
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
	#$(CC) -D__SMP__ -o check_asm check_asm.c
	# <hack> Until we can do this natively, a hack has to take place
	$(CC) -D__SMP__ $(CMODEL_CFLAG) -ffixed-g4 -S -o check_asm.s check_asm.c
	$(HOSTCC) -Wa,-Av9a -o check_asm check_asm.s
	@rm -f check_asm.s
	# </hack>
	./check_asm >> asm_offsets.h
	@rm -f check_asm check_asm.c
	@echo "" >> asm_offsets.h
	@echo "#else /* SPIN_LOCK_DEBUG */" >> asm_offsets.h
	@echo "" >> asm_offsets.h
	@echo "#include <linux/sched.h>" > tmp.c
	$(CC) -D__SMP__ -DSPIN_LOCK_DEBUG -E tmp.c -o tmp.i
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
	#$(CC) -D__SMP__ -DSPIN_LOCK_DEBUG -o check_asm check_asm.c
	# <hack> Until we can do this natively, a hack has to take place
	$(CC) -D__SMP__ -DSPIN_LOCK_DEBUG $(CMODEL_CFLAG) -ffixed-g4 -S -o check_asm.s check_asm.c
	$(HOSTCC) -Wa,-Av9a -o check_asm check_asm.s
	@rm -f check_asm.s
	# </hack>
	./check_asm >> asm_offsets.h
	@rm -f check_asm check_asm.c
	@echo "#endif /* SPIN_LOCK_DEBUG */" >> asm_offsets.h
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
