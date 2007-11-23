#ifndef _ALPHA_INIT_H
#define _ALPHA_INIT_H

#ifndef MODULE
#define __init __attribute__ ((__section__ (".text.init")))
#define __initdata __attribute__ ((__section__ (".data.init")))
#define __initfunc(__arginit) \
	__arginit __init; \
	__arginit

/* For assembly routines */
#define __INIT		.section	.text.init,"ax"
#define __FINIT		.previous
#define __INITDATA	.section	.data.init,"a"
#endif

#define __cacheline_aligned __attribute__((__aligned__(32)))

#endif
