/* $Id: ide.h,v 1.1 1999/08/21 22:19:17 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 *  This file contains the MIPS architecture specific IDE code.
 *
 * Copyright (C) 1994-1996  Linus Torvalds & authors
 */

#ifndef _ASM_IDE_H
#define _ASM_IDE_H

#ifdef __KERNEL__

#ifndef MAX_HWIFS
#define MAX_HWIFS	6
#endif

#define ide__sti()	__sti()

struct ide_ops {
	int (*ide_default_irq)(ide_ioreg_t base);
	ide_ioreg_t (*ide_default_io_base)(int index);
	void (*ide_init_hwif_ports)(hw_regs_t *hw, ide_ioreg_t data_port,
	                            ide_ioreg_t ctrl_port, int *irq);
	int (*ide_request_irq)(unsigned int irq, void (*handler)(int, void *,
	                       struct pt_regs *), unsigned long flags,
	                       const char *device, void *dev_id);
	void (*ide_free_irq)(unsigned int irq, void *dev_id);
	int (*ide_check_region) (ide_ioreg_t from, unsigned int extent);
	void (*ide_request_region)(ide_ioreg_t from, unsigned int extent,
	                        const char *name);
	void (*ide_release_region)(ide_ioreg_t from, unsigned int extent);
};

extern struct ide_ops *ide_ops;

static inline int
ide_default_irq(ide_ioreg_t base)
{
	return ide_ops->ide_default_irq(base);
}

static inline ide_ioreg_t
ide_default_io_base(int index)
{
	return ide_ops->ide_default_io_base(index);
}

static inline void
ide_init_hwif_ports(hw_regs_t *hw, ide_ioreg_t data_port,
                    ide_ioreg_t ctrl_port, int *irq)
{
	ide_ops->ide_init_hwif_ports(hw, data_port, ctrl_port, &hw->irq);

	hw->irq = ide_ops->ide_default_irq(data_port);
}

/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
static inline void ide_init_default_hwifs(void)
{
#ifndef CONFIG_BLK_DEV_IDEPCI
	hw_regs_t hw;
	int index;

	for (index = 0; index < MAX_HWIFS; index++) {
		ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, 0);
		hw.irq = ide_default_irq(ide_default_io_base(index));
		ide_register_hw(&hw, NULL);
	}
#endif /* CONFIG_BLK_DEV_IDEPCI */
}

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned head		: 4;	/* always zeros here */
		unsigned unit		: 1;	/* drive select number, 0 or 1 */
		unsigned bit5		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit7		: 1;	/* always 1 */
	} b;
	} select_t;

static inline int
ide_request_irq(unsigned int irq,
                void (*handler)(int,void *, struct pt_regs *),
                unsigned long flags, const char *device, void *dev_id)
{
	return ide_ops->ide_request_irq(irq, handler, flags, device, dev_id);
}

static inline void
ide_free_irq(unsigned int irq, void *dev_id)
{
	ide_ops->ide_free_irq(irq, dev_id);
}

static inline int
ide_check_region(ide_ioreg_t from, unsigned int extent)
{
	return ide_ops->ide_check_region(from, extent);
}

static inline void
ide_request_region(ide_ioreg_t from, unsigned int extent, const char *name)
{
	ide_ops->ide_request_region(from, extent, name);
}

static inline void
ide_release_region(ide_ioreg_t from, unsigned int extent)
{
	ide_ops->ide_release_region(from, extent);
}

/*
 * The following are not needed for the non-m68k ports
 */
#define ide_ack_intr(hwif)		(1)
#define ide_fix_driveid(id)		do {} while (0)
#define ide_release_lock(lock)		do {} while (0)
#define ide_get_lock(lock, hdlr, data)	do {} while (0)

#endif /* __KERNEL__ */

#endif /* _ASM_IDE_H */
