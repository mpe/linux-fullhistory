/*
 * Linux ethernet device driver for the 3Com Etherlink Plus (3C505)
 * 	By Craig Southeren
 *
 * elplus.c	This module implements an interface to the 3Com
 *		Etherlink Plus (3c505) ethernet card. Linux device 
 *		driver interface reverse engineered from the Linux 3C509
 *		device drivers. Vital 3C505 information gleaned from
 *		the Crynwr packet driver
 *
 * Version:	@(#)elplus.c	0.5	11-Jun-94
 *
 * Authors:	Linux 3c505 device driver by:
 *			Craig Southeren, <geoffw@extro.ucc.su.oz.au>
 *              Final debugging by:
 *			Andrew Tridgell, <tridge@nimbus.anu.edu.au>
 *		Auto irq, auto detect, cleanup and v1.1.4+ kernel mods by:
 *			Juha Laiho, <jlaiho@ichaos.nullnet.fi>
 *              Linux 3C509 driver by:
 *             		Donald Becker, <becker@super.org>
 *		Crynwr packet driver by
 *			Krishnan Gopalan and Gregg Stefancik,
 * 			   Clemson University Engineering Computer Operations.
 *			Portions of the code have been adapted from the 3c505
 *			   driver for NCSA Telnet by Bruce Orchard and later
 *			   modified by Warren Van Houten and krus@diku.dk.
 *              3C505 technical information provided by
 *                      Terry Murphy, of 3Com Network Adapter Division
 *                     
 */


/*********************************************************
 *
 *  set ELP_KERNEL_TYPE to the following values depending upon
 *  the kernel type:
 *       0   = 0.99pl14 or earlier
 *       1   = 0.99pl15 through 1.1.3
 *       2   = 1.1.4 through 1.1.11
 *       3   = 1.1.12 through 1.1.19
 *       4   = 1.1.20
 *
 *********************************************************/

#define	ELP_KERNEL_TYPE	4

/*********************************************************
 *
 *  set ELP_NEED_HARD_RESET to 1, if having problems with
 *  "warm resets" from DOS. Bootup will then take some more
 *  time, as the adapter will perform self-test upon hard
 *  reset. This misbehaviour is reported to happen at least
 *  after use of Windows real-mode NDIS drivers.
 *
 *********************************************************/

#define ELP_NEED_HARD_RESET 0

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <asm/bitops.h>
#include <asm/io.h>

#if (ELP_KERNEL_TYPE < 2)
#include "dev.h"
#include "eth.h"
#include "skbuff.h"
#include "arp.h"
#else
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#endif

#ifndef port_read
#include "iow.h"
#endif

#include "3c505.h"

#define ELPLUS_DEBUG 0

/*********************************************************
 *
 *  define debug messages here as common strings to reduce space
 *
 *********************************************************/

static char * filename = __FILE__;

static char * null_msg = "*** NULL at %s(%d) ***\n";
#define CHECK_NULL(p) if (!p) printk(null_msg, filename, __LINE__)

static char * timeout_msg = "*** timeout at %s(%d) ***\n";
#define TIMEOUT_MSG()     printk(timeout_msg, filename,__LINE__)

static char * invalid_pcb_msg = "*** invalid pcb length %d at %s(%d) ***\n";

static char * search_msg = "%s: Looking for 3c505 adapter at address 0x%x...";

static char * stilllooking_msg = "still looking...";

static char * found_msg = "found.\n";

static char * notfound_msg = "not found (reason = %d)\n";

static char * couldnot_msg = "%s: 3c505 not found\n";

/*********************************************************
 *
 *  various other debug stuff
 *
 *********************************************************/

#ifdef ELPLUS_DEBUG
static int elp_debug = ELPLUS_DEBUG;
#else
static int elp_debug = 0;
#endif

#if (ELP_KERNEL_TYPE < 2)
extern void	skb_check(struct sk_buff *skb,int, char *);
#ifndef IS_SKB
#define IS_SKB(skb)	skb_check((skb),__LINE__,filename)
#endif
#else
#ifndef IS_SKB
#define IS_SKB(skb)	skb_check((skb),0,__LINE__,filename)
#endif
#endif


/*
 *  0 = no messages
 *  1 = messages when high level commands performed
 *  2 = messages when low level commands performed
 *  3 = messages when interrupts received
 */

#define	ELPLUS_VERSION	"0.4.0"

/*****************************************************************
 *
 * useful macros
 *
 *****************************************************************/

/*
 *  kernels before pl15 used an unobvious method for accessing
 *  the skb data area
 */
#if (ELP_KERNEL_TYPE < 1)
#define	SKB_DATA	(skb+1)
#else
#define	SKB_DATA	(skb->data) 
#endif

/*
 *  not all kernels before 1.1.4 had an alloc_skb function (apparently!!)
 */
