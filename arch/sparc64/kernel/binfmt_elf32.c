/* binfmt_elf32.c: Support 32-bit Sparc ELF binaries on Ultra.
 *
 */

#define ELF_ARCH		EM_SPARC
#define ELF_CLASS		ELFCLASS32
#define ELF_DATA		ELFDATA2MSB;

#define elf_check_arch(x)	(((x) == EM_SPARC) || ((x) == EM_SPARC32PLUS))

#include <asm/processor.h>
#include <linux/module.h>
#include <linux/config.h>

#define elf_addr_t	u32
#define elf_caddr_t	u32
#undef start_thread
#define start_thread start_thread32
#define init_elf_binfmt init_elf32_binfmt
#undef CONFIG_BINFMT_ELF
#ifdef CONFIG_BINFMT_ELF32
#define CONFIG_BINFMT_ELF CONFIG_BINFMT_ELF32
#endif
#undef CONFIG_BINFMT_ELF_MODULE
#ifdef CONFIG_BINFMT_ELF32_MODULE
#define CONFIG_BINFMT_ELF_MODULE CONFIG_BINFMT_ELF32_MODULE
#endif
#define ELF_FLAGS_INIT	current->tss.flags |= SPARC_FLAG_32BIT

MODULE_DESCRIPTION("Binary format loader for compatibility with 32bit SparcLinux binaries on the Ultra");
MODULE_AUTHOR("Eric Youngdale, David S. Miller, Jakub Jelinek");

#undef MODULE_DESCRIPTION
#undef MODULE_AUTHOR

#include "../../../fs/binfmt_elf.c"
