/* cs89x0.c: A Crystal Semiconductor CS89[02]0 driver for linux. */
/*
	Written 1996 by Russell Nelson, with reference to skeleton.c
	written 1993-1994 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	The author may be reached at nelson@crynwr.com, Crynwr
	Software, 11 Grant St., Potsdam, NY 13676

  Changelog:

  Mike Cruse        : mcruse@cti-ltd.com
                    : Changes for Linux 2.0 compatibility. 
                    : Added dev_id parameter in net_interrupt(),
                    : request_irq() and free_irq(). Just NULL for now.

  Mike Cruse        : Added MOD_INC_USE_COUNT and MOD_DEC_USE_COUNT macros
                    : in net_open() and net_close() so kerneld would know
                    : that the module is in use and wouldn't eject the 
                    : driver prematurely.

  Mike Cruse        : Rewrote init_module() and cleanup_module using 8390.c
                    : as an example. Disabled autoprobing in init_module(),
                    : not a good thing to do to other devices while Linux
                    : is running from all accounts.
                    
  Alan Cox          : Removed 1.2 support, added 2.1 extra counters.
*/

static char *version =
"cs89x0.c:v1.02 11/26/96 Russell Nelson <nelson@crynwr.com>\n";

/* ======================= configure the driver here ======================= */

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 2
#endif

/* ======================= end of configuration ======================= */


/* Always include 'config.h' first in case the user wants to turn on
   or override something. */
#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#define PRINTK(x) printk x

/*
  Sources:

	Crynwr packet driver epktisa.

	Crystal Semiconductor data sheets.

*/

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include "cs89x0.h"

/* First, a few definitions that the brave might change. */
/* A zero-terminated list of I/O addresses to be probed. */
static unsigned int netcard_portlist[] __initdata =
   { 0x300, 0x320, 0x340, 0x200, 0x220, 0x240, 0x260, 0x280, 0x2a0, 0x2c0, 0x2e0, 0};

static unsigned int net_debug = NET_DEBUG;

/* The number of low I/O ports used by the ethercard. */
#define NETCARD_IO_EXTENT	16

/* Information that need to be kept for each board. */
struct net_local {
	struct net_device_stats stats;
	int chip_type;		/* one of: CS8900, CS8920, CS8920M */
	char chip_revision;	/* revision letter of the chip ('A'...) */
	int send_cmd;		/* the propercommand used to send a packet. */
	int auto_neg_cnf;
	int adapter_cnf;
	int isa_config;
	int irq_map;
	int rx_mode;
	int curr_rx_cfg;
        int linectl;
        int send_underrun;      /* keep track of how many underruns in a row we get */
	struct sk_buff *skb;
};

/* Index to functions, as function prototypes. */

extern int cs89x0_probe(struct device *dev);

static int cs89x0_probe1(struct device *dev, int ioaddr);
static int net_open(struct device *dev);
static int	net_send_packet(struct sk_buff *skb, struct device *dev);
static void net_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void set_multicast_list(struct device *dev);
static void net_rx(struct device *dev);
static int net_close(struct device *dev);
static struct net_device_stats *net_get_stats(struct device *dev);
static void reset_chip(struct device *dev);
static int get_eeprom_data(struct device *dev, int off, int len, int *buffer);
static int get_eeprom_cksum(int off, int len, int *buffer);
static int set_mac_address(struct device *dev, void *addr);


/* Example routines you must write ;->. */
#define tx_done(dev) 1


/* Check for a network adaptor of this type, and return '0' iff one exists.
   If dev->base_addr == 0, probe all likely locations.
   If dev->base_addr == 1, always return failure.
   If dev->base_addr == 2, allocate space for the device and return success
   (detachable devices only).
   */
#ifdef HAVE_DEVLIST
/* Support for a alternate probe manager, which will eliminate the
   boilerplate below. */
struct netdev_entry netcard_drv =
{"netcard", cs89x0_probe1, NETCARD_IO_EXTENT, netcard_portlist};
#else
__initfunc(int
cs89x0_probe(struct device *dev))
{
	int i;
	int base_addr = dev ? dev->base_addr : 0;

	if (base_addr > 0x1ff)		/* Check a single specified location. */
		return cs89x0_probe1(dev, base_addr);
	else if (base_addr != 0)	/* Don't probe at all. */
		return ENXIO;

	for (i = 0; netcard_portlist[i]; i++) {
		int ioaddr = netcard_portlist[i];
		if (check_region(ioaddr, NETCARD_IO_EXTENT))
			continue;
		if (cs89x0_probe1(dev, ioaddr) == 0)
			return 0;
	}
	printk("cs89x0: no cs8900 or cs8920 detected.  Be sure to disable PnP with SETUP\n");
	return ENODEV;
}
#endif

static int inline
readreg(struct device *dev, int portno)
{
	outw(portno, dev->base_addr + ADD_PORT);
	return inw(dev->base_addr + DATA_PORT);
}

