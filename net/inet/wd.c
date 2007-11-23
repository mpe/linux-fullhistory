/* wd.c: A WD80x3 ethernet driver for linux. */
/*
    Written 1993 by Donald Becker.
    Copyright 1993 United States Government as represented by the
    Director, National Security Agency.  This software may be used and
    distributed according to the terms of the GNU Public License,
    incorporated herein by reference.
    
    This is a driver for the WD8003 and WD8013 ethercards.

    The Author may be reached as becker@super.org or
    C/O Supercomputing Research Ctr., 17100 Science Dr., Bowie MD 20715

    Thanks to Russ Nelson (nelson@crnwyr.com) for loaning me a WD8013.
*/

static char *version =
    "wd.c:v0.99-10 5/28/93 Donald Becker (becker@super.org)\n";

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/system.h>
#include <memory.h>

#include "dev.h"
#include "8390.h"


extern void NS8390_init(struct device *dev, int startp);
extern int ei_debug;
extern struct sigaction ei_sigaction;

int wdprobe(int ioaddr, struct device *dev);
int wdprobe1(int ioaddr, struct device *dev);

static void wd_reset_8390(struct device *dev);
static int wd_block_input(struct device *dev, int count,
			  char *buf, int ring_offset);
static void wd_block_output(struct device *dev, int count,
			    const unsigned char *buf, const start_page);
static int wd_close_card(struct device *dev);


#define WD_START_PG	0x00	/* First page of TX buffer */
#define WD03_STOP_PG	0x20	/* Last page +1 of RX ring */
#define WD13_STOP_PG	0x40	/* Last page +1 of RX ring */

#define WD_CMDREG	0	/* Offset to ASIC command register. */
#define  WD_RESET	0x80	/* Board reset, in WD_CMDREG. */
#define  WD_MEMENB	0x40	/* Enable the shared memory. */
#define WD_CMDREG5	5	/* Offset to 16-bit-only ASIC register 5. */
#define  ISA16		0x80	/* Enable 16 bit access from the ISA bus. */
#define  NIC16		0x40	/* Enable 16 bit access from the 8390. */
#define WD_NIC_OFFSET	16	/* Offset to the 8390 NIC from the base_addr. */

/*  Probe for the WD8003 and WD8013.  These cards have the station
    address PROM at I/O ports <base>+8 to <base>+13, with a checksum
    following. A Soundblaster can have the same checksum as an WDethercard,
    so we have an extra exclusionary check for it.

    The wdprobe1() routine initializes the card and fills the
    station address field. */

int wdprobe(int ioaddr,  struct device *dev)
{
    int *port, ports[] = {0x300, 0x280, 0x380, 0x240, 0};

    if (ioaddr > 0x100)
	return wdprobe1(ioaddr, dev);

    for (port = &ports[0]; *port; port++)
	if (inb(*port + 8) != 0xff
	    && inb(*port + 9) != 0xff /* Extra check to avoid soundcard. */
	    && wdprobe1(*port, dev))
	    return *port;
    return 0;
}

