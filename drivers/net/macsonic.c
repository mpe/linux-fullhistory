/*
 * macsonic.c
 *
 * (C) 1998 Alan Cox
 *
 * Debugging Andreas Ehliar, Michael Schmitz
 *
 * Based on code
 * (C) 1996 by Thomas Bogendoerfer (tsbogend@bigbug.franken.de)
 * 
 * This driver is based on work from Andreas Busse, but most of
 * the code is rewritten.
 * 
 * (C) 1995 by Andreas Busse (andy@waldorf-gmbh.de)
 *
 * A driver for the Mac onboard Sonic ethernet chip.
 *
 * 98/12/21 MSch: judged from tests on Q800, it's basically working, 
 *		  but eating up both receive and transmit resources
 *		  and duplicating packets. Needs more testing.
 *
 * 99/01/03 MSch: upgraded to version 0.92 of the core driver, fixed.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/nubus.h>
#include <asm/bootinfo.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/macintosh.h>

#include <linux/errno.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <config/macsonic.h>

#define SREGS_PAD(n)    u16 n;

#include "sonic.h"

extern int mac_onboard_sonic_probe(void);

static int setup_debug = -1;
static int setup_offset = -1;
static int setup_shift = -1;

/*
 * This seems to be the right default for the Q800
 */

static int reg_offset = 0;
static int reg_shift = 0;

/*
 * Macros to access SONIC registers
 */
 
#define MAC_SONIC_REGISTERS	0x50F0A000
#define MAC_SONIC_PROM_BASE	0x50f08000
#define MAC_SONIC_IRQ		9	/* Nubus 9 */

/*
 *    FIXME: We may need to invert the byte ordering. These should
 *      be ok for other aspects as they are uncached spaces.
 *      The original macros from jazzsonic.c works for me
 *      on my LC 630, YMMV /Andreas Ehliar
 */

#if 0
#define SONIC_READ(reg) \
	*((volatile unsigned int *)base_addr+((reg)<<2)+2)

#define SONIC_WRITE(reg,val) \
	*((volatile unsigned int *)base_addr+((reg)<<2)+2) = val
#else
#define SONIC_READ(reg) \
	*((volatile unsigned int *)base_addr+reg)

#define SONIC_WRITE(reg,val) \
	*((volatile unsigned int *)base_addr+reg) = val
#endif

#define SONIC_READ_PROM(addr) \
	*((volatile unsigned char *)prom_addr+addr)
/*
 * Function : mac_sonic_setup(char *str, int *ints)
 *
 * Purpose : booter command line initialization of the overrides array,
 *
 * Inputs : str - unused, ints - array of integer parameters with ints[0]
 *	equal to the number of ints.
 *
 * Currently unused in the new driver; need to add settable parameters to the 
 * detect function.
 *
 */

void mac_sonic_setup(char *str, int *ints) {
	/* Format of macsonic parameter is:
	 *   macsonic=<debug>,<offset>,<shift>
	 * Negative values mean don't change.
	 */
	
	/* Grmbl... the standard parameter parsing can't handle negative numbers
	 * :-( So let's do it ourselves!
	 */

	int i = ints[0]+1, fact;

	while( str && (isdigit(*str) || *str == '-') && i <= 10) {
		if (*str == '-')
			fact = -1, ++str;
		else
			fact = 1;
		ints[i++] = simple_strtoul( str, NULL, 0 ) * fact;
		if ((str = strchr( str, ',' )) != NULL)
			++str;
	}
	ints[0] = i-1;
	
	if (ints[0] < 1) {
		printk( "mac_sonic_setup: no arguments!\n" );
		return;
	}

	if (ints[0] >= 1) {
	       	/* 0 <= n <= 2 */
		if (ints[1] >= 0 && ints[1] <= 8)
			setup_debug = ints[1];
		else if (ints[1] > 16)
			printk( "mac_sonic_setup: invalid debug level %d !\n", ints[1] );
	}
	if (ints[0] >= 2) {
	       	/* 0 <= n <= 2 */
		if (ints[2] >= 0 && ints[2] <= 16)
			setup_offset = ints[2];
		else if (ints[2] > 16)
			printk( "mac_sonic_setup: invalid offset %d !\n", ints[2] );
	}
	if (ints[0] >= 3) {
	       	/* 0 <= n <= 2 */
		if (ints[3] >= 0 && ints[3] <= 16)
			setup_shift = ints[3];
		else if (ints[3] > 16)
			printk( "mac_sonic_setup: invalid shift %d !\n", ints[3] );
	}
}

static int sonic_debug = 0;

/*
 * For reversing the PROM address
 */

static unsigned char nibbletab[] = {0, 8, 4, 12, 2, 10, 6, 14,
				    1, 9, 5, 13, 3, 11, 7, 15};

