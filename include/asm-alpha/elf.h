#ifndef __ASMaxp_ELF_H
#define __ASMaxp_ELF_H

/*
 * ELF register definitions..
 */

/* 
 * Note: ELF_NGREG must ben the same as EF_SIZE/8.
 */
#define ELF_NGREG	33
#define ELF_NFPREG	32

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x) == EM_ALPHA)

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS64
#define ELF_DATA	ELFDATA2LSB;
#define ELF_ARCH	EM_ALPHA

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	8192

#define ELF_CORE_COPY_REGS(_dest,_regs)			\
{ struct user _dump;					\
	dump_thread(_regs, &_dump);			\
	memcpy((char *) &_dest, (char *) &_dump.regs,	\
	       sizeof(elf_gregset_t)); }

#endif
