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

#include <linux/config.h>
#ifdef CONFIG_APUS
#include <linux/hdreg.h>

#define ide_init_hwif_ports m68k_ide_init_hwif_ports 
#include <asm-m68k/ide.h>
#undef ide_init_hwif_ports
#undef insw

void ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq);
void ide_insw(ide_ioreg_t port, void *buf, int ns);
void ide_outsw(ide_ioreg_t port, void *buf, int ns);
#define insw(port, buf, ns) 	do {			\
	if ( _machine != _MACH_Pmac && _machine != _MACH_apus )	\
		/* this must be the same as insw in io.h!! */	\
		_insw((unsigned short *)((port)+_IO_BASE), (buf), (ns)); \
	else						\
		ide_insw((port), (buf), (ns));		\
} while (0)
#undef outsw
#define outsw(port, buf, ns) 	do {			\
	if ( _machine != _MACH_Pmac && _machine != _MACH_apus )	\
		/* this must be the same as outsw in io.h!! */	\
		_outsw((unsigned short *)((port)+_IO_BASE), (buf), (ns)); \
	else						\
		ide_outsw((port), (buf), (ns));		\
} while (0)
#else /* CONFIG_APUS */

#ifdef __KERNEL__

#include <linux/hdreg.h>
#include <linux/ioport.h>
#include <asm/io.h>		/* so we can redefine insw/outsw */

#ifndef MAX_HWIFS
#define MAX_HWIFS	4
#endif

#undef	SUPPORT_SLOW_DATA_PORTS
#define	SUPPORT_SLOW_DATA_PORTS	0
#undef	SUPPORT_VLB_SYNC
#define SUPPORT_VLB_SYNC	0


#define ide__sti()	__sti()

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
	if ( _machine == _MACH_Pmac )
		return 0;
	else if ( _machine == _MACH_mbx )
		/* hardcode IRQ 14 on the MBX */
		return 14+16;		     
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
#if defined(CONFIG_BLK_DEV_IDE_PMAC)
        if (_machine == _MACH_Pmac) {
		return pmac_ide_regbase[index];
	}
#endif	
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

/* Convert the shorts/longs in hd_driveid from little to big endian;
   chars are endian independent, of course, but strings need to be flipped.
   (Despite what it says in drivers/block/ide.h, they come up as little endian...)
   Changes to linux/hdreg.h may require changes here. */
