  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 * $Id: apfddi.c,v 1.6 1996/12/18 01:45:51 tridge Exp $
 *
 * Network interface definitions for AP1000 fddi device.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/if_ether.h>	/* For the statistics structure. */
#include <linux/netdevice.h>
#include <linux/if_arp.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/io.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>

#include <asm/ap1000/apservice.h>
#include <asm/ap1000/apreg.h>
#include <asm/irq.h>

#include <net/arp.h>

#include "apfddi.h"
#include "smt-types.h"
#include "mac.h"
#include "plc.h"
#include "am79c830.h"
#include "apfddi-reg.h"

volatile struct formac *mac;
volatile struct plc *plc;
volatile int *csr0;
volatile int *csr1;
volatile int *buffer_mem;
volatile int *fifo;

#define APFDDI_DEBUG 0

#define APFDDI_IRQ    7

#define T(x)	(-SECS_TO_FDDI_TIME(x))

struct plc_info plc_info = {
    pt_s,			/* port_type */
    T(1.6e-3),			/* c_min */
    T(50e-6),			/* tl_min */
    T(5e-3),			/* tb_min */
    T(100e-3),			/* t_out */
    T(50e-3),			/* lc_short */
    T(500e-3),			/* lc_medium */
    T(5.0),			/* lc_long */
    T(50.0),			/* lc_extended */
    T(3.5e-3),			/* t_scrub */
    T(1.3e-3),			/* ns_max */
};

struct mac_info mac_info = {
    T(165e-3),			/* tmax */
    T(3.5e-3),			/* tvx */
    T(20e-3),			/* treq */
    { 0x42, 0x59 },		/* s_address */
    { 0x42, 0x59, 0x10, 0x76, 0x88, 0x82 },	/* l_address */
    { 0 },			/* s_group_adrs */
    { 0 },			/* l_group_adrs */
    0,				/* rcv_own_frames */
    1,				/* only_good_frames */
};

u_char fddi_bitrev[256] = {
    0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
    0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
    0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
    0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
    0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
    0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
    0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
    0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
    0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
    0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
    0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
    0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
    0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
    0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
    0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
    0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
    0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
    0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
    0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
    0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
    0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
    0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
    0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
    0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
    0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
    0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
    0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
    0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
    0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
    0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
    0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
    0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff,
};

/* XXX our hardware address, canonical bit order */
static u_char apfddi_saddr[6] = { 0x42, 0x9a, 0x08, 0x6e, 0x11, 0x41 };

struct device *apfddi_device = NULL;
struct net_device_stats *apfddi_stats = NULL;

volatile struct apfddi_queue *apfddi_queue_top = NULL;

void map_regs(void)
{
    unsigned long reg_base_addr = 0xfbf00000;

    mac = (volatile struct formac *) (reg_base_addr + FORMAC);
    plc = (volatile struct plc *) (reg_base_addr + PLC);
    csr0 = (volatile int *) (reg_base_addr + CSR0);
    csr1 = (volatile int *) (reg_base_addr + CSR1);
    buffer_mem = (volatile int *) (reg_base_addr + BUFFER_MEM);
    fifo = (volatile int *) (reg_base_addr + FIFO);
}

int ring_op;

void apfddi_startup(void)
{
    int reason;

#if APFDDI_DEBUG
    printk("In apfddi_startup\n");
#endif

    *csr0 = CS0_LED0;
    ring_op = 0;
    if (*csr1 & 0xf078) {
	*csr1 = CS1_RESET_MAC | CS1_RESET_FIFO;
	*csr1 = 0;
	reason = 1;
	printk("resetting after power-on\n");
    } else {
	*csr1 = CS1_RESET_FIFO;
	*csr1 = 0;
	reason = plc_inited(&plc_info);
	if (reason)
	    printk("resetting: plc reason %d\n", reason);
    }
    if (reason) {
#if APFDDI_DEBUG
	printk("Calling plc_init\n");
#endif
	plc_init(&plc_info);
#if APFDDI_DEBUG
	printk("Calling mac_init\n");
#endif
	mac_init(&mac_info);
	*csr0 |= CS0_LED1;
	pc_start(loop_none);

    } else {
	*csr0 |= CS0_LED2 | CS0_LED1;
	reason = mac_inited(&mac_info);
	if (reason) {
	    printk("resetting mac: reason %d\n", reason);
	    mac_init(&mac_info);
	    mac_reset(loop_none);
	    mac_claim();
	} else {
	    ring_op = 1;
	    *csr0 &= ~(CS0_LED0 | CS0_LED1 | CS0_LED2);
	}
    }
}

