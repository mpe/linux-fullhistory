/*
**
**	Pegasus: USB 10/100Mbps/HomePNA (1Mbps) Controller
**
**	Copyleft (L) 1999 Petko Manolov - Petkan (petkan@spct.net)
** 	
**	Distribute under GPL version 2 or later.
*/


#include <linux/module.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/usb.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#if LINUX_VERSION_CODE<0x2032d || !defined(__KERNEL__) || !defined(__OPTIMIZE__)
#error You can not compile this driver on this kernel with this C options!
#endif


#define	ADMTEK_VENDOR_ID	0x07a6
#define	ADMTEK_HPNA_PEGASUS	0x0986

#define	HPNA_MTU		1500
#define MAX_MTU			1536

#define	TX_TIMEOUT		(HZ*5)
#define	SOMETHING		(jiffies + TX_TIMEOUT)


static const char version[] = "pegasus.c: v0.2.27 2000/02/29 Written by Petko Manolov (petkan@spct.net)\n";


typedef struct usb_hpna
{
	struct usb_device	*usb_dev;
	struct net_device	*net_dev;
	int			present;
	int			active;
	void			*irq_handler;
	struct list_head	list;
	struct net_device_stats	stats;
	spinlock_t		hpna_lock;
	struct timer_list	timer;

	unsigned int		rx_pipe;
	unsigned char *		rx_buff;
	urb_t 			rx_urb;

	unsigned int		tx_pipe;
	unsigned char *		tx_buff;
	urb_t 			tx_urb;
	struct sk_buff *	tx_skbuff;

	__u8			intr_ival;
	unsigned int		intr_pipe;
	unsigned char 		intr_buff[8];
	urb_t 			intr_urb;
} usb_hpna_t;


usb_hpna_t	usb_dev_hpna;
static int	loopback = 0;
int		multicast_filter_limit = 32;
static LIST_HEAD(hpna_list);


MODULE_AUTHOR("Petko Manolov <petkan@spct.net>");
MODULE_DESCRIPTION("ADMtek \"Pegasus\" USB Ethernet driver");
MODULE_PARM(loopback, "i");



/*** vendor specific commands ***/
static __inline__ int	hpna_get_registers( struct usb_device *dev, __u16 indx, __u16 size, void *data )
{
	return	usb_control_msg(dev, usb_rcvctrlpipe(dev,0), 0xf0, 0xc0, 0,
				indx, data, size, HZ);
}


static __inline__ int	hpna_set_register( struct usb_device *dev, __u16 indx, __u8 value )
{
	__u8	data = value;
	return	usb_control_msg(dev, usb_sndctrlpipe(dev,0), 0xf1, 0x40,
				data, indx, &data, 1, HZ);
}


static __inline__ int	hpna_set_registers( struct usb_device *dev, __u16 indx, __u16 size, void *data )
{
	return	usb_control_msg(dev, usb_sndctrlpipe(dev,0), 0xf1, 0x40, 0,
				indx, data, size, HZ);
}


static int read_phy_word( struct usb_device *dev, __u8 index, __u16 *regdata )
{
	int	i;
	__u8	data[4];

	data[0] = 1;
	data[1] = 0;
	data[2] = 0;
	data[3] = 0x40 + index;
	hpna_set_registers( dev, 0x25, 4, data );
	for ( i=0; i<100; i++ ) {
		hpna_get_registers( dev, 0x25, 4, data );
		if ( data[3] & 0x80 ) {
			*regdata = *(__u16 *)(data+1);
			return	0;
		}
		udelay(100);
	}
	warn("read_phy_word() failed");
	return	1;
}


static int write_phy_word( struct usb_device *dev, __u8 index, __u16 regdata )
{
	int	i;
	__u8	data[4];

	data[0] = 1;
	data[1] = regdata;
	data[2] = regdata >> 8;
	data[3] = 0x20 + index;
	hpna_set_registers( dev, 0x25, 4, data );
	for ( i=0; i<100; i++ ) {
		hpna_get_registers( dev, 0x28, 1, data );
		if ( data[0] & 0x80 ) {
			return	0;
		}
		udelay(100);
	}
	warn("write_phy_word() failed");
	return	1;
}