static void inline
writereg(struct device *dev, int portno, int value)
{
	outw(portno, dev->base_addr + ADD_PORT);
	outw(value, dev->base_addr + DATA_PORT);
}


static int inline
readword(struct device *dev, int portno)
{
	return inw(dev->base_addr + portno);
}

static void inline
writeword(struct device *dev, int portno, int value)
{
	outw(value, dev->base_addr + portno);
}

__initfunc(static int
wait_eeprom_ready(struct device *dev))
{
	int timeout = jiffies;
	/* check to see if the EEPROM is ready, a timeout is used -
	   just in case EEPROM is ready when SI_BUSY in the
	   PP_SelfST is clear */
	while(readreg(dev, PP_SelfST) & SI_BUSY)
		if (jiffies - timeout >= 40)
			return -1;
	return 0;
}

__initfunc(static int
get_eeprom_data(struct device *dev, int off, int len, int *buffer))
{
	int i;

	if (net_debug > 3) printk("EEPROM data from %x for %x:\n",off,len);
	for (i = 0; i < len; i++) {
		if (wait_eeprom_ready(dev) < 0) return -1;
		/* Now send the EEPROM read command and EEPROM location to read */
		writereg(dev, PP_EECMD, (off + i) | EEPROM_READ_CMD);
		if (wait_eeprom_ready(dev) < 0) return -1;
		buffer[i] = readreg(dev, PP_EEData);
		if (net_debug > 3) printk("%04x ", buffer[i]);
	}
	if (net_debug > 3) printk("\n");
        return 0;
}

__initfunc(static int
get_eeprom_cksum(int off, int len, int *buffer))
{
	int i, cksum;

	cksum = 0;
	for (i = 0; i < len; i++)
		cksum += buffer[i];
	cksum &= 0xffff;
	if (cksum == 0)
		return 0;
	return -1;
}

/* This is the real probe routine.  Linux has a history of friendly device
   probes on the ISA bus.  A good device probes avoids doing writes, and
   verifies that the correct device exists and functions.  */

