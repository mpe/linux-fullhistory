/*
 * Linux ethernet device driver for the 3Com Etherlink Plus (3C505)
 * 	By Craig Southeren
 *
 * 3c505.c	This module implements an interface to the 3Com
 *		Etherlink Plus (3c505) ethernet card. Linux device 
 *		driver interface reverse engineered from the Linux 3C509
 *		device drivers. Vital 3C505 information gleaned from
 *		the Crynwr packet driver
 *
 * Version:	@(#)3c505.c	0.1	23/09/93
 *
 * Authors:	Linux 3c505 device driver by:
 *			Craig Southeren, <geoffw@extro.ucc.su.oz.au>
 *              Linux 3C509 driver by:
 *             		Donald Becker, <becker@super.org>
 *		Crynwr packet driver by
 *			Krishnan Gopalan and Gregg Stefancik,
 * 			   Clemson Univesity Engineering Computer Operations.
 *			Portions of the code have been adapted from the 3c505
 *			   driver for NCSA Telnet by Bruce Orchard and later
 *			   modified by Warren Van Houten and krus@diku.dk.
 *              3C505 technical information provided by
 *                      Terry Murphy, of 3Com Network Adapter Division
 *		Special thanks to Juha Laiho, <jlaiho@ichaos.nullnet.fi>
 *                     
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <asm/io.h>
#ifndef port_read
#include "iow.h"
#endif

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "3c505.h"

#ifdef ELP_DEBUG
static int elp_debug = ELP_DEBUG;
#else
static int elp_debug = 0;
#endif

/*
 *  0 = no messages
 *  1 = messages when high level commands performed
 *  2 = messages when low level commands performed
 *  3 = messages when interrupts received
 */

#define	ELP_VERSION	"0.1.0"

extern struct device *irq2dev_map[16];

/*****************************************************************
 *
 * useful macros
 *
 *****************************************************************/

#define	INB(port)	inb((unsigned short)port) 
#define	OUTB(val,port)	outb((unsigned char)val,(unsigned short)port);

#ifndef	TRUE
#define	TRUE	1
#endif

#ifndef	FALSE
#define	FALSE	0
#endif


/*****************************************************************
 *
 * PCB structure
 *
 *****************************************************************/

typedef struct {
  unsigned char   command;		/* PCB command code */
  unsigned char   length;		/* PCB data length */
  unsigned char   data[MAX_PCB_DATA];	/* PCB data */
} pcb_struct;


/*****************************************************************
 *
 *  structure to hold context information for adapter
 *
 *****************************************************************/

typedef struct {
  int        io_addr;	     /* base I/O address */
  short      got_configure;  /* set to TRUE when configure response received */
  pcb_struct tx_pcb;         /* PCB for foreground sending */
  pcb_struct rx_pcb;         /* PCB for foreground receiving */
  pcb_struct itx_pcb;        /* PCB for background sending */
  pcb_struct irx_pcb;        /* PCB for background receiving */
  struct enet_statistics stats;
} elp_device;

/*****************************************************************
 *
 *  useful functions for accessing the adapter
 *
 *****************************************************************/

/*
 * use this routine when accessing the ASF bits as they are
 * changed asynchronously by the adapter
 */

/* get adapter PCB status */
#define	GET_ASF()  	(get_status(adapter)&ASF_PCB_MASK)
#define	GET_STATUS()  	(get_status(adapter))

static int get_status (elp_device * adapter)

{
  register int stat1;
  do {
    stat1 = INB(adapter->io_addr+PORT_STATUS);
  } while (stat1 != INB(adapter->io_addr+PORT_STATUS));
  return stat1;
}

#define	SET_HSF(hsf)	(set_hsf(adapter,hsf))

static void set_hsf (elp_device * adapter, int hsf)

{
  cli();
  OUTB((INB(adapter->io_addr+PORT_CONTROL)&(~HSF_PCB_MASK))|hsf, adapter->io_addr+PORT_CONTROL); 
  sti(); 
}

#define	WAIT_HCRE(toval)	(wait_hcre(adapter,toval))

static int wait_hcre(elp_device * adapter, int toval)

{
  int timeout = jiffies + toval;
  while(((INB(adapter->io_addr+PORT_STATUS)&STATUS_HCRE)==0) &&
         (jiffies <= timeout))
    ;
  if (jiffies > timeout) {
    printk("elp0: timeout waiting for HCRE\n");
    return FALSE;
  }
  return TRUE;
}

