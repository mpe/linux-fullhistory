/*
 * linux/drivers/ide/via82cxxx.c	Version 0.10	June 9, 2000
 *
 *  Copyright (C) 1998-99	Michel Aubry, Maintainer
 *  Copyright (C) 1999		Jeff Garzik, MVP4 Support
 *					(jgarzik@mandrakesoft.com)
 *  Copyright (C) 1998-2000	Andre Hedrick <andre@linux-ide.org>
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  The VIA MVP-4 is reported OK with UDMA.
 *  The VIA MVP-3 is reported OK with UDMA.
 *  The TX Pro III is also reported OK with UDMA.
 *
 *  VIA chips also have a single FIFO, with the same 64 bytes deep
 *  buffer (16 levels of 4 bytes each).
 *
 *  However, VIA chips can have the buffer split either 8:8 levels,
 *  16:0 levels or 0:16 levels between both channels. One could think
 *  of using this feature, as even if no level of FIFO is given to a
 *  given channel, one can for instance always reach ATAPI drives through
 *  it, or, if one channel is unused, configuration defaults to
 *  an even split FIFO levels.
 *  
 *  This feature is available only through a kernel command line :
 *		"splitfifo=Chan,Thr0,Thr1" or "splitfifo=Chan".
 *		where:  Chan =1,2,3 or 4 and Thrx = 1,2,3,or 4.
 *
 *  If Chan == 1:
 *	gives all the fifo to channel 0,
 *	sets its threshold to Thr0/4,
 *	and disables any dma access to channel 1.
 *
 *  If chan == 2:
 *	gives all the fifo to channel 1,
 *	sets its threshold to Thr1/4,
 *	and disables any dma access to channel 0.
 *
 *  If chan == 3 or 4:
 *	shares evenly fifo between channels,
 *	gives channel 0 a threshold of Thr0/4,
 *	and channel 1 a threshold of Thr1/4.
 *
 *  Note that by default (if no command line is provided) and if a channel
 *  has been disabled in Bios, all the fifo is given to the active channel,
 *  and its threshold is set to 3/4.
 *
 *  VT82c586B
 *
 *    Offset 4B-48 - Drive Timing Control
 *             | pio0 | pio1 | pio2 | pio3 | pio4
 *    25.0 MHz | 0xA8 | 0x65 | 0x65 | 0x31 | 0x20 
 *    33.0 MHz | 0xA8 | 0x65 | 0x65 | 0x31 | 0x20
 *    37.5 MHz | 0xA9 | 0x76 | 0x76 | 0x32 | 0x21
 *
 *    Offset 53-50 - UltraDMA Extended Timing Control
 *      UDMA   |  NO  |   0  |   1  |   2
 *             | 0x03 | 0x62 | 0x61 | 0x60
 *
 *  VT82c596B & VT82c686A
 *
 *    Offset 4B-48 - Drive Timing Control
 *             | pio0 | pio1 | pio2 | pio3 | pio4
 *    25.0 MHz | 0xA8 | 0x65 | 0x65 | 0x31 | 0x20
 *    33.0 MHz | 0xA8 | 0x65 | 0x65 | 0x31 | 0x20
 *    37.5 MHz | 0xDB | 0x87 | 0x87 | 0x42 | 0x31
 *    41.5 MHz | 0xFE | 0xA8 | 0xA8 | 0x53 | 0x32
 *
 *    Offset 53-50 - UltraDMA Extended Timing Control
 *      UDMA   |  NO  |   0  |   1  |   2
 *    33.0 MHz | 0x03 | 0xE2 | 0xE1 | 0xE0
 *    37.5 MHz | 0x03 | 0xE2 | 0xE2 | 0xE1   (1)
 *
 *    Offset 53-50 - UltraDMA Extended Timing Control
 *      UDMA   |  NO  |   0  |   1  |   2  |   3  |   4
 *    33.0 MHz |  (2) | 0xE6 | 0xE4 | 0xE2 | 0xE1 | 0xE0
 *    37.5 MHz |  (2) | 0xE6 | 0xE6 | 0xE4 | 0xE2 | 0xE1   (1)
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "ide_modes.h"

static struct pci_dev *host_dev = NULL;
static struct pci_dev *isa_dev = NULL;

struct chipset_bus_clock_list_entry {
	byte	xfer_speed;

	byte	ultra_settings_25;
	byte	chipset_settings_25;
	byte	ultra_settings_33;
	byte	chipset_settings_33;
	byte	ultra_settings_37;
	byte	chipset_settings_37;
	byte	ultra_settings_41;
	byte	chipset_settings_41;
};

static struct chipset_bus_clock_list_entry * via82cxxx_table = NULL;

static struct chipset_bus_clock_list_entry via82cxxx_type_one [] = {
		/* speed */	/* 25 */	/* 33 */	/* 37.5 */	/* 41.5 */	
#ifdef CONFIG_BLK_DEV_IDEDMA
	{	XFER_UDMA_4,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00	},
	{	XFER_UDMA_3,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00	},
	{	XFER_UDMA_2,	0x60,	0x20,	0x60,	0x20,	0x60,	0x21,	0x00,	0x00	},
	{	XFER_UDMA_1,	0x61,	0x20,	0x61,	0x20,	0x61,	0x21,	0x00,	0x00	},
	{	XFER_UDMA_0,	0x62,	0x20,	0x62,	0x20,	0x62,	0x21,	0x00,	0x00	},

	{	XFER_MW_DMA_2,	0x03,	0x20,	0x03,	0x20,	0x03,	0x21,	0x00,	0x00	},
	{	XFER_MW_DMA_1,	0x03,	0x31,	0x03,	0x31,	0x03,	0x32,	0x00,	0x00	},
	{	XFER_MW_DMA_0,	0x03,	0x31,	0x03,	0x31,	0x03,	0x32,	0x00,	0x00	},