__initfunc(static int cs89x0_probe1(struct device *dev, int ioaddr))
{
	struct net_local *lp;
	static unsigned version_printed = 0;
	int i;
	unsigned rev_type = 0;
	int eeprom_buff[CHKSUM_LEN];

	/* Initialize the device structure. */
	if (dev->priv == NULL) {
		dev->priv = kmalloc(sizeof(struct net_local), GFP_KERNEL);
                memset(dev->priv, 0, sizeof(struct net_local));
        }
	lp = (struct net_local *)dev->priv;

	/* if they give us an odd I/O address, then do ONE write to
           the address port, to get it back to address zero, where we
           expect to find the EISA signature word. */
	if (ioaddr & 1) {
		ioaddr &= ~1;
		if ((inw(ioaddr + ADD_PORT) & ADD_MASK) != ADD_SIG)
			return ENODEV;
		outw(PP_ChipID, ioaddr + ADD_PORT);
	}

	if (inw(ioaddr + DATA_PORT) != CHIP_EISA_ID_SIG)
		return ENODEV;

	/* Fill in the 'dev' fields. */
	dev->base_addr = ioaddr;

	/* get the chip type */
	rev_type = readreg(dev, PRODUCT_ID_ADD);
	lp->chip_type = rev_type &~ REVISON_BITS;
	lp->chip_revision = ((rev_type & REVISON_BITS) >> 8) + 'A';

	/* Check the chip type and revision in order to set the correct send command
	CS8920 revision C and CS8900 revision F can use the faster send. */
	lp->send_cmd = TX_AFTER_381;
	if (lp->chip_type == CS8900 && lp->chip_revision >= 'F')
		lp->send_cmd = TX_NOW;
	if (lp->chip_type != CS8900 && lp->chip_revision >= 'C')
		lp->send_cmd = TX_NOW;

	if (net_debug  &&  version_printed++ == 0)
		printk(version);

	printk("%s: cs89%c0%s rev %c found at %#3lx",
	       dev->name,
	       lp->chip_type==CS8900?'0':'2',
	       lp->chip_type==CS8920M?"M":"",
	       lp->chip_revision,
	       dev->base_addr);

	reset_chip(dev);

	/* First check to see if an EEPROM is attached*/
	if ((readreg(dev, PP_SelfST) & EEPROM_PRESENT) == 0)
		printk("\ncs89x0: No EEPROM, relying on command line....\n");
	else if (get_eeprom_data(dev, START_EEPROM_DATA,CHKSUM_LEN,eeprom_buff) < 0) {
		printk("\ncs89x0: EEPROM read failed, relying on command line.\n");
        } else if (get_eeprom_cksum(START_EEPROM_DATA,CHKSUM_LEN,eeprom_buff) < 0) {
                printk("\ncs89x0: EEPROM checksum bad, relyong on command line\n");
        } else {
                /* get transmission control word  but keep the autonegotiation bits */
                if (!lp->auto_neg_cnf) lp->auto_neg_cnf = eeprom_buff[AUTO_NEG_CNF_OFFSET/2];
                /* Store adapter configuration */
                if (!lp->adapter_cnf) lp->adapter_cnf = eeprom_buff[ADAPTER_CNF_OFFSET/2];
                /* Store ISA configuration */
                lp->isa_config = eeprom_buff[ISA_CNF_OFFSET/2];
                /* store the initial memory base address */
                dev->mem_start = eeprom_buff[PACKET_PAGE_OFFSET/2] << 8;
                for (i = 0; i < ETH_ALEN/2; i++) {
                        dev->dev_addr[i*2] = eeprom_buff[i];
                        dev->dev_addr[i*2+1] = eeprom_buff[i] >> 8;
                }
        }


	printk(" media %s%s%s",
	       (lp->adapter_cnf & A_CNF_10B_T)?"RJ-45,":"",
	       (lp->adapter_cnf & A_CNF_AUI)?"AUI,":"",
	       (lp->adapter_cnf & A_CNF_10B_2)?"BNC,":"");

	lp->irq_map = 0xffff;

	/* If this is a CS8900 then no pnp soft */
	if (lp->chip_type != CS8900 &&
	    /* Check if the ISA IRQ has been set  */
		(i = readreg(dev, PP_CS8920_ISAINT) & 0xff,
		 (i != 0 && i < CS8920_NO_INTS))) {
		if (!dev->irq)
			dev->irq = i;
	} else {
		i = lp->isa_config & INT_NO_MASK;
		if (lp->chip_type == CS8900) {
			/* the table that follows is dependent upon how you wired up your cs8900
			 * in your system.  The table is the same as the cs8900 engineering demo
			 * board.  irq_map also depends on the contents of the table.  Also see
			 * write_irq, which is the reverse mapping of the table below. */
			switch(i) {
			case 0: i = 10; break;
			case 1: i = 11; break;
			case 2: i = 12; break;
			case 3: i =  5; break;
			default: printk("\ncs89x0: bug: isa_config is %d\n", i);
			}
			lp->irq_map = CS8900_IRQ_MAP; /* fixed IRQ map for CS8900 */
		} else {
			int irq_map_buff[IRQ_MAP_LEN/2];

			if (get_eeprom_data(dev, IRQ_MAP_EEPROM_DATA,
					    IRQ_MAP_LEN/2,
					    irq_map_buff) >= 0) {
				if ((irq_map_buff[0] & 0xff) == PNP_IRQ_FRMT)
					lp->irq_map = (irq_map_buff[0]>>8) | (irq_map_buff[1] << 8);
			}
		}
		if (!dev->irq)
			dev->irq = i;
	}

	printk(" IRQ %d", dev->irq);


	/* print the ethernet address. */
	for (i = 0; i < ETH_ALEN; i++)
		printk(" %2.2x", dev->dev_addr[i]);

	/* Grab the region so we can find another board if autoIRQ fails. */
	request_region(ioaddr, NETCARD_IO_EXTENT,"cs89x0");

	dev->open		= net_open;
	dev->stop		= net_close;
	dev->hard_start_xmit = net_send_packet;
	dev->get_stats	= net_get_stats;
	dev->set_multicast_list = &set_multicast_list;
	dev->set_mac_address = &set_mac_address;

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(dev);

	printk("\n");
	return 0;
}



__initfunc(void
reset_chip(struct device *dev))
{
	struct net_local *lp = (struct net_local *)dev->priv;
	int ioaddr = dev->base_addr;
	int reset_start_time;

	writereg(dev, PP_SelfCTL, readreg(dev, PP_SelfCTL) | POWER_ON_RESET);

	/* wait 30 ms */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(30*HZ/1000);

	if (lp->chip_type != CS8900) {
		/* Hardware problem requires PNP registers to be reconfigured after a reset */
		outw(PP_CS8920_ISAINT, ioaddr + ADD_PORT);
		outb(dev->irq, ioaddr + DATA_PORT);
		outb(0,      ioaddr + DATA_PORT + 1);

		outw(PP_CS8920_ISAMemB, ioaddr + ADD_PORT);
		outb((dev->mem_start >> 8) & 0xff, ioaddr + DATA_PORT);
		outb((dev->mem_start >> 24) & 0xff,   ioaddr + DATA_PORT + 1);
	}
	/* Wait until the chip is reset */
	reset_start_time = jiffies;
	while( (readreg(dev, PP_SelfST) & INIT_DONE) == 0 && jiffies - reset_start_time < 2)
		;
}


static void
control_dc_dc(struct device *dev, int on_not_off)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	unsigned int selfcontrol;
	int timenow = jiffies;
	/* control the DC to DC convertor in the SelfControl register.  */

	selfcontrol = HCB1_ENBL; /* Enable the HCB1 bit as an output */
	if (((lp->adapter_cnf & A_CNF_DC_DC_POLARITY) != 0) ^ on_not_off)
		selfcontrol |= HCB1;
	else
		selfcontrol &= ~HCB1;
	writereg(dev, PP_SelfCTL, selfcontrol);

	/* Wait for the DC/DC converter to power up - 500ms */
	while (jiffies - timenow < 100)
		;

}

