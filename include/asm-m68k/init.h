#ifndef _M68K_INIT_H
#define _M68K_INIT_H

#include <linux/config.h>

#ifndef CONFIG_KGDB

#define __init __attribute__ ((__section__ (".text.init")))
#define __initdata __attribute__ ((__section__ (".data.init")))
#define __initfunc(__arginit) \
	__arginit __init; \
	__arginit
/* For assembly routines */
#define __INIT		.section	".text.init",#alloc,#execinstr
#define __FINIT		.previous
#define __INITDATA	.section	".data.init",#alloc,#write

#define __cacheline_aligned __attribute__ \
		((__aligned__(16), __section__ (".data.cacheline_aligned")))

#else

/* gdb doesn't like it all if the code for one source file isn't together in
 * the executable, so we must avoid the .init sections :-( */
	
#define __init
#define __initdata
#define __initfunc(__arginit) __arginit
/* For assembly routines */
#define __INIT
#define __FINIT
#define __INITDATA
#define __cacheline_aligned __attribute__ ((__aligned__(16)))

#endif /* CONFIG_KGDB */

#endif
