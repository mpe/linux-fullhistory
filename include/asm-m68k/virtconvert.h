#ifndef __VIRT_CONVERT__
#define __VIRT_CONVERT__

/*
 * Macros used for converting between virtual and physical mappings.
 */

#ifdef __KERNEL__

#include <linux/config.h>
#include <asm/setup.h>

#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#endif

/*
 * Change virtual addresses to physical addresses and vv.
 */
extern unsigned long mm_vtop(unsigned long addr) __attribute__ ((const));
extern unsigned long mm_vtop_fallback (unsigned long) __attribute__ ((const));
extern unsigned long mm_ptov(unsigned long addr) __attribute__ ((const));

#ifdef CONFIG_SINGLE_MEMORY_CHUNK
extern inline unsigned long virt_to_phys(volatile void * address)
{
	unsigned long voff = (unsigned long) address;

	if (voff < m68k_memory[0].size)
		return m68k_memory[0].addr + voff;
	else
		return mm_vtop_fallback(voff);
}

extern inline void * phys_to_virt(unsigned long paddr)
{
	unsigned long base = m68k_memory[0].addr;

	if ((paddr >=  base) && (paddr < (base + m68k_memory[0].size)))
		return (void *)(paddr - base);
#ifdef CONFIG_AMIGA
	/*
	 * if on an amiga and address is in first 16M, move it 
	 * to the ZTWO_VADDR range
	 */
	if (MACH_IS_AMIGA && paddr < 16*1024*1024)
		return (void *)ZTWO_VADDR(paddr);
#endif
	return (void *)paddr;
}
#else
extern inline unsigned long virt_to_phys(volatile void * address)
{
	return mm_vtop((unsigned long)address);
}

extern inline void * phys_to_virt(unsigned long address)
{
	return (void *) mm_ptov(address);
}
#endif

/*
 * IO bus memory addresses are 1:1 with the physical address,
 * except on the PCI bus of the Hades.
 */
#ifdef CONFIG_HADES
#define virt_to_bus(a) (virt_to_phys(a) + (MACH_IS_HADES ? 0x80000000 : 0))
#define bus_to_virt(a) (phys_to_virt((a) - (MACH_IS_HADES ? 0x80000000 : 0)))
#else
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt
#endif

#endif
#endif