int wdprobe1(int ioaddr, struct device *dev)
{
  int i;
  unsigned char *station_addr = dev->dev_addr;
  int checksum = 0;
  int ancient = 0;		/* An old card without config registers. */

#if defined(EI_DEBUG) && EI_DEBUG > 2
  printk("WD80x3 ethercard at %#3x:", ioaddr);
  for (i = 0; i < 16; i++) {
      printk(" %2.2X", inb(ioaddr+i));
  }  
  printk("\n");
  printk("WD80x3 ethercard at %#3x:", ioaddr+i);
  for (;i < 33; i++) {
      printk(" %2.2X", inb(ioaddr+i));
  }  
  printk("\n");
#endif
  printk("WD80x3 ethercard probe at %#3x:", ioaddr);
  for (i = 0; i < 8; i++) {
      int inval = inb(ioaddr + 8 + i);
      checksum += inval;
      if (i < 6)
	  printk(" %2.2X", (station_addr[i] = inval));
  }
  
  if ((checksum & 0xff) != 0xFF) {
      printk(" not found (%#2.2x).\n", checksum);
      return 0;
  }

  ei_status.name = "WD8003";
  ei_status.word16 = 0;

  /* The following PureData probe code was contributed by
     Mike Jagdis <jaggy@purplet.demon.co.uk>. */
  /* Puredata seem to do software configuration differently from
   * others so we have to check for them.
   */
  if (inb(ioaddr+0) == 'P' && inb(ioaddr+1) == 'D') {
      ei_status.name = "PDI8023";

      /* We can also check whether it is an 8 bit (-8), 16 bit (-16)
       * or dumb (Toshiba) card. The Toshiba version doesn't support
       * anything except jumper configuration.
       */
      switch (inb(ioaddr+2)) {
          case 0x03:
              ei_status.word16 = 0;
              ei_status.name = "PDI8023-8";
              break;

          case 0x05:
              /* Not sure really... */
              ei_status.word16 = 0;
              ei_status.name = "PDUC8023";
              break;

          case 0x0a:
              ei_status.word16 = 1;
              ei_status.name = "PDI8023-16";
              break;

          default:
              /* Either 0x01 (dumb) or they've released a new version. */
              ei_status.word16 = 0;
              ei_status.name = "PDI8023";
              break;
      }
      dev->mem_start = ((inb(ioaddr+5) & 0x1c) + 0xc0) << 12;
      dev->irq = (inb(ioaddr+5) >> 5) & 0x07;
      if (dev->irq == 7)
          dev->irq = 10;
      else
          dev->irq++;
  }				/* End of PureData probe */

  /* This method of checking for a 16-bit board is borrowed from the
     we.c driver.  A simpler method is just to look in ASIC reg. 0x03.
     I'm comparing the two method in alpha test to make certain they
     return the same result. */
#ifndef FORCE_8BIT		/* Same define as we.c. */
  /* Check for 16 bit board - it doesn't have register 0/8 aliasing.
     Do NOT check i>=6 here -- it hangs some old 8003 boards! */
  for (i = 0; i < 6; i++)
      if (inb(ioaddr+i) != inb(ioaddr+8+i))
	  break;
  if (i >= 6) {
      ancient = 1;
      ei_status.name = "WD8003-old";
  } else {
      int tmp = inb(ioaddr+1); /* fiddle with 16bit bit */
      outb( tmp ^ 0x01, ioaddr+1 ); /* attempt to clear 16bit bit */
      if (((inb( ioaddr+1) & 0x01) == 0x01) /* A 16 bit card */
	  && (tmp & 0x01) == 0x01   ) {		/* In a 16 slot. */
	  int asic_reg5 = inb(ioaddr+WD_CMDREG5);
	  /* Magic to set ASIC to word-wide mode. */
	  outb( ISA16 | NIC16 | (asic_reg5&0x1f), ioaddr+WD_CMDREG5);
	  outb(tmp, ioaddr+1);
	  ei_status.name = "WD8013";
	  ei_status.word16 = 1; 	/* We have a 16bit board here! */
      }
      outb(tmp, ioaddr+1);		/* Restore original reg1 value. */
  }
#endif /* not FORCE_8BIT */

#ifndef final_version
  if ( !ancient && (inb(ioaddr+1) & 0x01) != (ei_status.word16 & 0x01))
      printk("\nWD80?3: Bus width conflict, %d (probe) != %d (reg report).",
	     ei_status.word16 ? 16:8, (inb(ioaddr+1) & 0x01) ? 16 : 8);
#endif

#if defined(WD_SHMEM) && WD_SHMEM > 0x80000
  /* Allow an override.  */
  dev->mem_start = WD_SHMEM;
#else
  if (dev->mem_start == 0) {
      /* Sanity and old 8003 check */
      int reg0 = inb(ioaddr);
      if (reg0 == 0xff) {
	  /* Future plan: this could check a few likely locations first. */
	  dev->mem_start = 0xd0000;
	  printk(" assigning address %#x", dev->mem_start);
      } else {
	  int high_addr_bits = inb(ioaddr+WD_CMDREG5) & 0x1f;
	  /* Some boards don't have the register 5 -- it returns 0xff. */
	  if (high_addr_bits == 0x1f || ei_status.word16 == 0)
	      high_addr_bits = 0x01;
	  dev->mem_start = ((reg0&0x3f) << 13) + (high_addr_bits << 19);
      }
  }
#endif

  /* The 8390 isn't at the base address -- the ASIC regs are there! */
  dev->base_addr = ioaddr+WD_NIC_OFFSET;

  ei_status.tx_start_page = WD_START_PG;
  ei_status.rx_start_page = WD_START_PG + TX_PAGES;
  ei_status.stop_page = ei_status.word16 ? WD13_STOP_PG : WD03_STOP_PG;

  dev->rmem_start = dev->mem_start + TX_PAGES*256;
  dev->mem_end = dev->rmem_end
      = dev->mem_start + (ei_status.stop_page - WD_START_PG)*256;

  if (dev->irq < 2) {
      int irqmap[] = {9,3,5,7,10,11,15,4};
      int reg1 = inb(ioaddr+1);
      int reg4 = inb(ioaddr+4);
      if (reg1 == 0xff){	/* Ack!! No way to read the IRQ! */
	  dev->irq = ei_status.word16 ? 10 : 5;
      } else
	  dev->irq = irqmap[((reg4 >> 5) & 0x03)
			    + (reg1 & 0x04)];
  } else if (dev->irq == 2)
      /* Fixup for users that don't know that IRQ 2 is really IRQ 9,
	 or don't know which one to set. */
      dev->irq = 9;

  /* Snarf the interrupt now.  There's no point in waiting since we cannot
     share and the board will usually be enabled. */
  { int irqval = irqaction (dev->irq, &ei_sigaction);
    if (irqval) {
	printk (" unable to get IRQ %d (irqval=%d).\n", dev->irq, irqval);
	return 0;
    }
  }

  printk("\n%s: %s using IRQ %d with shared memory at %#x-%#x.\n",
	 dev->name, ei_status.name, dev->irq, dev->mem_start, dev->mem_end-1);
  if (ei_debug > 1)
      printk(version);

#if defined(EI_DEBUG) && EI_DEBUG > 2
  printk("%s: Address read from register is %#x, setting address %#x\n",
	 ei_status.name,
	 ((inb(ioaddr+WD_CMDREG5)&0x1f)<<19) + ((inb(ioaddr)&0x3f) << 13),
	 dev->mem_start);
#endif

  /* Map in the shared memory. Always set register 0 last to remain
     compatible with very old boards. */
  if (ei_status.word16)
      outb( ISA16 | NIC16 | ((dev->mem_start>>19) & 0x1f), ioaddr+WD_CMDREG5);
  outb((((dev->mem_start>>13) & 0x3f)|WD_MEMENB), ioaddr); /* WD_CMDREG */

  ei_status.reset_8390 = &wd_reset_8390;
  ei_status.block_input = &wd_block_input;
  ei_status.block_output = &wd_block_output;
  dev->stop = &wd_close_card;
  NS8390_init(dev, 0);

  return dev->base_addr;
}