void apfddi_off(void)
{
    *csr0 &= ~CS0_LED1;
    pc_stop();
}

void apfddi_sleep(void)
{
    mac_sleep();
    plc_sleep();
}

void apfddi_poll(void)
{
    if (*csr0 & CS0_PHY_IRQ)
	plc_poll();
    if (*csr0 & CS0_MAC_IRQ)
	mac_poll();
}

void set_cf_join(int on)
{
    if (on) {
#if APFDDI_DEBUG
	printk("apfddi: joined the ring!\n");
#endif
	mac_reset(loop_none);
	*csr0 |= CS0_LED2;
	mac_claim();
    } else {
	mac_disable();
	ring_op = 0;
	*csr0 = (*csr0 & ~CS0_LED2) | CS0_LED1 | CS0_LED0;
    }
}

void set_ring_op(int up)
{
    ring_op = up;
    if (up) {
#if APFDDI_DEBUG
	printk("apfddi: ring operational!\n");
#endif
	*csr0 &= ~(CS0_LED2 | CS0_LED1 | CS0_LED0);
    } else
	*csr0 |= CS0_LED2 | CS0_LED1 | CS0_LED0;
}

void rmt_event(int st)
{
    if (st & (S2_BEACON_STATE|S2_MULTIPLE_DA|S2_TOKEN_ERR
	      |S2_DUPL_CLAIM|S2_TRT_EXP_RECOV)) {
	printk("st2 = %x\n", st);
    }
}


int apfddi_init(struct device *dev);
static void apfddi_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int apfddi_xmit(struct sk_buff *skb, struct device *dev);
int apfddi_rx(struct mac_buf *mbuf);
static struct net_device_stats *apfddi_get_stats(struct device *dev);
#if APFDDI_DEBUG
void dump_packet(char *action, char *buf, int len, int seq);
#endif

/*
 * Create FDDI header for an arbitrary protocol layer
 *
 * saddr=NULL means use device source address (always will anyway)
 * daddr=NULL means leave destination address (eg unresolved arp)
 */
