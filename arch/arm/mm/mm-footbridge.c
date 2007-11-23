/*
 * arch/arm/mm/mm-ebsa285.c
 *
 * Extra MM routines for the EBSA285 architecture
 *
 * Copyright (C) 1998 Russell King, Dave Gilbert.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
 
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/proc/mm-init.h>
#include <asm/dec21285.h>

/*
 * The first entry allows us to fiddle with the EEPROM from user-space.
 *  This entry will go away in time, once the fmu32 can mmap() the
 *  flash.  It can't at the moment.
 *
 * If you want to fiddle with PCI VGA cards from user space, then
 * change the '0, 1 }' for the PCI MEM and PCI IO to '1, 1 }'
 * You can then access the PCI bus at 0xe0000000 and 0xffe00000.
 */

#ifdef CONFIG_HOST_FOOTBRIDGE

/*
 * The mapping when the footbridge is in host mode.
 */
#define MAPPING \
 { FLASH_BASE,   DC21285_FLASH,			FLASH_SIZE,	DOMAIN_IO, 0, 1 }, \
 { PCIMEM_BASE,  DC21285_PCI_MEM,		PCIMEM_SIZE,	DOMAIN_IO, 0, 1 }, \
 { PCICFG0_BASE, DC21285_PCI_TYPE_0_CONFIG,	PCICFG0_SIZE,	DOMAIN_IO, 0, 1 }, \
 { PCICFG1_BASE, DC21285_PCI_TYPE_1_CONFIG,	PCICFG1_SIZE,	DOMAIN_IO, 0, 1 }, \
 { PCIIACK_BASE, DC21285_PCI_IACK,		PCIIACK_SIZE,	DOMAIN_IO, 0, 1 }, \
 { WFLUSH_BASE,  DC21285_OUTBOUND_WRITE_FLUSH,	WFLUSH_SIZE,	DOMAIN_IO, 0, 1 }, \
 { ARMCSR_BASE,  DC21285_ARMCSR_BASE,		ARMCSR_SIZE,	DOMAIN_IO, 0, 1 }, \
 { PCIO_BASE,    DC21285_PCI_IO,		PCIO_SIZE,	DOMAIN_IO, 0, 1 }, \
 { XBUS_BASE,    0x40000000,			XBUS_SIZE,	DOMAIN_IO, 0, 1 }

#else

/*
 * These two functions convert virtual addresses to PCI addresses
 * and PCI addresses to virtual addresses.  Note that it is only
 * legal to use these on memory obtained via get_free_page or
 * kmalloc.
 */
unsigned long __virt_to_bus(unsigned long res)
{
#ifdef CONFIG_DEBUG_ERRORS
	if (res < PAGE_OFFSET || res >= (unsigned long)high_memory) {
		printk("__virt_to_phys: invalid virtual address 0x%08lx\n", res);
		__backtrace();
	}
#endif
	return (res - PAGE_OFFSET) + (*CSR_PCISDRAMBASE & 0xfffffff0);
}

unsigned long __bus_to_virt(unsigned long res)
{
	res -= (*CSR_PCISDRAMBASE & 0xfffffff0);
	res += PAGE_OFFSET;

#ifdef CONFIG_DEBUG_ERRORS
	if (res < PAGE_OFFSET || res >= (unsigned long)high_memory) {
		printk("__phys_to_virt: invalid virtual address 0x%08lx\n", res);
		__backtrace();
	}
#endif
	return res;
}

/*
 * The mapping when the footbridge is in add-in mode.
 */
#define MAPPING \
 { PCIO_BASE,	 DC21285_PCI_IO,		PCIO_SIZE,	DOMAIN_IO, 0, 1 }, \
 { XBUS_BASE,	 0x40000000,			XBUS_SIZE,	DOMAIN_IO, 0, 1 }, \
 { ARMCSR_BASE,  DC21285_ARMCSR_BASE,		ARMCSR_SIZE,	DOMAIN_IO, 0, 1 }, \
 { WFLUSH_BASE,	 DC21285_OUTBOUND_WRITE_FLUSH,	WFLUSH_SIZE,	DOMAIN_IO, 0, 1 }, \
 { FLASH_BASE,	 DC21285_FLASH,			FLASH_SIZE,	DOMAIN_IO, 0, 1 }, \
 { PCIMEM_BASE,	 DC21285_PCI_MEM,		PCIMEM_SIZE,	DOMAIN_IO, 0, 1 }

#endif

#include "mm-armv.c"