int read_srom_word( struct usb_device *dev, __u8 index, __u16 *retdata)
{
	int	i;
	__u8	data[4];

	data[0] = index;
	data[1] = data[2] = 0;
	data[3] = 0x02;
	hpna_set_registers(dev, 0x20, 4, data);
	for ( i=0; i<100; i++ ) {
		hpna_get_registers(dev, 0x23, 1, data);
		if ( data[0] & 4 ) {
			hpna_get_registers(dev, 0x21, 2, data);
			*retdata = *(__u16 *)data;
			return	0;		
		}
	}
	warn("read_srom_word() failed");
	return	1;
}
/*** end ***/




int get_node_id( struct usb_device *dev, __u8 *id )
{
	int	i;

	for ( i=0; i<3; i++ ) {
		if ( read_srom_word(dev, i, (__u16 *)&id[i*2] ) )
			return	1;
	}
	return	0;
}


static int reset_mac( struct usb_device *dev )
{
	__u8	data = 0x8;
	int	i;
	
	hpna_set_register( dev, 1, 0x08 );
	for ( i=0; i<100; i++ ) {
		hpna_get_registers( dev, 1, 1, &data);
		if ( !(data & 0x08) ) {
			if ( loopback & 1 )
				return	0;
			else if ( loopback & 2 ) {
				write_phy_word( dev, 0, 0x4000 );
				/*return	0;*/
			}
			hpna_set_register( dev, 0x7e, 0x24 );
			hpna_set_register( dev, 0x7e, 0x27 );
			return	0;
		}
	}
	return 1;
}


int start_net( struct net_device *dev, struct usb_device *usb_dev )
{
	__u16	partmedia, temp;
	__u8	node_id[6];
	__u8	data[4];
	
	if ( get_node_id(usb_dev, node_id) )
		return	1;
	hpna_set_registers(usb_dev, 0x10, 6, node_id);
	memcpy(dev->dev_addr, node_id, 6);
	if ( read_phy_word(usb_dev, 1, &temp) )
		return	2;
	if ( !(temp & 4) ) {
		if ( loopback )
			goto ok;
		err("link NOT established - %x", temp);
		return	3;
	}
ok:
	if ( read_phy_word(usb_dev, 5, &partmedia) )
		return	4;
	temp = partmedia;
	partmedia &= 0x1f;
	if ( partmedia != 1 ) {
		err("party FAIL %x", temp);
		return	5;
	}
	partmedia = temp;
	if ( partmedia & 0x100 )
		data[1] = 0x30;
	else {
		if ( partmedia & 0x80 )
			data[1] = 0x10;
		else 
			data[1] = 0;
	}
	
	data[0] = 0xc9;
	data[2] = (loopback & 1) ? 0x08 : 0x00; 
	
	hpna_set_registers(usb_dev, 0, 3, data);
	
	return	0;
}


static void hpna_read_irq( purb_t urb )
{
	struct net_device *net_dev = urb->context;
	usb_hpna_t *hpna = net_dev->priv;
	int	count = urb->actual_length, res;
	int	rx_status = *(int *)(hpna->rx_buff + count - 4);


	if ( urb->status ) {
		info( "%s: RX status %d\n", net_dev->name, urb->status );
		goto goon;
	}

	if ( !count )
		goto goon;
/*	if ( rx_status & 0x00010000 )
		goto goon;
*/	
	if ( rx_status & 0x000e0000 ) {
		dbg("%s: error receiving packet %x",
			net_dev->name, rx_status & 0xe0000);
		hpna->stats.rx_errors++;
		if(rx_status & 0x060000) hpna->stats.rx_length_errors++;
		if(rx_status & 0x080000) hpna->stats.rx_crc_errors++;
		if(rx_status & 0x100000) hpna->stats.rx_frame_errors++;
	} else {
		struct sk_buff	*skb;
		__u16 		pkt_len = (rx_status & 0xfff) - 8;


		if((skb = dev_alloc_skb(pkt_len+2)) != NULL ) {
			skb->dev = net_dev;
			skb_reserve(skb, 2);
			eth_copy_and_sum(skb, hpna->rx_buff, pkt_len, 0);
			skb_put(skb, pkt_len);
		} else
			goto	goon;
		skb->protocol = eth_type_trans(skb, net_dev);
		netif_rx(skb);
		hpna->stats.rx_packets++;
		hpna->stats.rx_bytes += pkt_len;
	}
goon:
	if ( (res = usb_submit_urb( &hpna->rx_urb )) )
		warn("failed rx_urb %d", res);
}