static __inline__ void ide_fix_driveid (struct hd_driveid *id) {
	if (( _machine == _MACH_Pmac ) || (_machine == _MACH_chrp)|| (_machine == _MACH_mbx) ) {
		int i;
		unsigned short *stringcast;
		id->config         = __le16_to_cpu(id->config);
		id->cyls           = __le16_to_cpu(id->cyls);
		id->reserved2      = __le16_to_cpu(id->reserved2);
		id->heads          = __le16_to_cpu(id->heads);
		id->track_bytes    = __le16_to_cpu(id->track_bytes);
		id->sector_bytes   = __le16_to_cpu(id->sector_bytes);
		id->sectors        = __le16_to_cpu(id->sectors);
		id->vendor0        = __le16_to_cpu(id->vendor0);
		id->vendor1        = __le16_to_cpu(id->vendor1);
		id->vendor2        = __le16_to_cpu(id->vendor2);
		stringcast = (unsigned short *)&id->serial_no[0];
		for (i=0; i<(20/2); i++)
			stringcast[i] = __le16_to_cpu(stringcast[i]);
		id->buf_type       = __le16_to_cpu(id->buf_type);
		id->buf_size       = __le16_to_cpu(id->buf_size);
		id->ecc_bytes      = __le16_to_cpu(id->ecc_bytes);
		stringcast = (unsigned short *)&id->fw_rev[0];
		for (i=0; i<(8/2); i++)
			stringcast[i] = __le16_to_cpu(stringcast[i]);
		stringcast = (unsigned short *)&id->model[0];
		for (i=0; i<(40/2); i++)
			stringcast[i] = __le16_to_cpu(stringcast[i]);
		id->dword_io       = __le16_to_cpu(id->dword_io);
		id->reserved50     = __le16_to_cpu(id->reserved50);
		id->field_valid    = __le16_to_cpu(id->field_valid);
		id->cur_cyls       = __le16_to_cpu(id->cur_cyls);
		id->cur_heads      = __le16_to_cpu(id->cur_heads);
		id->cur_sectors    = __le16_to_cpu(id->cur_sectors);
		id->cur_capacity0  = __le16_to_cpu(id->cur_capacity0);
		id->cur_capacity1  = __le16_to_cpu(id->cur_capacity1);
		id->lba_capacity   = __le32_to_cpu(id->lba_capacity);
		id->dma_1word      = __le16_to_cpu(id->dma_1word);
		id->dma_mword      = __le16_to_cpu(id->dma_mword);
		id->eide_pio_modes = __le16_to_cpu(id->eide_pio_modes);
		id->eide_dma_min   = __le16_to_cpu(id->eide_dma_min);
		id->eide_dma_time  = __le16_to_cpu(id->eide_dma_time);
		id->eide_pio       = __le16_to_cpu(id->eide_pio);
		id->eide_pio_iordy = __le16_to_cpu(id->eide_pio_iordy);
		id->word69         = __le16_to_cpu(id->word69);
		id->word70         = __le16_to_cpu(id->word70);
		id->word71         = __le16_to_cpu(id->word71);
		id->word72         = __le16_to_cpu(id->word72);
		id->word73         = __le16_to_cpu(id->word73);
		id->word74         = __le16_to_cpu(id->word74);
		id->word75         = __le16_to_cpu(id->word75);
		id->word76         = __le16_to_cpu(id->word76);
		id->word77         = __le16_to_cpu(id->word77);
		id->word78         = __le16_to_cpu(id->word78);
		id->word79         = __le16_to_cpu(id->word79);
		id->word80         = __le16_to_cpu(id->word80);
		id->word81         = __le16_to_cpu(id->word81);
		id->command_sets   = __le16_to_cpu(id->command_sets);
		id->word83         = __le16_to_cpu(id->word83);
		id->word84         = __le16_to_cpu(id->word84);
		id->word85         = __le16_to_cpu(id->word85);
		id->word86         = __le16_to_cpu(id->word86);
		id->word87         = __le16_to_cpu(id->word87);
		id->dma_ultra      = __le16_to_cpu(id->dma_ultra);
		id->word89         = __le16_to_cpu(id->word89);
		id->word90         = __le16_to_cpu(id->word90);
		id->word91         = __le16_to_cpu(id->word91);
		id->word92         = __le16_to_cpu(id->word92);
		id->word93         = __le16_to_cpu(id->word93);
		id->word94         = __le16_to_cpu(id->word94);
		id->word95         = __le16_to_cpu(id->word95);
		id->word96         = __le16_to_cpu(id->word96);
		id->word97         = __le16_to_cpu(id->word97);
		id->word98         = __le16_to_cpu(id->word98);
		id->word99         = __le16_to_cpu(id->word99);
		id->word100        = __le16_to_cpu(id->word100);
		id->word101        = __le16_to_cpu(id->word101);
		id->word102        = __le16_to_cpu(id->word102);
		id->word103        = __le16_to_cpu(id->word103);
		id->word104        = __le16_to_cpu(id->word104);
		id->word105        = __le16_to_cpu(id->word105);
		id->word106        = __le16_to_cpu(id->word106);
		id->word107        = __le16_to_cpu(id->word107);
		id->word108        = __le16_to_cpu(id->word108);
		id->word109        = __le16_to_cpu(id->word109);
		id->word110        = __le16_to_cpu(id->word110);
		id->word111        = __le16_to_cpu(id->word111);
		id->word112        = __le16_to_cpu(id->word112);
		id->word113        = __le16_to_cpu(id->word113);
		id->word114        = __le16_to_cpu(id->word114);
		id->word115        = __le16_to_cpu(id->word115);
		id->word116        = __le16_to_cpu(id->word116);
		id->word117        = __le16_to_cpu(id->word117);
		id->word118        = __le16_to_cpu(id->word118);
		id->word119        = __le16_to_cpu(id->word119);
		id->word120        = __le16_to_cpu(id->word120);
		id->word121        = __le16_to_cpu(id->word121);
		id->word122        = __le16_to_cpu(id->word122);
		id->word123        = __le16_to_cpu(id->word123);
		id->word124        = __le16_to_cpu(id->word124);
		id->word125        = __le16_to_cpu(id->word125);
		id->word126        = __le16_to_cpu(id->word126);
		id->word127        = __le16_to_cpu(id->word127);
		id->security       = __le16_to_cpu(id->security);
		for (i=0; i<127; i++)
			id->reserved[i] = __le16_to_cpu(id->reserved[i]);
	}
}

#undef insw
#define insw(port, buf, ns) 	do {			\
	if ( _machine == _MACH_chrp)  {\
		 ide_insw((port)+_IO_BASE, (buf), (ns));  \
	}\
	else if ( (_machine == _MACH_Pmac) || (_machine == _MACH_mbx) )			\
		ide_insw((port)+((_machine==_MACH_mbx)? 0x80000000: 0), \
			 (buf), (ns));		\
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
	else if ( (_machine == _MACH_Pmac) || (_machine == _MACH_mbx) )	 \
		ide_outsw((port)+((_machine==_MACH_mbx)? 0x80000000: 0), \
			   (buf), (ns));		\
	else						\
		/* this must be the same as outsw in io.h!! */	\
		_outsw((unsigned short *)((port)+_IO_BASE), (buf), (ns)); \
} while (0)

#undef inb
#define inb(port)	\
	in_8((unsigned char *)((port) + \
			       ((_machine==_MACH_Pmac)? 0: _IO_BASE) + \
			       ((_machine==_MACH_mbx)? 0x80000000: 0)) )
#undef inb_p
#define inb_p(port)	inb(port)

#undef outb
#define outb(val, port)	\
	out_8((unsigned char *)((port) + \
				((_machine==_MACH_Pmac)? 0: _IO_BASE) + \
				((_machine==_MACH_mbx)? 0x80000000: 0)), (val) )
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
#endif /* CONFIG_APUS */

#endif /* __ASMPPC_IDE_H */