static int apfddi_hard_header(struct sk_buff *skb, struct device *dev,
			      unsigned short type, void *daddr,
			      void *saddr, unsigned len)
{
    struct fddi_header *fh;
    struct llc_header *lh;
    u_char *base_header;
    u_char *fd_daddr = (u_char *)daddr;
    int i;

#if APFDDI_DEBUG
    printk("In apfddi_hard_header\n");
#endif

    if (skb == NULL) {
	printk("Null skb in apfddi_hard_header... returning...\n");
	return 0;
    }

    switch(type) {
    case ETH_P_IP:
#if APFDDI_DEBUG
	printk("apfddi_hard_header: Processing IP packet\n");
#endif
	break;
    case ETH_P_ARP:
#if APFDDI_DEBUG
	printk("apfddi_hard_header: Processing ARP packet\n");
#endif
	break;
    case ETH_P_RARP:
#if APFDDI_DEBUG
	printk("apfddi_hard_header: Processing RARP packet\n");
#endif
	break;
    default:
	printk("apfddi_hard_header: I don't understand protocol %d (0x%x)\n",
	       type, type);
	apfddi_stats->tx_errors++;
	return 0;
    }

    base_header = (u_char *)skb_push(skb, FDDI_HARDHDR_LEN-4);
    if (base_header == NULL) {
	printk("apfddi_hard_header: Memory squeeze, dropping packet.\n");
	apfddi_stats->tx_dropped++;
	return 0;
    }
    fh = (struct fddi_header *)(base_header + 3);
    lh = (struct llc_header *)((char *)fh + FDDI_HDRLEN);

    lh->llc_dsap = lh->llc_ssap = LLC_SNAP_LSAP;
    lh->snap_control = LLC_UI;
    lh->snap_org_code[0] = 0;
    lh->snap_org_code[1] = 0;
    lh->snap_org_code[2] = 0;
    lh->snap_ether_type = htons(type);

#if APFDDI_DEBUG
    printk("snap_ether_type is %d (0x%x)\n", lh->snap_ether_type,
	   lh->snap_ether_type);
#endif
    
    fh->fddi_fc = FDDI_FC_LLC;
    
    /*
     * Fill in the source address.
     */
    for (i = 0; i < 6; i++)
	fh->fddi_shost[i] = fddi_bitrev[apfddi_saddr[i]];
    
    /*
     * Fill in the destination address.
     */
    if (daddr) {
#if APFDDI_DEBUG
	printk("daddr is: ");
#endif
	for (i = 0; i < 6; i++) {
	    fh->fddi_dhost[i] = fddi_bitrev[fd_daddr[i]];
#if APFDDI_DEBUG
	    printk("%x(%x):",fh->fddi_dhost[i], fd_daddr[i]);
#endif
	}
#if APFDDI_DEBUG
	printk("\n");
#endif
	return(FDDI_HARDHDR_LEN-4);
    }
    else {
#if APFDDI_DEBUG
	printk("apfddi_hard_header, daddr was NULL\n");
#endif
	return -(FDDI_HARDHDR_LEN-4);
    }
}

/*
 * Rebuild the FDDI header. This is called after an ARP (or in future
 * other address resolution) has completed on this sk_buff. We now let
 * ARP fill in the other fields.  
 */
static int apfddi_rebuild_header(void *buff, struct device *dev,
				 unsigned long raddr, struct sk_buff *skb)
{
    int i, status;
    struct fddi_header *fh = (struct fddi_header *)(buff+3);

#if APFDDI_DEBUG
    printk("In apfddi_rebuild_header, dev is %x apfddi_device is %x\n", dev,
	   apfddi_device);
    printk("rebuild header for fc 0x%x\n", fh->fddi_fc);
    printk("dest address is:\n");
    for (i = 0; i < 6; i++) printk("%x:", fh->fddi_dhost[i]);
#endif
    status = arp_find(raddr, skb) ? 1 : 0;
    
    if (!status) {
#if APFDDI_DEBUG
	printk("dest address is now:\n");
	for (i = 0; i < 6; i++) printk("%x:", fh->fddi_dhost[i]);
	printk("status is %d\n", status);
#endif
	/*
	 * Bit reverse the dest_address.
	 */
	for (i = 0; i < 6; i++)
	    fh->fddi_dhost[i] = fddi_bitrev[fh->fddi_dhost[i]];
    }
#if APFDDI_DEBUG    
    printk("\n");
#endif
    return(status);
}

static int apfddi_set_mac_address(struct device *dev, void *addr)
{
#if APFDDI_DEBUG
    printk("In apfddi_set_mac_address\n");
#endif
    return (0);
}

static void apfddi_set_multicast_list(struct device *dev)
{
#if APFDDI_DEBUG
    printk("In apfddi_set_multicast_list\n");
#endif
}

static int apfddi_do_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
#if APFDDI_DEBUG
    printk("In apfddi_do_ioctl\n");
#endif
    return (0);
}

static int apfddi_set_config(struct device *dev, struct ifmap *map)
{
#if APFDDI_DEBUG
    printk("In apfddi_set_config\n");
#endif
    return (0);
}

/*
 * Opening the fddi device through ifconfig.
 */
