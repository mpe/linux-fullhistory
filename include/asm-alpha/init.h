#ifndef _ALPHA_INIT_H
#define _ALPHA_INIT_H

#define __init __attribute__ ((__section__ (".text.init")))
#define __initdata __attribute__ ((__section__ (".data.init")))
#define __initfunc(__arginit) \
	__arginit __init; \
	__arginit

#if __GNUC__ >= 2 && __GNUC_MINOR__ >= 8
#define __initlocaldata  __initdata
#else
#define __initlocaldata
#endif

/* For assembly routines */
#define __INIT		.section	.text.init,"ax"
#define __FINIT		.previous
#define __INITDATA	.section	.data.init,"a"

#endif
