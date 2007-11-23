/* $Id: elf.h,v 1.18 1998/09/09 05:36:08 davem Exp $ */
#ifndef __ASM_SPARC64_ELF_H
#define __ASM_SPARC64_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>
#include <asm/processor.h>

/*
 * These are used to set parameters in the core dumps.
 */
#ifndef ELF_ARCH
#define ELF_ARCH		EM_SPARCV9
#define ELF_CLASS		ELFCLASS64
#define ELF_DATA		ELFDATA2MSB

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof (struct pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct {
	unsigned long	pr_regs[32];
	unsigned long	pr_fsr;
	unsigned long	pr_gsr;
	unsigned long	pr_fprs;
} elf_fpregset_t;
#endif

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#ifndef elf_check_arch
#define elf_check_arch(x) ((x) == ELF_ARCH)	/* Might be EM_SPARCV9 or EM_SPARC */
#endif

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	8192

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#ifndef ELF_ET_DYN_BASE
#define ELF_ET_DYN_BASE         0x50000000000
#endif


/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  */

/* On Ultra, we support all of the v8 capabilities. */
#define ELF_HWCAP	(HWCAP_SPARC_FLUSH | HWCAP_SPARC_STBAR | \
			 HWCAP_SPARC_SWAP | HWCAP_SPARC_MULDIV | \
			 HWCAP_SPARC_V9)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.  */

#define ELF_PLATFORM	(NULL)

#ifdef __KERNEL__
#define SET_PERSONALITY(ex, ibcs2)				\
do {								\
	if ((ex).e_ident[EI_CLASS] == ELFCLASS32)		\
		current->tss.flags |= SPARC_FLAG_32BIT;		\
	else							\
		current->tss.flags &= ~SPARC_FLAG_32BIT;	\
								\
	if (ibcs2)						\
		current->personality = PER_SVR4;		\
	else if (current->personality != PER_LINUX32)		\
		current->personality = PER_LINUX;		\
} while (0)
#endif

#endif /* !(__ASM_SPARC64_ELF_H) */
