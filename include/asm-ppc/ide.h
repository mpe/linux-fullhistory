/*
 *  linux/include/asm-ppc/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the ppc architecture specific IDE code.
 */

#ifndef __ASMPPC_IDE_H
#define __ASMPPC_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>

#ifndef MAX_HWIFS
#define MAX_HWIFS	4
#endif

#define ide_sti()	sti()

#ifdef CONFIG_PREP

typedef unsigned short ide_ioreg_t;

static __inline__ int ide_default_irq(ide_ioreg_t base)
{
	switch (base) {
		case 0x1f0: return 13;
		case 0x170: return 13;
		case 0x1e8: return 11;
		case 0x168: return 10;
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

#define ide_fix_driveid(id)		do {} while (0)

#endif

#ifdef CONFIG_PMAC

#include <asm/io.h>		/* so we can redefine insw/outsw */

typedef unsigned long ide_ioreg_t;

static __inline__ int ide_default_irq(ide_ioreg_t base)
{
	return 0;
}

extern __inline__ ide_ioreg_t ide_default_io_base(int index)
{
	return index;
}

void ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq);

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned bit7		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit5		: 1;	/* always 1 */
		unsigned unit		: 1;	/* drive select number, 0/1 */
		unsigned head		: 4;	/* always zeros here */
	} b;
} select_t;

#undef	SUPPORT_SLOW_DATA_PORTS
#define	SUPPORT_SLOW_DATA_PORTS	0
#undef	SUPPORT_VLB_SYNC
#define SUPPORT_VLB_SYNC	0

static __inline__ int ide_check_region (ide_ioreg_t from, unsigned int extent)
{
	return 0;
}

static __inline__ void ide_request_region (ide_ioreg_t from, unsigned int extent, const char *name)
{
}

static __inline__ void ide_release_region (ide_ioreg_t from, unsigned int extent)
{
}

#undef insw
#undef outsw
#define insw(port, buf, ns)	ide_insw((port), (buf), (ns))
#define outsw(port, buf, ns)	ide_outsw((port), (buf), (ns))

void ide_insw(ide_ioreg_t port, void *buf, int ns);
void ide_outsw(ide_ioreg_t port, void *buf, int ns);

#define ide_fix_driveid(id)	do {			\
	int nh;						\
	unsigned short *p = (unsigned short *) id;	\
	for (nh = SECTOR_WORDS * 2; nh != 0; --nh, ++p)	\
		*p = (*p << 8) + (*p >> 8);		\
} while (0)

#endif

static __inline__ int ide_request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
			unsigned long flags, const char *device, void *dev_id)
{
	return request_irq(irq, handler, flags, device, dev_id);
}			

static __inline__ void ide_free_irq(unsigned int irq, void *dev_id)
{
	free_irq(irq, dev_id);
}

/*
 * The following are not needed for the non-m68k ports
 */
#define ide_ack_intr(base, irq)		(1)
#define ide_release_lock(lock)		do {} while (0)
#define ide_get_lock(lock, hdlr, data)	do {} while (0)

#endif /* __KERNEL__ */

#endif /* __ASMPPC_IDE_H */
