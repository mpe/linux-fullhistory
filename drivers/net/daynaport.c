/* mac_ns8390.c: A Macintosh 8390 based ethernet driver for linux. */
/*
	Derived from code:
	
	Written 1993-94 by Donald Becker.

	Copyright 1993 United States Government as represented by the
	Director, National Security Agency.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	    TODO:

	    The block output routines may be wrong for non Dayna
	    cards

	    Reading MAC addresses
*/

static const char *version =
	"mac_ns8390.c:v0.01 7/5/97 Alan Cox (Alan.Cox@linux.org)\n";

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/nubus.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/hwtest.h>
#include <linux/delay.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include "8390.h"

int ns8390_probe1(struct device *dev, int word16, char *name, int id, int prom);

static int ns8390_open(struct device *dev);
static void ns8390_no_reset(struct device *dev);
static int ns8390_close_card(struct device *dev);

static void interlan_reset(struct device *dev);

static void dayna_get_8390_hdr(struct device *dev, struct e8390_pkt_hdr *hdr,
						int ring_page);
static void dayna_block_input(struct device *dev, int count,
						  struct sk_buff *skb, int ring_offset);
static void dayna_block_output(struct device *dev, int count,
						   const unsigned char *buf, const int start_page);

static void sane_get_8390_hdr(struct device *dev, struct e8390_pkt_hdr *hdr,
						int ring_page);
static void sane_block_input(struct device *dev, int count,
						  struct sk_buff *skb, int ring_offset);
static void sane_block_output(struct device *dev, int count,
						   const unsigned char *buf, const int start_page);

static void slow_sane_get_8390_hdr(struct device *dev, struct e8390_pkt_hdr *hdr,
						int ring_page);
static void slow_sane_block_input(struct device *dev, int count,
						  struct sk_buff *skb, int ring_offset);
static void slow_sane_block_output(struct device *dev, int count,
						   const unsigned char *buf, const int start_page);


#define WD_START_PG	0x00	/* First page of TX buffer */
#define WD03_STOP_PG	0x20	/* Last page +1 of RX ring */
#define WD13_STOP_PG	0x40	/* Last page +1 of RX ring */


#define DAYNA_MAC_BASE		0xf0007
#define DAYNA_8390_BASE		0x80000 /* 3 */
#define DAYNA_8390_MEM		0x00000
#define DAYNA_MEMSIZE		0x04000	/* First word of each long ! */

#define APPLE_8390_BASE		0xE0000
#define APPLE_8390_MEM		0xD0000
#define APPLE_MEMSIZE		8192    /* FIXME: need to dynamically check */

#define KINETICS_8390_BASE	0x80003
#define KINETICS_8390_MEM	0x00000
#define KINETICS_MEMSIZE	8192    /* FIXME: need to dynamically check */

static int test_8390(volatile char *ptr, int scale)
{
	int regd;
	int v;
	
	if(hwreg_present(&ptr[0x00])==0)
		return -EIO;
	if(hwreg_present(&ptr[0x0D<<scale])==0)
		return -EIO;
	if(hwreg_present(&ptr[0x0D<<scale])==0)
		return -EIO;
	ptr[0x00]=E8390_NODMA+E8390_PAGE1+E8390_STOP;
	regd=ptr[0x0D<<scale];
	ptr[0x0D<<scale]=0xFF;
	ptr[0x00]=E8390_NODMA+E8390_PAGE0;
	v=ptr[0x0D<<scale];
	if(ptr[0x0D<<scale]!=0)
	{
		ptr[0x0D<<scale]=regd;
		return -ENODEV;
	}
/*	printk("NS8390 found at %p scaled %d\n", ptr,scale);*/
	return 0;
}
/*
 *    Identify the species of NS8390 card/driver we need
 */

#define NS8390_DAYNA		1
#define NS8390_INTERLAN		2
#define NS8390_KINETICS		3
#define NS8390_APPLE		4
#define NS8390_FARALLON		5
#define NS8390_ASANTE		6

