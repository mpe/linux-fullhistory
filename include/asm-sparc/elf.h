/* $Id: elf.h,v 1.7 1997/08/29 17:04:34 richard Exp $ */
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

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x) == EM_SPARC)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_ARCH	EM_SPARC
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2MSB;

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         (TASK_UNMAPPED_BASE + 0x1000000)


#endif /* !(__ASMSPARC_ELF_H) */