static void
wd_reset_8390(struct device *dev)
{
    int wd_cmd_port = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */
    int reset_start_time = jiffies;

    outb(WD_RESET, wd_cmd_port);
    if (ei_debug > 1) printk("resetting the WD80x3 t=%d...", jiffies);
    ei_status.txing = 0;

    sti();
    /* We shouldn't use the boguscount for timing, but this hasn't been
       checked yet, and you could hang your machine if jiffies break... */
    {
	int boguscount = 150000;
	while(jiffies - reset_start_time < 2)
	    if (boguscount-- < 0) {
		printk("jiffy failure (t=%d)...", jiffies);
		break;
	    }
    }

    /* Set up the ASIC registers, just in case something changed them. */
    if (ei_status.word16)
	outb(NIC16 | ((dev->mem_start>>19) & 0x1f),
	     wd_cmd_port+WD_CMDREG5);
    outb((((dev->mem_start>>13) & 0x3f)|WD_MEMENB), wd_cmd_port);

    while ((inb(dev->base_addr+EN0_ISR) & ENISR_RESET) == 0)
	if (jiffies - reset_start_time > 2) {
	    printk("%s: wd_reset_8390() did not complete.\n", dev->name);
	    break;
	}
#if defined(EI_DEBUG) && EI_DEBUG > 2
    {
	int i;
	printk("\nWD80x3 ethercard at %#3x:", wd_cmd_port);
	for (i = 0; i < 16; i++) {
	    printk(" %2.2X", inb(wd_cmd_port+i));
	}  
	printk("\nWD80x3 ethercard at %#3x:", wd_cmd_port+i);
	for (;i < 33; i++) {
	    printk(" %2.2X", inb(wd_cmd_port+i));
	}  
	printk("\n");
    }
#endif
}