int ns8390_ident(struct nubus_type *nb)
{
	/* It appears anything with a software type of 0 is an apple
	   compatible - even if the hardware matches others */
	   
	if(nb->DrSW==0x0001 || nb->DrSW==0x0109 || nb->DrSW==0x0000 || nb->DrSW==0x0100)
		return NS8390_APPLE;
	
	/* Dayna ex Kinetics board */
	if(nb->DrHW==0x0103)
		return NS8390_DAYNA;

	/* Asante board */
	if(nb->DrHW==0x0104)
		return NS8390_ASANTE;
	if(nb->DrHW==0x0100)
		return NS8390_INTERLAN;
	if(nb->DrHW==0x0106)
		return NS8390_KINETICS;
	if(nb->DrSW==0x010C)
		return NS8390_FARALLON;
	return -1;
}

/*
 *	Memory probe for 8390 cards
 */
 
int apple_8390_mem_probe(volatile unsigned short *p)
{
	int i, j;
	/*
	 *	Algorithm.
	 *	1.	Check each block size of memory doesn't fault
	 *	2.	Write a value to it
	 *	3.	Check all previous blocks are unaffected
	 */
	
	for(i=0;i<2;i++)
	{
		volatile unsigned short *m=p+4096*i;
		/* Unwriteable - we have a fully decoded card and the
		   RAM end located */
		   
		if(hwreg_present(m)==0)
			return 8192*i;
			
		*m=0xA5A0|i;
		
		for(j=0;j<i;j++)
		{
			/* Partial decode and wrap ? */
			if(p[4096*j]!=(0xA5A0|j))
			{
				/* This is the first misdecode, so it had
				   one less page than we tried */
				return 8192*i;
			}
 			j++;
 		}
 		/* Ok it still decodes.. move on 8K */
 	}
 	/* 
 	 *	We don't look past 16K. That should cover most cards
 	 *	and above 16K there isnt really any gain.
 	 */
 	return 16384;
 }
 		
/*
 *    Probe for 8390 cards.  
 *    The ns8390_probe1() routine initializes the card and fills the
 *    station address field. On entry base_addr is set, irq is set
 *    (These come from the nubus probe code). dev->mem_start points
 *    at the memory ring, dev->mem_end gives the end of it.
 */

