#ifndef __ALPHA_IO_H
#define __ALPHA_IO_H

#include <linux/config.h>

#include <asm/system.h>

/*
 * The hae (hardware address extension) register is used to
 * access high IO addresses. To avoid doing an external cycle
 * every time we need to set the hae, we have a hae cache in
 * memory. The kernel entry code makes sure that the hae is
 * preserved across interrupts, so it is safe to set the hae
 * once and then depend on it staying the same in kernel code.
 */
extern struct hae {
	unsigned long	cache;
	unsigned long	*reg;
} hae;

/*
 * We try to avoid hae updates (thus the cache), but when we
 * do need to update the hae, we need to do it atomically, so
 * that any interrupts wouldn't get confused with the hae
 * register not being up-to-date with respect to the hardware
 * value.
 */
extern inline void set_hae(unsigned long new_hae)
{
	unsigned long ipl = swpipl(7);
	hae.cache = new_hae;
	*hae.reg = new_hae;
	mb();
	setipl(ipl);
}

/*
 * Virtual -> physical identity mapping starts at this offset
 */
#define IDENT_ADDR	(0xfffffc0000000000UL)

/*
 * Change virtual addresses to physical addresses and vv.
 */
extern inline unsigned long virt_to_phys(void * address)
{
	return 0xffffffffUL & (unsigned long) address;
}

extern inline void * phys_to_virt(unsigned long address)
{
	return (void *) (address + IDENT_ADDR);
}

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