static int
detect_tp(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	int timenow = jiffies;

	if (net_debug > 1) printk("%s: Attempting TP\n", dev->name);

        /* If connected to another full duplex capable 10-Base-T card the link pulses
           seem to be lost when the auto detect bit in the LineCTL is set.
           To overcome this the auto detect bit will be cleared whilst testing the
           10-Base-T interface.  This would not be necessary for the sparrow chip but
           is simpler to do it anyway. */
	writereg(dev, PP_LineCTL, lp->linectl &~ AUI_ONLY);
	control_dc_dc(dev, 0);

        /* Delay for the hardware to work out if the TP cable is present - 150ms */
	for (timenow = jiffies; jiffies - timenow < 15; )
                ;
	if ((readreg(dev, PP_LineST) & LINK_OK) == 0)
		return 0;

	if (lp->chip_type != CS8900) {

		writereg(dev, PP_AutoNegCTL, lp->auto_neg_cnf & AUTO_NEG_MASK);

		if ((lp->auto_neg_cnf & AUTO_NEG_BITS) == AUTO_NEG_ENABLE) {
			printk("%s: negotiating duplex...\n",dev->name);
			while (readreg(dev, PP_AutoNegST) & AUTO_NEG_BUSY) {
				if (jiffies - timenow > 4000) {
					printk("**** Full / half duplex auto-negotiation timed out ****\n");
					break;
				}
			}
		}
		if (readreg(dev, PP_AutoNegST) & FDX_ACTIVE)
			printk("%s: using full duplex\n", dev->name);
		else
			printk("%s: using half duplex\n", dev->name);
	}

	return A_CNF_MEDIA_10B_T;
}

/* send a test packet - return true if carrier bits are ok */
static int
send_test_pkt(struct device *dev)
{
	int ioaddr = dev->base_addr;
	char test_packet[] = { 0,0,0,0,0,0, 0,0,0,0,0,0,
				 0, 46, /* A 46 in network order */
				 0, 0, /* DSAP=0 & SSAP=0 fields */
				 0xf3, 0 /* Control (Test Req + P bit set) */ };
	long timenow = jiffies;

	writereg(dev, PP_LineCTL, readreg(dev, PP_LineCTL) | SERIAL_TX_ON);

	memcpy(test_packet,          dev->dev_addr, ETH_ALEN);
	memcpy(test_packet+ETH_ALEN, dev->dev_addr, ETH_ALEN);

        outw(TX_AFTER_ALL, ioaddr + TX_CMD_PORT);
        outw(ETH_ZLEN, ioaddr + TX_LEN_PORT);

	/* Test to see if the chip has allocated memory for the packet */
	while (jiffies - timenow < 5)
		if (readreg(dev, PP_BusST) & READY_FOR_TX_NOW)
			break;
	if (jiffies - timenow >= 5)
		return 0;	/* this shouldn't happen */

	/* Write the contents of the packet */
	if (dev->mem_start) {
		memcpy((void *)dev->mem_start + PP_TxFrame, test_packet, ETH_ZLEN);
	} else {
		outsw(ioaddr + TX_FRAME_PORT,test_packet,(ETH_ZLEN+1) >>1);
	}

	if (net_debug > 1) printk("Sending test packet ");
	/* wait a couple of jiffies for packet to be received */
	for (timenow = jiffies; jiffies - timenow < 3; )
                ;
        if ((readreg(dev, PP_TxEvent) & TX_SEND_OK_BITS) == TX_OK) {
                if (net_debug > 1) printk("succeeded\n");
                return 1;
        }
	if (net_debug > 1) printk("failed\n");
	return 0;
}


static int
detect_aui(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	if (net_debug > 1) printk("%s: Attempting AUI\n", dev->name);
	control_dc_dc(dev, 0);

	writereg(dev, PP_LineCTL, (lp->linectl &~ AUTO_AUI_10BASET) | AUI_ONLY);

	if (send_test_pkt(dev))
		return A_CNF_MEDIA_AUI;
	else
		return 0;
}

static int
detect_bnc(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	if (net_debug > 1) printk("%s: Attempting BNC\n", dev->name);
	control_dc_dc(dev, 1);

	writereg(dev, PP_LineCTL, (lp->linectl &~ AUTO_AUI_10BASET) | AUI_ONLY);

	if (send_test_pkt(dev))
		return A_CNF_MEDIA_10B_2;
	else
		return 0;
}


static void
write_irq(struct device *dev, int chip_type, int irq)
{
	int i;

	if (chip_type == CS8900) {
		switch(irq) {
		case 10: i = 0; break;
		case 11: i = 1; break;
		case 12: i = 2; break;
		case 5: i =  3; break;
		default: i = 3; break;
		}
		writereg(dev, PP_CS8900_ISAINT, i);
	} else {
		writereg(dev, PP_CS8920_ISAINT, irq);
	}
}