/*****************************************************************
 *
 * send_pcb
 *   Send a PCB to the adapter. 
 *
 *	output byte to command reg  --<--+
 *	wait until HCRE is non zero      |
 *	loop until all bytes sent   -->--+
 *	set HSF1 and HSF2 to 1
 *	output pcb length
 *	wait until ASF give ACK or NAK
 *	set HSF1 and HSF2 to 0
 *
 *****************************************************************/

static int send_pcb(elp_device * adapter, pcb_struct * pcb)

{
  int i;
  int cont;
  int retry = 0;
  int timeout;

  while (1) {

    cont = 1;

    /*
     * load each byte into the command register and
     * wait for the HCRE bit to indicate the adapter
     * had read the byte
     */
    SET_HSF(0); 
    OUTB(pcb->command, (adapter->io_addr)+PORT_COMMAND);
    cont = WAIT_HCRE(5);
   /* SET_HSF(0);  */

    if (cont) {
      OUTB(pcb->length, (adapter->io_addr)+PORT_COMMAND);
      cont = WAIT_HCRE(2);
    }

    for (i = 0; cont && (i < pcb->length); i++) {
      OUTB(pcb->data[i], (adapter->io_addr)+PORT_COMMAND);
      cont = WAIT_HCRE(2);
    }

    /* set the host status bits to indicate end of PCB */
    /* send the total packet length as well */
    /* wait for the adapter to indicate that it has read the PCB */
    if (cont) {
      SET_HSF(HSF_PCB_END);
      OUTB(2+pcb->length, adapter->io_addr+PORT_COMMAND);
      timeout = jiffies + 6;
      while (jiffies < timeout) {
        i = GET_ASF();
        if ((i == ASF_PCB_ACK) ||
            (i == ASF_PCB_NAK))
          break;
      }
      if (i == ASF_PCB_ACK) {
        SET_HSF(0); 
        return TRUE;
      } else if (i = ASF_PCB_NAK) {
        SET_HSF(0);
        printk("elp0: PCB send was NAKed\n");
        {
          int to = jiffies + 5;
          while (jiffies < to)
            ;
        }
      }
    }

    if (elp_debug >= 6)
      printk("elp0: NAK/timeout on send PCB\n");
    if ((retry++ & 7) == 0) 
      printk("elp0: retry #%i on send PCB\n", retry);
  }
}

/*****************************************************************
 *
 * receive_pcb
 *   Read a PCB to the adapter
 *
 *	wait for ACRF to be non-zero         ---<---+
 *      input a byte                                |
 *      if ASF1 and ASF2 were not both one          |
 *           before byte was read, loop      --->---+
 *      set HSF1 and HSF2 for ack
 *
 *****************************************************************/

static int receive_pcb(elp_device * adapter, pcb_struct * pcb)

{
  int i;
  int total_length;
  int stat;

  /* get the command code */
  while (((stat = GET_STATUS())&STATUS_ACRF) == 0)
    ;
  SET_HSF(0); 
  pcb->command = INB(adapter->io_addr+PORT_COMMAND);
  if ((stat & ASF_PCB_MASK) != ASF_PCB_END) {

    /* read the data length */
    while (((stat = GET_STATUS())&STATUS_ACRF) == 0)
      ;
    pcb->length = INB(adapter->io_addr+PORT_COMMAND);
    if ((stat & ASF_PCB_MASK) != ASF_PCB_END) {

      /* read the data */
      i = 0;
      do {
        while (((stat = GET_STATUS())&STATUS_ACRF) == 0)
          ;
        pcb->data[i++] = INB(adapter->io_addr+PORT_COMMAND);
      } while ((stat & ASF_PCB_MASK) != ASF_PCB_END);

      /* woops, the last "data" byte was really the length! */
      total_length = pcb->data[--i];

      /* safety check total length vs data length */
      if (total_length != (pcb->length + 2)) {
        if (elp_debug >= 6)
          printk("elp0: mangled PCB received\n");
        SET_HSF(HSF_PCB_NAK);
        return FALSE;
      }
      SET_HSF(HSF_PCB_ACK);
      return TRUE;
    }
  }

  SET_HSF(HSF_PCB_NAK); 
  return FALSE;
}

static void adapter_hard_reset(elp_device * adapter)

