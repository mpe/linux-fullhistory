#ifndef _SPARC_INIT_H
#define _SPARC_INIT_H

#if (defined (__svr4__) || defined (__ELF__))
#define __init __attribute__ ((__section__ (".text.init")))
#define __initdata __attribute__ ((__section__ (".data.init")))
#define __cacheline_aligned __attribute__ \
			((__section__ (".data.cacheline_aligned")))
/* For assembly routines */
#define __INIT		.section	".text.init",#alloc,#execinstr
#define __FINIT	.previous
#define __INITDATA	.section	".data.init",#alloc,#write
#else
#define	__init
#define __initdata
#define __cacheline_aligned
/* For assembly routines */
#define __INIT
#define __FINIT
#define __INITDATA
#endif

#endif
