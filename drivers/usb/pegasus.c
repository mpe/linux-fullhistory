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


static const char *version = __FILE__ ": v0.4.1 2000/08/08 (C) 1999-2000 Petko Manolov (petkan@dce.bg)\n";


#define	PEGASUS_MTU		1500
#define	PEGASUS_MAX_MTU		1536
#define	EPROM_WRITE		0x01
#define	EPROM_READ		0x02
#define	PEGASUS_TX_TIMEOUT	(HZ*10)
#define	PEGASUS_CTRL_TIMEOUT	(HZ*5)
#define	PEGASUS_CTRL_WAIT	(1<<31)
#define	PEGASUS_RUNNING		1
#define	PEGASUS_REQT_READ	0xc0
#define	PEGASUS_REQT_WRITE	0x40
#define	PEGASUS_REQ_GET_REGS	0xf0
#define	PEGASUS_REQ_SET_REGS	0xf1
#define	PEGASUS_REQ_SET_REG	PEGASUS_REQ_SET_REGS
#define	ALIGN(x)		x __attribute__((aligned(L1_CACHE_BYTES)))


enum pegasus_registers {
	EthCtrl0 = 0,
	EthCtrl1 = 1,
	EthCtrl2 = 2,
	EthID = 0x10,
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
	Gpio0 = 0x7e,
	Gpio1 = 0x7f,
};


struct pegasus {
	struct usb_device	*usb;
	struct net_device	*net;
	struct net_device_stats	stats;
	int			flags;
	struct urb		rx_urb, tx_urb, intr_urb, ctrl_urb;
	devrequest		dr;
	wait_queue_head_t	ctrl_wait;
	struct semaphore	ctrl_sem;
	unsigned char		ALIGN(rx_buff[PEGASUS_MAX_MTU]); 
	unsigned char		ALIGN(tx_buff[PEGASUS_MAX_MTU]); 
	unsigned char		ALIGN(intr_buff[8]);
};

struct usb_eth_dev {
	char	*name;
	__u16	vendor;
	__u16	device;
	void	*private;
};


static int loopback = 0;
static int multicast_filter_limit = 32;


MODULE_AUTHOR("Petko Manolov <petkan@dce.bg>");
MODULE_DESCRIPTION("ADMtek AN986 Pegasus USB Ethernet driver");
MODULE_PARM(loopback, "i");
MODULE_PARM_DESC(loopback, "Enable MAC loopback mode (bit 0)");


static struct usb_eth_dev usb_dev_id[] = {
	{"Billionton USB-100", 0x08dd, 0x0986, NULL},
	{"Corega FEter USB-TX", 0x7aa, 0x0004, NULL},
	{"MELCO/BUFFALO LUA-TX", 0x0411, 0x0001, NULL},
	{"D-Link DSB-650TX", 0x2001, 0x4001, NULL},
	{"D-Link DSB-650TX", 0x2001, 0x4002, NULL},
	{"D-Link DSB-650TX(PNA)", 0x2001, 0x4003, NULL},
	{"D-Link DSB-650", 0x2001, 0xabc1, NULL},
	{"D-Link DU-E10", 0x07b8, 0xabc1, NULL},
	{"D-Link DU-E100", 0x07b8, 0x4002, NULL},
	{"Linksys USB10TX", 0x066b, 0x2202, NULL},
	{"Linksys USB100TX", 0x066b, 0x2203, NULL},
	{"Linksys USB100TX", 0x066b, 0x2204, NULL},
	{"Linksys USB Ethernet Adapter", 0x066b, 0x2206, NULL},
	{"SMC 202 USB Ethernet", 0x0707, 0x0200, NULL},
	{"ADMtek AN986 \"Pegasus\" USB Ethernet (eval board)", 0x07a6, 0x0986, NULL},
	{"Accton USB 10/100 Ethernet Adapter", 0x083a, 0x1046, NULL},
	{"IO DATA USB ET/TX", 0x04bb, 0x0904, NULL},
	{"LANEED USB Ethernet LD-USB/TX", 0x056e, 0x4002, NULL},
	{"SOHOware NUB100 Ethernet", 0x15e8, 0x9100, NULL},
	{NULL, 0, 0, NULL}
};