/* Open/initialize the board.  This is called (in the current kernel)
   sometime after booting when the 'ifconfig' program is run.

   This routine should set everything up anew at each open, even
   registers that "should" only need to be set once at boot, so that
   there is non-reboot way to recover if something goes wrong.
   */
static int
net_open(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	int result = 0;
	int i;

	if (dev->irq < 2) {
		/* Allow interrupts to be generated by the chip */
		writereg(dev, PP_BusCTL, ENABLE_IRQ | MEMORY_ON);
		for (i = 2; i < CS8920_NO_INTS; i++) if ((1 << dev->irq) & lp->irq_map) {
			if (request_irq (i, NULL, 0, "cs8920", dev) != -EBUSY) {
				write_irq(dev, lp->chip_type, i);
				writereg(dev, PP_BufCFG, GENERATE_SW_INTERRUPT);
				if (request_irq (dev->irq = i, &net_interrupt, 0, "cs89x0", dev) == 0)
					break;
			}
		}


		if (i >= CS8920_NO_INTS) {
			writereg(dev, PP_BusCTL, 0);	/* disable interrupts. */
			return -EAGAIN;
		}
	} else {
		if (((1 << dev->irq) & lp->irq_map) == 0) {
			printk("%s: IRQ %d is not in our map of allowable IRQs, which is %x\n",
                               dev->name, dev->irq, lp->irq_map);
			return -EAGAIN;
		}
		writereg(dev, PP_BusCTL, ENABLE_IRQ | MEMORY_ON);
		write_irq(dev, lp->chip_type, dev->irq);
		if (request_irq(dev->irq, &net_interrupt, 0, "cs89x0", dev)) {
			return -EAGAIN;
		}
	}

	/* set the Ethernet address */
	for (i=0; i < ETH_ALEN/2; i++)
		writereg(dev, PP_IA+i*2, dev->dev_addr[i*2] | (dev->dev_addr[i*2+1] << 8));

	/* while we're testing the interface, leave interrupts disabled */
	writereg(dev, PP_BusCTL, MEMORY_ON);

	/* Set the LineCTL quintuplet based on adapter configuration read from EEPROM */
	if ((lp->adapter_cnf & A_CNF_EXTND_10B_2) && (lp->adapter_cnf & A_CNF_LOW_RX_SQUELCH))
                lp->linectl = LOW_RX_SQUELCH;
	else
                lp->linectl = 0;

        /* check to make sure that they have the "right" hardware available */
	switch(lp->adapter_cnf & A_CNF_MEDIA_TYPE) {
	case A_CNF_MEDIA_10B_T: result = lp->adapter_cnf & A_CNF_10B_T; break;
	case A_CNF_MEDIA_AUI:   result = lp->adapter_cnf & A_CNF_AUI; break;
	case A_CNF_MEDIA_10B_2: result = lp->adapter_cnf & A_CNF_10B_2; break;
        default: result = lp->adapter_cnf & (A_CNF_10B_T | A_CNF_AUI | A_CNF_10B_2);
        }
        if (!result) {
                printk("%s: EEPROM is configured for unavailable media\n", dev->name);
        release_irq:
                writereg(dev, PP_LineCTL, readreg(dev, PP_LineCTL) & ~(SERIAL_TX_ON | SERIAL_RX_ON));
                free_irq(dev->irq, dev);
		return -EAGAIN;
	}

        /* set the hardware to the configured choice */
	switch(lp->adapter_cnf & A_CNF_MEDIA_TYPE) {
	case A_CNF_MEDIA_10B_T:
                result = detect_tp(dev);
                if (!result) printk("%s: 10Base-T (RJ-45) has no cable\n", dev->name);
                if (lp->auto_neg_cnf & IMM_BIT) /* check "ignore missing media" bit */
                        result = A_CNF_MEDIA_10B_T; /* Yes! I don't care if I see a link pulse */
		break;
	case A_CNF_MEDIA_AUI:
                result = detect_aui(dev);
                if (!result) printk("%s: 10Base-5 (AUI) has no cable\n", dev->name);
                if (lp->auto_neg_cnf & IMM_BIT) /* check "ignore missing media" bit */
                        result = A_CNF_MEDIA_AUI; /* Yes! I don't care if I see a carrrier */
		break;
	case A_CNF_MEDIA_10B_2:
                result = detect_bnc(dev);
                if (!result) printk("%s: 10Base-2 (BNC) has no cable\n", dev->name);
                if (lp->auto_neg_cnf & IMM_BIT) /* check "ignore missing media" bit */
                        result = A_CNF_MEDIA_10B_2; /* Yes! I don't care if I can xmit a packet */
		break;
	case A_CNF_MEDIA_AUTO:
		writereg(dev, PP_LineCTL, lp->linectl | AUTO_AUI_10BASET);
		if (lp->adapter_cnf & A_CNF_10B_T)
			if ((result = detect_tp(dev)) != 0)
				break;
		if (lp->adapter_cnf & A_CNF_AUI)
			if ((result = detect_aui(dev)) != 0)
				break;
		if (lp->adapter_cnf & A_CNF_10B_2)
			if ((result = detect_bnc(dev)) != 0)
				break;
		printk("%s: no media detected\n", dev->name);
                goto release_irq;
	}
	switch(result) {
	case 0: printk("%s: no network cable attached to configured media\n", dev->name);
                goto release_irq;
	case A_CNF_MEDIA_10B_T: printk("%s: using 10Base-T (RJ-45)\n", dev->name);break;
	case A_CNF_MEDIA_AUI:   printk("%s: using 10Base-5 (AUI)\n", dev->name);break;
	case A_CNF_MEDIA_10B_2: printk("%s: using 10Base-2 (BNC)\n", dev->name);break;
	default: printk("%s: unexpected result was %x\n", dev->name, result); goto release_irq;
	}

	/* Turn on both receive and transmit operations */
	writereg(dev, PP_LineCTL, readreg(dev, PP_LineCTL) | SERIAL_RX_ON | SERIAL_TX_ON);

	/* Receive only error free packets addressed to this card */
	lp->rx_mode = 0;
	writereg(dev, PP_RxCTL, DEF_RX_ACCEPT);

	lp->curr_rx_cfg = RX_OK_ENBL | RX_CRC_ERROR_ENBL;
	if (lp->isa_config & STREAM_TRANSFER)
		lp->curr_rx_cfg |= RX_STREAM_ENBL;

	writereg(dev, PP_RxCFG, lp->curr_rx_cfg);

	writereg(dev, PP_TxCFG, TX_LOST_CRS_ENBL | TX_SQE_ERROR_ENBL | TX_OK_ENBL |
	       TX_LATE_COL_ENBL | TX_JBR_ENBL | TX_ANY_COL_ENBL | TX_16_COL_ENBL);

	writereg(dev, PP_BufCFG, READY_FOR_TX_ENBL | RX_MISS_COUNT_OVRFLOW_ENBL |
		 TX_COL_COUNT_OVRFLOW_ENBL | TX_UNDERRUN_ENBL);

	/* now that we've got our act together, enable everything */
	writereg(dev, PP_BusCTL, ENABLE_IRQ
                 );
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;
        MOD_INC_USE_COUNT;
	return 0;
}