int __init mac_onboard_sonic_probe(void)
{
	struct net_device *dev;
	unsigned int silicon_revision;
	unsigned int val;
	struct sonic_local *lp;
	int i;
	int base_addr = MAC_SONIC_REGISTERS;
	int prom_addr = MAC_SONIC_PROM_BASE;
	static int one=0;
	
	if (!MACH_IS_MAC)
		return -ENODEV;

	if(++one!=1)	/* Only one is allowed */
		return -ENODEV;

	printk(KERN_INFO "Checking for internal Macintosh ethernet (SONIC).. ");

	if (macintosh_config->ether_type != MAC_ETHER_SONIC)
	{
		printk("none.\n");
		return -ENODEV;
	}
	
	printk("yes\n");
	
	if (setup_debug >= 0)
	  sonic_debug = setup_debug;

	/*
	 * This may depend on the actual Mac model ... works for me.
	 */
	reg_offset = 
	  (setup_offset >= 0) ? setup_offset : 0;
	reg_shift = 
	  (setup_shift >= 0) ? setup_shift : 0;

	/*
	 * get the Silicon Revision ID. If this is one of the known
	 * one assume that we found a SONIC ethernet controller at
	 * the expected location.
	 * (This is not implemented in the Macintosh driver yet; need
	 * to collect values from various sources. Mine is 0x4 ...)
	 */

	silicon_revision = SONIC_READ(SONIC_SR);
	if (sonic_debug > 1)
		printk("SONIC Silicon Revision = 0x%04x\n", silicon_revision);

	/*
	 * We need to allocate sonic_local later on, making sure it's
	 * aligned on a 64k boundary. So, no space for dev->priv allocated
	 * here ...
	 */
	dev = init_etherdev(0,0);
	
	if(dev==NULL)
		return -ENOMEM;

	printk("%s: %s found at 0x%08x, ",
	       dev->name, "SONIC ethernet", base_addr);

	if (sonic_debug > 1)
		printk("using offset %d shift %d,", reg_offset, reg_shift);

	/* Fill in the 'dev' fields. */
	dev->base_addr = base_addr;
	dev->irq = MAC_SONIC_IRQ;

	/*
	 * Put the sonic into software reset, then
	 * retrieve and print the ethernet address.
	 */

	SONIC_WRITE(SONIC_CMD, SONIC_CR_RST);

	/*
	 *        We can't trust MacOS to initialise things it seems.
	 */

	if (sonic_debug > 1)
		printk("SONIC_DCR was %X\n",SONIC_READ(SONIC_DCR));
	
	SONIC_WRITE(SONIC_DCR,
		    SONIC_DCR_RFT1 | SONIC_DCR_TFT0 | SONIC_DCR_EXBUS | SONIC_DCR_DW);

	/*
	 *  We don't want floating spare IRQ's around, not on
	 *  level triggered systems!
	 *  Strange though - writing to the ISR only clears currently
	 *  pending IRQs, but doesn't disable them... Does this make 
	 *  a difference?? Seems it does ...
	 */
#if 1
	SONIC_WRITE(SONIC_ISR,0x7fff);
	SONIC_WRITE(SONIC_IMR,0);
#else
	SONIC_WRITE(SONIC_ISR, SONIC_IMR_DEFAULT);
#endif
	
	/* This is how it is done in jazzsonic.c
	 * It doesn't seem to work here though.
	 */
	if (sonic_debug > 2) {
		printk("Retreiving CAM entry 0. This should be the HW address.\n");
		
		SONIC_WRITE(SONIC_CEP, 0);
		for (i = 0; i < 3; i++)
		{
			val = SONIC_READ(SONIC_CAP0 - i);
			dev->dev_addr[i * 2] = val;
			dev->dev_addr[i * 2 + 1] = val >> 8;
		}

		printk("HW Address from CAM 0: ");
		for (i = 0; i < 6; i++)
		{
			printk("%2.2x", dev->dev_addr[i]);
			if (i < 5)
				printk(":");
		}
		printk("\n");

		printk("Retreiving CAM entry 15. Another candidate...\n");

		/*
		 * MacOS seems to use CAM entry 15 ...
		 */
	       	SONIC_WRITE(SONIC_CEP, 15);
		for (i = 0; i < 3; i++)
		{
			val = SONIC_READ(SONIC_CAP0 - i);
			dev->dev_addr[i * 2] = val;
			dev->dev_addr[i * 2 + 1] = val >> 8;
		}

		printk("HW Address from CAM 15: ");
		for (i = 0; i < 6; i++)
		{
			printk("%2.2x", dev->dev_addr[i]);
			if (i < 5)
				printk(":");
		}
		printk("\n");
	}

	/*
	 * if we can read the PROM, we're safe :-)
	 */
	if (sonic_debug > 1)
		printk("Retreiving HW address from the PROM: ");

	for(i=0;i<6;i++){
                dev->dev_addr[i]=SONIC_READ_PROM(i);
        }                             
	if (sonic_debug > 1) {
	        for (i = 0; i < 6; i++)
		{
			printk("%2.2x", dev->dev_addr[i]);
			if (i <	5)
				printk(":");
		}
		printk("\n");
	}
	/*
	 *                If its not one of these we have
	 *          screwed up on this Mac model
	 */

	if (memcmp(dev->dev_addr, "\x08\x00\x07", 3) &&
	    memcmp(dev->dev_addr, "\x00\xA0\x40", 3) &&
	    memcmp(dev->dev_addr, "\x00\x05\x02", 3))
	{
		/*
		 * Try bit reversed
		 */
		for(i=0;i<6;i++){
			val = SONIC_READ_PROM(i);
			dev->dev_addr[i]=(nibbletab[val & 0xf] << 4) | 
					  nibbletab[(val >> 4) &0xf];
		}
		if (sonic_debug > 1) {
			printk("Trying bit reversed:  ");
			for (i = 0; i < 6; i++)
			{
				printk("%2.2x", dev->dev_addr[i]);
				if (i < 5)
					printk(":");
			}
			printk("\n");
		}
		if (memcmp(dev->dev_addr, "\x08\x00\x07", 3) &&
		    memcmp(dev->dev_addr, "\x00\xA0\x40", 3) &&
		    memcmp(dev->dev_addr, "\x00\x05\x02", 3))
		{
		        /*
			 * Still nonsense ... messed up someplace!
			 */
			printk("ERROR (INVALID MAC)\n");
			return -EIO;
		}
	}

	printk(" MAC ");
	for (i = 0; i < 6; i++)
	{
		printk("%2.2x", dev->dev_addr[i]);
		if (i < 5)
			printk(":");
	}

	printk(" IRQ %d\n", MAC_SONIC_IRQ);

	/* Initialize the device structure. */
	if (dev->priv == NULL)
	{
		if (sonic_debug > 2) {
			printk("Allocating memory for dev->priv aka lp\n");
			printk("Memory to allocate: %d\n",sizeof(*lp));
		}
		/*
		 * the memory be located in the same 64kb segment
		 */
		lp = NULL;
		i = 0;
		do
		{
			lp = (struct sonic_local *) kmalloc(sizeof(*lp), GFP_KERNEL);
			if ((unsigned long) lp >> 16 != ((unsigned long) lp + sizeof(*lp)) >> 16)
			{
				/* FIXME, free the memory later */
				kfree(lp);
				lp = NULL;
			}
		}
		while (lp == NULL && i++ < 20);

		if (lp == NULL)
		{
			printk("%s: couldn't allocate memory for descriptors\n",
			       dev->name);
			return -ENOMEM;
		}

		if (sonic_debug > 2) {
			printk("Memory allocated after %d tries\n",i);
		}

		/* XXX sonic_local has the TDA, RRA, RDA, don't cache */
		kernel_set_cachemode((u32)lp, 8192, IOMAP_NOCACHE_SER);
		memset(lp, 0, sizeof(struct sonic_local));

		lp->cda_laddr = (u32)lp;
		if (sonic_debug > 2) {
			printk("memory allocated for sonic at 0x%x\n",lp);
		}
		lp->tda_laddr = lp->cda_laddr + sizeof(lp->cda);
		lp->rra_laddr = lp->tda_laddr + sizeof(lp->tda);
		lp->rda_laddr = lp->rra_laddr + sizeof(lp->rra);

		/* allocate receive buffer area */
		/* FIXME, maybe we should use skbs */
		if ((lp->rba = (char *) kmalloc(SONIC_NUM_RRS * SONIC_RBSIZE, GFP_KERNEL)) == NULL)
		{
			printk("%s: couldn't allocate receive buffers\n", dev->name);
			return -ENOMEM;
		}
		/* XXX RBA written by Sonic, not cached either */
		kernel_set_cachemode((u32)lp->rba, 6*8192, IOMAP_NOCACHE_SER);
		lp->rba_laddr = (u32)lp->rba;
		flush_cache_all();
		dev->priv = (struct sonic_local *) lp;
	}
	lp = (struct sonic_local *) dev->priv;
	dev->open = sonic_open;
	dev->stop = sonic_close;
	dev->hard_start_xmit = sonic_send_packet;
	dev->get_stats = sonic_get_stats;
	dev->set_multicast_list = &sonic_multicast_list;

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(dev);
	return 0;
}

/*
 *    SONIC uses a nubus IRQ
 */

#define sonic_request_irq(irq, vec, flags, name, dev) \
		nubus_request_irq(irq, dev, vec)
#define sonic_free_irq(irq,id)	nubus_free_irq(irq)

/*
 *    No funnies on memory mapping.
 */

#define sonic_chiptomem(x)	(x)

/*
 *    No VDMA on a Macintosh. So we need request no such facility.
 */

#define vdma_alloc(x,y)		((u32)(x))
#define vdma_free(x)
#define PHYSADDR(x)		(x)

#include "sonic.c"