static void pegasus_ctrl_end( urb_t *urb )
{
	struct pegasus *pegasus = urb->context;

	if ( pegasus->flags & PEGASUS_CTRL_WAIT ) {
		wake_up_interruptible(&pegasus->ctrl_wait);
		pegasus->flags &= ~PEGASUS_CTRL_WAIT;
	}
	if ( urb->status )
		warn("ctrl_urb end status %d", urb->status);
}


static int pegasus_ctrl_timeout( urb_t *ctrl_urb )
{
	struct	pegasus *pegasus = ctrl_urb->context;
	int	timeout=PEGASUS_CTRL_TIMEOUT;
	
	while ( ctrl_urb->status == -EINPROGRESS ) {
		if ( timeout ) {
			pegasus->flags |= PEGASUS_CTRL_WAIT;
			timeout = interruptible_sleep_on_timeout(&pegasus->ctrl_wait,timeout);
			pegasus->flags &= PEGASUS_CTRL_WAIT;
			continue;
		}
		err("ctrl urb busy %d", ctrl_urb->status);
		usb_unlink_urb( ctrl_urb );
		return	ctrl_urb->status;
	}
	return	0;
}


static int pegasus_get_registers( struct pegasus *pegasus, __u16 indx, __u16 size, void *data )
{
	int	ret;

	down( &pegasus->ctrl_sem);
	pegasus->dr.requesttype = PEGASUS_REQT_READ;
	pegasus->dr.request = PEGASUS_REQ_GET_REGS;
	pegasus->dr.value = 0;
	pegasus->dr.index = cpu_to_le16p(&indx);
	pegasus->dr.length = 
	pegasus->ctrl_urb.transfer_buffer_length = cpu_to_le16p(&size);

	FILL_CONTROL_URB( &pegasus->ctrl_urb, pegasus->usb,
			  usb_rcvctrlpipe(pegasus->usb,0), (char *)&pegasus->dr,
			  data, size, pegasus_ctrl_end, pegasus );

	if ( (ret = usb_submit_urb( &pegasus->ctrl_urb )) ) 
		err("BAD CTRLs %d", ret);
	else
		ret = pegasus_ctrl_timeout( &pegasus->ctrl_urb );
	up( &pegasus->ctrl_sem );
	
	return	ret;
}


static int pegasus_set_registers( struct pegasus *pegasus, __u16 indx, __u16 size, void *data )
{
	int	ret;

	down( &pegasus->ctrl_sem );
	pegasus->dr.requesttype = PEGASUS_REQT_WRITE;
	pegasus->dr.request = PEGASUS_REQ_SET_REGS;
	pegasus->dr.value = 0;
	pegasus->dr.index = cpu_to_le16p( &indx );
	pegasus->dr.length = 
	pegasus->ctrl_urb.transfer_buffer_length = cpu_to_le16p( &size );

	FILL_CONTROL_URB( &pegasus->ctrl_urb, pegasus->usb,
			  usb_sndctrlpipe(pegasus->usb,0), (char *)&pegasus->dr,
			  data, size, pegasus_ctrl_end, pegasus );

	if ( (ret = usb_submit_urb( &pegasus->ctrl_urb )) )
		err("BAD CTRL %d", ret);
	else
		ret = pegasus_ctrl_timeout( &pegasus->ctrl_urb );
	up( &pegasus->ctrl_sem );
	
	return	ret;
}


static int pegasus_set_register( struct pegasus *pegasus, __u16 indx,__u8 data )
{
	int	ret;
	
	down( &pegasus->ctrl_sem );
	pegasus->dr.requesttype = PEGASUS_REQT_WRITE;
	pegasus->dr.request = PEGASUS_REQ_SET_REG;
	pegasus->dr.value = cpu_to_le16p( &data );
	pegasus->dr.index = cpu_to_le16p( &indx );
	pegasus->dr.length = pegasus->ctrl_urb.transfer_buffer_length = 1;

	FILL_CONTROL_URB( &pegasus->ctrl_urb, pegasus->usb,
			  usb_sndctrlpipe(pegasus->usb,0), (char *)&pegasus->dr,
			  &data, 1, pegasus_ctrl_end, pegasus );

	if ( (ret = usb_submit_urb( &pegasus->ctrl_urb )) )
		err("BAD CTRL %d", ret);
	else
		ret = pegasus_ctrl_timeout( &pegasus->ctrl_urb );
	up( &pegasus->ctrl_sem );

	return	ret;
}