#endif /* CONFIG_BLK_DEV_IDEDMA */
	{	XFER_PIO_4,	0x03,	0x20,	0x03,	0x20,	0x03,	0x21,	0x00,	0x00	},
	{	XFER_PIO_3,	0x03,	0x31,	0x03,	0x31,	0x03,	0x32,	0x00,	0x00	},
	{	XFER_PIO_2,	0x03,	0x65,	0x03,	0x65,	0x03,	0x76,	0x00,	0x00	},
	{	XFER_PIO_1,	0x03,	0x65,	0x03,	0x65,	0x03,	0x76,	0x00,	0x00	},
	{	XFER_PIO_0,	0x03,	0xA8,	0x03,	0xA8,	0x03,	0xA9,	0x00,	0x00	},
	{	0,		0x03,	0xA8,	0x03,	0xA8,	0x03,	0xA9,	0x00,	0x00	}
};

static struct chipset_bus_clock_list_entry via82cxxx_type_two [] = {
		/* speed */	/* 25 */	/* 33 */	/* 37.5 */	/* 41.5 */
#ifdef CONFIG_BLK_DEV_IDEDMA
	{	XFER_UDMA_4,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00	},
	{	XFER_UDMA_3,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00	},
	{	XFER_UDMA_2,	0xE0,	0x20,	0xE0,	0x20,	0xE1,	0x31,	0xE1,	0x32	},
	{	XFER_UDMA_1,	0xE1,	0x20,	0xE1,	0x20,	0xE2,	0x31,	0xE2,	0x32	},
	{	XFER_UDMA_0,	0xE2,	0x20,	0xE2,	0x20,	0xE2,	0x31,	0xE2,	0x32	},

	{	XFER_MW_DMA_2,	0x03,	0x20,	0x03,	0x20,	0x03,	0x31,	0x03,	0x32	},
	{	XFER_MW_DMA_1,	0x03,	0x31,	0x03,	0x31,	0x03,	0x42,	0x03,	0x53	},
	{	XFER_MW_DMA_0,	0x03,	0x31,	0x03,	0x31,	0x03,	0x42,	0x03,	0x53	},
#endif /* CONFIG_BLK_DEV_IDEDMA */
	{	XFER_PIO_4,	0x03,	0x20,	0x03,	0x20,	0x03,	0x31,	0x03,	0x32	},
	{	XFER_PIO_3,	0x03,	0x31,	0x03,	0x31,	0x03,	0x42,	0x03,	0x53	},
	{	XFER_PIO_2,	0x03,	0x65,	0x03,	0x65,	0x03,	0x87,	0x03,	0xA8	},
	{	XFER_PIO_1,	0x03,	0x65,	0x03,	0x65,	0x03,	0x87,	0x03,	0xA8	},
	{	XFER_PIO_0,	0x03,	0xA8,	0x03,	0xA8,	0x03,	0xDB,	0x03,	0xFE	},
	{	0,		0x03,	0xA8,	0x03,	0xA8,	0x03,	0xDB,	0x03,	0xFE	}
};

static struct chipset_bus_clock_list_entry via82cxxx_type_three [] = {
		/* speed */	/* 25 */	/* 33 */	/* 37.5 */	/* 41.5 */
#ifdef CONFIG_BLK_DEV_IDEDMA
	{	XFER_UDMA_4,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00	},
	{	XFER_UDMA_3,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00	},
	{	XFER_UDMA_2,	0xE0,	0x20,	0xE0,	0x20,	0xE1,	0x31,	0xE1,	0x32	},
	{	XFER_UDMA_1,	0xE1,	0x20,	0xE1,	0x20,	0xE2,	0x31,	0xE2,	0x32	},
	{	XFER_UDMA_0,	0xE2,	0x20,	0xE2,	0x20,	0xE2,	0x31,	0xE2,	0x32	},

	{	XFER_MW_DMA_2,	0x03,	0x20,	0x03,	0x20,	0x03,	0x31,	0x03,	0x32	},
	{	XFER_MW_DMA_1,	0x03,	0x31,	0x03,	0x31,	0x03,	0x42,	0x03,	0x53	},
	{	XFER_MW_DMA_0,	0x03,	0x31,	0x03,	0x31,	0x03,	0x42,	0x03,	0x53	},
#endif /* CONFIG_BLK_DEV_IDEDMA */
	{	XFER_PIO_4,	0x03,	0x20,	0x03,	0x20,	0x03,	0x31,	0x03,	0x32	},
	{	XFER_PIO_3,	0x03,	0x31,	0x03,	0x31,	0x03,	0x42,	0x03,	0x53	},
	{	XFER_PIO_2,	0x03,	0x65,	0x03,	0x65,	0x03,	0x87,	0x03,	0xA8	},
	{	XFER_PIO_1,	0x03,	0x65,	0x03,	0x65,	0x03,	0x87,	0x03,	0xA8	},
	{	XFER_PIO_0,	0x03,	0xA8,	0x03,	0xA8,	0x03,	0xDB,	0x03,	0xFE	},
	{	0,		0x03,	0xA8,	0x03,	0xA8,	0x03,	0xDB,	0x03,	0xFE	}
};

