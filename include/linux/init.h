#ifndef _LINUX_INIT_H
#define _LINUX_INIT_H

#include <linux/config.h>

/* These macros are used to mark some functions or 
 * initialized data (doesn't apply to uninitialized data)
 * as `initialization' functions. The kernel can take this
 * as hint that the function is used only during the initialization
 * phase and free up used memory resources after
 *
 * Usage:
 * For functions:
 * 
 * You should add __init immediately before the function name, like:
 *
 * static void __init initme(int x, int y)
 * {
 *    extern int z; z = x * y;
 * }
 *
 * If the function has a prototype somewhere, you can also add
 * __init between closing brace of the prototype and semicolon:
 *
 * extern int initialize_foobar_device(int, int, int) __init;
 *
 * For initialized data:
 * You should insert __initdata between the variable name and equal
 * sign followed by value, e.g.:
 *
 * static int init_variable __initdata = 0;
 * static char linux_logo[] __initdata = { 0x32, 0x36, ... };
 *
 * For initialized data not at file scope, i.e. within a function,
 * you should use __initlocaldata instead, due to a bug in GCC 2.7.
 */

#ifndef MODULE

#ifndef __ASSEMBLY__

/*
 * Used for initialization calls..
 */
typedef int (*initcall_t)(void);

extern initcall_t __initcall_start, __initcall_end;

#define __initcall(fn)								\
	static initcall_t __initcall_##fn __init_call = fn

/*
 * Used for kernel command line parameter setup
 */
struct kernel_param {
	const char *str;
	int (*setup_func)(char *);
};

extern struct kernel_param __setup_start, __setup_end;

#define __setup(str, fn)								\
	static char __setup_str_##fn[] __initdata = str;				\
	static struct kernel_param __setup_##fn __initsetup = { __setup_str_##fn, fn }

#endif /* __ASSEMBLY__ */

/*
 * Mark functions and data as being only used at initialization
 * or exit time.
 */
#define __init		__attribute__ ((__section__ (".text.init")))
#define __exit		__attribute__ ((unused, __section__(".text.exit")))
#define __initdata	__attribute__ ((__section__ (".data.init")))
#define __exitdata	__attribute__ ((unused, __section__ (".data.exit")))
#define __initsetup	__attribute__ ((unused,__section__ (".setup.init")))
#define __init_call	__attribute__ ((unused,__section__ (".initcall.init")))

/* For assembly routines */
#define __INIT		.section	".text.init","ax"
#define __FINIT		.previous
#define __INITDATA	.section	".data.init","aw"

#define module_init(x)	__initcall(x);
#define module_exit(x)	/* nothing */

#else

#define __init
#define __exit
#define __initdata
#define __exitdata
#define __initcall(fn)
/* For assembly routines */
#define __INIT
#define __FINIT
#define __INITDATA

/* Not sure what version aliases were introduced in, but certainly in 2.91.66.  */
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 91)
#define module_init(x)	int init_module(void) __attribute__((alias(#x)));
#define module_exit(x)	void cleanup_module(void) __attribute__((alias(#x)));
#else
#define module_init(x)	int init_module(void) { return x(); }
#define module_exit(x)	void cleanup_module(void) { x(); }
#endif

#define __setup(str,func) /* nothing */

#endif

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 8)
#define __initlocaldata  __initdata
#else
#define __initlocaldata
#endif

#ifdef CONFIG_HOTPLUG
#define __devinit
#define __devinitdata
#define __devexit
#define __devexitdata
#else
#define __devinit __init
#define __devinitdata __initdata
#define __devexit __exit
#define __devexitdata __exitdata
#endif

#endif /* _LINUX_INIT_H */
