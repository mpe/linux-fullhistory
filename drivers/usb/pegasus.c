/*
**	Pegasus: USB 10/100Mbps/HomePNA (1Mbps) Controller
**
**	Copyright (c) 1999,2000 Petko Manolov - Petkan (petkan@dce.bg)
**	
**
**	ChangeLog:
**		....	Most of the time spend reading sources & docs.
**		v0.2.x	First official release for the Linux kernel.
**		v0.3.0	Beutified and structured, some bugs fixed.
**		v0.3.x	URBifying bulk requests and bugfixing. First relatively
**			stable release. Still can touch device's registers only
**			from top-halves.
**		v0.4.0	Control messages remained unurbified are now URBs.
**			Now we can touch the HW at any time.
**		v0.4.9	Control urbs again use process context to wait. Argh...
**			Some long standing bugs (start_net) fixed. Also nasty
**			trick with resubmiting control urb from interrupt
**			context used. Please let me know how it behaves.
**			Pegasus II support added since this version.
**			TODO: suppressing HCD warnings spewage on disconnect.
*/

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */


#include <linux/module.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/usb.h>


static const char *version = __FILE__ ": v0.4.9 2000/09/14 (C) 1999-2000 Petko Manolov (petkan@dce.bg)";


#define	PEGASUS_USE_INTR


#define	PEGASUS_II		0x80000000
#define	HAS_HOME_PNA		0x40000000

#define	PEGASUS_MTU		1500
#define	PEGASUS_MAX_MTU		1536

#define	EPROM_WRITE		0x01
#define	EPROM_READ		0x02
#define	EPROM_DONE		0x04
#define	EPROM_WR_ENABLE		0x10
#define	EPROM_LOAD		0x20

#define	MII_BMCR		0x00
#define	MII_BMSR		0x01
#define	BMSR_MEDIA		0x7808
#define	MII_ANLPA		0x05
#define	ANLPA_100TX_FD		0x0100
#define	ANLPA_100TX_HD		0x0080
#define	ANLPA_10T_FD		0x0040
#define	ANLPA_10T_HD		0x0020
#define	PHY_DONE		0x80
#define	PHY_READ		0x40
#define	PHY_WRITE		0x20
#define	DEFAULT_GPIO_RESET	0x24
#define	LINKSYS_GPIO_RESET	0x24
#define	DEFAULT_GPIO_SET	0x26

#define	PEGASUS_PRESENT		1
#define	PEGASUS_RUNNING		2
#define	CTRL_URB_RUNNING	4
#define	CTRL_URB_SLEEP		8
#define	ETH_REGS_CHANGE		0x40000000
#define	ETH_REGS_CHANGED	0x80000000

#define	RX_MULTICAST		2
#define	RX_PROMISCUOUS		4

#define	REG_TIMEOUT		(HZ)
#define	PEGASUS_TX_TIMEOUT	(HZ*10)

#ifdef	PEGASUS_USE_INTR
	#define	INTR_IVAL	0x80
#else
	#define	INTR_IVAL	0
#endif

#define	TX_UNDERRUN		0x80
#define	EXCESSIVE_COL		0x40
#define	LATE_COL		0x20
#define	NO_CARRIER		0x10
#define	LOSS_CARRIER		0x08
#define	JABBER_TIMEOUT		0x04

#define	PEGASUS_REQT_READ	0xc0
#define	PEGASUS_REQT_WRITE	0x40
#define	PEGASUS_REQ_GET_REGS	0xf0
#define	PEGASUS_REQ_SET_REGS	0xf1
#define	PEGASUS_REQ_SET_REG	PEGASUS_REQ_SET_REGS
#define	NUM_CTRL_URBS		0x10
#define	ALIGN(x)		x __attribute__((aligned(L1_CACHE_BYTES)))

enum pegasus_registers {
	EthCtrl0 = 0,
	EthCtrl1 = 1,
	EthCtrl2 = 2,
	EthID = 0x10,
	Reg1d = 0x1d,
	EpromOffset = 0x20,
	EpromData = 0x21,	/* 0x21 low, 0x22 high byte */
	EpromCtrl = 0x23,
	PhyAddr = 0x25,
	PhyData = 0x26, 	/* 0x26 low, 0x27 high byte */
	PhyCtrl = 0x28,
	UsbStst = 0x2a,
	EthTxStat0 = 0x2b,
	EthTxStat1 = 0x2c,
	EthRxStat = 0x2d,
	Reg7b = 0x7b,
	Gpio0 = 0x7e,
	Gpio1 = 0x7f,
	Reg81 = 0x81,
};