static struct chipset_bus_clock_list_entry via82cxxx_type_four [] = {
		/* speed */	/* 25 */	/* 33 */	/* 37.5 */	/* 41.5 */
#ifdef CONFIG_BLK_DEV_IDEDMA
	{	XFER_UDMA_4,	0x00,	0x00,	0xE0,	0x20,	0xE1,	0x31,	0x00,	0x00	},
	{	XFER_UDMA_3,	0x00,	0x00,	0xE1,	0x20,	0xE2,	0x31,	0x00,	0x00	},
	{	XFER_UDMA_2,	0x00,	0x00,	0xE2,	0x20,	0xE4,	0x31,	0x00,	0x00	},
	{	XFER_UDMA_1,	0x00,	0x00,	0xE4,	0x20,	0xE6,	0x31,	0x00,	0x00	},
	{	XFER_UDMA_0,	0x00,	0x00,	0xE6,	0x20,	0xE6,	0x31,	0x00,	0x00	},

	{	XFER_MW_DMA_2,	0x00,	0x00,	0x03,	0x20,	0x03,	0x31,	0x00,	0x00	},
	{	XFER_MW_DMA_1,	0x00,	0x00,	0x03,	0x31,	0x03,	0x42,	0x00,	0x00	},
	{	XFER_MW_DMA_0,	0x00,	0x00,	0x03,	0x31,	0x03,	0x42,	0x00,	0x00	},
#endif /* CONFIG_BLK_DEV_IDEDMA */
	{	XFER_PIO_4,	0x00,	0x00,	0x03,	0x20,	0x03,	0x31,	0x00,	0x00	},
	{	XFER_PIO_3,	0x00,	0x00,	0x03,	0x31,	0x03,	0x42,	0x00,	0x00	},
	{	XFER_PIO_2,	0x00,	0x00,	0x03,	0x65,	0x03,	0x87,	0x00,	0x00	},
	{	XFER_PIO_1,	0x00,	0x00,	0x03,	0x65,	0x03,	0x87,	0x00,	0x00	},
	{	XFER_PIO_0,	0x00,	0x00,	0x03,	0xA8,	0x03,	0xDB,	0x00,	0x00	},
	{	0,		0x00,	0x00,	0x03,	0xA8,	0x03,	0xDB,	0x00,	0x00	}
};

static const struct {
	const char *name;
	unsigned short vendor_id;
	unsigned short host_id;
} ApolloHostChipInfo[] = {
	{ "VT 82C585 Apollo VP1/VPX",	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C585, },
	{ "VT 82C595 Apollo VP2",	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C595, },
	{ "VT 82C597 Apollo VP3",	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C597_0, },
	{ "VT 82C598 Apollo MVP3",	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C598_0, },
	{ "VT 82C598 Apollo MVP3",	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C598_0, },
	{ "VT 82C680 Apollo P6",	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C680, },
	{ "VT 82C691 Apollo Pro",	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C691, },
	{ "VT 82C693 Apollo Pro Plus",	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C693, },
	{ "Apollo MVP4",		PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8501_0, },
	{ "VT 8371",			PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8371_0, },
	{ "VT 8601",			PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8601_0, },
	{ "AMD IronGate",		PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_FE_GATE_7006, },
};

#define NUM_APOLLO_ISA_CHIP_DEVICES	2
#define VIA_FLAG_NULL			0x00000000
#define VIA_FLAG_CHECK_REV		0x00000001
#define VIA_FLAG_ATA_66			0x00000002

static const struct {
	unsigned short host_id;
	unsigned short isa_id;
	unsigned int flags;
	struct chipset_bus_clock_list_entry * chipset_table;
} ApolloISAChipInfo[] = {
	{ PCI_DEVICE_ID_VIA_82C585,		PCI_DEVICE_ID_VIA_82C586_1,	VIA_FLAG_CHECK_REV,	via82cxxx_type_one	},
	{ PCI_DEVICE_ID_VIA_82C595,		PCI_DEVICE_ID_VIA_82C586_1,	VIA_FLAG_CHECK_REV,	via82cxxx_type_one	},
	{ PCI_DEVICE_ID_VIA_82C597_0,		PCI_DEVICE_ID_VIA_82C586_1,	VIA_FLAG_CHECK_REV,	via82cxxx_type_one	},
	{ PCI_DEVICE_ID_VIA_82C598_0,		PCI_DEVICE_ID_VIA_82C586_1,	VIA_FLAG_CHECK_REV,	via82cxxx_type_one	},
	{ PCI_DEVICE_ID_VIA_82C598_0,		PCI_DEVICE_ID_VIA_82C596,	VIA_FLAG_NULL,		via82cxxx_type_one	},
	{ PCI_DEVICE_ID_VIA_82C680,		PCI_DEVICE_ID_VIA_82C586_1,	VIA_FLAG_CHECK_REV,	via82cxxx_type_one	},
	{ PCI_DEVICE_ID_VIA_82C691,		PCI_DEVICE_ID_VIA_82C596,	VIA_FLAG_ATA_66,	via82cxxx_type_two	},
	{ PCI_DEVICE_ID_VIA_82C693,		PCI_DEVICE_ID_VIA_82C596,	VIA_FLAG_NULL,		via82cxxx_type_one	},
	{ PCI_DEVICE_ID_VIA_8501_0,		PCI_DEVICE_ID_VIA_82C686,	VIA_FLAG_ATA_66,	via82cxxx_type_two	},
	{ PCI_DEVICE_ID_VIA_8371_0,		PCI_DEVICE_ID_VIA_82C686,	VIA_FLAG_ATA_66,	via82cxxx_type_two	},
	{ PCI_DEVICE_ID_VIA_8601_0,		PCI_DEVICE_ID_VIA_8231,		VIA_FLAG_ATA_66,	via82cxxx_type_two	},
	{ PCI_DEVICE_ID_AMD_FE_GATE_7006,	PCI_DEVICE_ID_VIA_82C686,	VIA_FLAG_ATA_66,	via82cxxx_type_two	},
};

