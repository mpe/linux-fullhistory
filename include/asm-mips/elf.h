#ifndef __ASM_MIPS_ELF_H
#define __ASM_MIPS_ELF_H

/*
 * ELF register definitions
 * This is "make it compile" stuff!
 */
#define ELF_NGREG	32
#define ELF_NFPREG	32

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x) == EM_MIPS)

/*
 * These are used to set parameters in the core dumps.
 * FIXME(eric) I don't know what the correct endianness to use is.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2MSB;
#define ELF_ARCH	EM_MIPS

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

#endif /* __ASM_MIPS_ELF_H */