int apfddi_open(struct device *dev)
{
    static int already_run = 0;
    unsigned flags;
    int res;

    if (already_run) {
	apfddi_startup();
	*csr0 |= CS0_INT_ENABLE;
	return 0;
    }
    already_run = 1;

    map_regs();
    apfddi_startup();

    save_flags(flags); cli();
    if ((res = request_irq(APFDDI_IRQ, apfddi_interrupt, SA_INTERRUPT, 
			   "apfddi", dev))) {
	printk("Failed to install apfddi handler error=%d\n", res);
	restore_flags(flags);
	return(0);
    }
    enable_irq(APFDDI_IRQ);
    restore_flags(flags);

#if APFDDI_DEBUG
    printk("Installed apfddi interrupt handler\n");
#endif
    *csr0 |= CS0_INT_ENABLE;
#if APFDDI_DEBUG
    printk("Enabled fddi interrupts\n");
#endif
    
    return 0;
}

/*
 * Stop the fddi device through ifconfig.
 */
int apfddi_stop(struct device *dev)
{
    *csr0 &= ~CS0_INT_ENABLE;
    apfddi_sleep();
    return 0;
}


/*
 * Initialise fddi network interface.
 */
int apfddi_init(struct device *dev)
{
    int i;

    /*
     * Check if this thing has already been initialised.
     */
    if (apfddi_device != NULL)
	return -ENODEV;

    printk("apfddi_init(): Initialising fddi interface\n");

    apfddi_device = dev;

    dev->open = apfddi_open;
    dev->stop = apfddi_stop;
    dev->hard_start_xmit = apfddi_xmit;
    dev->get_stats = apfddi_get_stats;
    dev->priv = kmalloc(sizeof(struct net_device_stats), GFP_ATOMIC); 
    if (dev->priv == NULL)
	return -ENOMEM;
    memset(dev->priv, 0, sizeof(struct net_device_stats)); 
    apfddi_stats = (struct net_device_stats *)apfddi_device->priv;

    /* Initialise the fddi device structure */
    for (i = 0; i < DEV_NUMBUFFS; i++)
	skb_queue_head_init(&dev->buffs[i]);
    
    dev->hard_header = apfddi_hard_header;
    dev->rebuild_header	= apfddi_rebuild_header;
    dev->set_mac_address = apfddi_set_mac_address;
    dev->header_cache_update = NULL;
    dev->do_ioctl = apfddi_do_ioctl;    
    dev->set_config = apfddi_set_config;
    dev->set_multicast_list = apfddi_set_multicast_list;
    dev->type = ARPHRD_ETHER;
    dev->hard_header_len = FDDI_HARDHDR_LEN;
    dev->mtu = FDDIMTU;
    dev->addr_len = 6;
    memcpy(dev->dev_addr, apfddi_saddr, sizeof(apfddi_saddr));
    dev->tx_queue_len = 100;    /* XXX What should this be? */
    dev->irq = APFDDI_IRQ;

    memset(dev->broadcast, 0xFF, ETH_ALEN);
    
    return(0);
}

static void apfddi_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
#if APFDDI_DEBUG
    static int times = 0;
#endif
    unsigned flags;
    save_flags(flags); cli();
    
#if APFDDI_DEBUG
    printk("In apfddi_interrupt irq %d dev_id %p times %d\n",
	   irq, dev_id, ++times);
#endif
    
    apfddi_poll();
    restore_flags(flags);
}

#if APFDDI_DEBUG
static char *flagbits[8] = {
    "fin", "syn", "rst", "push", "ack", "urg", "6", "7"
};