typedef struct pegasus {
	struct usb_device	*usb;
	struct net_device	*net;
	struct net_device_stats	stats;
	unsigned		flags;
	unsigned		features;
	struct urb		ctrl_urb, rx_urb, tx_urb, intr_urb;
	devrequest		dr;
	wait_queue_head_t	ctrl_wait;
	struct semaphore	ctrl_sem;
	unsigned char		ALIGN(rx_buff[PEGASUS_MAX_MTU]);
	unsigned char		ALIGN(tx_buff[PEGASUS_MAX_MTU]);
	unsigned char		ALIGN(intr_buff[8]);
	__u8			eth_regs[4];
	__u8			phy;
	__u8			gpio_res;
} pegasus_t;

struct usb_eth_dev {
	char	*name;
	__u16	vendor;
	__u16	device;
	__u32	private; /* LSB is gpio reset value */
};


static int loopback = 0;
static int mode = 0;
static int multicast_filter_limit = 32;


MODULE_AUTHOR("Petko Manolov <petkan@dce.bg>");
MODULE_DESCRIPTION("ADMtek AN986 Pegasus USB Ethernet driver");
MODULE_PARM(loopback, "i");
MODULE_PARM(mode, "i");
MODULE_PARM_DESC(loopback, "Enable MAC loopback mode (bit 0)");
MODULE_PARM_DESC(mode, "Enable HomePNA mode (bit 0) - default = MII mode = 0");


static struct usb_eth_dev usb_dev_id[] = {
	{"Billionton USB-100", 0x08dd, 0x0986, DEFAULT_GPIO_RESET},
	{"Corega FEter USB-TX", 0x7aa, 0x0004, DEFAULT_GPIO_RESET},
	{"MELCO/BUFFALO LUA-TX", 0x0411, 0x0001, DEFAULT_GPIO_RESET},
	{"D-Link DSB-650TX", 0x2001, 0x4001, LINKSYS_GPIO_RESET},
	{"D-Link DSB-650TX", 0x2001, 0x4002, LINKSYS_GPIO_RESET},
	{"D-Link DSB-650TX(PNA)", 0x2001, 0x4003, DEFAULT_GPIO_RESET},
	{"D-Link DSB-650", 0x2001, 0xabc1, DEFAULT_GPIO_RESET},
	{"D-Link DU-E10", 0x07b8, 0xabc1, DEFAULT_GPIO_RESET},
	{"D-Link DU-E100", 0x07b8, 0x4002, DEFAULT_GPIO_RESET},
	{"Linksys USB10TX", 0x066b, 0x2202, LINKSYS_GPIO_RESET},
	{"Linksys USB100TX", 0x066b, 0x2203, LINKSYS_GPIO_RESET},
	{"Linksys USB100TX", 0x066b, 0x2204, LINKSYS_GPIO_RESET},
	{"Linksys USB Ethernet Adapter", 0x066b, 0x2206, LINKSYS_GPIO_RESET},
	{"SMC 202 USB Ethernet", 0x0707, 0x0200, DEFAULT_GPIO_RESET},
	{"ADMtek AN986 \"Pegasus\" USB Ethernet (eval board)", 0x07a6, 0x0986, 
		HAS_HOME_PNA | DEFAULT_GPIO_RESET},
	{"Accton USB 10/100 Ethernet Adapter", 0x083a, 0x1046, 
		DEFAULT_GPIO_RESET},
	{"IO DATA USB ET/TX", 0x04bb, 0x0904, DEFAULT_GPIO_RESET},
	{"LANEED USB Ethernet LD-USB/TX", 0x056e, 0x4002, DEFAULT_GPIO_RESET},
	{"SOHOware NUB100 Ethernet", 0x15e8, 0x9100, DEFAULT_GPIO_RESET},
	{"ADMtek ADM8511 \"Pegasus II\" USB Ethernet", 0x07a6, 0x8511, 
		PEGASUS_II | DEFAULT_GPIO_RESET},
	{NULL, 0, 0, 0}
};


