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

#define __cacheline_aligned __attribute__((__aligned__(32)))

/*
 * Used for initialization calls.
 */

typedef int (*initcall_t)(void);

extern initcall_t __initcall_start, __initcall_end;

#define __initcall(fn)							\
	static __attribute__ ((unused, __section__ (".initcall.init")))	\
	  initcall_t __initcall_##fn = fn

/*
 * Used for kernel command line parameter setup.
 */

struct kernel_param {
	const char *str;
	int (*setup_func)(char *);
};

extern struct kernel_param __setup_start, __setup_end;

#define __setup(str, fn)						\
	static __attribute__ ((__section__ (".data.init")))		\
	  char __setup_str_##fn[] = str;				\
	static __attribute__ ((unused, __section__ (".setup.init")))	\
	  struct kernel_param __setup_##fn = { __setup_str_##fn, fn }

#endif /* MODULE */
#endif /* _ALPHA_INIT_H */