#if (ELP_KERNEL_TYPE < 2)
#ifndef HAVE_ALLOC_SKB
#define alloc_skb(size, priority) (struct sk_buff *) kmalloc(size,priority)
#endif
#endif

#define	INB(port)	inb((unsigned short)(port)) 
#define	OUTB(val,port)	outb((unsigned char)(val),(unsigned short)(port));

#ifndef	TRUE
#define	TRUE	1
#endif

#ifndef	FALSE
#define	FALSE	0
#endif


/*****************************************************************
 *
 * List of I/O-addresses we try to auto-sense
 * Last element MUST BE 0!
 *****************************************************************/

const int addr_list[]={0x300,0x280,0x310,0}; 

/*****************************************************************
 *
 * PCB structure
 *
 *****************************************************************/

#include "3c505dta.h"

/*****************************************************************
 *
 *  structure to hold context information for adapter
 *
 *****************************************************************/

typedef struct {
  int        io_addr;	     /* base I/O address */
  char *     name;           /* used for debug output */
  short      got[NUM_TRANSMIT_CMDS];  /* flags for command completion */
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
  int timeout = jiffies + TIMEOUT;  
  register int stat1;
  do {
    stat1 = INB(adapter->io_addr+PORT_STATUS);
  } while (stat1 != INB(adapter->io_addr+PORT_STATUS) && jiffies < timeout);
  if (jiffies >= timeout)
    TIMEOUT_MSG();
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
  if (jiffies >= timeout) {
    TIMEOUT_MSG();
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

  CHECK_NULL(pcb);
  CHECK_NULL(adapter);

  if (pcb->length > MAX_PCB_DATA)
    printk(invalid_pcb_msg, pcb->length, filename, __LINE__);

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

    if (cont) {
      OUTB(pcb->length, (adapter->io_addr)+PORT_COMMAND);
      cont = WAIT_HCRE(2);
    }

    for (i = 0; cont && (i < pcb->length); i++) {
      OUTB(pcb->data.raw[i], (adapter->io_addr)+PORT_COMMAND);
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

      if (jiffies >= timeout)
        TIMEOUT_MSG();

      if (i == ASF_PCB_ACK) {
        SET_HSF(0); 
        return TRUE;
      } else if (i == ASF_PCB_NAK) {
        SET_HSF(0);
        printk("%s: PCB send was NAKed\n", adapter->name);
        {
          int to = jiffies + 5;
          while (jiffies < to)
            ;
        }
      }
    }

    if (elp_debug >= 2)
      printk("%s: NAK/timeout on send PCB\n", adapter->name);
    if ((retry++ & 7) == 0) 
      printk("%s: retry #%i on send PCB\n", adapter->name, retry);
  }

  return FALSE;
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
  int timeout;

  CHECK_NULL(pcb);
  CHECK_NULL(adapter);

  /* get the command code */
  timeout = jiffies + TIMEOUT;
  while (((stat = GET_STATUS())&STATUS_ACRF) == 0 && jiffies < timeout)
    ;
  if (jiffies >= timeout)
    TIMEOUT_MSG();

  SET_HSF(0); 
  pcb->command = INB(adapter->io_addr+PORT_COMMAND);
  if ((stat & ASF_PCB_MASK) != ASF_PCB_END) {

    /* read the data length */
    timeout = jiffies + TIMEOUT;
    while (((stat = GET_STATUS())&STATUS_ACRF) == 0 && jiffies < timeout)
      ;
    if (jiffies >= timeout)
      TIMEOUT_MSG();
    pcb->length = INB(adapter->io_addr+PORT_COMMAND);

    if (pcb->length > MAX_PCB_DATA)
      printk(invalid_pcb_msg, pcb->length, filename,__LINE__);

    if ((stat & ASF_PCB_MASK) != ASF_PCB_END) {

      /* read the data */
      i = 0;
      timeout = jiffies + TIMEOUT;
      do {
        while (((stat = GET_STATUS())&STATUS_ACRF) == 0 && jiffies < timeout)
          ;
        pcb->data.raw[i++] = INB(adapter->io_addr+PORT_COMMAND);
	if (i > MAX_PCB_DATA)
	  printk(invalid_pcb_msg, i, filename, __LINE__);
      } while ((stat & ASF_PCB_MASK) != ASF_PCB_END && jiffies < timeout);

      if (jiffies >= timeout)
        TIMEOUT_MSG();
      
      /* woops, the last "data" byte was really the length! */
      total_length = pcb->data.raw[--i];

      /* safety check total length vs data length */
      if (total_length != (pcb->length + 2)) {
        if (elp_debug >= 2)
          printk("%s: mangled PCB received\n", adapter->name);
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

#if ELP_NEED_HARD_RESET

static void adapter_hard_reset(elp_device * adapter)

{
  int timeout;

  CHECK_NULL(adapter);

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

#endif /* ELP_NEED_HARD_RESET */

static void adapter_reset(elp_device * adapter)
{
  int timeout;

  CHECK_NULL(adapter);

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
  CHECK_NULL(adapter);
  CHECK_NULL(tx_pcb);

  if (elp_debug >= 3)
    printk("%s: restarting receiver\n", adapter->name);
  tx_pcb->command = CMD_RECEIVE_PACKET;
  tx_pcb->length = sizeof(struct Rcv_pkt);
  tx_pcb->data.rcv_pkt.buf_seg = tx_pcb->data.rcv_pkt.buf_ofs = 0; /* Unused */
  tx_pcb->data.rcv_pkt.buf_len = 1600;
  tx_pcb->data.rcv_pkt.timeout = 0;	/* set timeout to zero */
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
  int timeout;
  int rlen;
  struct sk_buff *skb;

  /*
   * allocate a buffer to put the packet into.
   * (for kernels prior to 1.1.4 only)
   */
#if (ELP_KERNEL_TYPE < 2)
  int sksize = sizeof(struct sk_buff) + len + 4;
#endif

  CHECK_NULL(dev);
  CHECK_NULL(adapter);

  if (len <= 0 || ((len & ~1) != len))
    if (elp_debug >= 3)
      printk("*** bad packet len %d at %s(%d)\n",len,filename,__LINE__);

  rlen = (len+1) & ~1;

#if (ELP_KERNEL_TYPE < 2)
  skb = alloc_skb(sksize, GFP_ATOMIC);
#else
  skb = alloc_skb(rlen, GFP_ATOMIC);
#endif

  /*
   * make sure the data register is going the right way
   */
  OUTB(INB(adapter->io_addr+PORT_CONTROL)|CONTROL_DIR, adapter->io_addr+PORT_CONTROL); 

  /*
   * if buffer could not be allocated, swallow it
   */
  if (skb == NULL) {
    for (i = 0; i < (rlen/2); i++) {
      timeout = jiffies + TIMEOUT;
      while ((INB(adapter->io_addr+PORT_STATUS)&STATUS_HRDY) == 0 && 
	     jiffies < timeout)
        ;
      if (jiffies >= timeout)
        TIMEOUT_MSG();

      d = inw(adapter->io_addr+PORT_DATA);
    }
    adapter->stats.rx_dropped++;

  } else {
    skb->lock     = 0;
    skb->len = rlen;
    skb->dev = dev;

/*
 * for kernels before 1.1.4, the driver allocated the buffer
 */
#if (ELP_KERNEL_TYPE < 2)
    skb->mem_len = sksize;
    skb->mem_addr = skb;
#endif

    /*
     * now read the data from the adapter
     */
    ptr = (unsigned short *)SKB_DATA;
    for (i = 0; i < (rlen/2); i++) { 
      timeout = jiffies + TIMEOUT;
      while ((INB(adapter->io_addr+PORT_STATUS)&STATUS_HRDY) == 0 && 
	     jiffies < timeout) 
	;
      if (jiffies >= timeout)
	{
	  printk("*** timeout at %s(%d) reading word %d of %d ***\n",
                        filename,__LINE__, i, rlen/2);	
#if (ELP_KERNEL_TYPE < 2)
	  kfree_s(skb, sksize);
#else
	  kfree_s(skb, rlen);
#endif
	  return;
	}

      *ptr = inw(adapter->io_addr+PORT_DATA); 
      ptr++; 
    }

    /*
     * the magic routine "dev_rint" passes the packet up the
     * protocol chain. If it returns 0, we can assume the packet was
     * swallowed up. If not, then we are responsible for freeing memory
     */

    IS_SKB(skb);

/*
 * for kernels before 1.1.4, the driver allocated the buffer, so it had
 * to free it
 */
#if (ELP_KERNEL_TYPE < 2)
    if (dev_rint((unsigned char *)skb, rlen, IN_SKBUFF, dev) != 0) {
      printk("%s: receive buffers full.\n", dev->name);
      kfree_s(skb, sksize);
    }
#else
    netif_rx(skb);
#endif
  }

  OUTB(INB(adapter->io_addr+PORT_CONTROL)&(~CONTROL_DIR), adapter->io_addr+PORT_CONTROL); 
}


/******************************************************
 *
 * interrupt handler
 *
 ******************************************************/

static void elp_interrupt(int irq, struct pt_regs *regs)

{
  int len;
  int dlen;
  struct device *dev;
  elp_device * adapter;
  int timeout;

  if (irq < 0 || irq > 15) {
    printk ("elp_interrupt(): illegal IRQ number found in interrupt routine (%i)\n", irq);
    return;
  }

/* FIXME: How do I do this kind of check without a fixed IRQ? */
#if 0
  if (irq != ELP_IRQ) {
    printk ("elp_interrupt(): - interrupt routine has incorrect IRQ of %i\n", irq);
    return;
  }
#endif

  dev = irq2dev_map[irq];

  if (dev == NULL) {
    printk ("elp_interrupt(): irq %d for unknown device.\n", irq);
    return;
  }

  adapter = (elp_device *) dev->priv;

  CHECK_NULL(adapter);

  if (dev->interrupt)
    if (elp_debug >= 2)
      printk("%s: Re-entering the interrupt handler.\n", dev->name);
  dev->interrupt = 1;

  /*
   * allow interrupts (we need timers!)
   */
  sti();

  /*
   * receive a PCB from the adapter
   */
  timeout = jiffies + TIMEOUT;
  while ((INB(adapter->io_addr+PORT_STATUS)&STATUS_ACRF) != 0 &&
	 jiffies < timeout) {
    
    if (receive_pcb(adapter, &adapter->irx_pcb)) {

      switch (adapter->irx_pcb.command) {

        /*
         * 82586 configured correctly
         */
        case CMD_CONFIGURE_82586_RESPONSE:
          adapter->got[CMD_CONFIGURE_82586] = 1;
          if (elp_debug >= 3)
            printk("%s: interrupt - configure response received\n", dev->name);
          break;

	/*
	 * Adapter memory configuration
	 */
	case CMD_CONFIGURE_ADAPTER_RESPONSE:
	  adapter->got[CMD_CONFIGURE_ADAPTER_MEMORY] = 1;
	  if (elp_debug >= 3)
	    printk("%s: Adapter memory configuration %s.",dev->name,
	           adapter->irx_pcb.data.failed?"failed":"succeeded");
	  break;
	
	/*
	 * Multicast list loading
	 */
	case CMD_LOAD_MULTICAST_RESPONSE:
	  adapter->got[CMD_LOAD_MULTICAST_LIST] = 1;
          if (elp_debug >= 3)
	    printk("%s: Multicast address list loading %s.",dev->name,
	           adapter->irx_pcb.data.failed?"failed":"succeeded");
	  break;

	/*
	 * Station address setting
	 */
	case CMD_SET_ADDRESS_RESPONSE:
	  adapter->got[CMD_SET_STATION_ADDRESS] = 1;
          if (elp_debug >= 3)
            printk("%s: Ethernet address setting %s.",dev->name,
                   adapter->irx_pcb.data.failed?"failed":"succeeded");
          break;


        /*
         * received board statistics
         */
        case CMD_NETWORK_STATISTICS_RESPONSE:
          adapter->stats.rx_packets += adapter->irx_pcb.data.netstat.tot_recv;
          adapter->stats.tx_packets += adapter->irx_pcb.data.netstat.tot_xmit;
          adapter->stats.rx_crc_errors += adapter->irx_pcb.data.netstat.err_CRC;
          adapter->stats.rx_frame_errors += adapter->irx_pcb.data.netstat.err_align;
          adapter->stats.rx_fifo_errors += adapter->irx_pcb.data.netstat.err_ovrrun;
          adapter->got[CMD_NETWORK_STATISTICS] = 1;
          if (elp_debug >= 3)
            printk("%s: interrupt - statistics response received\n", dev->name);
          break;

        /*
         * received a packet
         */
        case CMD_RECEIVE_PACKET_COMPLETE:
          /* if the device isn't open, don't pass packets up the stack */
          if (dev->start == 0)
            break;
          len = adapter->irx_pcb.data.rcv_resp.pkt_len;
          dlen = adapter->irx_pcb.data.rcv_resp.buf_len;
          if (adapter->irx_pcb.data.rcv_resp.timeout != 0) {
            printk("%s: interrupt - packet not received correctly\n", dev->name);
          } else {
            if (elp_debug >= 3)
              printk("%s: interrupt - packet received of length %i (%i)\n", dev->name, len, dlen);
            receive_packet(dev, adapter, dlen);
            if (elp_debug >= 3)
              printk("%s: packet received\n", dev->name);
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
          if (dev->start == 0)
            break;
          if (adapter->irx_pcb.data.xmit_resp.c_stat != 0)
            if (elp_debug >= 2)
              printk("%s: interrupt - error sending packet %4.4x\n",
                dev->name, adapter->irx_pcb.data.xmit_resp.c_stat);
          dev->tbusy = 0;
#if (ELP_KERNEL_TYPE < 3)
          mark_bh(INET_BH);
#else
          mark_bh(NET_BH);
#endif
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
  if (jiffies >= timeout)
    TIMEOUT_MSG();

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

  CHECK_NULL(dev);

  if (elp_debug >= 3)  
    printk("%s: request to open device\n", dev->name);

  /*
   * make sure we actually found the device
   */
  if (adapter == NULL) {
    printk("%s: Opening a non-existent physical device\n", dev->name);
    return -EAGAIN;
  }

  /*
   * disable interrupts on the board
   */
  OUTB(0x00, adapter->io_addr+PORT_CONTROL);

  /*
   * clear any pending interrupts
   */
  INB(adapter->io_addr+PORT_COMMAND);

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
  if (request_irq(dev->irq, &elp_interrupt, 0, "3c505"))
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
   * device is now officially open!
   */
  dev->start = 1;

  /*
   * configure adapter memory: we need 10 multicast addresses, default==0
   */
  if (elp_debug >= 3)
    printk("%s: sending 3c505 memory configuration command\n", dev->name);
  adapter->tx_pcb.command = CMD_CONFIGURE_ADAPTER_MEMORY;
  adapter->tx_pcb.data.memconf.cmd_q = 10;
  adapter->tx_pcb.data.memconf.rcv_q = 20;
  adapter->tx_pcb.data.memconf.mcast = 10;
  adapter->tx_pcb.data.memconf.frame = 20;
  adapter->tx_pcb.data.memconf.rcv_b = 20;
  adapter->tx_pcb.data.memconf.progs = 0;
  adapter->tx_pcb.length = sizeof(struct Memconf);
  adapter->got[CMD_CONFIGURE_ADAPTER_MEMORY] = 0;
  if (!send_pcb(adapter, &adapter->tx_pcb))
    printk("%s: couldn't send memory configuration command\n", dev->name);
  else {
    int timeout = jiffies + TIMEOUT;
    while (adapter->got[CMD_CONFIGURE_ADAPTER_MEMORY] == 0 && jiffies < timeout)
      ;
    if (jiffies >= timeout)
      TIMEOUT_MSG();
  }


  /*
   * configure adapter to receive broadcast messages and wait for response
   */
  if (elp_debug >= 3)
    printk("%s: sending 82586 configure command\n", dev->name);
  adapter->tx_pcb.command = CMD_CONFIGURE_82586;
  adapter->tx_pcb.data.configure = NO_LOOPBACK | RECV_BROAD;
  adapter->tx_pcb.length  = 2;
  adapter->got[CMD_CONFIGURE_82586] = 0;
  if (!send_pcb(adapter, &adapter->tx_pcb))
    printk("%s: couldn't send 82586 configure command\n", dev->name);
  else {
    int timeout = jiffies + TIMEOUT;
    while (adapter->got[CMD_CONFIGURE_82586] == 0 && jiffies < timeout)
      ;
    if (jiffies >= timeout)
      TIMEOUT_MSG();
  }

  /*
   * queue receive commands to provide buffering
   */
  if (!start_receive(adapter, &adapter->tx_pcb))
    printk("%s: start receive command failed \n", dev->name);
  if (elp_debug >= 3)
    printk("%s: start receive command sent\n", dev->name);

  return 0;			/* Always succeed */
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

  CHECK_NULL(adapter);
  CHECK_NULL(ptr);

  if (nlen < len)
    printk("Warning, bad length nlen=%d len=%d %s(%d)\n",nlen,len,filename,__LINE__);

  /*
   * send the adapter a transmit packet command. Ignore segment and offset
   * and make sure the length is even
   */
  adapter->tx_pcb.command = CMD_TRANSMIT_PACKET;
  adapter->tx_pcb.length = sizeof(struct Xmit_pkt);
  adapter->tx_pcb.data.xmit_pkt.buf_ofs = adapter->tx_pcb.data.xmit_pkt.buf_seg = 0; /* Unused */
  adapter->tx_pcb.data.xmit_pkt.pkt_len = nlen;
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
    int timeout = jiffies + TIMEOUT;
    while ((INB(adapter->io_addr+PORT_STATUS)&STATUS_HRDY) == 0 && jiffies < timeout)
      ;
    if (jiffies >= timeout) {
      printk("*** timeout at %s(%d) writing word %d of %d ***\n",
            filename,__LINE__, i, nlen/2);
      return FALSE;
    }

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

  CHECK_NULL(dev);

  /*
   * not sure what this does, but the 3c509 driver does it, so...
   */
  if (skb == NULL) {
    dev_tint(dev);
    return 0;
  }

  /*
   * Fill in the ethernet header 
   * (for kernels prior to 1.1.4 only)
   */
#if (ELP_KERNEL_TYPE < 2)
  IS_SKB(skb);
  if (!skb->arp && dev->rebuild_header(SKB_DATA, dev)) {
    skb->dev = dev;
    IS_SKB(skb);
    arp_queue (skb);
    return 0;
  }
#endif

  /*
   * if we ended up with a munged length, don't send it
   */
  if (skb->len <= 0)
    return 0;

  if (elp_debug >= 3)
    printk("%s: request to send packet of length %d\n", dev->name, (int)skb->len);

  /*
   * if the transmitter is still busy, we have a transmit timeout...
   */
  if (dev->tbusy) {
    int tickssofar = jiffies - dev->trans_start;
    if (tickssofar < 200) /* was 500, AJT */
      return 1;
    printk("%s: transmit timed out, resetting adapter\n", dev->name);
    if ((INB(adapter->io_addr+PORT_STATUS)&STATUS_ACRF) != 0) 
      printk("%s: hmmm...seemed to have missed an interrupt!\n", dev->name);
    adapter_reset(adapter);
    dev->trans_start = jiffies;
    dev->tbusy = 0;
  }

  /*
   * send the packet at skb->data for skb->len
   */
  if (!send_packet(adapter, (unsigned char *)SKB_DATA, skb->len)) {
    printk("%s: send packet PCB failed\n", dev->name);
    return 1;
  }

  if (elp_debug >= 3)
    printk("%s: packet of length %d sent\n", dev->name, (int)skb->len);


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
#if (ELP_KERNEL_TYPE < 4)
  if (skb->free)
    {
      IS_SKB(skb);
      kfree_skb(skb, FREE_WRITE);
    }
#else
  dev_kfree_skb(skb, FREE_WRITE);
#endif

  return 0;
}

/******************************************************
 *
 * return statistics on the board
 *
 ******************************************************/

static struct enet_statistics * elp_get_stats(struct device *dev)

{
  elp_device *adapter = (elp_device *) dev->priv;

  if (elp_debug >= 3)
    printk("%s: request for stats\n", dev->name);

  /* If the device is closed, just return the latest stats we have,
     - we cannot ask from the adapter without interrupts */
  if (!dev->start)
    return &adapter->stats;

  /* send a get statistics command to the board */
  adapter->tx_pcb.command = CMD_NETWORK_STATISTICS;
  adapter->tx_pcb.length  = 0;
  adapter->got[CMD_NETWORK_STATISTICS] = 0;
  if (!send_pcb(adapter, &adapter->tx_pcb))
    printk("%s: couldn't send get statistics command\n", dev->name);
  else
    {
      int timeout = jiffies + TIMEOUT;
      while (adapter->got[CMD_NETWORK_STATISTICS] == 0 && jiffies < timeout)
	;
      if (jiffies >= timeout) {
        TIMEOUT_MSG();
        return &adapter->stats;
      }
    }

  /* statistics are now up to date */
  return &adapter->stats;
}

/******************************************************
 *
 * close the board
 *
 ******************************************************/

static int elp_close (struct device *dev)

{
  elp_device * adapter = (elp_device *) dev->priv;

  CHECK_NULL(dev);
  CHECK_NULL(adapter);

  if (elp_debug >= 3)
    printk("%s: request to close device\n", dev->name);

  /* Someone may request the device statistic information even when
   * the interface is closed. The following will update the statistics
   * structure in the driver, so we'll be able to give current statistics.
   */
  (void) elp_get_stats(dev);

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


/************************************************************
 *
 * Set multicast list
 * num_addrs==0: clear mc_list
 * num_addrs==-1: set promiscuous mode
 * num_addrs>0: set mc_list
 *
 ************************************************************/

static void elp_set_mc_list(struct device *dev, int num_addrs, void *addrs)
{
  elp_device *adapter = (elp_device *) dev->priv;
  int i;

  if (elp_debug >= 3)
    printk("%s: request to set multicast list\n", dev->name);

  if (num_addrs != -1) {
    /* send a "load multicast list" command to the board, max 10 addrs/cmd */
    /* if num_addrs==0 the list will be cleared */
    adapter->tx_pcb.command = CMD_LOAD_MULTICAST_LIST;
    adapter->tx_pcb.length  = 6*num_addrs;
    for (i=0;i<num_addrs;i++)
      memcpy(adapter->tx_pcb.data.multicast[i], addrs+6*i,6);
    adapter->got[CMD_LOAD_MULTICAST_LIST] = 0;
    if (!send_pcb(adapter, &adapter->tx_pcb))
      printk("%s: couldn't send set_multicast command\n", dev->name);
    else {
      int timeout = jiffies + TIMEOUT;
      while (adapter->got[CMD_LOAD_MULTICAST_LIST] == 0 && jiffies < timeout)
        ;
      if (jiffies >= timeout) {
        TIMEOUT_MSG();
      }
    }
    if (num_addrs)
      adapter->tx_pcb.data.configure = NO_LOOPBACK | RECV_BROAD | RECV_MULTI;
    else /* num_addrs == 0 */
      adapter->tx_pcb.data.configure = NO_LOOPBACK | RECV_BROAD;
  } else /* num_addrs == -1 */
    adapter->tx_pcb.data.configure = NO_LOOPBACK | RECV_ALL;
  /*
   * configure adapter to receive messages (as specified above)
   * and wait for response
   */
  if (elp_debug >= 3)
    printk("%s: sending 82586 configure command\n", dev->name);
  adapter->tx_pcb.command = CMD_CONFIGURE_82586;
  adapter->tx_pcb.length  = 2;
  adapter->got[CMD_CONFIGURE_82586]  = 0;
  if (!send_pcb(adapter, &adapter->tx_pcb))
    printk("%s: couldn't send 82586 configure command\n", dev->name);
  else {
    int timeout = jiffies + TIMEOUT;
    while (adapter->got[CMD_CONFIGURE_82586] == 0 && jiffies < timeout)
      ;
    if (jiffies >= timeout)
      TIMEOUT_MSG();
  }
}

/******************************************************
 *
 * initialise Etherlink Plus board
 *
 ******************************************************/

static void elp_init(struct device *dev)

{
  elp_device * adapter;

  CHECK_NULL(dev);

  /*
   * NULL out buffer pointers
   * (kernels prior to 1.1.4 only)
   */
#if (ELP_KERNEL_TYPE < 2)
  {
    int i;
    for (i = 0; i < DEV_NUMBUFFS; i++)
      dev->buffs[i] = NULL;
  }
#endif

  /*
   * set ptrs to various functions
   */
  dev->open             = elp_open;		/* local */
  dev->stop             = elp_close;		/* local */
  dev->get_stats	= elp_get_stats;	/* local */
  dev->hard_start_xmit  = elp_start_xmit;       /* local */
  dev->set_multicast_list = elp_set_mc_list;	/* local */

#if (ELP_KERNEL_TYPE < 2)
  dev->hard_header	= eth_header;		/* eth.c */
  dev->add_arp	        = eth_add_arp;		/* eth.c */
  dev->rebuild_header	= eth_rebuild_header;	/* eth.c */
  dev->type_trans	= eth_type_trans;	/* eth.c */
  dev->queue_xmit	= dev_queue_xmit;	/* dev.c */
#else
  /* Setup the generic properties */
  ether_setup(dev);
#endif

  /*
   * setup ptr to adapter specific information
   */
  adapter = (elp_device *)(dev->priv = kmalloc(sizeof(elp_device), GFP_KERNEL));
  CHECK_NULL(adapter);
  adapter->io_addr = dev->base_addr;
  adapter->name    = dev->name;
  memset(&(adapter->stats), 0, sizeof(struct enet_statistics));


  /*
   * Ethernet information
   * (for kernels prior to 1.1.4 only)
   */
#if (ELP_KERNEL_TYPE < 2)
  dev->type		= ARPHRD_ETHER;
  dev->hard_header_len  = ETH_HLEN;
  dev->mtu		= 1500;         /* eth_mtu */
  dev->addr_len	        = ETH_ALEN;
  {
    int i;
    for (i = 0; i < dev->addr_len; i++) 
      dev->broadcast[i] = 0xff;
  }

  /*
   * New-style flags. 
   */
  dev->flags		= IFF_BROADCAST;
  dev->family		= AF_INET;
  dev->pa_addr	        = 0;
  dev->pa_brdaddr	= 0;
  dev->pa_mask	        = 0;
  dev->pa_alen        	= sizeof(unsigned long);
#endif

  /*
   * memory information
   */
  dev->mem_start = dev->mem_end = dev->rmem_end = dev->mem_start = 0;

#if ELP_NEED_HARD_RESET
  adapter_hard_reset(adapter);
#else
  adapter_reset(adapter);
#endif
}

/************************************************************
 *
 * A couple of tests to see if there's 3C505 or not
 * Called only by elp_autodetect
 ************************************************************/

static int elp_sense(int addr)
{
  int timeout;
  byte orig_HCR=INB(addr+PORT_CONTROL),
       orig_HSR=INB(addr+PORT_STATUS);
  
  if (((orig_HCR==0xff) && (orig_HSR==0xff)) ||
       ( (orig_HCR & CONTROL_DIR) != (orig_HSR & STATUS_DIR) ) )
    return 1;                /* It can't be 3c505 if HCR.DIR != HSR.DIR */

  /* Wait for a while; the adapter may still be booting up */
  if (elp_debug > 0)
    printk(stilllooking_msg);
  for (timeout = jiffies + (100 * 15); jiffies <= timeout; ) 
    if ((INB(addr+PORT_STATUS) & ASF_PCB_MASK) != ASF_PCB_END)
      break;

  if (orig_HCR & CONTROL_DIR) {
    /* If HCR.DIR is up, we pull it down. HSR.DIR should follow. */
    OUTB(orig_HCR & ~CONTROL_DIR,addr+PORT_CONTROL);
    timeout = jiffies+30;
    while (jiffies < timeout)
      ;
    if (INB(addr+PORT_STATUS) & STATUS_DIR) {
      OUTB(orig_HCR,addr+PORT_CONTROL);
      return 2;
    }
  } else {
    /* If HCR.DIR is down, we pull it up. HSR.DIR should follow. */
    OUTB(orig_HCR | CONTROL_DIR,addr+PORT_CONTROL);
    timeout = jiffies+300;
    while (jiffies < timeout)
      ;
    if (!(INB(addr+PORT_STATUS) & STATUS_DIR)) {
      OUTB(orig_HCR,addr+PORT_CONTROL);
      return 3;
    }
  }
  return 0;	/* It certainly looks like a 3c505. */
}

/*************************************************************
 *
 * Search through addr_list[] and try to find a 3C505
 * Called only by eplus_probe
 *************************************************************/

static int elp_autodetect(struct device * dev)
{
  int idx=0, addr;

  /* if base address set, then only check that address
     otherwise, run through the table */
  if ( (addr=dev->base_addr) ) { /* dev->base_addr == 0 ==> plain autodetect */
    if (elp_debug > 0)
      printk(search_msg, dev->name, addr);
    if (elp_sense(addr) == 0)
    {
      if (elp_debug > 0)
        printk(found_msg);
      return addr;
    } else if (elp_debug > 0)
      printk(notfound_msg);
  } else while ( (addr=addr_list[idx++]) ) {
    if (elp_debug > 0)
      printk(search_msg, dev->name, addr);
    if (elp_sense(addr) == 0) {
      if (elp_debug > 0)
        printk(found_msg);
      return addr;
    } else if (elp_debug > 0)
      printk(notfound_msg);
  }

  /* could not find an adapter */
  if (elp_debug == 0)
    printk(couldnot_msg, dev->name);
  return 0; /* Because of this, the layer above will return -ENODEV */
}

/******************************************************
 *
 * probe for an Etherlink Plus board at the specified address
 *
 ******************************************************/

int elplus_probe(struct device *dev)

{
  elp_device adapter;
  int            i;

  CHECK_NULL(dev);

  /*
   *  setup adapter structure
   */

  adapter.io_addr = dev->base_addr = elp_autodetect(dev);
  if ( !adapter.io_addr )
    return -ENODEV;

  /*
   *  As we enter here from bootup, the adapter should have IRQs enabled,
   *  but we can as well enable them anyway.
   */
  OUTB(INB(dev->base_addr+PORT_CONTROL) | CONTROL_CMDE,
       dev->base_addr+PORT_CONTROL);
  autoirq_setup(0);

  /*
   * use ethernet address command to probe for board in polled mode
   * (this also makes us the IRQ that we need for automatic detection)
   */
  adapter.tx_pcb.command = CMD_STATION_ADDRESS;
  adapter.tx_pcb.length  = 0;
  if (!send_pcb   (&adapter, &adapter.tx_pcb) ||
      !receive_pcb(&adapter, &adapter.rx_pcb) ||
      (adapter.rx_pcb.command != CMD_ADDRESS_RESPONSE) ||
      (adapter.rx_pcb.length != 6)) {
    printk("%s: not responding to first PCB\n", dev->name);
    return -ENODEV;
  }
  if (dev->irq) { /* Is there a preset IRQ? */
     if (dev->irq != autoirq_report(0)) {
        printk("%s: Detected IRQ doesn't match user-defined one.\n",dev->name);
	return -ENODEV;
     }
     /* if dev->irq == autoirq_report(0), all is well */
  } else /* No preset IRQ; just use what we can detect */
     dev->irq=autoirq_report(0);
  switch (dev->irq) { /* Legal, sane? */
    case 0: printk("%s: No IRQ reported by autoirq_report().\n",dev->name);
            printk("%s: Check the jumpers of your 3c505 board.\n",dev->name);
	    return -ENODEV;
    case 1:
    case 6:
    case 8:
    case 13: 
            printk("%s: Impossible IRQ %d reported by autoirq_report().\n",
	           dev->name,
		   dev->irq);
	    return -ENODEV;
  }
  /*
   *  Now we have the IRQ number so we can disable the interrupts from
   *  the board until the board is opened.
   */
  OUTB(INB(dev->base_addr+PORT_CONTROL) & ~CONTROL_CMDE,
       dev->base_addr+PORT_CONTROL);
  
  /*
   * copy ethernet address into structure
   */
  for (i = 0; i < 6; i++) 
    dev->dev_addr[i] = adapter.rx_pcb.data.eth_addr[i];

  /*
   * print remainder of startup message
   */
#if (ELP_KERNEL_TYPE < 2)
  printk("%s: 3c505 card found at I/O 0x%x using IRQ%d has address %s\n",
         dev->name, dev->base_addr, dev->irq, eth_print(dev->dev_addr));
#else
  printk("%s: 3c505 card found at I/O 0x%x using IRQ%d has address %02x:%02x:%02x:%02x:%02x:%02x\n",
         dev->name, dev->base_addr, dev->irq,
         dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
         dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);
#endif
  
  /*
   * initialise the device
   */
  elp_init(dev);
  return 0;
}
