#ifndef __ASMARM_ELF_H
#define __ASMARM_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>
#include <asm/procinfo.h>

typedef unsigned long elf_greg_t;

#define EM_ARM	40

#define ELF_NGREG (sizeof (struct pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct { void *null; } elf_fpregset_t;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ( ((x) == EM_ARM) )

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB;
#define ELF_ARCH	EM_ARM

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	32768

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE	(2 * TASK_SIZE / 3)

/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports. */

#define ELF_HWCAP	(armidlist[armidindex].hwcap)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo. */

/* For now we just provide a fairly general string that describes the
   processor family.  This could be made more specific later if someone
   implemented optimisations that require it.  26-bit CPUs give you
   "v1l" for ARM2 (no SWP) and "v2l" for anything else (ARM1 isn't
   supported).  32-bit CPUs give you "v3[lb]" for anything based on an
   ARM6 or ARM7 core and "armv4[lb]" for anything based on a StrongARM-1
   core.  */

#define ELF_PLATFORM_SIZE 8
extern char elf_platform[];
#define ELF_PLATFORM	(elf_platform)

#ifdef __KERNEL__
#define SET_PERSONALITY(ex,ibcs2) \
	current->personality = PER_LINUX_32BIT
#endif

#define R_ARM_NONE	(0)
#define R_ARM_32	(1)	/* => ld 32 */
#define R_ARM_PC26	(2)	/* => ld b/bl branches */
#define R_ARM_PC32	(3)
#define R_ARM_GOT32	(4)	/* -> object relocation into GOT */
#define R_ARM_PLT32	(5)
#define R_ARM_COPY	(6)	/* => dlink copy object */
#define R_ARM_GLOB_DAT	(7)	/* => dlink 32bit absolute address for .got */
#define R_ARM_JUMP_SLOT	(8)	/* => dlink 32bit absolute address for .got.plt */
#define R_ARM_RELATIVE	(9)	/* => ld resolved 32bit absolute address requiring load address adjustment */
#define R_ARM_GOTOFF	(10)	/* => ld calculates offset of data from base of GOT */
#define R_ARM_GOTPC	(11)	/* => ld 32-bit relative offset */

#endif