static int
net_send_packet(struct sk_buff *skb, struct device *dev)
{
	if (dev->tbusy) {
		/* If we get here, some higher level has decided we are broken.
		   There should really be a "kick me" function call instead. */
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 5)
			return 1;
		if (net_debug > 0) printk("%s: transmit timed out, %s?\n", dev->name,
			   tx_done(dev) ? "IRQ conflict" : "network cable problem");
		/* Try to restart the adaptor. */
		dev->tbusy=0;
		dev->trans_start = jiffies;
	}

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0)
		printk("%s: Transmitter access conflict.\n", dev->name);
	else {
		struct net_local *lp = (struct net_local *)dev->priv;
		short ioaddr = dev->base_addr;
		unsigned long flags;

		if (net_debug > 3)printk("%s: sent %d byte packet of type %x\n", dev->name, skb->len, (skb->data[ETH_ALEN+ETH_ALEN] << 8) | skb->data[ETH_ALEN+ETH_ALEN+1]);

		/* keep the upload from being interrupted, since we
                   ask the chip to start transmitting before the
                   whole packet has been completely uploaded. */
		save_flags(flags);
		cli();

		/* initiate a transmit sequence */
		outw(lp->send_cmd, ioaddr + TX_CMD_PORT);
		outw(skb->len, ioaddr + TX_LEN_PORT);

		/* Test to see if the chip has allocated memory for the packet */
		if ((readreg(dev, PP_BusST) & READY_FOR_TX_NOW) == 0) {
			/* Gasp!  It hasn't.  But that shouldn't happen since
			   we're waiting for TxOk, so return 1 and requeue this packet. */
			restore_flags(flags);
			return 1;
		}

		/* Write the contents of the packet */
                outsw(ioaddr + TX_FRAME_PORT,skb->data,(skb->len+1) >>1);

		restore_flags(flags);
		dev->trans_start = jiffies;
	}
	dev_kfree_skb (skb);

	return 0;
}

/* The typical workload of the driver:
   Handle the network interface interrupts. */