#define arraysize(x)	(sizeof(x)/sizeof(*(x)))

#define DISPLAY_VIA_TIMINGS

#if defined(DISPLAY_VIA_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static char *FIFO_str[] = {
	" 1 ",
	"3/4",
	"1/2",
	"1/4"
};

static char *control3_str[] = {
	"No limitation",
	"64",
	"128",
	"192"
};

static int via_get_info(char *, char **, off_t, int);
extern int (*via_display_info)(char *, char **, off_t, int); /* ide-proc.c */
static struct pci_dev *bmide_dev;

static char * print_apollo_drive_config (char *buf, struct pci_dev *dev)
{
	int rc;
	unsigned int time;
	byte tm;
	char *p = buf;  
 
	/* Drive Timing Control */
	rc = pci_read_config_dword(dev, 0x48, &time);
	p += sprintf(p, "Act Pls Width:  %02d          %02d           %02d          %02d\n",
			((time & 0xf0000000)>>28) + 1,
			((time & 0xf00000)>>20) + 1,
			((time & 0xf000)>>12) + 1,
			((time & 0xf0)>>4) + 1 );
	p += sprintf(p, "Recovery Time:  %02d          %02d           %02d          %02d\n",
			((time & 0x0f000000)>>24) + 1,
			((time & 0x0f0000)>>16) + 1,
			((time & 0x0f00)>>8) + 1,
			(time & 0x0f) + 1 );
 
	/* Address Setup Time */
	rc = pci_read_config_byte(dev, 0x4C, &tm);
	p += sprintf(p, "Add. Setup T.:  %01dT          %01dT           %01dT          %01dT\n",
			((tm & 0xc0)>>6) + 1,
			((tm & 0x30)>>4) + 1,
			((tm & 0x0c)>>2) + 1,
			(tm & 0x03) + 1 );
 
	/* UltraDMA33 Extended Timing Control */
 	rc = pci_read_config_dword(dev, 0x50, &time);
 	p += sprintf(p, "------------------UDMA-Timing-Control------------------------\n");
 	p += sprintf(p, "Enable Meth.:    %01d           %01d            %01d           %01d\n",
			(time & 0x80000000)	? 1 : 0,
			(time & 0x800000)	? 1 : 0,
			(time & 0x8000)		? 1 : 0,
			(time & 0x80)		? 1 : 0 );
 	p += sprintf(p, "Enable:         %s         %s          %s         %s\n",
			(time & 0x40000000)	? "yes" : "no ",
			(time & 0x400000)	? "yes" : "no ",
			(time & 0x4000)		? "yes" : "no ",
			(time & 0x40)		? "yes" : "no " );
 	p += sprintf(p, "Transfer Mode: %s         %s          %s         %s\n",
			(time & 0x20000000)	? "PIO" : "DMA",
			(time & 0x200000)	? "PIO" : "DMA",
			(time & 0x2000)		? "PIO" : "DMA",
			(time & 0x20)		? "PIO" : "DMA" );
 	p += sprintf(p, "Cycle Time:     %01dT          %01dT           %01dT          %01dT\n",
			((time & 0x03000000)>>24) + 2,
			((time & 0x030000)>>16) + 2,
			((time & 0x0300)>>8) + 2,
			(time & 0x03) + 2 );
 
 	return (char *)p;
}

static char * print_apollo_ide_config (char *buf, struct pci_dev *dev)
{
	byte time, tmp; 
	unsigned short size0, size1;
	int rc;
	char *p = buf;  
 
	rc = pci_read_config_byte(dev, 0x41, &time);
	p += sprintf(p, "Prefetch Buffer :      %s                     %s\n",
		(time & 128)	? "on " : "off",
		(time & 32)	? "on " : "off" );
	p += sprintf(p, "Post Write Buffer:     %s                     %s\n",
		(time & 64)	? "on " : "off",
		(time & 16)	? "on " : "off" );
 
	/* FIFO configuration */
	rc = pci_read_config_byte(dev, 0x43, &time);
	tmp = ((time & 0x20)>>2) + ((time & 0x40)>>3);
	p += sprintf(p, "FIFO Conf/Chan. :      %02d                      %02d\n",
			16 - tmp, tmp);
	tmp = (time & 0x0F)>>2;
	p += sprintf(p, "Threshold Prim. :      %s                     %s\n",
			FIFO_str[tmp],
			FIFO_str[time & 0x03] );
 
	/* chipset Control3 */
	rc = pci_read_config_byte(dev, 0x46, &time);
	p += sprintf(p, "Read DMA FIFO flush:   %s                     %s\n",
			(time & 0x80)	? "on " : "off",
			(time & 0x40)	? "on " : "off" );
	p += sprintf(p, "End Sect. FIFO flush:  %s                     %s\n",
			(time & 0x20)	? "on " : "off",
			(time & 0x10)	? "on " : "off" );
	p += sprintf(p, "Max DRDY Pulse Width:  %s %s\n",
			control3_str[(time & 0x03)],
			(time & 0x03) ? "PCI clocks" : "" );
 
	/* Primary and Secondary sector sizes */
	rc = pci_read_config_word(dev, 0x60, &size0);
	rc = pci_read_config_word(dev, 0x68, &size1);
	p += sprintf(p, "Bytes Per Sector:      %03d                     %03d\n",
			size0 & 0xfff,
			size1 & 0xfff );

	return (char *)p;
}

