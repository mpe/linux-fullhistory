#ifndef __ALPHA_IO_H
#define __ALPHA_IO_H

#include <linux/config.h>

#ifndef mb
#define mb() __asm__ __volatile__("mb": : :"memory")
#endif

/*
 * There are different version of the alpha motherboards: the
 * "interesting" (read: slightly braindead) Jensen type hardware
 * and the PCI version
 */
#ifdef CONFIG_PCI
#include <asm/lca.h>		/* get chip-specific definitions */
#else
#include <asm/jensen.h>
#endif

#endif
