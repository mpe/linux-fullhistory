/* $Id: elf.h,v 1.3 1996/04/22 15:48:48 miguel Exp $ */
#ifndef __ASMSPARC_ELF_H
#define __ASMSPARC_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof (struct pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef unsigned long elf_fpregset_t;

#define elf_check_arch(x) ((x) == EM_SPARC)
#define ELF_ARCH EM_SPARC
#endif