static char * print_apollo_chipset_control1 (char *buf, struct pci_dev *dev)
{
	byte t;
	int rc;
	char *p = buf;  
	unsigned short c;
	byte l, l_max;   
 
	rc = pci_read_config_word(dev, 0x04, &c);
	rc = pci_read_config_byte(dev, 0x44, &t);
	rc = pci_read_config_byte(dev, 0x0d, &l);
	rc = pci_read_config_byte(dev, 0x3f, &l_max);

	p += sprintf(p, "Command register = 0x%x\n", c);
	p += sprintf(p, "Master Read  Cycle IRDY %d Wait State\n",
			(t & 64) >>6 );
	p += sprintf(p, "Master Write Cycle IRDY %d Wait State\n",
			(t & 32) >> 5 );
	p += sprintf(p, "FIFO Output Data 1/2 Clock Advance: %s\n",
			(t & 16) ? "on " : "off" );
	p += sprintf(p, "Bus Master IDE Status Register Read Retry: %s\n",
			(t & 8) ? "on " : "off" );
	p += sprintf(p, "Latency timer = %d (max. = %d)\n",
			l, l_max);

	return (char *)p;
}
 
static char * print_apollo_chipset_control2 (char *buf, struct pci_dev *dev)
{
	byte t;
	int rc;
	char *p = buf;  
	rc = pci_read_config_byte(dev, 0x45, &t);
	p += sprintf(p, "Interrupt Steering Swap: %s\n",
			(t & 64) ? "on ":"off" );

	return (char *)p;
}
 
static char * print_apollo_chipset_control3 (char *buf, struct pci_dev *dev,
						unsigned short n)
{
	/*
	 * at that point we can be sure that register 0x20 of the
	 * chipset contains the right address...
	 */
	unsigned int bibma;
	int rc;
	byte c0, c1;    
	char *p = buf; 
 
	rc = pci_read_config_dword(dev, 0x20, &bibma);
	bibma = (bibma & 0xfff0) ;
 
	/*
	 * at that point bibma+0x2 et bibma+0xa are byte registers
	 * to investigate:
	 */
	c0 = inb((unsigned short)bibma + 0x02);
	c1 = inb((unsigned short)bibma + 0x0a);
 
	if (n == 0) {
        /*p = sprintf(p,"--------------------Primary IDE------------Secondary IDE-----");*/
		p += sprintf(p, "both channels togth:   %s                     %s\n",
				(c0&0x80) ? "no" : "yes",
				(c1&0x80) ? "no" : "yes" );
	} else {
        /*p = sprintf(p,"--------------drive0------drive1-------drive0------drive1----");*/
		p += sprintf(p, "DMA enabled:    %s         %s          %s         %s\n",
				(c0&0x20) ? "yes" : "no ",
				(c0&0x40) ? "yes" : "no ",
				(c1&0x20) ? "yes" : "no ",
				(c1&0x40) ? "yes" : "no " );
	}
 
	return (char *)p;
}

static int via_get_info (char *buffer, char **addr, off_t offset, int count)
{
	/*
	 * print what /proc/via displays,
	 * if required from DISPLAY_APOLLO_TIMINGS
	 */
	char *p = buffer;
	/* Parameter of chipset : */

	/* Miscellaneous control 1 */
	p = print_apollo_chipset_control1(buffer, bmide_dev);

	/* Miscellaneous control 2 */
	p = print_apollo_chipset_control2(p, bmide_dev);
	/* Parameters of drives: */

	/* Header */
	p += sprintf(p, "------------------Primary IDE------------Secondary IDE-----\n");
	p = print_apollo_chipset_control3(p, bmide_dev, 0);
	p = print_apollo_ide_config(p, bmide_dev);
	p += sprintf(p, "--------------drive0------drive1-------drive0------drive1----\n");
	p = print_apollo_chipset_control3(p, bmide_dev, 1);
	p = print_apollo_drive_config(p, bmide_dev);
 
	return p-buffer;	/* hoping it is less than 4K... */
}

#endif  /* defined(DISPLAY_VIA_TIMINGS) && defined(CONFIG_PROC_FS) */

/*
 *  Used to set Fifo configuration via kernel command line:
 */

byte via_proc = 0;
byte fifoconfig = 0;
static byte newfifo = 0;

/* Used to just intialize once Fifo configuration */
static short int done = 0;

/*
 *  Set VIA Chipset Timings for (U)DMA modes enabled.
 *
 *  VIA Apollo chipset has complete support for
 *  setting up the timing parameters.
 */