void dump_packet(action, buf, len, seq)
    char *action, *buf;
    int len, seq;
{
    int i, flags;
    char *sep;

    printk("%s packet %d of %d bytes at %d:\n", action, seq,
	   len, jiffies);
    printk("  from %x to %x pktid=%d ttl=%d pcol=%d len=%d\n",
	   *(long *)(buf+12), *(long *)(buf+16), *(u_short *)(buf+4),
	   *(unsigned char *)(buf+8), buf[9], *(u_short *)(buf+2));
    if( buf[9] == 6 || buf[9] == 17 ){
	/* TCP or UDP */
	printk("  sport=%d dport=%d",
	       *(u_short *)(buf+20), *(u_short *)(buf+22));
	if( buf[9] == 6 ){
	    printk(" seq=%d ack=%d win=%d flags=<",
		   *(long *)(buf+24), *(long *)(buf+28),
		   *(unsigned short *)(buf+34));
	    flags = buf[33];
	    sep = "";
	    for (i = 7; i >= 0; --i) {
		if (flags & (1 << i)) {
		    printk("%s%s", sep, flagbits[i]);
		    sep = "+";
		}
	    }
	    printk(">");
	}
	printk("\n");
    }
}
#endif

#if APFDDI_DEBUG
static void apfddi_print_frame(struct sk_buff *skb)
{
    int i;
    struct llc_header *lh;
    static int seq = 0;

#if 0
    printk("skb->len is %d\n", skb->len);
    printk("fc is 0x%x\n", *(u_char *)(skb->data+3));
    printk("dest address is:\n");
    for (i = 0; i < 6; i++) {
	printk("%x:", fddi_bitrev[*(u_char *)(skb->data+4+i)]);
    }
    printk("\n");
    printk("source address is:\n");
    for (i = 0; i < 6; i++) {
	printk("%x:", fddi_bitrev[*(u_char *)(skb->data+10+i)]);
    }
    printk("\n");
#endif
    lh = (struct llc_header *)(skb->data+16);
#if 0
    printk("llc_dsp %d llc_ssap %d snap_control %d org_code [0]=%d [1]=%d [2]=%d ether_type=%d\n",
	   lh->llc_dsap, lh->llc_ssap, lh->snap_control,
	   lh->snap_org_code[0], lh->snap_org_code[1], lh->snap_org_code[2],
	   lh->snap_ether_type);
#endif
    if (lh->snap_ether_type == ETH_P_IP)
	dump_packet("apfddi_xmit:", skb->data+24, skb->len-24, seq++);
}
#endif

/*
 * Transmitting packet over FDDI.
 */
static int apfddi_xmit(struct sk_buff *skb, struct device *dev)
{
    unsigned long flags;
    
#if APFDDI_DEBUG
    printk("In apfddi_xmit\n");
#endif

    /*
     * Check there is some work to do.
     */
    if (skb == NULL || dev == NULL) 
	return(0);

#if APFDDI_DEBUG
    printk("skb address is for apfddi 0x%x\n", skb);
#endif

    /*
     * Check lock variable.
     */
    save_flags(flags); cli();
    if (dev->tbusy != 0) {
	restore_flags(flags);
	printk("apfddi_xmit: device busy\n");
	apfddi_stats->tx_errors++;
	return 1;
    }
    restore_flags(flags);
    dev->tbusy = 1;

    dev->trans_start = jiffies;

    skb->mac.raw = skb->data;

    /*
     * Append packet onto send queue.
     */
    if (mac_queue_append(skb)) {
	/*
	 * No memory.
	 */
	return 1;
    }

    /*
     * Process packet queue.
     */
    mac_process();

    apfddi_stats->tx_packets++;
    dev->tbusy = 0;
    return 0;
}

#if APFDDI_DEBUG
void print_mbuf(struct mac_buf *mbuf)
{
    printk("mac %p length=%d ptr=%p wraplen=%d wrapptr=%x fr_start=%d fr_end=%d\n",
	   mbuf, mbuf->length, mbuf->ptr, mbuf->wraplen, mbuf->wrapptr,
	   mbuf->fr_start, mbuf->fr_end);
}
#endif

/*
 * Return statistics of fddi driver.
 */
static struct net_device_stats *apfddi_get_stats(struct device *dev)
{
    return((struct net_device_stats *)dev->priv);
}

    