static int update_eth_regs_async( pegasus_t * );
/* Aargh!!! I _really_ hate such tweaks */
static void ctrl_callback( urb_t *urb )
{
	pegasus_t		*pegasus = urb->context;

	if ( !pegasus )
		return;

	switch ( urb->status ) {
		case USB_ST_NOERROR:
			if ( pegasus->flags & ETH_REGS_CHANGE ) {
				pegasus->flags &= ~ETH_REGS_CHANGE;
				pegasus->flags |= ETH_REGS_CHANGED;
				update_eth_regs_async( pegasus );
				return;
			}
			break;
		case USB_ST_URB_PENDING:
			return;
		case USB_ST_URB_KILLED:
			break;
		default:
			warn( __FUNCTION__ " status %d", urb->status);
	}
	pegasus->flags &= ~ETH_REGS_CHANGED;
	if ( pegasus->flags & CTRL_URB_SLEEP ) {
		pegasus->flags &= ~CTRL_URB_SLEEP;
		wake_up_interruptible( &pegasus->ctrl_wait );
	}
}


static int get_registers(pegasus_t *pegasus, __u16 indx, __u16 size, void *data)
{
	int	ret;

	if ( pegasus->flags & ETH_REGS_CHANGED ) {
		pegasus->flags |= CTRL_URB_SLEEP;
		interruptible_sleep_on( &pegasus->ctrl_wait );
	}
	pegasus->dr.requesttype = PEGASUS_REQT_READ;
	pegasus->dr.request = PEGASUS_REQ_GET_REGS;
	pegasus->dr.value = 0;
	pegasus->dr.index = cpu_to_le16p(&indx);
	pegasus->dr.length = 
	pegasus->ctrl_urb.transfer_buffer_length = cpu_to_le16p(&size);

	FILL_CONTROL_URB( &pegasus->ctrl_urb, pegasus->usb,
			  usb_rcvctrlpipe(pegasus->usb,0),
			  (char *)&pegasus->dr,
			  data, size, ctrl_callback, pegasus );

	if ( (ret = usb_submit_urb( &pegasus->ctrl_urb )) ) {
		err( __FUNCTION__ " BAD CTRLs %d", ret);
		return	ret;
	}
	pegasus->flags |= CTRL_URB_SLEEP;
	interruptible_sleep_on( &pegasus->ctrl_wait );

	return	ret;
}


static int set_registers(pegasus_t *pegasus, __u16 indx, __u16 size, void *data)
{
	int	ret;

	if ( pegasus->flags & ETH_REGS_CHANGED ) {
		pegasus->flags |= CTRL_URB_SLEEP ;
		interruptible_sleep_on( &pegasus->ctrl_wait );
	}
	pegasus->dr.requesttype = PEGASUS_REQT_WRITE;
	pegasus->dr.request = PEGASUS_REQ_SET_REGS;
	pegasus->dr.value = 0;
	pegasus->dr.index = cpu_to_le16p( &indx );
	pegasus->dr.length = 
	pegasus->ctrl_urb.transfer_buffer_length = cpu_to_le16p( &size );

	FILL_CONTROL_URB( &pegasus->ctrl_urb, pegasus->usb,
			  usb_sndctrlpipe(pegasus->usb,0),
			  (char *)&pegasus->dr,
			  data, size, ctrl_callback, pegasus );

	if ( (ret = usb_submit_urb( &pegasus->ctrl_urb )) ) {
		err( __FUNCTION__ " BAD CTRL %d", ret);
		return	ret;
	}
	pegasus->flags |= CTRL_URB_SLEEP;
	interruptible_sleep_on( &pegasus->ctrl_wait );

	return	ret;
}


static int set_register( pegasus_t *pegasus, __u16 indx, __u8 data )
{
	int	ret;

	if ( pegasus->flags & ETH_REGS_CHANGED ) {
		pegasus->flags |= CTRL_URB_SLEEP;
		interruptible_sleep_on( &pegasus->ctrl_wait );
	}
	pegasus->dr.requesttype = PEGASUS_REQT_WRITE;
	pegasus->dr.request = PEGASUS_REQ_SET_REG;
	pegasus->dr.value = data;
	pegasus->dr.index = cpu_to_le16p( &indx );
	pegasus->dr.length = pegasus->ctrl_urb.transfer_buffer_length = 1;

	FILL_CONTROL_URB( &pegasus->ctrl_urb, pegasus->usb,
			  usb_sndctrlpipe(pegasus->usb,0),
			  (char *)&pegasus->dr,
			  &data, 1, ctrl_callback, pegasus );

	if ( (ret = usb_submit_urb( &pegasus->ctrl_urb )) ) {
		err( __FUNCTION__ " BAD CTRL %d", ret);
		return	ret;
	}
	pegasus->flags |= CTRL_URB_SLEEP;
	interruptible_sleep_on( &pegasus->ctrl_wait );
	
	return	ret;
}