{
  int timeout;

  /*
   * take FLSH and ATTN high
   */
  OUTB(CONTROL_ATTN|CONTROL_FLSH, adapter->io_addr+PORT_CONTROL); 

  /*
   * wait for a little bit
   */
  for (timeout = jiffies + 20; jiffies <= timeout; )
    ;
  
  /*
   * now take them low
   */
  OUTB(0, adapter->io_addr+PORT_CONTROL); 

  /*
   * wait for a little bit
   */
  for (timeout = jiffies + 20; jiffies <= timeout; )
    ;
  
  /*
   * now hang around until the board gets it's act together
   */
  for (timeout = jiffies + (100 * 15); jiffies <= timeout; ) 
    if (GET_ASF() != ASF_PCB_END)
      break;
}

static void adapter_reset(elp_device * adapter)

{
  int timeout;

  cli();
  OUTB(CONTROL_ATTN|INB(adapter->io_addr+PORT_CONTROL), adapter->io_addr+PORT_CONTROL);
  sti();

  /*
   * wait for a little bit
   */
  for (timeout = jiffies + 20; jiffies <= timeout; )
    ;
  
  cli();
  OUTB(INB(adapter->io_addr+PORT_CONTROL)&~(CONTROL_ATTN), adapter->io_addr+PORT_CONTROL);
  sti();
}

/******************************************************
 *
 *  queue a receive command on the adapter so we will get an
 *  interrupt when a packet is received.
 *
 ******************************************************/

static int start_receive(elp_device * adapter, pcb_struct * tx_pcb)

{
  if (elp_debug > 3)
    printk("elp0: restarting receiver\n");
  tx_pcb->command = CMD_RECEIVE_PACKET;
  tx_pcb->length  = 8;
  tx_pcb->data[4] = 1600 & 0xff;
  tx_pcb->data[5] = 1600 >> 8;
  tx_pcb->data[6] = 0;		/* set timeout to zero */
  tx_pcb->data[7] = 0;
  return send_pcb(adapter, tx_pcb); 
}

/******************************************************
 *
 * extract a packet from the adapter
 * this routine is only called from within the interrupt
 * service routine, so no cli/sti calls are needed
 * note that the length is always assumed to be even
 *
 ******************************************************/

static void receive_packet(struct device * dev,
                              elp_device * adapter,
                                       int len)

{
  register int i;
  unsigned short * ptr;
  short d;

  /*
   * allocate a buffer to put the packet into.
   */
  struct sk_buff *skb;
  skb = alloc_skb(len+3, GFP_ATOMIC);

  /*
   * make sure the data register is going the right way
   */
  OUTB(INB(adapter->io_addr+PORT_CONTROL)|CONTROL_DIR, adapter->io_addr+PORT_CONTROL); 

  /*
   * if buffer could not be allocated, swallow it
   */
  if (skb == NULL) {
    for (i = 0; i < (len/2); i++) {
      while ((INB(adapter->io_addr+PORT_STATUS)&STATUS_HRDY) == 0)
        ;
      d = inw(adapter->io_addr+PORT_DATA);
    }
    adapter->stats.rx_dropped++;

  } else {

    /*
     * now read the data from the adapter
     */
    ptr = (unsigned short *)(skb->data);
    for (i = 0; i < (len/2); i++) { 
      while ((INB(adapter->io_addr+PORT_STATUS)&STATUS_HRDY) == 0) {
        ;
      }
      *ptr = inw(adapter->io_addr+PORT_DATA); 
      ptr++; 
    }

    /*
     * the magic routine "dev_rint" passes the packet up the
     * protocol chain. If it returns 0, we can assume the packet was
     * swallowed up. If not, then we are responsible for freeing memory
     */
    if (dev_rint((unsigned char *)skb, len, IN_SKBUFF, dev) != 0) {
      printk("%s: receive buffers full.\n", dev->name);
      kfree_skb(skb, FREE_READ);
    }
  }

  OUTB(INB(adapter->io_addr+PORT_CONTROL)&(~CONTROL_DIR), adapter->io_addr+PORT_CONTROL); 
}


/******************************************************
 *
 * interrupt handler
 *
 ******************************************************/

static void elp_interrupt(int reg_ptr)