/* Block input and output are easy on shared memory ethercards, and trivial
   on the Western digital card where there is no choice of how to do it. */

static int
wd_block_input(struct device *dev, int count, char *buf, int ring_offset)
{
    void *xfer_start = (void *)(dev->mem_start + ring_offset - (WD_START_PG<<8));
#if !defined(WD_no_mapout)
    int wd_cmdreg = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */
    int reg5_val = ((dev->mem_start>>19) & 0x1f) | NIC16;

    /* Map in the shared memory. */
    if (ei_status.word16)
	outb(ISA16 | reg5_val, wd_cmdreg + WD_CMDREG5 );
    outb((((dev->mem_start>>13) & 0x3f) | WD_MEMENB), wd_cmdreg);
#endif
    if (xfer_start + count > (void*) dev->rmem_end) {
	/* We must wrap the input move. */
	int semi_count = (void*)dev->rmem_end - xfer_start;
	memcpy(buf, xfer_start, semi_count);
	count -= semi_count;
	memcpy(buf + semi_count, (char *)dev->rmem_start, count);
	return dev->rmem_start + count;
    }
    memcpy(buf, xfer_start, count);
    if (ei_debug > 4) {
	unsigned short *board = (unsigned short *) xfer_start;
	printk("%s: wd8013 block_input(cnt=%d offset=%3x addr=%#x) = %2x %2x %2x...\n",
	       dev->name, count, ring_offset, xfer_start,
	       board[-1], board[0], board[1]);
    }
#if !defined(WD_no_mapout)
    /* Turn off 16 bit access so that reboot works. */
    if (ei_status.word16)
	outb(reg5_val, wd_cmdreg + WD_CMDREG5 );
#endif
    return ring_offset + count;
}

/* This could only be outputting to the transmit buffer.  The
   ping-pong transmit setup doesn't work with this yet. */
static void
wd_block_output(struct device *dev, int count, const unsigned char *buf,
		int start_page)
{
    unsigned char *shmem
	= (unsigned char *)dev->mem_start + ((start_page - WD_START_PG)<<8);
#if !defined(WD_no_mapout)
    int wd_cmdreg = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */
    int reg5_val = ((dev->mem_start>>19) & 0x1f) | NIC16;

    /* Map in the shared memory. */
    if (ei_status.word16)
	outb(ISA16 | reg5_val, wd_cmdreg + WD_CMDREG5 );
    outb((((dev->mem_start>>13) & 0x3f)|WD_MEMENB), wd_cmdreg);
#endif
    memcpy(shmem, buf, count);
    if (ei_debug > 4)
	printk("%s: wd80*3 block_output(addr=%#x cnt=%d) -> %2x=%2x %2x=%2x %d...\n",
	       shmem, count, shmem[23], buf[23], shmem[24], buf[24], memcmp(shmem,buf,count));
#if !defined(WD_no_mapout)
    /* Turn off 16 bit access so that reboot works. */
    if (ei_status.word16)
	outb(reg5_val, wd_cmdreg + WD_CMDREG5 );
#endif
}

/* This function resets the ethercard if something screws up. */
static int
wd_close_card(struct device *dev)
{
    int wd_cmdreg = dev->base_addr - WD_NIC_OFFSET; /* WD_CMDREG */

    if (ei_debug > 1)
	printk("%s: Shutting down ethercard.\n", dev->name);
    NS8390_init(dev, 0);

    /* Turn off 16-bit shared memory so reboot works. */
    outb(((dev->mem_start>>19) & 0x1f) | NIC16, wd_cmdreg + WD_CMDREG5 );
    outb((((dev->mem_start>>13) & 0x3f)), wd_cmdreg);

    return 0;
}


/*
 * Local variables:
 *  compile-command: "gcc -DKERNEL -Wall -O6 -fomit-frame-pointer -I/usr/src/linux/net/tcp -c wd.c"
 *  version-control: t
 *  kept-new-versions: 5
 * End:
 */
