/* $Id: elf.h,v 1.8 1997/08/21 18:09:07 richard Exp $ */
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
#define ELF_DATA		ELFDATA2MSB
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

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#ifndef ELF_ET_DYN_BASE
#define ELF_ET_DYN_BASE         (2 * TASK_SIZE / 3)
#endif

#endif /* !(__ASM_SPARC64_ELF_H) */