static int pegasus_read_phy_word(struct pegasus *pegasus, __u8 index, __u16 *regdata)
{
	int i;
	__u8 data[4] = { 1, 0, 0, 0x40 + index };

	pegasus_set_registers(pegasus, PhyAddr, 4, data);
	for (i = 0; i < 100; i++) {
		pegasus_get_registers(pegasus, PhyData, 3, data);
		if (data[2] & 0x80) {
			*regdata = *(__u16 *)(data);
			return 0;
		}
	}
	warn("read_phy_word() failed");
	
	return 1;
}


static int pegasus_write_phy_word(struct pegasus *pegasus, __u8 index, __u16 regdata)
{
	int i;
	__u8 data[4] = { 1, regdata, regdata >> 8, 0x20 + index };

	pegasus_set_registers(pegasus, PhyAddr, 4, data);
	for (i = 0; i < 100; i++) {
		pegasus_get_registers(pegasus, PhyCtrl, 1, data);
		if (data[0] & 0x80)
			return 0;
	}
	warn("write_phy_word() failed");

	return 1;
}


static int pegasus_rw_eprom_word(struct pegasus *pegasus, __u8 index, __u16 *retdata, __u8 direction)
{
	int i;
	__u8 data[4] = { index, 0, 0, direction };

	pegasus_set_registers(pegasus, EpromOffset, 4, data);
	for (i = 0; i < 100; i++) {
		pegasus_get_registers(pegasus, EpromCtrl, 1, data);
		if (data[0] & 4) {
			pegasus_get_registers(pegasus, EpromData, 2, data);
			*retdata = *(__u16 *)data;
			return 0;
		}
	}
	warn("pegasus_rw_eprom_word() failed");
	
	return 1;
}


static int pegasus_get_node_id(struct pegasus *pegasus, __u8 *id)
{
	int i;
	for (i = 0; i < 3; i++)
		if (pegasus_rw_eprom_word(pegasus, i, (__u16 *)&id[i*2], EPROM_READ))
			return 1;
	return 0;
}


static int pegasus_reset_mac(struct pegasus *pegasus)
{
	__u8 data = 0x8;
	int i;

	pegasus_set_register(pegasus, EthCtrl1, data);
	for (i = 0; i < 100; i++) {
		pegasus_get_registers(pegasus, EthCtrl1, 1, &data);
		if (~data & 0x08) {
			if (loopback & 1) 
				return 0;
			pegasus_set_register(pegasus, Gpio0, 0x24);
			pegasus_set_register(pegasus, Gpio0, 0x27);
			return 0;
		}
	}

	return 1;
}


static int pegasus_start_net(struct net_device *dev, struct usb_device *usb)
{
	__u16 partmedia, temp;
	__u8 node_id[6];
	__u8 data[4];
	struct pegasus *pegasus = dev->priv;

	if (pegasus_get_node_id(pegasus, node_id)) 
		return 1;

	pegasus_set_registers(pegasus, EthID, 6, node_id);
	memcpy(dev->dev_addr, node_id, 6);
	if (pegasus_read_phy_word(pegasus, 1, &temp)) 
		return 2;

	if ((~temp & 4) && !loopback) {
		warn("%s: link NOT established (0x%x) - check the cable.",
			dev->name, temp);
	}

	if (pegasus_read_phy_word(pegasus, 5, &partmedia))
		return 4;

	if ((partmedia & 0x1f) != 1) {
		warn("party FAIL %x", partmedia);
	}

	data[0] = 0xc9;
	data[1] = (partmedia & 0x100) ? 0x30 : ((partmedia & 0x80) ? 0x10 : 0);
	data[2] = (loopback & 1) ? 0x09 : 0x01;

	pegasus_set_registers(pegasus, EthCtrl0, 3, data);

	return 0;
}