static int update_eth_regs_async( pegasus_t *pegasus )
{
	int	ret;

	pegasus->dr.requesttype = PEGASUS_REQT_WRITE;
	pegasus->dr.request = PEGASUS_REQ_SET_REGS;
	pegasus->dr.value = 0;
	pegasus->dr.index = EthCtrl0;
	pegasus->dr.length = 
	pegasus->ctrl_urb.transfer_buffer_length = 3;

	FILL_CONTROL_URB( &pegasus->ctrl_urb, pegasus->usb,
			  usb_sndctrlpipe(pegasus->usb,0),
			  (char *)&pegasus->dr,
			  pegasus->eth_regs, 3, ctrl_callback, pegasus );

	if ( (ret = usb_submit_urb( &pegasus->ctrl_urb )) )
		err( __FUNCTION__ " BAD CTRL %d, flags %x",ret,pegasus->flags );

	return	ret;
}


static int read_phy_word( pegasus_t *pegasus, __u8 phy, __u8 indx, __u16 *regd )
{
	int	i;
	__u8	data[4] = { phy, 0, 0, indx };

	set_register( pegasus, PhyCtrl, 0 );
	set_registers( pegasus, PhyAddr, sizeof(data), data );
	set_register( pegasus, PhyCtrl, (indx | PHY_READ) );
	for (i = 0; i < REG_TIMEOUT; i++) {
		get_registers(pegasus, PhyCtrl, 1, data);
		if ( data[0] & PHY_DONE ) 
			break;
	}
	if ( i < REG_TIMEOUT ) {
		get_registers( pegasus, PhyData, 2, regd );
		return	0;
	}
	warn( __FUNCTION__ " failed" );
	
	return 1;
}


static int write_phy_word( pegasus_t *pegasus, __u8 phy, __u8 indx, __u16 regd )
{
	int	i;
	__u8	data[4] = { phy, 0, 0, indx };
	
	*(data + 1) = cpu_to_le16p( &regd );
	set_register( pegasus, PhyCtrl, 0 );
	set_registers( pegasus, PhyAddr, 4, data );
	set_register( pegasus, PhyCtrl, (indx | PHY_WRITE) );
	for (i = 0; i < REG_TIMEOUT; i++) {
		get_registers(pegasus, PhyCtrl, 1, data);
		if ( data[0] & PHY_DONE ) 
			break;
	}
	if ( i < REG_TIMEOUT )
		return	0;
	warn( __FUNCTION__ " failed" );

	return 1;
}


static int read_eprom_word( pegasus_t *pegasus, __u8 index, __u16 *retdata )
{
	int	i, tmp;

	set_register( pegasus, EpromCtrl, 0 );
	set_register( pegasus, EpromOffset, index );
	set_register( pegasus, EpromCtrl, EPROM_READ); 
	for ( i=0; i < REG_TIMEOUT; i++ ) {
		get_registers( pegasus, EpromCtrl, 1, &tmp );
		if ( tmp & EPROM_DONE )
			break;
	}
	if ( i < REG_TIMEOUT ) {
		get_registers( pegasus, EpromData, 2, retdata );
		return	0;
	}
	warn( __FUNCTION__ " failed" );

	return -1;
}


static inline void enable_eprom_write( pegasus_t *pegasus )
{
	__u8	tmp;

	get_registers( pegasus, EthCtrl2, 1, &tmp );
	set_register( pegasus, EthCtrl2, tmp | EPROM_WR_ENABLE );
}


static inline void disable_eprom_write( pegasus_t *pegasus )
{
	__u8 	tmp;

	get_registers( pegasus, EthCtrl2, 1, &tmp );
	set_register( pegasus, EpromCtrl, 0 );
	set_register( pegasus, EthCtrl2, tmp & ~EPROM_WR_ENABLE );
}