int ns8390_probe(struct nubus_device_specifier *d, int slot, struct nubus_type *match)
{
	struct device *dev;
	volatile unsigned short *i;
	volatile unsigned char *p;
	int plen;
	int id;

	if(match->category!=NUBUS_CAT_NETWORK || match->type!=1)
		return -ENODEV;		
	/* Ok so it is an ethernet network device */
	if((id=ns8390_ident(match))==-1)
	{
		printk("Ethernet but type unknown %d\n",match->DrHW);
		return -ENODEV;
	}
	dev = init_etherdev(0, 0);
	if(dev==NULL)
		return -ENOMEM;

	/*
	 *	Dayna specific init
	 */
	if(id==NS8390_DAYNA)
	{
		dev->base_addr=(int)(nubus_slot_addr(slot)+DAYNA_8390_BASE);
		dev->mem_start=(int)(nubus_slot_addr(slot)+DAYNA_8390_MEM);
		dev->mem_end=dev->mem_start+DAYNA_MEMSIZE; /* 8K it seems */
	
		printk("daynaport: testing board: ");

		printk("memory - ");	
	
		i=(void *)dev->mem_start;
		memset((void *)i,0xAA, DAYNA_MEMSIZE);
		while(i<(volatile unsigned short *)dev->mem_end)
		{
			if(*i!=0xAAAA)
				goto membad;
			*i=0x5555;
			if(*i!=0x5555)
				goto membad;
			i+=2;	/* Skip a word */
		}

		printk("controller - ");
	
		p=(void *)dev->base_addr;
		plen=0;
	
		while(plen<0x3FF00)
		{
			if(test_8390(p,0)==0)
				break;
			if(test_8390(p,1)==0)
				break;
			if(test_8390(p,2)==0)
				break;
			if(test_8390(p,3)==0)
				break;
			plen++;
			p++;
		}
		if(plen==0x3FF00)
			goto membad;
		printk("OK\n");
		dev->irq=slot;
		if(ns8390_probe1(dev, 0, "dayna", id, -1)==0)
		return 0;
	}
	/* Apple, Farallon, Asante */
	if(id==NS8390_APPLE|| id==NS8390_FARALLON || id==NS8390_ASANTE)
	{
		int memsize;
		
		dev->base_addr=(int)(nubus_slot_addr(slot)+APPLE_8390_BASE);
		dev->mem_start=(int)(nubus_slot_addr(slot)+APPLE_8390_MEM);
		
		memsize = apple_8390_mem_probe((void *)dev->mem_start);
		
		dev->mem_end=dev->mem_start+memsize;
		dev->irq=slot;
		printk("apple/clone: testing board: ");

		printk("%dK memory - ", memsize>>10);		

		i=(void *)dev->mem_start;
		memset((void *)i,0xAA, memsize);
		while(i<(volatile unsigned short *)dev->mem_end)
		{
			if(*i!=0xAAAA)
				goto membad;
			*i=0x5555;
			if(*i!=0x5555)
				goto membad;
			i+=2;	/* Skip a word */
		}
		printk("OK\n");

		if(id==NS8390_FARALLON)
		{
			if(ns8390_probe1(dev, 1, "farallon", id, -1)==0)
				return 0;
		}
		else
		{
			if(ns8390_probe1(dev, 1, "apple/clone", id, -1)==0)
			    return 0;
		}
	}
	/* Interlan */
	if(id==NS8390_INTERLAN)
	{
		/* As apple and asante */
		dev->base_addr=(int)(nubus_slot_addr(slot)+APPLE_8390_BASE);
		dev->mem_start=(int)(nubus_slot_addr(slot)+APPLE_8390_MEM);
		dev->mem_end=dev->mem_start+APPLE_MEMSIZE; /* 8K it seems */
		dev->irq=slot;
		if(ns8390_probe1(dev, 1, "interlan", id, -1)==0)
			return 0;
	}
	/* Kinetics */
	if(id==NS8390_KINETICS)
	{
		dev->base_addr=(int)(nubus_slot_addr(slot)+KINETICS_8390_BASE);
		dev->mem_start=(int)(nubus_slot_addr(slot)+KINETICS_8390_MEM);
		dev->mem_end=dev->mem_start+KINETICS_MEMSIZE; /* 8K it seems */
		dev->irq=slot;
		if(ns8390_probe1(dev, 0, "kinetics", id, -1)==0)
			return 0;
	}
	kfree(dev);
	return -ENODEV;
membad:
	printk("failed.\n");
	kfree(dev);
	return -ENODEV;
}

