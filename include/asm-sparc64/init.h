#ifndef _SPARC64_INIT_H
#define _SPARC64_INIT_H

#ifndef __init
/* 'cause of buggy linker, we don't use this for now... */
#if 0 && !defined (MODULE)
#define __init __attribute__ ((__section__ (".text.init")))
#define __initdata __attribute__ ((__section__ (".data.init")))
#define __initfunc(__arginit) \
	__arginit __init; \
	__arginit
/* For assembly routines */
#define __INIT		.section	".text.init",#alloc,#execinstr
#define __FINIT	.previous
#define __INITDATA	.section	".data.init",#alloc,#write
#else
#define	__init
#define __initdata
#define __initfunc(__arginit) __arginit
/* For assembly routines */
#define __INIT
#define __FINIT
#define __INITDATA
#endif
#endif

#endif
