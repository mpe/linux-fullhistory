/*
**	Pegasus: USB 10/100Mbps/HomePNA (1Mbps) Controller
**
**	Copyright (R) 1999,2000 Petko Manolov - Petkan (petkan@spct.net)
**
**	Distribute under GPL version 2 or later.
*/

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/usb.h>


static const char *version = __FILE__ ": v0.3.9 2000/04/11 Written by Petko Manolov (petkan@spct.net)\n";


#define	PEGASUS_MTU		1500
#define	PEGASUS_MAX_MTU		1536
#define	SROM_WRITE		0x01
#define	SROM_READ		0x02
#define	PEGASUS_TX_TIMEOUT	(HZ*5)
#define	ALIGN(x)		x __attribute__((aligned(L1_CACHE_BYTES)))

struct pegasus {
	struct usb_device	*usb;
	struct net_device	*net;
	struct net_device_stats	stats;
	spinlock_t		pegasus_lock;
	struct urb		rx_urb, tx_urb, intr_urb;
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

MODULE_AUTHOR("Petko Manolov <petkan@spct.net>");
MODULE_DESCRIPTION("ADMtek AN986 Pegasus USB Ethernet driver");
MODULE_PARM(loopback, "i");


static struct usb_eth_dev usb_dev_id[] = {
	{"Billionton USB-100", 0x08dd, 0x0986, NULL},
	{"Corega FEter USB-TX", 0x7aa, 0x0004, NULL},
	{"MELCO/BUFFALO LUA-TX", 0x0411, 0x0001, NULL},
	{"D-Link DSB-650TX", 0x2001, 0x4001, NULL},
	{"D-Link DSB-650TX", 0x2001, 0x4002, NULL},
	{"D-Link DSB-650TX(PNA)", 0x2001, 0x4003, NULL},
	{"Linksys USB100TX", 0x066b, 0x2203, NULL},
	{"Linksys USB100TX", 0x066b, 0x2204, NULL},
	{"SMC 202 USB Ethernet", 0x0707, 0x0200, NULL},
	{"ADMtek AN986 \"Pegasus\" USB Ethernet (eval board)", 0x07a6, 0x0986, NULL},
	{"Accton USB 10/100 Ethernet Adapter", 0x083a, 0x1046, NULL},
	{"IO DATA USB ET/TX", 0x04bb, 0x0904, NULL},
	{"LANEED USB Ethernet LD-USB/TX", 0x056e, 0x4002, NULL},
	{NULL, 0, 0, NULL}
};


#define pegasus_get_registers(dev, indx, size, data)\
	usb_control_msg(dev, usb_rcvctrlpipe(dev,0), 0xf0, 0xc0, 0, indx, data, size, HZ);
#define pegasus_set_registers(dev, indx, size, data)\
	usb_control_msg(dev, usb_sndctrlpipe(dev,0), 0xf1, 0x40, 0, indx, data, size, HZ);
#define pegasus_set_register(dev, indx, value)	\
	{ __u8	data = value;			\
	usb_control_msg(dev, usb_sndctrlpipe(dev,0), 0xf1, 0x40, data, indx, &data, 1, HZ);}


static int pegasus_read_phy_word(struct usb_device *dev, __u8 index, __u16 *regdata)
{
	int i;
	__u8 data[4] = { 1, 0, 0, 0x40 + index };

	pegasus_set_registers(dev, 0x25, 4, data);
	for (i = 0; i < 100; i++) {
		pegasus_get_registers(dev, 0x26, 3, data);
		if (data[2] & 0x80) {
			*regdata = *(__u16 *)(data);
			return 0;
		}
		udelay(100);
	}

	warn("read_phy_word() failed");
	return 1;
}

static int pegasus_write_phy_word(struct usb_device *dev, __u8 index, __u16 regdata)
{
	int i;
	__u8 data[4] = { 1, regdata, regdata >> 8, 0x20 + index };

	pegasus_set_registers(dev, 0x25, 4, data);
	for (i = 0; i < 100; i++) {
		pegasus_get_registers(dev, 0x28, 1, data);
		if (data[0] & 0x80)
			return 0;
		udelay(100);
	}

	warn("write_phy_word() failed");
	return 1;
}

static int pegasus_rw_srom_word(struct usb_device *dev, __u8 index, __u16 *retdata, __u8 direction)
{
	int i;
	__u8 data[4] = { index, 0, 0, direction };

	pegasus_set_registers(dev, 0x20, 4, data);
	for (i = 0; i < 100; i++) {
		pegasus_get_registers(dev, 0x23, 1, data);
		if (data[0] & 4) {
			pegasus_get_registers(dev, 0x21, 2, data);
			*retdata = *(__u16 *)data;
			return 0;
		}
	}

	warn("pegasus_rw_srom_word() failed");
	return 1;
}

static int pegasus_get_node_id(struct usb_device *dev, __u8 *id)
{
	int i;
	for (i = 0; i < 3; i++)
		if (pegasus_rw_srom_word(dev,i,(__u16 *)&id[i * 2],SROM_READ))
			return 1;
	return 0;
}

static int pegasus_reset_mac(struct usb_device *dev)
{
	__u8 data = 0x8;
	int i;

	pegasus_set_register(dev, 1, data);
	for (i = 0; i < 100; i++) {
		pegasus_get_registers(dev, 1, 1, &data);
		if (~data & 0x08) {
			if (loopback & 1) 
				return 0;
			if (loopback & 2) 
				pegasus_write_phy_word(dev, 0, 0x4000);
			pegasus_set_register(dev, 0x7e, 0x24);
			pegasus_set_register(dev, 0x7e, 0x27);
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

	if (pegasus_get_node_id(usb, node_id)) 
		return 1;

	pegasus_set_registers(usb, 0x10, 6, node_id);
	memcpy(dev->dev_addr, node_id, 6);
	if (pegasus_read_phy_word(usb, 1, &temp)) 
		return 2;

	if ((~temp & 4) && !loopback) {
		warn("%s: link NOT established (0x%x), check the cable.",
			dev->name, temp);
		/* return 3; FIXME */
	}

	if (pegasus_read_phy_word(usb, 5, &partmedia))
		return 4;

	if ((partmedia & 0x1f) != 1) {
		warn("party FAIL %x", partmedia);
		/* return 5;	FIXME */ 
	}

	data[0] = 0xc9;
	data[1] = (partmedia & 0x100) ? 0x30 : ((partmedia & 0x80) ? 0x10 : 0);
	data[2] = (loopback & 1) ? 0x08 : 0x00;

	pegasus_set_registers(usb, 0, 3, data);

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

	if (urb->status) {
		info("%s: RX status %d", net->name, urb->status);
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
	if ((res = usb_submit_urb(&pegasus->rx_urb)))
		warn("(prb)failed rx_urb %d", res);
}

static void pegasus_irq(urb_t *urb)
{
	if(urb->status) {
		__u8	*d = urb->transfer_buffer;
		printk("txst0 %x, txst1 %x, rxst %x, rxlst0 %x, rxlst1 %x, wakest %x",
			d[0], d[1], d[2], d[3], d[4], d[5]);
	}
}

static void pegasus_write_bulk(struct urb *urb)
{
	struct pegasus *pegasus = urb->context;

	spin_lock(&pegasus->pegasus_lock);

	if (urb->status)
		info("%s: TX status %d", pegasus->net->name, urb->status);
	netif_wake_queue(pegasus->net);

	spin_unlock(&pegasus->pegasus_lock);
}

static void pegasus_tx_timeout(struct net_device *net)
{
	struct pegasus *pegasus = net->priv;

	warn("%s: Tx timed out. Reseting...", net->name);
	pegasus->stats.tx_errors++;
	net->trans_start = jiffies;

	netif_wake_queue(net);
}

static int pegasus_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	struct pegasus	*pegasus = net->priv;
	int count = ((skb->len+2) & 0x3f) ? skb->len+2 : skb->len+3;
	int res;

	spin_lock(&pegasus->pegasus_lock);

	netif_stop_queue(net);

	((__u16 *)pegasus->tx_buff)[0] = skb->len;
	memcpy(pegasus->tx_buff+2, skb->data, skb->len);
	(&pegasus->tx_urb)->transfer_buffer_length = count;

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

	spin_unlock(&pegasus->pegasus_lock);

	return 0;
}

static struct net_device_stats *pegasus_netdev_stats(struct net_device *dev)
{
	return &((struct pegasus *)dev->priv)->stats;
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

/*	usb_submit_urb(&pegasus->intr_urb);*/
	netif_start_queue(net);

	MOD_INC_USE_COUNT;

	return 0;
}

static int pegasus_close(struct net_device *net)
{
	struct pegasus	*pegasus = net->priv;

	netif_stop_queue(net);

	usb_unlink_urb(&pegasus->rx_urb);
	usb_unlink_urb(&pegasus->tx_urb);
/*	usb_unlink_urb(&pegasus->intr_urb); */

	MOD_DEC_USE_COUNT;

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
			pegasus_read_phy_word(pegasus->usb, data[1] & 0x1f, &data[3]);
			return 0;
		case SIOCDEVPRIVATE+2:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			pegasus_write_phy_word(pegasus->usb, data[1] & 0x1f, data[2]);
			return 0;
		default:
			return -EOPNOTSUPP;
	}
}

static void pegasus_set_rx_mode(struct net_device *net)
{
	struct pegasus *pegasus = net->priv;

	netif_stop_queue(net);

	if (net->flags & IFF_PROMISC) {
		info("%s: Promiscuous mode enabled", net->name);
/*		pegasus_set_register(pegasus->usb, 2, 0x04); FIXME */
	} else if ((net->mc_count > multicast_filter_limit) ||
			(net->flags & IFF_ALLMULTI)) {
		pegasus_set_register(pegasus->usb, 0, 0xfa);
		pegasus_set_register(pegasus->usb, 2, 0);
		info("%s set allmulti", net->name);
	} else {
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

	if (pegasus_reset_mac(dev)) {
		err("can't reset MAC");
		kfree(pegasus);
		return NULL;
	}
	
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
	pegasus->pegasus_lock = SPIN_LOCK_UNLOCKED;

	FILL_BULK_URB(&pegasus->rx_urb, dev, usb_rcvbulkpipe(dev, 1),
			pegasus->rx_buff, PEGASUS_MAX_MTU, pegasus_read_bulk, 
			pegasus);
	FILL_BULK_URB(&pegasus->tx_urb, dev, usb_sndbulkpipe(dev, 2),
			pegasus->tx_buff, PEGASUS_MAX_MTU, pegasus_write_bulk,
			pegasus);
	FILL_INT_URB(&pegasus->intr_urb, dev, usb_rcvintpipe(dev, 3),
			pegasus->intr_buff, 8, pegasus_irq, pegasus, 250);


	printk(KERN_INFO "%s: %s\n", net->name, usb_dev_id[dev_indx].name);

	return pegasus;
}

static void pegasus_disconnect(struct usb_device *dev, void *ptr)
{
	struct pegasus *pegasus = ptr;

	if (!pegasus) {
		warn("unregistering non-existant device");
		return;
	}

	if (pegasus->net->flags & IFF_UP)
		dev_close(pegasus->net);

	unregister_netdev(pegasus->net);

	usb_unlink_urb(&pegasus->rx_urb);
	usb_unlink_urb(&pegasus->tx_urb);
/*	usb_unlink_urb(&pegasus->intr_urb);*/

	kfree(pegasus);
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