int ns8390_probe1(struct device *dev, int word16, char *model_name, int type, int promoff)
{
	static unsigned version_printed = 0;

	static u32 fwrd4_offsets[16]={
		0,      4,      8,      12,
		16,     20,     24,     28,
		32,     36,     40,     44,
		48,     52,     56,     60
	};
	static u32 back4_offsets[16]={
		60,     56,     52,     48,
		44,     40,     36,     32,
		28,     24,     20,     16,
		12,     8,      4,      0
	};

	unsigned char *prom=((unsigned char *)nubus_slot_addr(dev->irq))+promoff;

	if (ei_debug  &&  version_printed++ == 0)
		printk(version);
	
	/* Snarf the interrupt now.  There's no point in waiting since we cannot
	   share a slot! and the board will usually be enabled. */
	if (nubus_request_irq(dev->irq, dev, ei_interrupt)) 
	{
		printk (" unable to get nubus IRQ %d.\n", dev->irq);
		return EAGAIN;
	}
	
	/* Allocate dev->priv and fill in 8390 specific dev fields. */
	if (ethdev_init(dev)) 
	{	
		printk (" unable to get memory for dev->priv.\n");
		nubus_free_irq(dev->irq);
		return -ENOMEM;
	}

	/* OK, we are certain this is going to work.  Setup the device. */

	ei_status.name = model_name;
	ei_status.word16 = word16;
	ei_status.tx_start_page = WD_START_PG;
	ei_status.rx_start_page = WD_START_PG + TX_PAGES;

	dev->rmem_start = dev->mem_start + TX_PAGES*256;
	ei_status.stop_page = (dev->mem_end - dev->mem_start)/256;
	dev->rmem_end = dev->mem_end;
	
	if(promoff==-1)		/* Use nubus resources ? */
	{
		if(nubus_ethernet_addr(dev->irq /* slot */, dev->dev_addr))
		{
		  printk("mac_ns8390: MAC address not in resources!\n");
		  return -ENODEV;
		}
	}
	else			/* Pull it off the card */
	{
		int i=0;
		int x=1;
		/* These should go in the end I hope */
		if(type==NS8390_DAYNA)
			x=2;
		if(type==NS8390_INTERLAN)
			x=4;
		while(i<6)
		{
			dev->dev_addr[i]=*prom;
			prom+=x;
			if(i)
				printk(":");
			printk("%02X",dev->dev_addr[i++]);
		}
	}

	printk(" %s, IRQ %d, shared memory at %#lx-%#lx.\n",
		   model_name, dev->irq, dev->mem_start, dev->mem_end-1);

	switch(type)
	{
		case NS8390_DAYNA:      /* Dayna card */
			/* 16 bit, 4 word offsets */
			ei_status.reset_8390 = &ns8390_no_reset;
			ei_status.block_input = &dayna_block_input;
			ei_status.block_output = &dayna_block_output;
			ei_status.get_8390_hdr = &dayna_get_8390_hdr;
			ei_status.reg_offset = fwrd4_offsets;
			break;
		case NS8390_FARALLON:
		case NS8390_APPLE:	/* Apple/Asante/Farallon */
			/*      16 bit card, register map is reversed */
			ei_status.reset_8390 = &ns8390_no_reset;
			ei_status.block_input = &slow_sane_block_input;
			ei_status.block_output = &slow_sane_block_output;
			ei_status.get_8390_hdr = &slow_sane_get_8390_hdr;
			ei_status.reg_offset = back4_offsets;
			break;
		case NS8390_ASANTE:
			/*      16 bit card, register map is reversed */
			ei_status.reset_8390 = &ns8390_no_reset;
			ei_status.block_input = &sane_block_input;
			ei_status.block_output = &sane_block_output;
			ei_status.get_8390_hdr = &sane_get_8390_hdr;
			ei_status.reg_offset = back4_offsets;
			break;
		case NS8390_INTERLAN:   /* Interlan */
			/*      16 bit card, map is forward */
			ei_status.reset_8390 = &interlan_reset;
			ei_status.block_input = &sane_block_input;
			ei_status.block_output = &sane_block_output;
			ei_status.get_8390_hdr = &sane_get_8390_hdr;
			ei_status.reg_offset = back4_offsets;
			break;
		case NS8390_KINETICS:   /* Kinetics */
			/*      8bit card, map is forward */
			ei_status.reset_8390 = &ns8390_no_reset;
			ei_status.block_input = &sane_block_input;
			ei_status.block_output = &sane_block_output;
			ei_status.get_8390_hdr = &sane_get_8390_hdr;
			ei_status.reg_offset = back4_offsets;
			break;
		default:
			panic("Detected a card I can't drive - whoops\n");
	}
	dev->open = &ns8390_open;
	dev->stop = &ns8390_close_card;

	NS8390_init(dev, 0);

	return 0;
}