static void set_via_timings (ide_hwif_t *hwif)
{
	struct pci_dev  *dev = hwif->pci_dev;
	byte post  = hwif->channel ? 0x30 : 0xc0;
	byte flush = hwif->channel ? 0x50 : 0xa0;
	int mask = hwif->channel ? ((newfifo & 0x60) ? 0 : 1) :
				   (((newfifo & 0x60) == 0x60) ? 1 : 0);
	byte via_config = 0;
	int rc = 0, errors = 0;

	printk("%s: VIA Bus-Master ", hwif->name);

	/*
	 * setting IDE read prefetch buffer and IDE post write buffer.
	 * (This feature allows prefetched reads and post writes).
	 */
	if ((rc = pci_read_config_byte(dev, 0x41, &via_config))) 
		errors++;

	if (mask) {
		if ((rc = pci_write_config_byte(dev, 0x41, via_config & ~post)))
			errors++;
	} else {
		if ((rc = pci_write_config_byte(dev, 0x41, via_config | post)))
			errors++;
	}

	/*
	 * setting Channel read and End-of-sector FIFO flush.
	 * (This feature ensures that FIFO flush is enabled:
	 *  - for read DMA when interrupt asserts the given channel.
	 *  - at the end of each sector for the given channel.)
	 */
	if ((rc = pci_read_config_byte(dev, 0x46, &via_config)))
		errors++;

	if (mask) {
		if ((rc = pci_write_config_byte(dev, 0x46, via_config & ~flush)))
			errors++;
	} else {
		if ((rc = pci_write_config_byte(dev, 0x46, via_config | flush)))
			errors++;
	}

	if (!hwif->dma_base)
		printk("Config %s. No DMA Enabled\n",
			errors ? "ERROR":"Success");
	else
		printk("(U)DMA Timing Config %s\n",
			errors ? "ERROR" : "Success");
}

/*
 *  Sets VIA 82cxxx FIFO configuration:
 *  This chipsets gets a splitable fifo. This can be driven either by command
 *  line option (eg "splitfifo=2,2,3" which asks this driver to switch all the 
 *  16 fifo levels to the second drive, and give it a threshold of 3 for (u)dma 
 *  triggering.
 */

static int via_set_fifoconfig(ide_hwif_t *hwif)
{
	byte fifo;
	unsigned int timings;
	struct pci_dev  *dev = hwif->pci_dev;

	/* read port configuration */
	if (pci_read_config_dword(dev, 0x40, &timings))
		return 1;
   
	/* first read actual fifo config: */
	if (pci_read_config_byte(dev, 0x43, &fifo))
		return 1;

	/* keep 4 and 7 bit as they seem to differ between chipsets flavors... */
	newfifo = fifo & 0x90;

	if (fifoconfig) {
		/* we received a config request from kernel command line: */
		newfifo |= fifoconfig & 0x6f;
	} else {
		/* If ever just one channel is unused, allocate all fifo levels to it
		 * and give it a 3/4 threshold for (u)dma transfers.
		 * Otherwise, share it evenly between channels:
		 */
		if ((timings & 3) == 2) {
			/* only primary channel is  enabled
			 * 16 buf. to prim. chan. thresh=3/4
			 */
			newfifo |= 0x06;
		} else if ((timings & 3) == 1) {
			/* only secondary channel is enabled!
			 * 16 buffers to sec. ch. thresh=3/4
			 */
			newfifo |= 0x69;
		} else {
			/* fifo evenly distributed: */
			newfifo |= 0x2a;
		}
	}

	/* write resulting configuration to chipset: */
	if (pci_write_config_byte(dev, 0x43, newfifo))
		return 1;

	/* and then reread it to get the actual one */
	if (pci_read_config_byte(dev, 0x43, &newfifo))
		return 1;

	/* print a kernel report: */
	printk("Split FIFO Configuration: %s Primary buffers, threshold = %s\n",
		((newfifo & 0x60) == 0x60)	? " 0" :
		((newfifo & 0x60)		? " 8" : "16"),
		!(newfifo & 0x0c)		? "1" :
		(!(newfifo & 0x08)		? "3/4" :
		(newfifo & 0x04)		? "1/4" : "1/2"));

	printk("                          %s Second. buffers, threshold = %s\n",
		((newfifo & 0x60) == 0x60)	? "16" :
		((newfifo & 0x60)		? " 8" : " 0"),
		!(newfifo & 0x03)		? "1" :
		(!(newfifo & 0x02)		? "3/4" :
		(newfifo & 0x01)		? "1/4" : "1/2"));
	return 0;
}

#ifdef CONFIG_VIA82CXXX_TUNING
static byte pci_bus_clock_list (byte speed, int ide_clock, struct chipset_bus_clock_list_entry * chipset_table)
{
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			switch(ide_clock) {
				case 25: return chipset_table->chipset_settings_25;
				case 33: return chipset_table->chipset_settings_33;
				case 37: return chipset_table->chipset_settings_37;
				case 41: return chipset_table->chipset_settings_41;
				default: break;
			}
		}
	return 0x00;
}

static byte pci_bus_clock_list_ultra (byte speed, int ide_clock, struct chipset_bus_clock_list_entry * chipset_table)
{
	for ( ; chipset_table->xfer_speed ; chipset_table++)
		if (chipset_table->xfer_speed == speed) {
			switch(ide_clock) {
				case 25: return chipset_table->ultra_settings_25;
				case 33: return chipset_table->ultra_settings_33;
				case 37: return chipset_table->ultra_settings_37;
				case 41: return chipset_table->ultra_settings_41;
				default: break;
			}
		}
	return 0x00;
}

