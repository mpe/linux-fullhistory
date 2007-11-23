#ifndef __ASMm68k_ELF_H
#define __ASMm68k_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>

typedef unsigned long elf_greg_t;

#define ELF_NGREG 20 /* d1-d7/a0-a6/d0/usp/orig_d0/sr/pc/fmtvec */
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct user_m68kfp_struct elf_fpregset_t;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x) == EM_68K)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2MSB;
#define ELF_ARCH	EM_68K

	/* For SVR4/m68k the function pointer to be registered with
	   `atexit' is passed in %a1.  Although my copy of the ABI has
	   no such statement, it is actually used on ASV.  */
#define ELF_PLAT_INIT(_r)	_r->a1 = 0

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

#define ELF_CORE_COPY_REGS(pr_reg, regs)				\
	/* Bleech. */							\
	pr_reg[0] = regs->d1;						\
	pr_reg[1] = regs->d2;						\
	pr_reg[2] = regs->d3;						\
	pr_reg[3] = regs->d4;						\
	pr_reg[4] = regs->d5;						\
	pr_reg[7] = regs->a0;						\
	pr_reg[8] = regs->a1;						\
	pr_reg[14] = regs->d0;						\
	pr_reg[15] = rdusp();						\
	pr_reg[16] = 0; /* orig_d0 */					\
	pr_reg[17] = regs->sr;						\
	pr_reg[18] = regs->pc;						\
	{								\
	  struct switch_stack *sw = ((struct switch_stack *)regs) - 1;	\
	  pr_reg[5] = sw->d6;						\
	  pr_reg[6] = sw->d7;						\
	  pr_reg[9] = sw->a2;						\
	  pr_reg[10] = sw->a3;						\
	  pr_reg[11] = sw->a4;						\
	  pr_reg[12] = sw->a5;						\
	  pr_reg[13] = sw->a6;						\
	}

#endif