static int write_eprom_word( pegasus_t *pegasus, __u8 index, __u16 data )
{
	int	i, tmp;
	__u8	d[4] = {0x3f, 0, 0, EPROM_WRITE};

	set_registers( pegasus, EpromOffset, 4, d );
	enable_eprom_write( pegasus );
	set_register( pegasus, EpromOffset, index );
	set_registers( pegasus, EpromData, 2, &data );
	set_register( pegasus, EpromCtrl, EPROM_WRITE );

	for ( i=0; i < REG_TIMEOUT; i++ ) {
		get_registers( pegasus, EpromCtrl, 1, &tmp );
		if ( tmp & EPROM_DONE )
			break;
	}
	disable_eprom_write( pegasus );
	if ( i < REG_TIMEOUT )
		return	0;
	warn( __FUNCTION__ " failed" );
	return	-1;
}


static void set_intr_interval( pegasus_t *pegasus )
{
	__u16	d;
	__u8	tmp;

	read_eprom_word( pegasus, 4, &d );
	((__u8 *)&d)[1] = INTR_IVAL;
	write_eprom_word( pegasus, 4, d );
	get_registers( pegasus, EthCtrl2, 1, &tmp );
	set_register( pegasus, EthCtrl2, tmp | EPROM_LOAD );
	udelay( 10000 );
	set_register( pegasus, EthCtrl2, tmp );
#ifdef	PEGASUS_DUMP_EEPROM  
	{ int	i;
	for ( i=0; i < 0x40; i++ ) {
		read_eprom_word( pegasus, i, &d );
		printk( "eepromword %02x-%04x, ", i, d );
	}
	printk( "\n" );
	}
#endif
}


static inline int get_node_id( pegasus_t *pegasus, __u8 *id )
{
	int i;

	for (i = 0; i < 3; i++)
		if ( read_eprom_word( pegasus, i, (__u16 *)&id[i*2]) )
			return 1;
	return 0;
}


static inline int reset_mac( pegasus_t *pegasus )
{
	__u8	data = 0x8;
	int	i;

	set_register(pegasus, EthCtrl1, data);
	for (i = 0; i < REG_TIMEOUT; i++) {
		get_registers(pegasus, EthCtrl1, 1, &data);
		if (~data & 0x08) {
			if (loopback & 1) 
				break;
			if ( mode && (pegasus->features & HAS_HOME_PNA) )
				set_register( pegasus, Gpio1, 0x34 );
			else
				set_register( pegasus, Gpio1, 0x26 );
			set_register( pegasus, Gpio0, pegasus->features );
			set_register( pegasus, Gpio0, DEFAULT_GPIO_SET );
			break;
		}
	}
	if ( i == REG_TIMEOUT )
		return 1;
	return	0;
}


static int start_net( struct net_device *dev, struct usb_device *usb )
{
	__u16	linkpart, bmsr;
	__u8	node_id[6];
	__u8	data[4];
	pegasus_t *pegasus = dev->priv;

	if ( get_node_id(pegasus, node_id) ) 
		return 1;

	set_registers( pegasus, EthID, sizeof(node_id), node_id );
	memcpy( dev->dev_addr, node_id, sizeof(node_id) );
	if ( read_phy_word(pegasus, pegasus->phy, MII_BMSR, &bmsr) ) 
		return 2;
	if ( !(bmsr & 0x20) && !loopback ) 
		warn( "%s: link NOT established (0x%x) - check the cable.",
			dev->name, bmsr );
	if ( read_phy_word(pegasus, pegasus->phy, MII_ANLPA, &linkpart) )
		return 4;
	if ( !(linkpart & 1) )
		warn( "link partner stat %x", linkpart );

	data[0] = 0xc9;
	data[1] = 0;
	if ( linkpart & (ANLPA_100TX_FD | ANLPA_10T_FD) )
		data[1] |= 0x20; /* set full duplex */
	if ( linkpart & (ANLPA_100TX_FD | ANLPA_100TX_HD) )
		data[1] |= 0x10; /* set 100 Mbps */
	if ( mode )
		data[1] = 0;
	data[2] = (loopback & 1) ? 0x09 : 0x01;

	*(unsigned *)pegasus->eth_regs = *(unsigned *)data;

	set_registers( pegasus, EthCtrl0, 3, data );

	return 0;
}


