/* $Id: elf.h,v 1.7 1997/06/14 21:28:07 davem Exp $ */
#ifndef __ASM_SPARC64_ELF_H
#define __ASM_SPARC64_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>
#include <asm/processor.h>

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof (struct pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef unsigned long elf_fpregset_t;

/*
 * These are used to set parameters in the core dumps.
 */
#ifndef ELF_ARCH
#define ELF_ARCH		EM_SPARC64
#define ELF_CLASS		ELFCLASS64
#define ELF_DATA		ELFDATA2MSB;
#endif

#ifndef ELF_FLAGS_INIT
#define ELF_FLAGS_INIT current->tss.flags &= ~SPARC_FLAG_32BIT
#endif

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#ifndef elf_check_arch
#define elf_check_arch(x) ((x) == ELF_ARCH)	/* Might be EM_SPARC64 or EM_SPARC */
#endif

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	8192

#endif /* !(__ASM_SPARC64_ELF_H) */