{
  int len;
  int dlen;
  int irq = -(((struct pt_regs *)reg_ptr)->orig_eax+2);
  struct device *dev;
  elp_device * adapter;

  if (irq < 0 || irq > 15) {
    printk ("elp0: illegal IRQ number found in interrupt routine (%i)\n", irq);
    return;
  }

  if (irq != 0xc) {
    printk ("elp0: warning - interrupt routine has incorrect IRQ of %i\n", irq);
    return;
  }

  dev = irq2dev_map[irq];
  adapter = (elp_device *) dev->priv;

  if (dev == NULL) {
    printk ("elp_interrupt(): irq %d for unknown device.\n", irq);
    return;
  }

  if (dev->interrupt)
    if (elp_debug >= 3)
      printk("%s: Re-entering the interrupt handler.\n", dev->name);
  dev->interrupt = 1;

  /*
   * allow interrupts (we need timers!)
   */
  sti();

  /*
   * receive a PCB from the adapter
   */
  while ((INB(adapter->io_addr+PORT_STATUS)&STATUS_ACRF) != 0) {
    
    if (receive_pcb(adapter, &adapter->irx_pcb)) {

      switch (adapter->irx_pcb.command) {

        /*
         * 82586 configured correctly
         */
        case CMD_CONFIGURE_82586_RESPONSE:
          adapter->got_configure = 1;
          if (elp_debug >= 3)
            printk("%s: interrupt - configure response received\n", dev->name);
          break;

        /*
         * received a packet
         */
        case CMD_RECEIVE_PACKET_COMPLETE:
          len  = adapter->irx_pcb.data[6] + (adapter->irx_pcb.data[7] << 8);
          dlen  = adapter->irx_pcb.data[4] + (adapter->irx_pcb.data[5] << 8);
          if (adapter->irx_pcb.data[8] != 0) {
            printk("%s: interrupt - packet not received correctly\n", dev->name);
          } else {
            if (elp_debug >= 3)
              printk("%s: interrupt - packet received of length %i (%i)\n", dev->name, len, dlen);
            receive_packet(dev, adapter, dlen);
            if (elp_debug >= 3)
              printk("%s: packet received\n", dev->name);
            adapter->stats.rx_packets++;
          }
          if (dev->start && !start_receive(adapter, &adapter->itx_pcb)) 
            if (elp_debug >= 2)
              printk("%s: interrupt - failed to send receive start PCB\n", dev->name);
          if (elp_debug >= 3)
            printk("%s: receive procedure complete\n", dev->name);

          break;

        /*
         * sent a packet
         */
        case CMD_TRANSMIT_PACKET_COMPLETE:
          if (elp_debug >= 3) 
            printk("%s: interrupt - packet sent\n", dev->name);
          if (adapter->irx_pcb.data[4] != 0)
            if (elp_debug >= 2)
              printk("%s: interrupt - error sending packet %4.4x\n", dev->name, 
                adapter->irx_pcb.data[4] + (adapter->irx_pcb.data[5] << 8));
          dev->tbusy = 0;
          mark_bh(INET_BH);
          adapter->stats.tx_packets++;
          break;

        /*
         * some unknown PCB
         */
        default:
          printk("%s: unknown PCB received - %2.2x\n", dev->name, adapter->irx_pcb.command);
          break;
      }
    } else 
      printk("%s: failed to read PCB on interrupt\n", dev->name);
  }

  /*
   * indicate no longer in interrupt routine
   */
  dev->interrupt = 0;
}


/******************************************************
 *
 * open the board
 *
 ******************************************************/

static int elp_open (struct device *dev)