static void read_bulk_callback( struct urb *urb )
{
	pegasus_t *pegasus = urb->context;
	struct net_device *net;
	int count = urb->actual_length, res;
	int rx_status;
	struct sk_buff	*skb;
	__u16 pkt_len;

	if ( !pegasus || !(pegasus->flags & PEGASUS_RUNNING) )
		return;

	net = pegasus->net;
	rx_status = *(int *)(pegasus->rx_buff + count - 4);

	if (urb->status) {
		dbg("%s: RX status %d", net->name, urb->status);
		goto goon;
	}

	if ( !count )
		goto goon;

	if ( rx_status & 0x000e0000 ) {

		dbg("%s: error receiving packet %x", net->name, rx_status & 0xe0000);
		pegasus->stats.rx_errors++;
		if ( rx_status & 0x060000 )
			pegasus->stats.rx_length_errors++;
		if ( rx_status & 0x080000 )
			pegasus->stats.rx_crc_errors++;
		if ( rx_status & 0x100000 )
			pegasus->stats.rx_frame_errors++;

		goto goon;
	}

	pkt_len = (rx_status & 0xfff) - 8;

	if ( !(skb = dev_alloc_skb(pkt_len+2)) )
		goto goon;

	skb->dev = net;
	skb_reserve(skb, 2);
	eth_copy_and_sum(skb, pegasus->rx_buff, pkt_len, 0);
	skb_put(skb, pkt_len);

	skb->protocol = eth_type_trans(skb, net);
	netif_rx(skb);
	pegasus->stats.rx_packets++;
	pegasus->stats.rx_bytes += pkt_len;

goon:
	if ( (res = usb_submit_urb(&pegasus->rx_urb)) )
		warn( __FUNCTION__ " failed submint rx_urb %d", res);
}


static void write_bulk_callback( struct urb *urb )
{
	pegasus_t *pegasus = urb->context;

	if ( !pegasus || !(pegasus->flags & PEGASUS_RUNNING) )
		return;
	if ( urb->status )
		info("%s: TX status %d", pegasus->net->name, urb->status);

	netif_wake_queue(pegasus->net);
}


static void intr_callback( struct urb *urb )
{
	pegasus_t *pegasus = urb->context;
	struct net_device *net;
	__u8	*d;

	if ( !pegasus )
		return;
	d = urb->transfer_buffer;
	net = pegasus->net;
	if ( d[0] & 0xfc ) {
		pegasus->stats.tx_errors++;
		if ( d[0] & TX_UNDERRUN )
			pegasus->stats.tx_fifo_errors++;
		if ( d[0] & (EXCESSIVE_COL | JABBER_TIMEOUT) )
			pegasus->stats.tx_aborted_errors++;
		if ( d[0] & LATE_COL )
			pegasus->stats.tx_window_errors++;
		if ( d[0] & (NO_CARRIER | LOSS_CARRIER) )
			pegasus->stats.tx_carrier_errors++;
	}
	switch ( urb->status ) {
		case USB_ST_NOERROR:
			break;
		case USB_ST_URB_KILLED:
			break;
		default:
			info("intr status %d", urb->status);
	}
}


static void pegasus_tx_timeout( struct net_device *net )
{
	pegasus_t *pegasus = net->priv;

	if ( !pegasus )
		return;
	
	usb_unlink_urb(&pegasus->tx_urb);
	warn("%s: Tx timed out.", net->name);
	pegasus->stats.tx_errors++;
	net->trans_start = jiffies;

	netif_wake_queue(net);
}


static int pegasus_start_xmit( struct sk_buff *skb, struct net_device *net )
{
	pegasus_t	*pegasus = net->priv;
	int 	count = ((skb->len+2) & 0x3f) ? skb->len+2 : skb->len+3;
	int 	res;

	netif_stop_queue(net);
	if ( !(pegasus->flags & PEGASUS_RUNNING) )
		return	0;

	((__u16 *)pegasus->tx_buff)[0] = skb->len;
	memcpy(pegasus->tx_buff+2, skb->data, skb->len);
	pegasus->tx_urb.transfer_buffer_length = count;
	pegasus->tx_urb.transfer_flags |= USB_ASYNC_UNLINK;

	if ((res = usb_submit_urb(&pegasus->tx_urb))) {
		warn("failed tx_urb %d", res);
		pegasus->stats.tx_errors++;
		netif_start_queue(net);
	} else {
		pegasus->stats.tx_packets++;
		pegasus->stats.tx_bytes += skb->len;
		net->trans_start = jiffies;
	}

	dev_kfree_skb(skb);

	return 0;
}