static void pegasus_read_bulk(struct urb *urb)
{
	struct pegasus *pegasus = urb->context;
	struct net_device *net = pegasus->net;
	int count = urb->actual_length, res;
	int rx_status = *(int *)(pegasus->rx_buff + count - 4);
	struct sk_buff	*skb;
	__u16 pkt_len;

	if ( !(pegasus->flags & PEGASUS_RUNNING) )
		return;

	if (urb->status) {
		dbg("%s: RX status %d", net->name, urb->status);
		goto goon;
	}

	if (!count)
		goto goon;
#if 0
	if (rx_status & 0x00010000)
		goto goon;
#endif
	if (rx_status & 0x000e0000) {

		dbg("%s: error receiving packet %x", net->name, rx_status & 0xe0000);
		pegasus->stats.rx_errors++;
		if(rx_status & 0x060000) pegasus->stats.rx_length_errors++;
		if(rx_status & 0x080000) pegasus->stats.rx_crc_errors++;
		if(rx_status & 0x100000) pegasus->stats.rx_frame_errors++;

		goto goon;
	}

	pkt_len = (rx_status & 0xfff) - 8;

	if(!(skb = dev_alloc_skb(pkt_len+2)))
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
		warn("(prb)failed rx_urb %d", res);
}


static void pegasus_irq(urb_t *urb)
{
	__u8	*d = urb->transfer_buffer;
	
	if ( d[0] )
		dbg("txst0=0x%2x", d[0]);
}


static void pegasus_write_bulk(struct urb *urb)
{
	struct pegasus *pegasus = urb->context;


	if (urb->status)
		info("%s: TX status %d", pegasus->net->name, urb->status);
	netif_wake_queue(pegasus->net);
}

static void pegasus_tx_timeout(struct net_device *net)
{
	struct pegasus *pegasus = net->priv;

	
	usb_unlink_urb(&pegasus->tx_urb);
	warn("%s: Tx timed out.", net->name);
	pegasus->stats.tx_errors++;
	net->trans_start = jiffies;

	netif_wake_queue(net);
}


static int pegasus_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	struct pegasus	*pegasus = net->priv;
	int count = ((skb->len+2) & 0x3f) ? skb->len+2 : skb->len+3;
	int res;

	netif_stop_queue(net);
	if ( !(pegasus->flags & PEGASUS_RUNNING) )
		return	0;

	((__u16 *)pegasus->tx_buff)[0] = skb->len;
	memcpy(pegasus->tx_buff+2, skb->data, skb->len);
	pegasus->tx_urb.transfer_buffer_length = count;

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


static struct net_device_stats *pegasus_netdev_stats(struct net_device *dev)
{
	return &((struct pegasus *)dev->priv)->stats;
}


static inline void pegasus_stop_net( struct pegasus *pegasus )
{
	int 	tmp;

	pegasus_get_registers( pegasus, EthCtrl0, 1, &tmp );
	pegasus_set_register( pegasus, EthCtrl0, tmp & 0x3f );
}


static int pegasus_open(struct net_device *net)
{
	struct pegasus *pegasus = (struct pegasus *)net->priv;
	int res;

	if ((res = pegasus_start_net(net, pegasus->usb))) {
		err("can't start_net() - %d", res);
		return -EIO;
	}

	if ((res = usb_submit_urb(&pegasus->rx_urb)))
		warn("(open)failed rx_urb %d", res);
		
	if ((res = usb_submit_urb(&pegasus->intr_urb)))
		warn("(open)failed intr_urb %d", res);
		
	netif_start_queue(net);
	pegasus->flags |= PEGASUS_RUNNING;

	return 0;
}


static int pegasus_close(struct net_device *net)
{
	struct pegasus	*pegasus = net->priv;

	pegasus->flags &= ~PEGASUS_RUNNING;
	pegasus_stop_net( pegasus );
	
	netif_stop_queue(net);

	usb_unlink_urb(&pegasus->ctrl_urb);
	usb_unlink_urb(&pegasus->rx_urb);
	usb_unlink_urb(&pegasus->tx_urb);
	usb_unlink_urb(&pegasus->intr_urb);

	return 0;
}


