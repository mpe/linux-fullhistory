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

#include <linux/ioport.h>
#include <asm/io.h>		/* so we can redefine insw/outsw */

#ifndef MAX_HWIFS
#define MAX_HWIFS	4
#endif

#undef	SUPPORT_SLOW_DATA_PORTS
#define	SUPPORT_SLOW_DATA_PORTS	0
#undef	SUPPORT_VLB_SYNC
#define SUPPORT_VLB_SYNC	0


#define ide_sti()	sti()

typedef unsigned int ide_ioreg_t;
void ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq);
void prep_ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq);
void mbx_ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq);
void pmac_ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq);
void chrp_ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq);
void ide_insw(ide_ioreg_t port, void *buf, int ns);
void ide_outsw(ide_ioreg_t port, void *buf, int ns);

extern int pmac_ide_ports_known;
extern ide_ioreg_t pmac_ide_regbase[MAX_HWIFS];
extern int pmac_ide_irq[MAX_HWIFS];
extern void pmac_ide_probe(void);

extern int chrp_ide_ports_known;
extern ide_ioreg_t chrp_ide_regbase[MAX_HWIFS];
extern ide_ioreg_t chrp_idedma_regbase; /* one for both channels */
extern unsigned int chrp_ide_irq;
extern void chrp_ide_probe(void);

static __inline__ int ide_default_irq(ide_ioreg_t base)
{
	if ( (_machine == _MACH_Pmac) || (_machine == _MACH_mbx) )
		return 0;
        else if ( _machine == _MACH_chrp) {
                if (chrp_ide_ports_known == 0) 
			chrp_ide_probe();
                return chrp_ide_irq;
        }
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
        if (_machine == _MACH_Pmac) {
		if (!pmac_ide_ports_known)
			pmac_ide_probe();
		return pmac_ide_regbase[index];
	}
	if (_machine == _MACH_mbx) return index;
        if ( _machine == _MACH_chrp ) {
                if (chrp_ide_ports_known == 0)
                        chrp_ide_probe();
                return chrp_ide_regbase[index];
        }
	switch (index) {
		case 0:	return 0x1f0;
		case 1:	return 0x170;
		case 2: return 0x1e8;
		case 3: return 0x168;
		default:
			return 0;
	}
}

static __inline__ int ide_check_region (ide_ioreg_t from, unsigned int extent)
{
	if ( (_machine == _MACH_Pmac) || (_machine == _MACH_mbx))
		return 0;
	return check_region(from, extent);
}

static __inline__ void ide_request_region (ide_ioreg_t from, unsigned int extent, const char *name)
{
	if ( (_machine == _MACH_Pmac) || (_machine == _MACH_mbx) )
		return;
	request_region(from, extent, name);
}

static __inline__ void ide_release_region (ide_ioreg_t from, unsigned int extent)
{
	if ( (_machine == _MACH_Pmac) || (_machine == _MACH_mbx) )
		return;
	release_region(from, extent);
}

#define ide_fix_driveid(id)	do {			\
	int nh;						\
	unsigned short *p = (unsigned short *) id;	\
	if (( _machine == _MACH_Pmac ) || (_machine == _MACH_chrp)|| (_machine == _MACH_mbx) )	\
		for (nh = SECTOR_WORDS * 2; nh != 0; --nh, ++p)	\
			*p = (*p << 8) + (*p >> 8);	\
} while (0)


#undef insw
#define insw(port, buf, ns) 	do {			\
	if ( _machine == _MACH_chrp)  {\
		 ide_insw((port)+_IO_BASE, (buf), (ns));  \
	}\
	else if ( (_machine == _MACH_Pmac) || (_machine == _MACH_mbx) )			\
		ide_insw((port), (buf), (ns));		\
	else						\
		/* this must be the same as insw in io.h!! */	\
		_insw((unsigned short *)((port)+_IO_BASE), (buf), (ns)); \
} while (0)
#undef outsw
/*	printk("port: %x buf: %p ns: %d\n",port,buf,ns); \ */
#define outsw(port, buf, ns) 	do {			\
	if ( _machine == _MACH_chrp) {\
		ide_outsw((port)+_IO_BASE, (buf), (ns)); \
	}\
	else if ( (_machine == _MACH_Pmac) || (_machine == _MACH_mbx) )			\
		ide_outsw((port), (buf), (ns));		\
	else						\
		/* this must be the same as outsw in io.h!! */	\
		_outsw((unsigned short *)((port)+_IO_BASE), (buf), (ns)); \
} while (0)

#undef inb
#define inb(port)	\
	in_8((unsigned char *)((port) + ((_machine==_MACH_Pmac)? 0: _IO_BASE)))
#undef inb_p
#define inb_p(port)	inb(port)

#undef outb
#define outb(val, port)	\
	out_8((unsigned char *)((port) + ((_machine==_MACH_Pmac)? 0: _IO_BASE)), (val))
#undef outb_p
#define outb_p(val, port)	outb(val, port)

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