static struct net_device_stats *pegasus_netdev_stats( struct net_device *dev )
{
	return &((pegasus_t *)dev->priv)->stats;
}


static inline void stop_net( pegasus_t *pegasus )
{
	int 	tmp=0;

	set_registers( pegasus, EthCtrl0, 2, &tmp );
}


static int pegasus_open(struct net_device *net)
{
	pegasus_t *pegasus = (pegasus_t *)net->priv;
	int	res;

	if ((res = start_net(net, pegasus->usb))) {
		err("can't start_net() - %d", res);
		return -EIO;
	}
	if ( (res = usb_submit_urb(&pegasus->rx_urb)) )
		warn( __FUNCTION__ " failed rx_urb %d", res );
#ifdef	PEGASUS_USE_INTR
	if ( (res = usb_submit_urb(&pegasus->intr_urb)) )
		warn( __FUNCTION__ " failed intr_urb %d", res);
#endif		
	netif_start_queue( net );
	pegasus->flags |= PEGASUS_RUNNING;

	return 0;
}


static int pegasus_close( struct net_device *net )
{
	pegasus_t	*pegasus = net->priv;

	stop_net( pegasus );
	pegasus->flags &= ~PEGASUS_RUNNING;
	netif_stop_queue(net);

	usb_unlink_urb( &pegasus->rx_urb );
	usb_unlink_urb( &pegasus->tx_urb );
	usb_unlink_urb( &pegasus->ctrl_urb );
	usb_unlink_urb( &pegasus->intr_urb );

	return 0;
}


static int pegasus_ioctl( struct net_device *net, struct ifreq *rq, int cmd )
{
	__u16 *data = (__u16 *)&rq->ifr_data;
	pegasus_t	*pegasus = net->priv;

	switch(cmd) {
		case SIOCDEVPRIVATE:
			data[0] = pegasus->phy;
		case SIOCDEVPRIVATE+1:
			read_phy_word(pegasus, data[0], data[1]&0x1f, &data[3]);
			return 0;
		case SIOCDEVPRIVATE+2:
			if ( !capable(CAP_NET_ADMIN) )
				return -EPERM;
			write_phy_word(pegasus, pegasus->phy, data[1] & 0x1f, data[2]);
			return 0;
		default:
			return -EOPNOTSUPP;
	}
}


static void pegasus_set_multicast( struct net_device *net )
{
	pegasus_t *pegasus = net->priv;

	netif_stop_queue(net);

	if (net->flags & IFF_PROMISC) {
		pegasus->eth_regs[EthCtrl2] |= RX_PROMISCUOUS;
		info("%s: Promiscuous mode enabled", net->name);
	} else if ((net->mc_count > multicast_filter_limit) ||
			(net->flags & IFF_ALLMULTI)) {
		pegasus->eth_regs[EthCtrl0] |= RX_MULTICAST;
		pegasus->eth_regs[EthCtrl2] &= ~RX_PROMISCUOUS;
		info("%s set allmulti", net->name);
	} else {
		pegasus->eth_regs[EthCtrl0] &= ~RX_MULTICAST;
		pegasus->eth_regs[EthCtrl2] &= ~RX_PROMISCUOUS;
		info("%s: set Rx mode", net->name);
	}

	pegasus->flags |= ETH_REGS_CHANGE;
	ctrl_callback( &pegasus->ctrl_urb );

	netif_wake_queue(net);
}


static int check_device_ids( __u16 vendor, __u16 product )
{
	int i=0;
	
	while ( usb_dev_id[i].name ) {
		if ( (usb_dev_id[i].vendor == vendor) && 
			(usb_dev_id[i].device == product) )
			return i;
		i++;
	}
	return	-1;
}


static __u8 mii_phy_probe( pegasus_t *pegasus )
{
	int	i;
	__u16	tmp;

	for ( i=0; i < 32; i++ ) {
		read_phy_word( pegasus, i, MII_BMSR, &tmp );
		if ( tmp == 0 || tmp == 0xffff || (tmp & BMSR_MEDIA) == 0 )
			continue;
		else
			return	i;
	}

	return	0;
}


