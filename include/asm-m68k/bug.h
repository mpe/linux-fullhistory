#ifndef _M68K_BUG_H
#define _M68K_BUG_H

#include <linux/config.h>

#ifdef CONFIG_DEBUG_BUGVERBOSE
#ifndef CONFIG_SUN3
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	asm volatile("illegal"); \
} while (0)
#else
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	panic("BUG!"); \
} while (0)
#endif
#else
#define BUG() do { \
	asm volatile("illegal"); \
} while (0)
#endif

#define HAVE_ARCH_BUG
#include <asm-generic/bug.h>

#endif
