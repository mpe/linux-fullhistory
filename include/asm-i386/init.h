#ifndef _I386_INIT_H
#define _I386_INIT_H

typedef int (*initcall_t)(void);

extern initcall_t __initcall_start, __initcall_end;

struct kernel_param {
	const char *str;
	int (*setup_func)(char *);
};

extern struct kernel_param __setup_start, __setup_end;

/* Used for initialization calls.. */
#define __initcall(fn)	\
	static __attribute__ ((unused,__section__ (".initcall.init"))) initcall_t __initcall_##fn = fn

/* Used for kernel command line parameter setup */
#define __setup(str, fn) \
	static __attribute__ ((unused,__section__ (".setup.init"))) struct kernel_param __setup_##fn = { str, fn }


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
