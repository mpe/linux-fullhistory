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

#endif