{
  elp_device * adapter = (elp_device *) dev->priv;

  if (elp_debug >= 1)
    printk("%s: request to open device\n", dev->name);

  /*
   * make sure we actually found the device
   */
  if (adapter == NULL) {
    printk("%s: Opening a non-existent physical device\n", dev->name);
    return -EAGAIN;
  }

  /*
   * interrupt routine not entered
   */
  dev->interrupt = 0;

  /*
   *  transmitter not busy 
   */
  dev->tbusy = 0;

  /*
   * install our interrupt service routine
   */
  if (request_irq(dev->irq, &elp_interrupt))  
    return -EAGAIN;

  /*
   * make sure we can find the device header given the interrupt number
   */
  irq2dev_map[dev->irq] = dev;

  /*
   * enable interrupts on the board
   */
  OUTB(CONTROL_CMDE, adapter->io_addr+PORT_CONTROL);

  /*
   * device is now offically open!
   */
  dev->start = 1;

  /*
   * configure adapter to receive broadcast messages and wait for response
   */
  if (elp_debug >= 2)
    printk("%s: sending 82586 configure command\n", dev->name);
  adapter->tx_pcb.command = CMD_CONFIGURE_82586;
  adapter->tx_pcb.data[0] = 1;
  adapter->tx_pcb.data[1] = 0;
  adapter->tx_pcb.length  = 2;
  adapter->got_configure  = 0;
  if (!send_pcb(adapter, &adapter->tx_pcb))
    printk("%s: couldn't send 82586 configure command\n", dev->name);
  else
    while (adapter->got_configure == 0)
      ;

  /*
   * queue a receive command to start things rolling
   */
  if (!start_receive(adapter, &adapter->tx_pcb))
    printk("%s: start receive command failed \n", dev->name);
  if (elp_debug >= 2)
    printk("%s: start receive command sent\n", dev->name);

  return 0;			/* Always succeed */
}

/******************************************************
 *
 * close the board
 *
 ******************************************************/

static int elp_close (struct device *dev)

{
  elp_device * adapter = (elp_device *) dev->priv;

  if (elp_debug >= 1)
    printk("%s: request to close device\n", dev->name);

  /*
   * disable interrupts on the board
   */
  OUTB(0x00, adapter->io_addr+PORT_CONTROL);

  /*
   *  flag transmitter as busy (i.e. not available)
   */
  dev->tbusy = 1;

  /*
   *  indicate device is closed
   */
  dev->start = 0;

  /*
   * release the IRQ
   */
  free_irq(dev->irq);

  /*
   * and we no longer have to map irq to dev either
   */
  irq2dev_map[dev->irq] = 0;

  return 0;
}


/******************************************************
 *
 * send a packet to the adapter
 *
 ******************************************************/

static int send_packet (elp_device * adapter, unsigned char * ptr, int len)

{
  int i;

  /*
   * make sure the length is even and no shorter than 60 bytes
   */
  unsigned int nlen = (((len < 60) ? 60 : len) + 1) & (~1);

  /*
   * send the adapter a transmit packet command. Ignore segment and offset
   * and make sure the length is even
   */
  adapter->tx_pcb.command = CMD_TRANSMIT_PACKET;
  adapter->tx_pcb.length  = 6;
  adapter->tx_pcb.data[4] = nlen & 0xff;
  adapter->tx_pcb.data[5] = nlen >> 8;
  if (!send_pcb(adapter, &adapter->tx_pcb)) 
    return FALSE;

  /*
   * make sure the data register is going the right way
   */
  cli(); 
  OUTB(INB(adapter->io_addr+PORT_CONTROL)&(~CONTROL_DIR), adapter->io_addr+PORT_CONTROL); 
  sti(); 

  /*
   * write data to the adapter
   */
  for (i = 0; i < (nlen/2);i++) {
    while ((INB(adapter->io_addr+PORT_STATUS)&STATUS_HRDY) == 0)
      ;
    outw(*(short *)ptr, adapter->io_addr+PORT_DATA);
    ptr +=2;
  }
  
  return TRUE;
}

/******************************************************
 *
 * start the transmitter
 *    return 0 if sent OK, else return 1
 *
 ******************************************************/

static int elp_start_xmit(struct sk_buff *skb, struct device *dev)

{
  elp_device * adapter = (elp_device *) dev->priv;

  /*
   * not sure what this does, but the 3c609 driver does it, so...
   */
  if (skb == NULL) {
    dev_tint(dev);
    return 0;
  }

  /*
   * if we ended up with a munged length, don't send it
   */
  if (skb->len <= 0)
    return 0;

  if (elp_debug >= 1)
    printk("%s: request to send packet of length %i\n", dev->name, skb->len);

  /*
   * if the transmitter is still busy, we have a transmit timeout...
   */
  if (dev->tbusy) {
    int tickssofar = jiffies - dev->trans_start;
    if (tickssofar < 500)
      return 1;
    printk("%s: transmit timed out, resetting adapter\n", dev->name);
    if ((INB(adapter->io_addr+PORT_STATUS)&STATUS_ACRF) != 0) 
      printk("%s: hmmm...seemed to have missed an interrupt!\n", dev->name);
    adapter_reset(adapter);
    dev->trans_start = jiffies;
    dev->tbusy = 0;
  }

  /*
   * send the packet at (void *)(skb+1) for skb->len
   */
  if (!send_packet(adapter, (unsigned char *)(skb->data), skb->len)) {
    printk("%s: send packet PCB failed\n", dev->name);
    return 1;
  }

  if (elp_debug >= 2)
    printk("%s: packet of length %i sent\n", dev->name, skb->len);


  /*
   * start the transmit timeout
   */
  dev->trans_start = jiffies;

  /*
   * the transmitter is now busy
   */
  dev->tbusy = 1;

  /*
   * if we have been asked to free the buffer, do so
   */
  dev_kfree_skb(skb, FREE_WRITE);

  return 0;
}

