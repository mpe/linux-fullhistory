/*
 *  linux/include/asm-i386/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the i386 architecture specific IDE code.
 */

#ifndef __ASMi386_IDE_H
#define __ASMi386_IDE_H

#ifdef __KERNEL__

typedef unsigned short ide_ioreg_t;

#ifndef MAX_HWIFS
#define MAX_HWIFS	6
#endif

#define ide__sti()	__sti()

static __inline__ int ide_default_irq(ide_ioreg_t base)
{
	switch (base) {
		case 0x1f0: return 14;
		case 0x170: return 15;
		case 0x1e8: return 11;
		case 0x168: return 10;
		case 0x1e0: return 8;
		case 0x160: return 12;
		default:
			return 0;
	}
}

static __inline__ ide_ioreg_t ide_default_io_base(int index)
{
	switch (index) {
		case 0:	return 0x1f0;
		case 1:	return 0x170;
		case 2: return 0x1e8;
		case 3: return 0x168;
		case 4: return 0x1e0;
		case 5: return 0x160;
		default:
			return 0;
	}
}

static __inline__ void ide_init_hwif_ports (ide_ioreg_t *p, ide_ioreg_t base, int *irq)
{
	ide_ioreg_t port = base;
	int i = 8;

	while (i--)
		*p++ = port++;
	*p++ = base + 0x206;
	if (irq != NULL)
		*irq = 0;
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

static __inline__ int ide_request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
			unsigned long flags, const char *device, void *dev_id)
{
	return request_irq(irq, handler, flags, device, dev_id);
}			

static __inline__ void ide_free_irq(unsigned int irq, void *dev_id)
{
	free_irq(irq, dev_id);
}

static __inline__ int ide_check_region (ide_ioreg_t from, unsigned int extent)
{
	return check_region(from, extent);
}

static __inline__ void ide_request_region (ide_ioreg_t from, unsigned int extent, const char *name)
{
	request_region(from, extent, name);
}

static __inline__ void ide_release_region (ide_ioreg_t from, unsigned int extent)
{
	release_region(from, extent);
}

/*
 * The following are not needed for the non-m68k ports
 */
static __inline__ int ide_ack_intr (ide_ioreg_t status_port, ide_ioreg_t irq_port)
{
	return(1);
}

static __inline__ void ide_fix_driveid(struct hd_driveid *id)
{
}

static __inline__ void ide_release_lock (int *ide_lock)
{
}

static __inline__ void ide_get_lock (int *ide_lock, void (*handler)(int, void *, struct pt_regs *), void *data)
{
}

#endif /* __KERNEL__ */

#endif /* __ASMi386_IDE_H */