static int ns8390_open(struct device *dev)
{
	ei_open(dev);
	MOD_INC_USE_COUNT;
	return 0;
}

static void ns8390_no_reset(struct device *dev)
{
	if (ei_debug > 1) 
		printk("Need to reset the NS8390 t=%lu...", jiffies);
	ei_status.txing = 0;
	if (ei_debug > 1) printk("reset not supported\n");
	return;
}

static int ns8390_close_card(struct device *dev)
{
	if (ei_debug > 1)
		printk("%s: Shutting down ethercard.\n", dev->name);
	ei_close(dev);
	MOD_DEC_USE_COUNT;
	return 0;
}

struct nubus_device_specifier nubus_8390={
	ns8390_probe,
	NULL
};


/*
 *    Interlan Specific Code Starts Here
 */

static void interlan_reset(struct device *dev)
{
	unsigned char *target=nubus_slot_addr(dev->irq);
	if (ei_debug > 1) 
		printk("Need to reset the NS8390 t=%lu...", jiffies);
	ei_status.txing = 0;
	/* This write resets the card */
	target[0xC0000]=0;
	if (ei_debug > 1) printk("reset complete\n");
	return;
}

/*
 *    Daynaport code (some is used by other drivers)
 */


/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */


/* Block input and output are easy on shared memory ethercards, and trivial
   on the Daynaport card where there is no choice of how to do it.
   The only complications are that the ring buffer wraps.
*/

static void dayna_cpu_memcpy(struct device *dev, void *to, int from, int count)
{
	volatile unsigned short *ptr;
	unsigned short *target=to;
	from<<=1;	/* word, skip overhead */
	ptr=(unsigned short *)(dev->mem_start+from);
	while(count>=2)
	{
		*target++=*ptr++;	/* Copy and */
		ptr++;			/* Cruft and */
		count-=2;
	}
	/*
	 *	Trailing byte ?
	 */
	if(count)
	{
		/* Big endian */
		unsigned short v=*ptr;
		*((char *)target)=v>>8;
	}
}

static void cpu_dayna_memcpy(struct device *dev, int to, const void *from, int count)
{
	volatile unsigned short *ptr;
	const unsigned short *src=from;
	to<<=1;	/* word, skip overhead */
	ptr=(unsigned short *)(dev->mem_start+to);
	while(count>=2)
	{
		*ptr++=*src++;		/* Copy and */
		ptr++;			/* Cruft and */
		count-=2;
	}
	/*
	 *	Trailing byte ?
	 */
	if(count)
	{
		/* Big endian */
		unsigned short v=*src;
		*((char *)ptr)=v>>8;
	}
}

static void dayna_get_8390_hdr(struct device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	unsigned long hdr_start = (ring_page - WD_START_PG)<<8;
	dayna_cpu_memcpy(dev, (void *)hdr, hdr_start, 4);
	/* Register endianism - fix here rather than 8390.c */
	hdr->count=(hdr->count&0xFF)<<8|(hdr->count>>8);
}

static void dayna_block_input(struct device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	unsigned long xfer_base = ring_offset - (WD_START_PG<<8);
	unsigned long xfer_start = xfer_base+dev->mem_start;

	/*
	 *	Note the offset maths is done in card memory space which
	 *	is word per long onto our space.
	 */
	 
	if (xfer_start + count > dev->rmem_end) 
	{
		/* We must wrap the input move. */
		int semi_count = dev->rmem_end - xfer_start;
		dayna_cpu_memcpy(dev, skb->data, xfer_base, semi_count);
		count -= semi_count;
		dayna_cpu_memcpy(dev, skb->data + semi_count, 
			dev->rmem_start - dev->mem_start, count);
	}
	else
	{
		dayna_cpu_memcpy(dev, skb->data, xfer_base, count);
	}
}

