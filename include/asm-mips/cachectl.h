/*
 * include/asm-mips/cachectl.h
 *
 * Written by Ralf Baechle,
 * Copyright (C) 1994 by Waldorf GMBH
 */
#ifndef	__ASM_MIPS_CACHECTL
#define	__ASM_MIPS_CACHECTL

/*
 * cachectl.h -- defines for MIPS cache control system calls
 */

/*
 * Options for cacheflush system call
 */
#define	ICACHE	(1<<0)		/* flush instruction cache        */
#define	DCACHE	(1<<1)		/* writeback and flush data cache */
#define	BCACHE	(ICACHE|DCACHE)	/* flush both caches              */

#ifdef __KERNEL__
#define CACHELINES      512	/* number of cachelines (kludgy)  */

/*
 * Cache Operations - for use by assembler code
 */
#define Index_Invalidate_I      0x00
#define Index_Writeback_Inv_D   0x01
#define Index_Load_Tag_D        0x05

#ifndef __LANGUAGE_ASSEMBLY__

extern int sys_cacheflush(void *addr, int nbytes, int cache);

#endif /* !__LANGUAGE_ASSEMBLY__ */
#endif   /* __KERNEL__ */
#endif	/* __ASM_MIPS_CACHECTL */
