/* $Id: ide.h,v 1.7 2002/01/16 20:58:40 davem Exp $
 * ide.h: SPARC PCI specific IDE glue.
 *
 * Copyright (C) 1997  David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998  Eddie C. Dost   (ecd@skynet.be)
 * Adaptation from sparc64 version to sparc by Pete Zaitcev.
 */

#ifndef _SPARC_IDE_H
#define _SPARC_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/hdreg.h>
#include <asm/psr.h>

#undef  MAX_HWIFS
#define MAX_HWIFS	2

static __inline__ int ide_default_irq(unsigned long base)
{
	return 0;
}

static __inline__ unsigned long ide_default_io_base(int index)
{
	return 0;
}

/*
 * Doing any sort of ioremap() here does not work
 * because this function may be called with null aguments.
 */
static __inline__ void ide_init_hwif_ports(hw_regs_t *hw, unsigned long data_port, unsigned long ctrl_port, int *irq)
{
	unsigned long reg = data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] = hw->io_ports[IDE_DATA_OFFSET] + 0x206;
	}
	if (irq != NULL)
		*irq = 0;
	hw->io_ports[IDE_IRQ_OFFSET] = 0;
}

/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
static __inline__ void ide_init_default_hwifs(void)
{
#ifndef CONFIG_PCI
	hw_regs_t hw;
	int index;

	for (index = 0; index < MAX_HWIFS; index++) {
		memset(&hw, 0, sizeof hw);
		ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, NULL);
		hw.irq = ide_default_irq(ide_default_io_base(index));
		ide_register_hw(&hw, NULL);
	}
#endif
}

#define __ide_insl(data_reg, buffer, wcount) \
	__ide_insw(data_reg, buffer, (wcount)<<1)
#define __ide_outsl(data_reg, buffer, wcount) \
	__ide_outsw(data_reg, buffer, (wcount)<<1)

/* On sparc, I/O ports and MMIO registers are accessed identically.  */
#define __ide_mm_insw	__ide_insw
#define __ide_mm_insl	__ide_insl
#define __ide_mm_outsw	__ide_outsw
#define __ide_mm_outsl	__ide_outsl

static __inline__ void __ide_insw(unsigned long port,
				  void *dst,
				  unsigned long count)
{
	volatile unsigned short *data_port;
	/* unsigned long end = (unsigned long)dst + (count << 1); */ /* P3 */
	u16 *ps = dst;
	u32 *pi;

	data_port = (volatile unsigned short *)port;

	if(((unsigned long)ps) & 0x2) {
		*ps++ = *data_port;
		count--;
	}
	pi = (u32 *)ps;
	while(count >= 2) {
		u32 w;

		w  = (*data_port) << 16;
		w |= (*data_port);
		*pi++ = w;
		count -= 2;
	}
	ps = (u16 *)pi;
	if(count)
		*ps++ = *data_port;

	/* __flush_dcache_range((unsigned long)dst, end); */ /* P3 see hme */
}

static __inline__ void __ide_outsw(unsigned long port,
				   const void *src,
				   unsigned long count)
{
	volatile unsigned short *data_port;
	/* unsigned long end = (unsigned long)src + (count << 1); */
	const u16 *ps = src;
	const u32 *pi;

	data_port = (volatile unsigned short *)port;

	if(((unsigned long)src) & 0x2) {
		*data_port = *ps++;
		count--;
	}
	pi = (const u32 *)ps;
	while(count >= 2) {
		u32 w;

		w = *pi++;
		*data_port = (w >> 16);
		*data_port = w;
		count -= 2;
	}
	ps = (const u16 *)pi;
	if(count)
		*data_port = *ps;

	/* __flush_dcache_range((unsigned long)src, end); */ /* P3 see hme */
}

#endif /* __KERNEL__ */

#endif /* _SPARC_IDE_H */