static void dayna_block_output(struct device *dev, int count, const unsigned char *buf,
				int start_page)
{
	long shmem = (start_page - WD_START_PG)<<8;
	
	cpu_dayna_memcpy(dev, shmem, buf, count);
}

/*
 *	Cards with full width memory
 */


static void sane_get_8390_hdr(struct device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	unsigned long hdr_start = (ring_page - WD_START_PG)<<8;
	memcpy((void *)hdr, (char *)dev->mem_start+hdr_start, 4);
	/* Register endianism - fix here rather than 8390.c */
	hdr->count=(hdr->count&0xFF)<<8|(hdr->count>>8);
}

static void sane_block_input(struct device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	unsigned long xfer_base = ring_offset - (WD_START_PG<<8);
	unsigned long xfer_start = xfer_base+dev->mem_start;

	if (xfer_start + count > dev->rmem_end) 
	{
		/* We must wrap the input move. */
		int semi_count = dev->rmem_end - xfer_start;
		memcpy(skb->data, (char *)dev->mem_start+xfer_base, semi_count);
		count -= semi_count;
		memcpy(skb->data + semi_count, 
			(char *)dev->rmem_start, count);
	}
	else
	{
		memcpy(skb->data, (char *)dev->mem_start+xfer_base, count);
	}
}


static void sane_block_output(struct device *dev, int count, const unsigned char *buf,
				int start_page)
{
	long shmem = (start_page - WD_START_PG)<<8;
	
	memcpy((char *)dev->mem_start+shmem, buf, count);
}

static void word_memcpy_tocard(void *tp, const void *fp, int count)
{
	volatile unsigned short *to = tp;
	const unsigned short *from = fp;
	
	count++;
	count/=2;
	
	while(count--)
		*to++=*from++;
}

static void word_memcpy_fromcard(void *tp, const void *fp, int count)
{
	unsigned short *to = tp;
	const volatile unsigned short *from = fp;
	
	count++;
	count/=2;
	
	while(count--)
		*to++=*from++;
}

static void slow_sane_get_8390_hdr(struct device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	unsigned long hdr_start = (ring_page - WD_START_PG)<<8;
	word_memcpy_fromcard((void *)hdr, (char *)dev->mem_start+hdr_start, 4);
	/* Register endianism - fix here rather than 8390.c */
	hdr->count=(hdr->count&0xFF)<<8|(hdr->count>>8);
}

static void slow_sane_block_input(struct device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	unsigned long xfer_base = ring_offset - (WD_START_PG<<8);
	unsigned long xfer_start = xfer_base+dev->mem_start;

	if (xfer_start + count > dev->rmem_end) 
	{
		/* We must wrap the input move. */
		int semi_count = dev->rmem_end - xfer_start;
		word_memcpy_fromcard(skb->data, (char *)dev->mem_start+xfer_base, semi_count);
		count -= semi_count;
		word_memcpy_fromcard(skb->data + semi_count, 
			(char *)dev->rmem_start, count);
	}
	else
	{
		word_memcpy_fromcard(skb->data, (char *)dev->mem_start+xfer_base, count);
	}
}

static void slow_sane_block_output(struct device *dev, int count, const unsigned char *buf,
				int start_page)
{
	long shmem = (start_page - WD_START_PG)<<8;
	
	word_memcpy_tocard((char *)dev->mem_start+shmem, buf, count);
#if 0
	long shmem = (start_page - WD_START_PG)<<8;
	volatile unsigned short *to=(unsigned short *)(dev->mem_start+shmem);
	volatile int p;
	unsigned short *bp=(unsigned short *)buf;
	
	count=(count+1)/2;
	
	while(count--)
	{
		*to++=*bp++;
		for(p=0;p<10;p++)
			p++;
	}
#endif	
}

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c daynaport.c"
 *  version-control: t
 *  tab-width: 4
 *  kept-new-versions: 5
 * End:
 */
