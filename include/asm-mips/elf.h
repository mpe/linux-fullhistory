#ifndef __ASM_MIPS_ELF_H
#define __ASM_MIPS_ELF_H

/* ELF register definitions */
#define ELF_NGREG	45
#define ELF_NFPREG	33

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x) == EM_MIPS || (x) == EM_MIPS_RS4_BE)

/*
 * These are used to set parameters in the core dumps.
 * FIXME(eric) I don't know what the correct endianness to use is.
 */
#define ELF_CLASS	ELFCLASS32
#ifdef __MIPSEB__
#define ELF_DATA       ELFDATA2MSB;
#elif __MIPSEL__
#define ELF_DATA       ELFDATA2LSB;
#endif
#define ELF_ARCH	EM_MIPS

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

#define ELF_CORE_COPY_REGS(_dest,_regs)				\
	memcpy((char *) &_dest, (char *) _regs,			\
	       sizeof(struct pt_regs));

/* See comments in asm-alpha/elf.h, this is the same thing
 * on the MIPS.
 */
#define ELF_PLAT_INIT(_r)	_r->regs[2] = 0;

#endif /* __ASM_MIPS_ELF_H */
