/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994-1996  Linus Torvalds & authors
 *
 * Copied from i386; many of the especially older MIPS or ISA-based platforms
 * are basically identical.  Using this file probably implies i8259 PIC
 * support in a system but the very least interrupt numbers 0 - 15 need to
 * be put aside for legacy devices.
 */
#ifndef __ASM_MACH_GENERIC_IDE_H
#define __ASM_MACH_GENERIC_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/stddef.h>

#ifndef MAX_HWIFS
# ifdef CONFIG_BLK_DEV_IDEPCI
#define MAX_HWIFS	10
# else
#define MAX_HWIFS	6
# endif
#endif

#define IDE_ARCH_OBSOLETE_DEFAULTS

static __inline__ int ide_probe_legacy(void)
{
#ifdef CONFIG_PCI
	struct pci_dev *dev;
	if ((dev = pci_get_class(PCI_CLASS_BRIDGE_EISA << 8, NULL)) != NULL ||
	    (dev = pci_get_class(PCI_CLASS_BRIDGE_ISA << 8, NULL)) != NULL) {
		pci_dev_put(dev);

		return 1;
	}
	return 0;
#elif defined(CONFIG_EISA) || defined(CONFIG_ISA)
	return 1;
#else
	return 0;
#endif
}

static __inline__ int ide_default_irq(unsigned long base)
{
	if (ide_probe_legacy())
		switch (base) {
		case 0x1f0:
			return 14;
		case 0x170:
			return 15;
		case 0x1e8:
			return 11;
		case 0x168:
			return 10;
		case 0x1e0:
			return 8;
		case 0x160:
			return 12;
		default:
			return 0;
		}
	else
		return 0;
}

static __inline__ unsigned long ide_default_io_base(int index)
{
	if (ide_probe_legacy())
		switch (index) {
		case 0:
			return 0x1f0;
		case 1:
			return 0x170;
		case 2:
			return 0x1e8;
		case 3:
			return 0x168;
		case 4:
			return 0x1e0;
		case 5:
			return 0x160;
		default:
			return 0;
		}
	else
		return 0;
}

#define IDE_ARCH_OBSOLETE_INIT
#define ide_default_io_ctl(base)	((base) + 0x206) /* obsolete */

#ifdef CONFIG_BLK_DEV_IDEPCI
#define ide_init_default_irq(base)	(0)
#else
#define ide_init_default_irq(base)	ide_default_irq(base)
#endif

/* MIPS port and memory-mapped I/O string operations.  */

#define __ide_insw	insw
#define __ide_insl	insl
#define __ide_outsw	outsw
#define __ide_outsl	outsl

#define __ide_mm_insw	readsw
#define __ide_mm_insl	readsl
#define __ide_mm_outsw	writesw
#define __ide_mm_outsl	writesl

#endif /* __KERNEL__ */

#endif /* __ASM_MACH_GENERIC_IDE_H */