static int via82cxxx_tune_chipset (ide_drive_t *drive, byte speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	struct chipset_bus_clock_list_entry * temp_table = NULL;

	byte unit		= (drive->select.b.unit & 0x01) ? 1 : 0;
	byte ata2_pci		= 0x00;
	byte ata3_pci		= 0x00;
	byte timing		= 0x00;
	byte ultra		= 0x00;
	int			err;

	int bus_speed		= system_bus_clock();

	if (via82cxxx_table == NULL)
		return -1;

	switch(drive->dn) {
		case 0: ata2_pci = 0x4b; ata3_pci = 0x53; break;
		case 1: ata2_pci = 0x4a; ata3_pci = 0x52; break;
		case 2: ata2_pci = 0x49; ata3_pci = 0x51; break;
		case 3: ata2_pci = 0x48; ata3_pci = 0x50; break;
		default:
			return -1;
	}

	if ((via82cxxx_table == via82cxxx_type_four) &&
	    (!(hwif->udma_four)) &&
	    (speed <= XFER_UDMA_2)) {
		temp_table = via82cxxx_type_three;
	} else {
		temp_table = via82cxxx_table;
	}

	pci_read_config_byte(dev, ata2_pci, &timing);
	timing = pci_bus_clock_list(speed, bus_speed, temp_table);
	pci_write_config_byte(dev, ata2_pci, timing);

	pci_read_config_byte(dev, ata3_pci, &ultra);
	ultra = pci_bus_clock_list_ultra(speed, bus_speed, temp_table);
	if ((unit) && (hwif->udma_four))
		ultra |= 0x04;
	pci_write_config_byte(dev, ata3_pci, ultra);

	if (!drive->init_speed)
		drive->init_speed = speed;

	err = ide_config_drive_speed(drive, speed);

	drive->current_speed = speed;
	return(err);
}

static void config_chipset_for_pio (ide_drive_t *drive)
{
	unsigned short eide_pio_timing[6] = {960, 480, 240, 180, 120, 90};
	unsigned short xfer_pio		= drive->id->eide_pio_modes;
	byte				timing, speed, pio;

	pio = ide_get_best_pio_mode(drive, 255, 5, NULL);

	if (xfer_pio> 4)
		xfer_pio = 0;

	if (drive->id->eide_pio_iordy > 0) {
		for (xfer_pio = 5;
			xfer_pio>0 &&
			drive->id->eide_pio_iordy>eide_pio_timing[xfer_pio];
			xfer_pio--);
	} else {
		xfer_pio = (drive->id->eide_pio_modes & 4) ? 0x05 :
			   (drive->id->eide_pio_modes & 2) ? 0x04 :
			   (drive->id->eide_pio_modes & 1) ? 0x03 :
			   (drive->id->tPIO & 2) ? 0x02 :
			   (drive->id->tPIO & 1) ? 0x01 : xfer_pio;
	}

	timing = (xfer_pio >= pio) ? xfer_pio : pio;
	switch(timing) {
		case 4:	speed = XFER_PIO_4; break;
		case 3:	speed = XFER_PIO_3; break;
		case 2:	speed = XFER_PIO_2; break;
		case 1:	speed = XFER_PIO_1; break;
		default:
			speed = (!drive->id->tPIO) ? XFER_PIO_0 : XFER_PIO_SLOW;
			break;
	}
	(void) via82cxxx_tune_chipset(drive, speed);
}

static void via82cxxx_tune_drive (ide_drive_t *drive, byte pio)
{
	byte speed;
	switch(pio) {
		case 4:		speed = XFER_PIO_4; break;
		case 3:		speed = XFER_PIO_3; break;
		case 2:		speed = XFER_PIO_2; break;
		case 1:		speed = XFER_PIO_1; break;
		default:	speed = XFER_PIO_0; break;
	}
	(void) via82cxxx_tune_chipset(drive, speed);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	byte speed		= 0x00;
	byte ultra66		= eighty_ninty_three(drive);
	byte ultra100		= 0;
	int  rval;

	if ((id->dma_ultra & 0x0020) && (ultra66) && (ultra100)) {
		speed = XFER_UDMA_5;
	} else if ((id->dma_ultra & 0x0010) && (ultra66)) {
		speed = XFER_UDMA_4;
	} else if ((id->dma_ultra & 0x0008) && (ultra66)) {
		speed = XFER_UDMA_3;
	} else if (id->dma_ultra & 0x0004) {
		speed = XFER_UDMA_2;
	} else if (id->dma_ultra & 0x0002) {
		speed = XFER_UDMA_1;
	} else if (id->dma_ultra & 0x0001) {
		speed = XFER_UDMA_0;
	} else if (id->dma_mword & 0x0004) {
		speed = XFER_MW_DMA_2;
	} else if (id->dma_mword & 0x0002) {
		speed = XFER_MW_DMA_1;
	} else if (id->dma_mword & 0x0001) {
		speed = XFER_MW_DMA_0;
	} else {
		return ((int) ide_dma_off_quietly);
	}

	(void) via82cxxx_tune_chipset(drive, speed);

	rval = (int)(	((id->dma_ultra >> 11) & 3) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);
	return rval;
}

