/*
 * linux/drivers/block/via82c586.c	Version 0.03	Nov. 19, 1998
 *
 *  Copyright (C) 1998 Michel Aubry, Maintainer
 *  Copyright (C) 1998 Andre Hedrick, Integrater
 *
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
 */

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

static int via_get_info(char *, char **, off_t, int, int);
extern int (*via_display_info)(char *, char **, off_t, int, int); /* ide-proc.c */
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

static int via_get_info (char *buffer, char **addr, off_t offset, int count, int dummy)
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
 *  Sets VIA 82c586 FIFO configuration:
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

#if defined(DISPLAY_VIA_TIMINGS) && defined(CONFIG_PROC_FS)
	bmide_dev = hwif->pci_dev;
	via_display_info = &via_get_info;
#endif /* DISPLAY_VIA_TIMINGS &&  CONFIG_PROC_FS*/
	return 0;
}

/*
 *  ide_dmacapable_via82c568(ide_hwif_t *, unsigned long)
 *  checks if channel "channel" of if hwif is dma
 *  capable or not, according to kernel command line,
 *  and the new fifo settings.
 *  It calls "ide_setup_dma" on capable mainboards, and
 *  bypasses the setup if not capable.
 */

void ide_dmacapable_via82c586 (ide_hwif_t *hwif, unsigned long dmabase)
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

__initfunc(void ide_init_via82c586 (ide_hwif_t *hwif))
{
	set_via_timings(hwif);
}