static void hpna_irq( urb_t *urb)
{
	if( urb->status ) {
		__u8	*d = urb->transfer_buffer;
		printk("txst0 %x, txst1 %x, rxst %x, rxlst0 %x, rxlst1 %x, wakest %x",
			d[0], d[1], d[2], d[3], d[4], d[5] );
	}
}


static void hpna_write_irq( purb_t urb )
{
	struct net_device *net_dev = urb->context;
	usb_hpna_t *hpna = net_dev->priv;


	spin_lock( &hpna->hpna_lock );
	
	if ( urb->status ) 
		info("%s: TX status %d\n", net_dev->name, urb->status);
	netif_wake_queue( net_dev );

	spin_unlock( &hpna->hpna_lock );
}


static void tx_timeout( struct net_device *dev )
{
	usb_hpna_t	*hpna = dev->priv;

	warn( "%s: Tx timed out. Reseting...", dev->name );
	hpna->stats.tx_errors++;
	dev->trans_start = jiffies;
	netif_wake_queue( dev );
}


static int hpna_start_xmit( struct sk_buff *skb, struct net_device *net_dev )
{
	usb_hpna_t	*hpna = (usb_hpna_t *)net_dev->priv;
	int		count = skb->len+2 % 64 ? skb->len+2 : skb->len+3;
	int		res;

	spin_lock( &hpna->hpna_lock );

	netif_stop_queue( net_dev );
	((__u16 *)hpna->tx_buff)[0] = skb->len;
	memcpy(hpna->tx_buff+2, skb->data, skb->len);
	(&hpna->tx_urb)->transfer_buffer_length = count;
	if ( (res = usb_submit_urb( &hpna->tx_urb )) ) {
		warn("failed tx_urb %d", res);
		hpna->stats.tx_errors++;
		netif_start_queue( net_dev );
	} else {
		hpna->stats.tx_packets++;
		hpna->stats.tx_bytes += skb->len;
		net_dev->trans_start = jiffies;
	}
	dev_kfree_skb( skb );
	spin_unlock( &hpna->hpna_lock );
	return	0;
}


static struct net_device_stats *hpna_netdev_stats( struct net_device *dev )
{
	return	&((usb_hpna_t *)dev->priv)->stats;
}

static int hpna_open( struct net_device *net_dev )
{
	usb_hpna_t *hpna = (usb_hpna_t *)net_dev->priv;
	int	res;

	if ( hpna->active )
		return	-EBUSY;
	else
		hpna->active = 1;

	if ( start_net(net_dev, hpna->usb_dev) ) {
		err("can't start_net()");
		return	-EIO;
	}

	if ( (res = usb_submit_urb( &hpna->rx_urb )) )
		warn("failed rx_urb %d", res);

/*	usb_submit_urb( &hpna->intr_urb );*/
	netif_start_queue( net_dev );

	MOD_INC_USE_COUNT;

	return 0;
}


static int hpna_close( struct net_device *net_dev )
{
	usb_hpna_t	*hpna = net_dev->priv;


	netif_stop_queue( net_dev );

	usb_unlink_urb( &hpna->rx_urb );
	usb_unlink_urb( &hpna->tx_urb );
/*	usb_unlink_urb( hpna->intr_urb );*/

	hpna->active = 0;

	MOD_DEC_USE_COUNT;

	return 0;
}


static int hpna_ioctl( struct net_device *dev, struct ifreq *rq, int cmd )
{
	__u16	*data = (__u16 *)&rq->ifr_data;
	usb_hpna_t	*hpna = dev->priv;

	switch( cmd ) {
		case SIOCDEVPRIVATE: 
			data[0] = 1;
		case SIOCDEVPRIVATE+1:
			read_phy_word(hpna->usb_dev, data[1] & 0x1f, &data[3]);
			return	0;
		case SIOCDEVPRIVATE+2:
			if ( !capable(CAP_NET_ADMIN) )
				return	-EPERM;
			write_phy_word(hpna->usb_dev, data[1] & 0x1f, data[2]);
			return	0;
		default:
			return	-EOPNOTSUPP;
	}
}


static void set_rx_mode( struct net_device *net_dev )
{
	usb_hpna_t	*hpna=net_dev->priv;

	netif_stop_queue( net_dev );
	
	if ( net_dev->flags & IFF_PROMISC ) {
		info("%s: Promiscuous mode enabled", net_dev->name);
		hpna_set_register( hpna->usb_dev, 2, 0x04 );
	} else if ((net_dev->mc_count > multicast_filter_limit) ||
			(net_dev->flags & IFF_ALLMULTI)) {
		hpna_set_register(hpna->usb_dev, 0, 0xfa);
		hpna_set_register(hpna->usb_dev, 2, 0);
	} else {
		dbg("%s: set Rx mode", net_dev->name);
	}

	netif_wake_queue( net_dev );
}


