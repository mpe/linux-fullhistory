#ifndef _ASMARM_INIT_H
#define _ASMARM_INIT_H

/* C routines */

#ifdef CONFIG_BINUTILS_NEW

#define __init __attribute__ ((__section__ (".text.init")))
#define __initfunc(__arginit) \
	__arginit __init; \
	__arginit

#else

#define __init
#define __initfunc(__arginit) __arginit

#endif

#define __initdata __attribute__ ((__section__ (".data.init")))

/* Assembly routines */
#define __INIT		.section	".text.init",@alloc,@execinstr
#define __INITDATA	.section	".data.init",@alloc,@write
#define __FINIT	.previous

#endif