static int pegasus_ioctl(struct net_device *net, struct ifreq *rq, int cmd)
{
	__u16 *data = (__u16 *)&rq->ifr_data;
	struct pegasus	*pegasus = net->priv;

	switch(cmd) {
		case SIOCDEVPRIVATE:
			data[0] = 1;
		case SIOCDEVPRIVATE+1:
			pegasus_read_phy_word(pegasus, data[1] & 0x1f, &data[3]);
			return 0;
		case SIOCDEVPRIVATE+2:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			pegasus_write_phy_word(pegasus, data[1] & 0x1f, data[2]);
			return 0;
		default:
			return -EOPNOTSUPP;
	}
}


static void pegasus_set_rx_mode(struct net_device *net)
{
/*	struct pegasus *pegasus = net->priv;*/

	netif_stop_queue(net);

	if (net->flags & IFF_PROMISC) {
/*		pegasus_get_registers(pegasus, EthCtrl2, 1, &tmp);
		pegasus_set_register(pegasus, EthCtrl2, tmp | 4);*/
		info("%s: Promiscuous mode enabled", net->name);
	} else if ((net->mc_count > multicast_filter_limit) ||
			(net->flags & IFF_ALLMULTI)) {
/*		pegasus_set_register(pegasus, EthCtrl0, 0xfa);
		pegasus_set_register(pegasus, EthCtrl2, 0);*/
		info("%s set allmulti", net->name);
	} else {
/*		pegasus_get_registers(pegasus, EthCtrl2, 1, &tmp);
		pegasus_set_register(pegasus, EthCtrl2, tmp & ~4);*/
		info("%s: set Rx mode", net->name);
	}

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


static void * pegasus_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct net_device *net;
	struct pegasus *pegasus;
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
	net->set_multicast_list = pegasus_set_rx_mode;
	net->get_stats = pegasus_netdev_stats;
	net->mtu = PEGASUS_MTU;

	pegasus->usb = dev;
	pegasus->net = net;

	init_waitqueue_head( &pegasus->ctrl_wait );
	init_MUTEX( &pegasus->ctrl_sem );

	FILL_BULK_URB(&pegasus->rx_urb, dev, usb_rcvbulkpipe(dev, 1),
			pegasus->rx_buff, PEGASUS_MAX_MTU, pegasus_read_bulk, 
			pegasus);
	FILL_BULK_URB(&pegasus->tx_urb, dev, usb_sndbulkpipe(dev, 2),
			pegasus->tx_buff, PEGASUS_MAX_MTU, pegasus_write_bulk,
			pegasus);
	FILL_INT_URB(&pegasus->intr_urb, dev, usb_rcvintpipe(dev, 3),
			pegasus->intr_buff, 8, pegasus_irq, pegasus, 128);

	if (pegasus_reset_mac(pegasus)) {
		err("can't reset MAC");
		kfree(pegasus);
		return NULL;
	}
	
	printk(KERN_INFO "%s: %s\n", net->name, usb_dev_id[dev_indx].name);

	MOD_INC_USE_COUNT;

	return pegasus;
}


static void pegasus_disconnect(struct usb_device *dev, void *ptr)
{
	struct pegasus *pegasus = ptr;

	if (!pegasus) {
		warn("unregistering non-existant device");
		return;
	}

	pegasus->flags &= ~PEGASUS_RUNNING;
	unregister_netdev(pegasus->net);

	if ( pegasus->flags & PEGASUS_CTRL_WAIT )
		wake_up_interruptible( &pegasus->ctrl_wait );
	
	usb_unlink_urb(&pegasus->ctrl_urb);
	usb_unlink_urb(&pegasus->rx_urb);
	usb_unlink_urb(&pegasus->tx_urb);
	usb_unlink_urb(&pegasus->intr_urb);

	kfree(pegasus);

	MOD_DEC_USE_COUNT;
}


static struct usb_driver pegasus_driver = {
	name:		"pegasus",
	probe:		pegasus_probe,
	disconnect:	pegasus_disconnect,
};

int __init pegasus_init(void)
{
	printk( version );
	return usb_register(&pegasus_driver);
}

void __exit pegasus_exit(void)
{
	usb_deregister(&pegasus_driver);
}

module_init(pegasus_init);
module_exit(pegasus_exit);
