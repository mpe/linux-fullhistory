#ifndef __PPC_ELF_H
#define __PPC_ELF_H

/*
 * ELF register definitions..
 */

#define ELF_NGREG	32
#define ELF_NFPREG	32

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

#define elf_check_arch(x) ((x) == EM_PPC)

/*
 * These are used to set parameters in the core dumps.
 * FIXME(eric) I don't know what the correct endianness to use is.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB;
#define ELF_ARCH	EM_PPC

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

#endif