/******************************************************
 *
 * return statistics on the board
 *
 ******************************************************/

static struct enet_statistics * elp_get_stats(struct device *dev)

{
  if (elp_debug >= 1)
    printk("%s: request for stats\n", dev->name);

  elp_device * adapter = (elp_device *) dev->priv;
  return &adapter->stats;
}

/******************************************************
 *
 * initialise Etherlink Pus board
 *
 ******************************************************/

static void elp_init(struct device *dev)

{
  int i;
  elp_device * adapter;

  /*
   * NULL out buffer pointers
   */
  for (i = 0; i < DEV_NUMBUFFS; i++)
    dev->buffs[i] = NULL;

  /*
   * set ptrs to various functions
   */
  dev->open             = elp_open;		/* local */
  dev->stop             = elp_close;		/* local */
  dev->get_stats	= elp_get_stats;	/* local */
  dev->hard_start_xmit  = elp_start_xmit;       /* local */

  dev->hard_header	= eth_header;		/* eth.c */
  dev->add_arp	        = eth_add_arp;		/* eth.c */
  dev->rebuild_header	= eth_rebuild_header;	/* eth.c */
  dev->type_trans	= eth_type_trans;	/* eth.c */

  dev->queue_xmit	= dev_queue_xmit;	/* dev.c */

  /*
   * setup ptr to adapter specific information
   */
  adapter = (elp_device *)(dev->priv = kmalloc(sizeof(elp_device), GFP_KERNEL));
  adapter->io_addr = dev->base_addr;
  memset(&(adapter->stats), 0, sizeof(struct enet_statistics));


  /*
   * Ethernet information
   */
  dev->type		= ARPHRD_ETHER;
  dev->hard_header_len  = ETH_HLEN;
  dev->mtu		= 1500;         /* eth_mtu */
  dev->addr_len	        = ETH_ALEN;
  for (i = 0; i < dev->addr_len; i++) 
    dev->broadcast[i] = 0xff;

  /*
   * New-style flags. 
   */
  dev->flags		= IFF_BROADCAST;
  dev->family		= AF_INET;
  dev->pa_addr	        = 0;
  dev->pa_brdaddr	= 0;
  dev->pa_mask	        = 0;
  dev->pa_alen        	= sizeof(unsigned long);

  /*
   * memory information
   */
  dev->mem_start = dev->mem_end = dev->rmem_end = dev->mem_start = 0;
}


/******************************************************
 *
 * probe for an Etherlink Plus board at the specified address
 * by attempting to get the ethernet address. 
 *
 ******************************************************/

int elp_probe(struct device *dev)

{
  elp_device adapter;
  int            i;

  /*
   *  setup adapter structure
   */
  adapter.io_addr = dev->base_addr;

  printk ("%s: probing for 3c505...", dev->name);

  /*
   * get the adapter's undivided attention (if it's there!)
   */
  adapter_hard_reset(&adapter); 

  /*
   * use ethernet address command to probe for board in polled mode
   */
  adapter.tx_pcb.command = CMD_STATION_ADDRESS;
  adapter.tx_pcb.length  = 0;
  if (!send_pcb   (&adapter, &adapter.tx_pcb) ||
      !receive_pcb(&adapter, &adapter.rx_pcb) ||
      (adapter.rx_pcb.command != CMD_ADDRESS_RESPONSE) ||
      (adapter.rx_pcb.length != 6)) {
    printk("not found\n");
    return -ENODEV;
  }

  for (i = 0; i < 6; i++) 
    dev->dev_addr[i] = adapter.rx_pcb.data[i];

  printk("found at port 0x%x, address = %s\n", dev->base_addr, eth_print(dev->dev_addr));
  
  elp_init(dev);
  return 0;
}

