/* $Id: ide.h,v 1.4 1998/05/08 21:05:26 davem Exp $
 *
 *  linux/include/asm-mips/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the MIPS architecture specific IDE code.
 */

#ifndef __ASM_MIPS_IDE_H
#define __ASM_MIPS_IDE_H

#ifdef __KERNEL__

typedef unsigned short ide_ioreg_t;

#ifndef MAX_HWIFS
#define MAX_HWIFS	6
#endif

#define ide__sti()	__sti()

struct ide_ops {
	int (*ide_default_irq)(ide_ioreg_t base);
	ide_ioreg_t (*ide_default_io_base)(int index);
	void (*ide_init_hwif_ports)(ide_ioreg_t *p, ide_ioreg_t base, int *irq);
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

static __inline__ int ide_default_irq(ide_ioreg_t base)
{
	return ide_ops->ide_default_irq(base);
}

static __inline__ ide_ioreg_t ide_default_io_base(int index)
{
	return ide_ops->ide_default_io_base(index);
}

static __inline__ void ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base,
                                           int *irq)
{
	ide_ops->ide_init_hwif_ports(p, base, irq);
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

static __inline__ int ide_request_irq(unsigned int irq, void (*handler)(int,void *, struct pt_regs *),
			unsigned long flags, const char *device, void *dev_id)
{
	return ide_ops->ide_request_irq(irq, handler, flags, device, dev_id);
}

static __inline__ void ide_free_irq(unsigned int irq, void *dev_id)
{
	ide_ops->ide_free_irq(irq, dev_id);
}

static __inline__ int ide_check_region (ide_ioreg_t from, unsigned int extent)
{
	return ide_ops->ide_check_region(from, extent);
}

static __inline__ void ide_request_region(ide_ioreg_t from,
                                          unsigned int extent, const char *name)
{
	ide_ops->ide_request_region(from, extent, name);
}

static __inline__ void ide_release_region(ide_ioreg_t from,
                                          unsigned int extent)
{
	ide_ops->ide_release_region(from, extent);
}

/*
 * The following are not needed for the non-m68k ports
 */
static __inline__ int ide_ack_intr (ide_ioreg_t status_port,
                                    ide_ioreg_t irq_port)
{
	return 1;
}

static __inline__ void ide_fix_driveid(struct hd_driveid *id)
{
}

static __inline__ void ide_release_lock (int *ide_lock)
{
}

static __inline__ void ide_get_lock (int *ide_lock,
                                     void (*handler)(int, void *,
                                                    struct pt_regs *),
                                     void *data)
{
}

#endif /* __KERNEL__ */

#endif /* __ASM_MIPS_IDE_H */