static void * usb_hpna_probe( struct usb_device *dev, unsigned int ifnum )
{
	struct net_device 		*net_dev;
	usb_hpna_t			*hpna = &usb_dev_hpna;



	if ( dev->descriptor.idVendor != ADMTEK_VENDOR_ID ||
	     dev->descriptor.idProduct != ADMTEK_HPNA_PEGASUS ) {
		return	NULL;
	}

	printk("USB HPNA Pegasus found\n");

	if ( usb_set_configuration(dev, dev->config[0].bConfigurationValue)) {
		err("usb_set_configuration() failed");
		return NULL;
	}

	hpna->usb_dev = dev;

	hpna->rx_pipe = usb_rcvbulkpipe(hpna->usb_dev, 1);
	hpna->tx_pipe = usb_sndbulkpipe(hpna->usb_dev, 2);
	hpna->intr_pipe = usb_rcvintpipe(hpna->usb_dev, 0);

	if ( reset_mac(dev) ) {
		err("can't reset MAC");
	}

	hpna->present = 1;

	if(!(hpna->rx_buff=kmalloc(MAX_MTU, GFP_KERNEL))) {
		err("not enough mem for out buff");
		return	NULL;
	}
	if(!(hpna->tx_buff=kmalloc(MAX_MTU, GFP_KERNEL))) {
		kfree_s(hpna->rx_buff, MAX_MTU);
		err("not enough mem for out buff");
		return	NULL;
	}

	net_dev = init_etherdev( 0, 0 );
	hpna->net_dev = net_dev;
	net_dev->priv = hpna;
	net_dev->open = hpna_open;
	net_dev->stop = hpna_close;
	net_dev->watchdog_timeo = TX_TIMEOUT;
	net_dev->tx_timeout = tx_timeout;
	net_dev->do_ioctl = hpna_ioctl; 
	net_dev->hard_start_xmit = hpna_start_xmit;
	net_dev->set_multicast_list = set_rx_mode;
	net_dev->get_stats = hpna_netdev_stats; 
	net_dev->mtu = HPNA_MTU;
	hpna->hpna_lock = SPIN_LOCK_UNLOCKED;
	
	FILL_BULK_URB( &hpna->rx_urb, hpna->usb_dev, hpna->rx_pipe, 
			hpna->rx_buff, MAX_MTU, hpna_read_irq, net_dev );
	FILL_BULK_URB( &hpna->tx_urb, hpna->usb_dev, hpna->tx_pipe, 
			hpna->tx_buff, MAX_MTU, hpna_write_irq, net_dev );
	FILL_INT_URB( &hpna->intr_urb, hpna->usb_dev, hpna->intr_pipe,
			hpna->intr_buff, 8, hpna_irq, net_dev, 250 );
	
/*	list_add( &hpna->list, &hpna_list );*/
	
	return	net_dev; 
}


static void usb_hpna_disconnect( struct usb_device *dev, void *ptr )
{
	struct net_device	*net_dev = ptr;
	struct usb_hpna		*hpna = net_dev->priv;


	if ( net_dev->flags & IFF_UP )
		dev_close(net_dev);
	
	unregister_netdev( net_dev );

	if ( !hpna ) /* should never happen */
		return;
		
	usb_unlink_urb( &hpna->rx_urb );
	usb_unlink_urb( &hpna->tx_urb );
/*	usb_unlink_urb( &hpna->intr_urb );*/
	kfree_s(hpna->rx_buff, MAX_MTU);
	kfree_s(hpna->tx_buff, MAX_MTU);

	hpna->usb_dev = NULL;
	hpna->present = 0;

	printk("USB HPNA disconnected\n");
}


static struct usb_driver usb_hpna_driver = {
	"ADMtek \"Pegasus\" USB Ethernet",
	usb_hpna_probe,
	usb_hpna_disconnect,
	{NULL, NULL}
};



static int  __init start_hpna( void )
{
	printk( version );
	return	usb_register( &usb_hpna_driver );
}


static void __exit stop_hpna( void )
{
	usb_deregister( &usb_hpna_driver );
}


module_init( start_hpna );
module_exit( stop_hpna );