static int config_drive_xfer_rate (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	ide_dma_action_t dma_func = ide_dma_on;

	if (id && (id->capability & 1) && HWIF(drive)->autodma) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			dma_func = ide_dma_off;
			goto fast_ata_pio;
		}
		dma_func = ide_dma_off_quietly;
		if (id->field_valid & 4) {
			if (id->dma_ultra & 0x002F) {
				/* Force if Capable UltraDMA */
				dma_func = config_chipset_for_dma(drive);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if (id->dma_mword & 0x0007) {
				/* Force if Capable regular DMA modes */
				dma_func = config_chipset_for_dma(drive);
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
		} else if (ide_dmaproc(ide_dma_good_drive, drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			dma_func = config_chipset_for_dma(drive);
			if (dma_func != ide_dma_on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		dma_func = ide_dma_off_quietly;
no_dma_set:
		config_chipset_for_pio(drive);
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

int via82cxxx_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return config_drive_xfer_rate(drive);
		default:
			break;
	}
	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}
#endif /* CONFIG_BLK_DEV_IDEDMA */
#endif /* CONFIG_VIA82CXXX_TUNING */

unsigned int __init pci_init_via82cxxx (struct pci_dev *dev, const char *name)
{
	struct pci_dev *host;
	struct pci_dev *isa;
	int i, j, ata33, ata66;

	int bus_speed = system_bus_clock();
	byte revision = 0;

	for (i = 0; i < arraysize (ApolloHostChipInfo) && !host_dev; i++) {
		host = pci_find_device (ApolloHostChipInfo[i].vendor_id,
					ApolloHostChipInfo[i].host_id,
					NULL);
		if (!host)
			continue;

		host_dev = host;
		printk(ApolloHostChipInfo[i].name);
		printk("\n");
		for (j = 0; j < arraysize (ApolloISAChipInfo) && !isa_dev; j++) {
			if (ApolloISAChipInfo[j].host_id !=
			    ApolloHostChipInfo[i].host_id)
				continue;

			isa = pci_find_device (PCI_VENDOR_ID_VIA,
					ApolloISAChipInfo[j].isa_id,
					NULL);
			if (!isa)
				continue;

			isa_dev = isa;

			ata33 = 1;
			ata66 = 0;

			via82cxxx_table = ApolloISAChipInfo[j].chipset_table;

			if (ApolloISAChipInfo[j].flags & VIA_FLAG_CHECK_REV) {
				pci_read_config_byte(isa_dev, 0x0d, &revision);
				ata33 = (revision >= 0x20) ? 1 : 0;
			} else if (ApolloISAChipInfo[j].flags & VIA_FLAG_ATA_66) {
				byte ata66_0 = 0, ata66_1 = 0;
				ata33 = 0;
				ata66 = 1;
				pci_read_config_byte(dev, 0x50, &ata66_1);
				pci_read_config_byte(dev, 0x52, &ata66_0);
				if ((ata66_0 & 0x04) || (ata66_1 & 0x04)) {
					via82cxxx_table = (bus_speed == 33 || bus_speed == 37) ?
						via82cxxx_type_four :
						via82cxxx_type_three;
				}
			}

			if (ata33 | ata66)
				printk(" Chipset Core ATA-%s", ata66 ? "66" : "33");
		}
		printk("\n");
	}

#if defined(DISPLAY_VIA_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!via_proc) {
		via_proc = 1;
		bmide_dev = dev;
		via_display_info = &via_get_info;
	}
#endif /* DISPLAY_VIA_TIMINGS &&  CONFIG_PROC_FS*/

	return 0;
}

unsigned int __init ata66_via82cxxx (ide_hwif_t *hwif)
{
	byte ata66	= 0;
	byte ata66reg	= hwif->channel ? 0x50 : 0x52;
	pci_read_config_byte(hwif->pci_dev, ata66reg, &ata66);

	return ((ata66 & 0x04) ? 1 : 0);
}

void __init ide_init_via82cxxx (ide_hwif_t *hwif)
{
	set_via_timings(hwif);

#ifdef CONFIG_VIA82CXXX_TUNING
	hwif->tuneproc = &via82cxxx_tune_drive;
	hwif->speedproc = &via82cxxx_tune_chipset;
	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;
	hwif->autodma = 0;
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (hwif->dma_base) {
		hwif->dmaproc = &via82cxxx_dmaproc;
		hwif->autodma = 1;
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */
#endif /* CONFIG_VIA82CXXX_TUNING */
}

/*
 *  ide_dmacapable_via82cxxx(ide_hwif_t *, unsigned long)
 *  checks if channel "channel" of if hwif is dma
 *  capable or not, according to kernel command line,
 *  and the new fifo settings.
 *  It calls "ide_setup_dma" on capable mainboards, and
 *  bypasses the setup if not capable.
 */

void ide_dmacapable_via82cxxx (ide_hwif_t *hwif, unsigned long dmabase)
{
	if (!done) {
		via_set_fifoconfig(hwif);
		done = 1;
	}

	/*
	 * check if any fifo is available for requested port:
	 */
	if (((hwif->channel == 0) && ((newfifo & 0x60) == 0x60)) ||
	    ((hwif->channel == 1) && ((newfifo & 0x60) == 0x00))) {
		printk("    %s: VP_IDE Bus-Master DMA disabled (FIFO setting)\n", hwif->name);
	} else {
		ide_setup_dma(hwif, dmabase, 8);
	}
}
