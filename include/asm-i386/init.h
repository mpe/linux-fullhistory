#ifndef _I386_INIT_H
#define _I386_INIT_H

#define __init __attribute__ ((__section__ (".text.init")))
#define __initdata __attribute__ ((__section__ (".data.init")))
#define __initfunc(__arginit) \
	__arginit __init; \
	__arginit
/* For assembly routines */
#define __INIT		.section	".text.init",#alloc,#execinstr
#define __FINIT	.previous
#define __INITDATA	.section	".data.init",#alloc,#write

#define __cacheline_aligned __attribute__ \
			 ((__section__ (".data.cacheline_aligned")))

#endif