static void net_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct device *dev = dev_id;
	struct net_local *lp;
	int ioaddr, status;

	if (dev == NULL) {
		printk ("net_interrupt(): irq %d for unknown device.\n", irq);
		return;
	}
	if (dev->interrupt)
		printk("%s: Re-entering the interrupt handler.\n", dev->name);
	dev->interrupt = 1;

	ioaddr = dev->base_addr;
	lp = (struct net_local *)dev->priv;

	/* we MUST read all the events out of the ISQ, otherwise we'll never
           get interrupted again.  As a consequence, we can't have any limit
           on the number of times we loop in the interrupt handler.  The
           hardware guarantees that eventually we'll run out of events.  Of
           course, if you're on a slow machine, and packets are arriving
           faster than you can read them off, you're screwed.  Hasta la
           vista, baby!  */
	while ((status = readword(dev, ISQ_PORT))) {
		if (net_debug > 4)printk("%s: event=%04x\n", dev->name, status);
		switch(status & ISQ_EVENT_MASK) {
		case ISQ_RECEIVER_EVENT:
			/* Got a packet(s). */
			net_rx(dev);
			break;
		case ISQ_TRANSMITTER_EVENT:
			lp->stats.tx_packets++;
			dev->tbusy = 0;
			mark_bh(NET_BH);	/* Inform upper layers. */
			if ((status & TX_OK) == 0) lp->stats.tx_errors++;
			if (status & TX_LOST_CRS) lp->stats.tx_carrier_errors++;
			if (status & TX_SQE_ERROR) lp->stats.tx_heartbeat_errors++;
			if (status & TX_LATE_COL) lp->stats.tx_window_errors++;
			if (status & TX_16_COL) lp->stats.tx_aborted_errors++;
			break;
		case ISQ_BUFFER_EVENT:
			if (status & READY_FOR_TX) {
				/* we tried to transmit a packet earlier,
                                   but inexplicably ran out of buffers.
                                   That shouldn't happen since we only ever
                                   load one packet.  Shrug.  Do the right
                                   thing anyway. */
				dev->tbusy = 0;
				mark_bh(NET_BH);	/* Inform upper layers. */
			}
			if (status & TX_UNDERRUN) {
				if (net_debug > 0) printk("%s: transmit underrun\n", dev->name);
                                lp->send_underrun++;
                                if (lp->send_underrun == 3) lp->send_cmd = TX_AFTER_381;
                                else if (lp->send_underrun == 6) lp->send_cmd = TX_AFTER_ALL;
                        }
			break;
		case ISQ_RX_MISS_EVENT:
			lp->stats.rx_missed_errors += (status >>6);
			break;
		case ISQ_TX_COL_EVENT:
			lp->stats.collisions += (status >>6);
			break;
		}
	}
	dev->interrupt = 0;
	return;
}

/* We have a good packet(s), get it/them out of the buffers. */
static void
net_rx(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	int ioaddr = dev->base_addr;
	struct sk_buff *skb;
	int status, length;

	status = inw(ioaddr + RX_FRAME_PORT);
	length = inw(ioaddr + RX_FRAME_PORT);
	if ((status & RX_OK) == 0) {
		lp->stats.rx_errors++;
		if (status & RX_RUNT) lp->stats.rx_length_errors++;
		if (status & RX_EXTRA_DATA) lp->stats.rx_length_errors++;
		if (status & RX_CRC_ERROR) if (!(status & (RX_EXTRA_DATA|RX_RUNT)))
			/* per str 172 */
			lp->stats.rx_crc_errors++;
		if (status & RX_DRIBBLE) lp->stats.rx_frame_errors++;
		return;
	}

	/* Malloc up new buffer. */
	skb = alloc_skb(length, GFP_ATOMIC);
	if (skb == NULL) {
		printk("%s: Memory squeeze, dropping packet.\n", dev->name);
		lp->stats.rx_dropped++;
		return;
	}
	skb->len = length;
	skb->dev = dev;

        insw(ioaddr + RX_FRAME_PORT, skb->data, length >> 1);
	if (length & 1)
		skb->data[length-1] = inw(ioaddr + RX_FRAME_PORT);

	if (net_debug > 3)printk("%s: received %d byte packet of type %x\n",
                                 dev->name, length,
                                 (skb->data[ETH_ALEN+ETH_ALEN] << 8) | skb->data[ETH_ALEN+ETH_ALEN+1]);

        skb->protocol=eth_type_trans(skb,dev);
	netif_rx(skb);
	lp->stats.rx_packets++;
	lp->stats.rx_bytes+=skb->len;
	return;
}

/* The inverse routine to net_open(). */
static int
net_close(struct device *dev)
{

	writereg(dev, PP_RxCFG, 0);
	writereg(dev, PP_TxCFG, 0);
	writereg(dev, PP_BufCFG, 0);
	writereg(dev, PP_BusCTL, 0);

	dev->start = 0;

	free_irq(dev->irq, dev);

	/* Update the statistics here. */

        MOD_DEC_USE_COUNT;
	return 0;

}

/* Get the current statistics.	This may be called with the card open or
   closed. */
static struct net_device_stats *
net_get_stats(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	cli();
	/* Update the statistics from the device registers. */
	lp->stats.rx_missed_errors += (readreg(dev, PP_RxMiss) >> 6);
	lp->stats.collisions += (readreg(dev, PP_TxCol) >> 6);
	sti();

	return &lp->stats;
}

