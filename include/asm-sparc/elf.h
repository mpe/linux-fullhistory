/* $Id: elf.h,v 1.11 1997/09/26 18:37:32 tdyas Exp $ */
#ifndef __ASMSPARC_ELF_H
#define __ASMSPARC_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>
#include <asm/mbus.h>

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
#define ELF_DATA	ELFDATA2MSB

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         (TASK_UNMAPPED_BASE + 0x1000000)

/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  This can NOT be done in userspace
   on Sparc.  */

/* Sun4c has none of the capabilities, most sun4m's have them all.
 * XXX This is gross, set some global variable at boot time. -DaveM
 */
#define ELF_HWCAP	((sparc_cpu_model == sun4c) ? 0 : \
			 (HWCAP_SPARC_FLUSH | HWCAP_SPARC_STBAR | \
			  HWCAP_SPARC_SWAP | \
			  ((srmmu_modtype != Cypress && \
			    srmmu_modtype != Cypress_vE && \
			    srmmu_modtype != Cypress_vD) ? \
			   HWCAP_SPARC_MULDIV : 0)))

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.  */

#define ELF_PLATFORM	(NULL)

#ifdef __KERNEL__
#define SET_PERSONALITY(ex, ibcs2) \
	current->personality = (ibcs2 ? PER_SVR4 : PER_LINUX)
#endif

#endif /* !(__ASMSPARC_ELF_H) */