static inline void setup_pegasus_II( pegasus_t *pegasus )
{
	set_register( pegasus, Reg1d, 0 );
	set_register( pegasus, Reg7b, 2 );
	if ( pegasus->features & HAS_HOME_PNA )
		set_register( pegasus, Reg81, 6 );
	else
		set_register( pegasus, Reg81, 2 );
}


static void * pegasus_probe( struct usb_device *dev, unsigned int ifnum )
{
	struct net_device *net;
	pegasus_t *pegasus;
	int	dev_indx;

	if ( (dev_indx = check_device_ids(dev->descriptor.idVendor, dev->descriptor.idProduct)) == -1 ) {
		return NULL;
	}

	if (usb_set_configuration(dev, dev->config[0].bConfigurationValue)) {
		err("usb_set_configuration() failed");
		return NULL;
	}

	if(!(pegasus = kmalloc(sizeof(struct pegasus), GFP_KERNEL))) {
		err("out of memory allocating device structure");
		return NULL;
	}
	memset(pegasus, 0, sizeof(struct pegasus));

	net = init_etherdev(0, 0);
	net->priv = pegasus;
	net->open = pegasus_open;
	net->stop = pegasus_close;
	net->watchdog_timeo = PEGASUS_TX_TIMEOUT;
	net->tx_timeout = pegasus_tx_timeout;
	net->do_ioctl = pegasus_ioctl;
	net->hard_start_xmit = pegasus_start_xmit;
	net->set_multicast_list = pegasus_set_multicast;
	net->get_stats = pegasus_netdev_stats;
	net->mtu = PEGASUS_MTU;

	init_MUTEX( &pegasus-> ctrl_sem );
	init_waitqueue_head( &pegasus->ctrl_wait );

	pegasus->usb = dev;
	pegasus->net = net;

	FILL_BULK_URB( &pegasus->rx_urb, dev, usb_rcvbulkpipe(dev, 1),
			pegasus->rx_buff, PEGASUS_MAX_MTU, 
			read_bulk_callback, pegasus );
	FILL_BULK_URB( &pegasus->tx_urb, dev, usb_sndbulkpipe(dev, 2),
			pegasus->tx_buff, PEGASUS_MAX_MTU, 
			write_bulk_callback, pegasus );
	FILL_INT_URB( &pegasus->intr_urb, dev, usb_rcvintpipe(dev, 3),
			pegasus->intr_buff, 8, intr_callback,
			pegasus, INTR_IVAL );

	pegasus->features = usb_dev_id[dev_indx].private;
	if ( reset_mac(pegasus) ) {
		err("can't reset MAC");
		unregister_netdev( pegasus->net );
		kfree(pegasus);
		pegasus = NULL;
		return NULL;
	}

	if ( pegasus->features & PEGASUS_II ) {
		info( "setup Pegasus II specific registers" );
		setup_pegasus_II( pegasus );
	}

	pegasus->phy = mii_phy_probe( pegasus );
	if ( !pegasus->phy ) {
		warn( "can't locate MII phy, using default" );
		pegasus->phy = 1;
	}

	set_intr_interval( pegasus );

	info( "%s: %s", net->name, usb_dev_id[dev_indx].name );

	MOD_INC_USE_COUNT;

	return pegasus;
}


static void pegasus_disconnect( struct usb_device *dev, void *ptr )
{
	struct pegasus *pegasus = ptr;

	if ( !pegasus ) {
		warn("unregistering non-existant device");
		return;
	}

	stop_net( pegasus );
	pegasus->flags &= ~PEGASUS_RUNNING;
	netif_stop_queue( pegasus->net );
	unregister_netdev( pegasus->net );

	usb_unlink_urb( &pegasus->rx_urb );
	usb_unlink_urb( &pegasus->tx_urb );
	usb_unlink_urb( &pegasus->ctrl_urb );
	usb_unlink_urb( &pegasus->intr_urb );
	
	kfree( pegasus );
	pegasus = NULL;

	MOD_DEC_USE_COUNT;
}


static struct usb_driver pegasus_driver = {
	name:		"pegasus",
	probe:		pegasus_probe,
	disconnect:	pegasus_disconnect,
};

int __init pegasus_init(void)
{
	info( "%s", version );
	return usb_register( &pegasus_driver );
}

void __exit pegasus_exit(void)
{
	usb_deregister( &pegasus_driver );
}

module_init( pegasus_init );
module_exit( pegasus_exit );