static void set_multicast_list(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;

	if(dev->flags&IFF_PROMISC)
	{
		lp->rx_mode = RX_ALL_ACCEPT;
	}
	else if((dev->flags&IFF_ALLMULTI)||dev->mc_list)
	{
		/* The multicast-accept list is initialized to accept-all, and we
		   rely on higher-level filtering for now. */
		lp->rx_mode = RX_MULTCAST_ACCEPT;
	} 
	else
		lp->rx_mode = 0;

	writereg(dev, PP_RxCTL, DEF_RX_ACCEPT | lp->rx_mode);

	/* in promiscuous mode, we accept errored packets, so we have to enable interrupts on them also */
	writereg(dev, PP_RxCFG, lp->curr_rx_cfg |
	     (lp->rx_mode == RX_ALL_ACCEPT? (RX_CRC_ERROR_ENBL|RX_RUNT_ENBL|RX_EXTRA_DATA_ENBL) : 0));
}


static int set_mac_address(struct device *dev, void *addr)
{
	int i;
	if (dev->start)
		return -EBUSY;
	printk("%s: Setting MAC address to ", dev->name);
	for (i = 0; i < 6; i++)
		printk(" %2.2x", dev->dev_addr[i] = ((unsigned char *)addr)[i]);
	printk(".\n");
	/* set the Ethernet address */
	for (i=0; i < ETH_ALEN/2; i++)
		writereg(dev, PP_IA+i*2, dev->dev_addr[i*2] | (dev->dev_addr[i*2+1] << 8));

	return 0;
}

#ifdef MODULE

static char namespace[16] = "";
static struct device dev_cs89x0 = {
        NULL,
        0, 0, 0, 0,
        0, 0,
        0, 0, 0, NULL, NULL };

int io=0;
int irq=0;
#endif
#ifdef MODULE
int debug=1;
char *media="auto";
char *duplex="f";

/*
* media=t             - specify media type
   or media=2
   or media=aui
   or medai=auto
* duplex=f            - specify forced half/full/autonegotiate duplex
   or duplex=h
   or duplex=auto
* debug=#             - debug level


* Default Chip Configuration:
  * DMA Burst = enabled
  * IOCHRDY Enabled = enabled
    * UseSA = enabled
    * CS8900 defaults to half-duplex if not specified on command-line
    * CS8920 defaults to autoneg if not specified on command-line
    * Use reset defaults for other config parameters

* Assumptions:
  * media type specified is supported (circuitry is present)
  * if memory address is > 1MB, then required mem decode hw is present
  * if 10B-2, then agent other than driver will enable DC/DC converter
    (hw or software util)


*/

int
init_module(void)
{
	struct net_local *lp;

	net_debug = debug;
        dev_cs89x0.name = namespace;
	dev_cs89x0.irq = irq;
	dev_cs89x0.base_addr = io;
        dev_cs89x0.init = cs89x0_probe;
        dev_cs89x0.priv = kmalloc(sizeof(struct net_local), GFP_KERNEL);
	memset(dev_cs89x0.priv, 0, sizeof(struct net_local));
	lp = (struct net_local *)dev_cs89x0.priv;

        /* boy, they'd better get these right */
        if (!strcmp(media, "rj45"))
		lp->adapter_cnf = A_CNF_MEDIA_10B_T | A_CNF_10B_T;
	if (!strcmp(media, "aui"))
		lp->adapter_cnf = A_CNF_MEDIA_AUI   | A_CNF_AUI;
	if (!strcmp(media, "bnc"))
		lp->adapter_cnf = A_CNF_MEDIA_10B_2 | A_CNF_10B_2;

        if (!strcmp(duplex, "auto"))
		lp->auto_neg_cnf = AUTO_NEG_ENABLE;

        if (io == 0)  {
                printk(KERN_NOTICE "cs89x0.c: Module autoprobing not allowed.\n");
                printk(KERN_NOTICE "cs89x0.c: Append io=0xNNN\n");
                return -EPERM;
        }
        if (register_netdev(&dev_cs89x0) != 0) {
                printk(KERN_WARNING "cs89x0.c: No card found at 0x%x\n", io);
                return -ENXIO;
        }
	return 0;
}

void
cleanup_module(void)
{

#endif
#ifdef MODULE
	outw(0, dev_cs89x0.base_addr + ADD_PORT);
#endif
#ifdef MODULE

        if (dev_cs89x0.priv != NULL) {
                /* Free up the private structure, or leak memory :-)  */
                unregister_netdev(&dev_cs89x0);
                kfree(dev_cs89x0.priv);
                dev_cs89x0.priv = NULL;	/* gets re-allocated by cs89x0_probe1 */
                /* If we don't do this, we can't re-insmod it later. */
                release_region(dev_cs89x0.base_addr, NETCARD_IO_EXTENT);
        }
}
#endif /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/include -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -DMODULE -DCONFIG_MODVERSIONS -c cs89x0.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  c-indent-level: 8
 *  tab-width: 8
 * End:
 *
 */
