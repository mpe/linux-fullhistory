/*
**	Pegasus: USB 10/100Mbps/HomePNA (1Mbps) Controller
**
**	Copyright (c) 1999-2002 Petko Manolov (petkan@users.sourceforge.net)
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
**			Some long standing bugs (enable_net_traffic) fixed.
**			Also nasty trick about resubmiting control urb from
**			interrupt context used. Please let me know how it
**			behaves. Pegasus II support added since this version.
**			TODO: suppressing HCD warnings spewage on disconnect.
**		v0.4.13	Ethernet address is now set at probe(), not at open()
**			time as this seems to break dhcpd. 
**		v0.5.0	branch to 2.5.x kernels
**		v0.5.1	ethtool support added
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

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/module.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include "pegasus.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.5.4 (2002/04/11)"
#define DRIVER_AUTHOR "Petko Manolov <petkan@users.sourceforge.net>"
#define DRIVER_DESC "Pegasus/Pegasus II USB Ethernet driver"

static const char driver_name [] = "pegasus";

#define	PEGASUS_USE_INTR
#define	PEGASUS_WRITE_EEPROM
#define	BMSR_MEDIA	(BMSR_10HALF | BMSR_10FULL | BMSR_100HALF | \
			BMSR_100FULL | BMSR_ANEGCAPABLE)

static int loopback = 0;
static int mii_mode = 0;
static int multicast_filter_limit = 32;

static struct usb_eth_dev usb_dev_id[] = {
#define	PEGASUS_DEV(pn, vid, pid, flags)	\
	{name:pn, vendor:vid, device:pid, private:flags},
#include "pegasus.h"
#undef	PEGASUS_DEV
	{NULL, 0, 0, 0}
};

static struct usb_device_id pegasus_ids[] = {
#define	PEGASUS_DEV(pn, vid, pid, flags) \
	{match_flags: USB_DEVICE_ID_MATCH_DEVICE, idVendor:vid, idProduct:pid},
#include "pegasus.h"
#undef	PEGASUS_DEV
	{}
};

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_PARM(loopback, "i");
MODULE_PARM(mii_mode, "i");
MODULE_PARM_DESC(loopback, "Enable MAC loopback mode (bit 0)");
MODULE_PARM_DESC(mii_mode, "Enable HomePNA mode (bit 0),default=MII mode = 0");

MODULE_DEVICE_TABLE(usb, pegasus_ids);

static int update_eth_regs_async(pegasus_t *);
/* Aargh!!! I _really_ hate such tweaks */
static void ctrl_callback(struct urb *urb)
{
	pegasus_t *pegasus = urb->context;

	if (!pegasus)
		return;

	switch (urb->status) {
	case 0:
		if (pegasus->flags & ETH_REGS_CHANGE) {
			pegasus->flags &= ~ETH_REGS_CHANGE;
			pegasus->flags |= ETH_REGS_CHANGED;
			update_eth_regs_async(pegasus);
			return;
		}
		break;
	case -EINPROGRESS:
		return;
	case -ENOENT:
		break;
	default:
		warn("%s: status %d", __FUNCTION__, urb->status);
	}
	pegasus->flags &= ~ETH_REGS_CHANGED;
	wake_up(&pegasus->ctrl_wait);
}

static int get_registers(pegasus_t * pegasus, __u16 indx, __u16 size,
			 void *data)
{
	int ret;
	unsigned char *buffer;
	DECLARE_WAITQUEUE(wait, current);

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer) {
		err("unable to allocate memory for configuration descriptors");
		return 0;
	}
	memcpy(buffer, data, size);

	add_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);
	while (pegasus->flags & ETH_REGS_CHANGED)
		schedule();
	remove_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_RUNNING);

	pegasus->dr.bRequestType = PEGASUS_REQT_READ;
	pegasus->dr.bRequest = PEGASUS_REQ_GET_REGS;
	pegasus->dr.wValue = cpu_to_le16(0);
	pegasus->dr.wIndex = cpu_to_le16p(&indx);
	pegasus->dr.wLength = cpu_to_le16p(&size);
	pegasus->ctrl_urb->transfer_buffer_length = size;

	FILL_CONTROL_URB(pegasus->ctrl_urb, pegasus->usb,
			 usb_rcvctrlpipe(pegasus->usb, 0),
			 (char *) &pegasus->dr,
			 buffer, size, ctrl_callback, pegasus);

	add_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);

	/* using ATOMIC, we'd never wake up if we slept */
	if ((ret = usb_submit_urb(pegasus->ctrl_urb, GFP_ATOMIC))) {
		err("%s: BAD CTRLs %d", __FUNCTION__, ret);
		goto out;
	}

	schedule();
out:
	remove_wait_queue(&pegasus->ctrl_wait, &wait);
	memcpy(data, buffer, size);
	kfree(buffer);

	return ret;
}

static int set_registers(pegasus_t * pegasus, __u16 indx, __u16 size,
			 void *data)
{
	int ret;
	unsigned char *buffer;
	DECLARE_WAITQUEUE(wait, current);

	buffer = kmalloc(size, GFP_KERNEL);
	if (!buffer) {
		err("unable to allocate memory for configuration descriptors");
		return 0;
	}
	memcpy(buffer, data, size);

	add_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);
	while (pegasus->flags & ETH_REGS_CHANGED)
		schedule();
	remove_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_RUNNING);

	pegasus->dr.bRequestType = PEGASUS_REQT_WRITE;
	pegasus->dr.bRequest = PEGASUS_REQ_SET_REGS;
	pegasus->dr.wValue = cpu_to_le16(0);
	pegasus->dr.wIndex = cpu_to_le16p(&indx);
	pegasus->dr.wLength = cpu_to_le16p(&size);
	pegasus->ctrl_urb->transfer_buffer_length = size;

	FILL_CONTROL_URB(pegasus->ctrl_urb, pegasus->usb,
			 usb_sndctrlpipe(pegasus->usb, 0),
			 (char *) &pegasus->dr,
			 buffer, size, ctrl_callback, pegasus);

	add_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);

	if ((ret = usb_submit_urb(pegasus->ctrl_urb, GFP_ATOMIC))) {
		err("%s: BAD CTRL %d", __FUNCTION__, ret);
		goto out;
	}

	schedule();
out:
	remove_wait_queue(&pegasus->ctrl_wait, &wait);
	kfree(buffer);

	return ret;
}

static int set_register(pegasus_t * pegasus, __u16 indx, __u8 data)
{
	int ret;
	unsigned char *buffer;
	__u16 dat = data;
	DECLARE_WAITQUEUE(wait, current);

	buffer = kmalloc(1, GFP_KERNEL);
	if (!buffer) {
		err("unable to allocate memory for configuration descriptors");
		return 0;
	}
	memcpy(buffer, &data, 1);

	add_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);
	while (pegasus->flags & ETH_REGS_CHANGED)
		schedule();
	remove_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_RUNNING);

	pegasus->dr.bRequestType = PEGASUS_REQT_WRITE;
	pegasus->dr.bRequest = PEGASUS_REQ_SET_REG;
	pegasus->dr.wValue = cpu_to_le16p(&dat);
	pegasus->dr.wIndex = cpu_to_le16p(&indx);
	pegasus->dr.wLength = cpu_to_le16(1);
	pegasus->ctrl_urb->transfer_buffer_length = 1;

	FILL_CONTROL_URB(pegasus->ctrl_urb, pegasus->usb,
			 usb_sndctrlpipe(pegasus->usb, 0),
			 (char *) &pegasus->dr,
			 buffer, 1, ctrl_callback, pegasus);

	add_wait_queue(&pegasus->ctrl_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);

	if ((ret = usb_submit_urb(pegasus->ctrl_urb, GFP_ATOMIC))) {
		err("%s: BAD CTRL %d", __FUNCTION__, ret);
		goto out;
	}

	schedule();
out:
	remove_wait_queue(&pegasus->ctrl_wait, &wait);
	kfree(buffer);

	return ret;
}

static int update_eth_regs_async(pegasus_t * pegasus)
{
	int ret;

	pegasus->dr.bRequestType = PEGASUS_REQT_WRITE;
	pegasus->dr.bRequest = PEGASUS_REQ_SET_REGS;
	pegasus->dr.wValue = 0;
	pegasus->dr.wIndex = cpu_to_le16(EthCtrl0);
	pegasus->dr.wLength = cpu_to_le16(3);
	pegasus->ctrl_urb->transfer_buffer_length = 3;

	FILL_CONTROL_URB(pegasus->ctrl_urb, pegasus->usb,
			 usb_sndctrlpipe(pegasus->usb, 0),
			 (char *) &pegasus->dr,
			 pegasus->eth_regs, 3, ctrl_callback, pegasus);

	if ((ret = usb_submit_urb(pegasus->ctrl_urb, GFP_ATOMIC)))
		err("%s: BAD CTRL %d, flgs %x", __FUNCTION__, ret,
		    pegasus->flags);

	return ret;
}

static int read_mii_word(pegasus_t * pegasus, __u8 phy, __u8 indx, __u16 * regd)
{
	int i;
	__u8 data[4] = { phy, 0, 0, indx };
	__u16 regdi;

	set_register(pegasus, PhyCtrl, 0);
	set_registers(pegasus, PhyAddr, sizeof(data), data);
	set_register(pegasus, PhyCtrl, (indx | PHY_READ));
	for (i = 0; i < REG_TIMEOUT; i++) {
		get_registers(pegasus, PhyCtrl, 1, data);
		if (data[0] & PHY_DONE)
			break;
	}
	if (i < REG_TIMEOUT) {
		get_registers(pegasus, PhyData, 2, &regdi);
		*regd = le16_to_cpu(regdi);
		return 0;
	}
	warn("%s: failed", __FUNCTION__);

	return 1;
}

static int write_mii_word(pegasus_t * pegasus, __u8 phy, __u8 indx, __u16 regd)
{
	int i;
	__u8 data[4] = { phy, 0, 0, indx };

	*(data + 1) = cpu_to_le16p(&regd);
	set_register(pegasus, PhyCtrl, 0);
	set_registers(pegasus, PhyAddr, 4, data);
	set_register(pegasus, PhyCtrl, (indx | PHY_WRITE));
	for (i = 0; i < REG_TIMEOUT; i++) {
		get_registers(pegasus, PhyCtrl, 1, data);
		if (data[0] & PHY_DONE)
			break;
	}
	if (i < REG_TIMEOUT)
		return 0;
	warn("%s: failed", __FUNCTION__);

	return 1;
}

static int read_eprom_word(pegasus_t * pegasus, __u8 index, __u16 * retdata)
{
	int i;
	__u8 tmp;
	__u16 retdatai;

	set_register(pegasus, EpromCtrl, 0);
	set_register(pegasus, EpromOffset, index);
	set_register(pegasus, EpromCtrl, EPROM_READ);

	for (i = 0; i < REG_TIMEOUT; i++) {
		get_registers(pegasus, EpromCtrl, 1, &tmp);
		if (tmp & EPROM_DONE)
			break;
	}
	if (i < REG_TIMEOUT) {
		get_registers(pegasus, EpromData, 2, &retdatai);
		*retdata = le16_to_cpu(retdatai);
		return 0;
	}
	warn("%s: failed", __FUNCTION__);

	return -1;
}

#ifdef	PEGASUS_WRITE_EEPROM
static inline void enable_eprom_write(pegasus_t * pegasus)
{
	__u8 tmp;

	get_registers(pegasus, EthCtrl2, 1, &tmp);
	set_register(pegasus, EthCtrl2, tmp | EPROM_WR_ENABLE);
}

static inline void disable_eprom_write(pegasus_t * pegasus)
{
	__u8 tmp;

	get_registers(pegasus, EthCtrl2, 1, &tmp);
	set_register(pegasus, EpromCtrl, 0);
	set_register(pegasus, EthCtrl2, tmp & ~EPROM_WR_ENABLE);
}

static int write_eprom_word(pegasus_t * pegasus, __u8 index, __u16 data)
{
	int i, tmp;
	__u8 d[4] = { 0x3f, 0, 0, EPROM_WRITE };

	set_registers(pegasus, EpromOffset, 4, d);
	enable_eprom_write(pegasus);
	set_register(pegasus, EpromOffset, index);
	set_registers(pegasus, EpromData, 2, &data);
	set_register(pegasus, EpromCtrl, EPROM_WRITE);

	for (i = 0; i < REG_TIMEOUT; i++) {
		get_registers(pegasus, EpromCtrl, 1, &tmp);
		if (tmp & EPROM_DONE)
			break;
	}
	disable_eprom_write(pegasus);
	if (i < REG_TIMEOUT)
		return 0;
	warn("%s: failed", __FUNCTION__);
	return -1;
}
#endif				/* PEGASUS_WRITE_EEPROM */

static inline void get_node_id(pegasus_t * pegasus, __u8 * id)
{
	int i;
	__u16 w16;

	for (i = 0; i < 3; i++) {
		read_eprom_word(pegasus, i, &w16);
		((__u16 *) id)[i] = cpu_to_le16p(&w16);
	}
}

static void set_ethernet_addr(pegasus_t * pegasus)
{
	__u8 node_id[6];

	get_node_id(pegasus, node_id);
	set_registers(pegasus, EthID, sizeof(node_id), node_id);
	memcpy(pegasus->net->dev_addr, node_id, sizeof(node_id));
}

static inline int reset_mac(pegasus_t * pegasus)
{
	__u8 data = 0x8;
	int i;

	set_register(pegasus, EthCtrl1, data);
	for (i = 0; i < REG_TIMEOUT; i++) {
		get_registers(pegasus, EthCtrl1, 1, &data);
		if (~data & 0x08) {
			if (loopback & 1)
				break;
			if (mii_mode && (pegasus->features & HAS_HOME_PNA))
				set_register(pegasus, Gpio1, 0x34);
			else
				set_register(pegasus, Gpio1, 0x26);
			set_register(pegasus, Gpio0, pegasus->features);
			set_register(pegasus, Gpio0, DEFAULT_GPIO_SET);
			break;
		}
	}
	if (i == REG_TIMEOUT)
		return 1;

	if (usb_dev_id[pegasus->dev_index].vendor == VENDOR_LINKSYS ||
	    usb_dev_id[pegasus->dev_index].vendor == VENDOR_DLINK) {
		set_register(pegasus, Gpio0, 0x24);
		set_register(pegasus, Gpio0, 0x26);
	}
	if (usb_dev_id[pegasus->dev_index].vendor == VENDOR_ELCON) {
		__u16 auxmode;
		read_mii_word(pegasus, 3, 0x1b, &auxmode);
		write_mii_word(pegasus, 3, 0x1b, auxmode | 4);
	}

	return 0;
}

static int enable_net_traffic(struct net_device *dev, struct usb_device *usb)
{
	__u16 linkpart;
	__u8 data[4];
	pegasus_t *pegasus = dev->priv;

	read_mii_word(pegasus, pegasus->phy, MII_LPA, &linkpart);
	data[0] = 0xc9;
	data[1] = 0;
	if (linkpart & (ADVERTISE_100FULL | ADVERTISE_10FULL))
		data[1] |= 0x20;	/* set full duplex */
	if (linkpart & (ADVERTISE_100FULL | ADVERTISE_100HALF))
		data[1] |= 0x10;	/* set 100 Mbps */
	if (mii_mode)
		data[1] = 0;
	data[2] = (loopback & 1) ? 0x09 : 0x01;

	memcpy(pegasus->eth_regs, data, sizeof(data));
	set_registers(pegasus, EthCtrl0, 3, data);

	if (usb_dev_id[pegasus->dev_index].vendor == VENDOR_LINKSYS ||
	    usb_dev_id[pegasus->dev_index].vendor == VENDOR_DLINK) {
		u16 auxmode;
		read_mii_word(pegasus, 0, 0x1b, &auxmode);
		write_mii_word(pegasus, 0, 0x1b, auxmode | 4);
	}

	return 0;
}

static void read_bulk_callback(struct urb *urb)
{
	pegasus_t *pegasus = urb->context;
	struct net_device *net;
	int count = urb->actual_length;
	int rx_status;
	struct sk_buff *skb;
	__u16 pkt_len;

	if (!pegasus || !(pegasus->flags & PEGASUS_RUNNING))
		return;

	net = pegasus->net;
	if (!netif_device_present(net))
		return;

	switch (urb->status) {
	case 0:
		break;
	case -ETIMEDOUT:
		dbg("reset MAC");
		pegasus->flags &= ~PEGASUS_RX_BUSY;
		break;
	case -ENOENT:
		return;
	default:
		dbg("%s: RX status %d", net->name, urb->status);
		goto goon;
	}

	if (!count)
		goto goon;

	rx_status = le32_to_cpu(*(int *)(urb->transfer_buffer + count - 4));
	if (rx_status & 0x000e0000) {
		dbg("%s: RX packet error %x", net->name, rx_status & 0xe0000);
		pegasus->stats.rx_errors++;
		if (rx_status & 0x060000)
			pegasus->stats.rx_length_errors++;
		if (rx_status & 0x080000)
			pegasus->stats.rx_crc_errors++;
		if (rx_status & 0x100000)
			pegasus->stats.rx_frame_errors++;
		goto goon;
	}
	pkt_len = (rx_status & 0xfff) - 8;

	if (!pegasus->rx_skb)
		goto tl_sched;

	skb_put(pegasus->rx_skb, pkt_len);
	pegasus->rx_skb->protocol = eth_type_trans(pegasus->rx_skb, net);
	netif_rx(pegasus->rx_skb);

	if (!(skb = dev_alloc_skb(PEGASUS_MTU + 2))) {
		pegasus->rx_skb = NULL;
		goto tl_sched;
	}
	
	skb->dev = net;
	skb_reserve(skb, 2);
	pegasus->rx_skb = skb;
	pegasus->stats.rx_packets++;
	pegasus->stats.rx_bytes += pkt_len;
goon:
	FILL_BULK_URB(pegasus->rx_urb, pegasus->usb,
		      usb_rcvbulkpipe(pegasus->usb, 1),
		      pegasus->rx_skb->data, PEGASUS_MTU + 8,
		      read_bulk_callback, pegasus);
	if (usb_submit_urb(pegasus->rx_urb, GFP_ATOMIC)) {
		pegasus->flags |= PEGASUS_RX_URB_FAIL;
		goto tl_sched;
	} else {
		pegasus->flags &= ~PEGASUS_RX_URB_FAIL;
	}
	
	return;
	
tl_sched:
	tasklet_schedule(&pegasus->rx_tl);
}

static void rx_fixup(unsigned long data)
{
	pegasus_t *pegasus;

	pegasus = (pegasus_t *)data;

	if (pegasus->flags & PEGASUS_RX_URB_FAIL)
		if (pegasus->rx_skb)
			goto try_again;

	if (!(pegasus->rx_skb = dev_alloc_skb(PEGASUS_MTU + 2))) {
		tasklet_schedule(&pegasus->rx_tl);
		return;
	}
	FILL_BULK_URB(pegasus->rx_urb, pegasus->usb,
 	              usb_rcvbulkpipe(pegasus->usb, 1),
	              pegasus->rx_skb->data, PEGASUS_MTU + 8,
	              read_bulk_callback, pegasus);	
try_again:
	if (usb_submit_urb(pegasus->rx_urb, GFP_ATOMIC)) {
		pegasus->flags |= PEGASUS_RX_URB_FAIL;
		tasklet_schedule(&pegasus->rx_tl);
	} else {
		pegasus->flags &= ~PEGASUS_RX_URB_FAIL;
	}
}

static void write_bulk_callback(struct urb *urb)
{
	pegasus_t *pegasus = urb->context;

	if (!pegasus || !(pegasus->flags & PEGASUS_RUNNING))
		return;

	if (!netif_device_present(pegasus->net))
		return;

	if (urb->status)
		info("%s: TX status %d", pegasus->net->name, urb->status);

	pegasus->net->trans_start = jiffies;
	netif_wake_queue(pegasus->net);
}

#ifdef	PEGASUS_USE_INTR
static void intr_callback(struct urb *urb)
{
	pegasus_t *pegasus = urb->context;
	struct net_device *net;
	__u8 *d;

	if (!pegasus)
		return;

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
		return;
	default:
		info("intr status %d", urb->status);
	}

	d = urb->transfer_buffer;
	net = pegasus->net;
	if (d[0] & 0xfc) {
		pegasus->stats.tx_errors++;
		if (d[0] & TX_UNDERRUN)
			pegasus->stats.tx_fifo_errors++;
		if (d[0] & (EXCESSIVE_COL | JABBER_TIMEOUT))
			pegasus->stats.tx_aborted_errors++;
		if (d[0] & LATE_COL)
			pegasus->stats.tx_window_errors++;
		if (d[0] & (NO_CARRIER | LOSS_CARRIER)) {
			pegasus->stats.tx_carrier_errors++;
			netif_carrier_off(net);
		} else {
			netif_carrier_on(net);
		}
	}
}
#endif

static void pegasus_tx_timeout(struct net_device *net)
{
	pegasus_t *pegasus = net->priv;

	if (!pegasus)
		return;

	warn("%s: Tx timed out.", net->name);
	pegasus->tx_urb->transfer_flags |= USB_ASYNC_UNLINK;
	usb_unlink_urb(pegasus->tx_urb);
	pegasus->stats.tx_errors++;
}

static int pegasus_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	pegasus_t *pegasus = net->priv;
	int count = ((skb->len + 2) & 0x3f) ? skb->len + 2 : skb->len + 3;
	int res;
	__u16 l16 = skb->len;

	netif_stop_queue(net);

	((__u16 *) pegasus->tx_buff)[0] = cpu_to_le16(l16);
	memcpy(pegasus->tx_buff + 2, skb->data, skb->len);
	FILL_BULK_URB(pegasus->tx_urb, pegasus->usb,
		      usb_sndbulkpipe(pegasus->usb, 2),
		      pegasus->tx_buff, count,
		      write_bulk_callback, pegasus);
	if ((res = usb_submit_urb(pegasus->tx_urb, GFP_ATOMIC))) {
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
	return &((pegasus_t *) dev->priv)->stats;
}

static inline void disable_net_traffic(pegasus_t * pegasus)
{
	int tmp = 0;

	set_registers(pegasus, EthCtrl0, 2, &tmp);
}

static inline void get_interrupt_interval(pegasus_t * pegasus)
{
	__u8 data[2];

	read_eprom_word(pegasus, 4, (__u16 *) data);
	if (data[1] < 0x80) {
		info("intr interval will be changed from %ums to %ums",
		     data[1], 0x80);
		data[1] = 0x80;
#ifdef	PEGASUS_WRITE_EEPROM
		write_eprom_word(pegasus, 4, *(__u16 *) data);
#endif
	}
	pegasus->intr_interval = data[1];
}

static void set_carrier(struct net_device *net)
{
	pegasus_t *pegasus;
	short tmp;

	pegasus = net->priv;
	read_mii_word(pegasus, pegasus->phy, MII_BMSR, &tmp);
	if (tmp & BMSR_LSTATUS)
		netif_carrier_on(net);
	else
		netif_carrier_off(net);

}

static int pegasus_open(struct net_device *net)
{
	pegasus_t *pegasus = (pegasus_t *) net->priv;
	int res;

	if (!(pegasus->rx_skb = dev_alloc_skb(PEGASUS_MTU + 2)))
		return -ENOMEM;
	pegasus->rx_skb->dev = net;
	skb_reserve(pegasus->rx_skb, 2);

	down(&pegasus->sem);
	FILL_BULK_URB(pegasus->rx_urb, pegasus->usb,
		      usb_rcvbulkpipe(pegasus->usb, 1),
		      pegasus->rx_skb->data, PEGASUS_MTU + 8,
		      read_bulk_callback, pegasus);
	if ((res = usb_submit_urb(pegasus->rx_urb, GFP_KERNEL)))
		warn("%s: failed rx_urb %d", __FUNCTION__, res);
#ifdef	PEGASUS_USE_INTR
	FILL_INT_URB(pegasus->intr_urb, pegasus->usb,
		     usb_rcvintpipe(pegasus->usb, 3),
		     pegasus->intr_buff, sizeof(pegasus->intr_buff),
		     intr_callback, pegasus, pegasus->intr_interval);
	if ((res = usb_submit_urb(pegasus->intr_urb, GFP_KERNEL)))
		warn("%s: failed intr_urb %d", __FUNCTION__, res);
#endif
	netif_start_queue(net);
	pegasus->flags |= PEGASUS_RUNNING;
	if ((res = enable_net_traffic(net, pegasus->usb))) {
		err("can't enable_net_traffic() - %d", res);
		res = -EIO;
		goto exit;
	}
	set_carrier(net);
	res = 0;
exit:
	up(&pegasus->sem);

	return res;
}

static int pegasus_close(struct net_device *net)
{
	pegasus_t *pegasus = net->priv;

	down(&pegasus->sem);
	pegasus->flags &= ~PEGASUS_RUNNING;
	netif_stop_queue(net);
	if (!(pegasus->flags & PEGASUS_UNPLUG))
		disable_net_traffic(pegasus);

	usb_unlink_urb(pegasus->rx_urb);
	usb_unlink_urb(pegasus->tx_urb);
	usb_unlink_urb(pegasus->ctrl_urb);
#ifdef	PEGASUS_USE_INTR
	usb_unlink_urb(pegasus->intr_urb);
#endif
	up(&pegasus->sem);

	return 0;
}

static int pegasus_ethtool_ioctl(struct net_device *net, void *uaddr)
{
	pegasus_t *pegasus;
	int cmd;

	pegasus = net->priv;
	if (get_user(cmd, (int *) uaddr))
		return -EFAULT;
	switch (cmd) {
	case ETHTOOL_GDRVINFO:{
			struct ethtool_drvinfo info = { ETHTOOL_GDRVINFO };
			strncpy(info.driver, driver_name, sizeof info.driver);
			strncpy(info.version, DRIVER_VERSION,
				ETHTOOL_BUSINFO_LEN);
			usb_make_path(pegasus->usb, info.bus_info,
				sizeof info.bus_info);
			if (copy_to_user(uaddr, &info, sizeof(info)))
				return -EFAULT;
			return 0;
		}
	case ETHTOOL_GSET:{
			struct ethtool_cmd ecmd;
			short lpa, bmcr;

			memset(&ecmd, 0, sizeof ecmd);
			ecmd.supported = (SUPPORTED_10baseT_Half |
					  SUPPORTED_10baseT_Full |
					  SUPPORTED_100baseT_Half |
					  SUPPORTED_100baseT_Full |
					  SUPPORTED_Autoneg |
					  SUPPORTED_TP | SUPPORTED_MII);
			ecmd.port = PORT_TP;
			ecmd.transceiver = XCVR_INTERNAL;
			ecmd.phy_address = pegasus->phy;
			read_mii_word(pegasus, pegasus->phy, MII_BMCR, &bmcr);
			read_mii_word(pegasus, pegasus->phy, MII_LPA, &lpa);
			if (bmcr & BMCR_ANENABLE) {
				ecmd.autoneg = AUTONEG_ENABLE;
				ecmd.speed = lpa & (LPA_100HALF | LPA_100FULL) ?
				    SPEED_100 : SPEED_10;
				if (ecmd.speed == SPEED_100)
					ecmd.duplex = lpa & LPA_100FULL ?
					    DUPLEX_FULL : DUPLEX_HALF;
				else
					ecmd.duplex = lpa & LPA_10FULL ?
					    DUPLEX_FULL : DUPLEX_HALF;
			} else {
				ecmd.autoneg = AUTONEG_DISABLE;
				ecmd.speed = bmcr & BMCR_SPEED100 ?
				    SPEED_100 : SPEED_10;
				ecmd.duplex = bmcr & BMCR_FULLDPLX ?
				    DUPLEX_FULL : DUPLEX_HALF;
			}
			if (copy_to_user(uaddr, &ecmd, sizeof(ecmd)))
				return -EFAULT;

			return 0;
		}
	case ETHTOOL_SSET:{
			return -EOPNOTSUPP;
		}
	case ETHTOOL_GLINK:{
			struct ethtool_value edata = { ETHTOOL_GLINK };
			edata.data = netif_carrier_ok(net);
			if (copy_to_user(uaddr, &edata, sizeof(edata)))
				return -EFAULT;
			return 0;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static int pegasus_ioctl(struct net_device *net, struct ifreq *rq, int cmd)
{
	__u16 *data = (__u16 *) & rq->ifr_data;
	pegasus_t *pegasus = net->priv;
	int res;

	down(&pegasus->sem);
	switch (cmd) {
	case SIOCETHTOOL:
		res = pegasus_ethtool_ioctl(net, rq->ifr_data);
		break;
	case SIOCDEVPRIVATE:
		data[0] = pegasus->phy;
	case SIOCDEVPRIVATE + 1:
		read_mii_word(pegasus, data[0], data[1] & 0x1f, &data[3]);
		res = 0;
		break;
	case SIOCDEVPRIVATE + 2:
		if (!capable(CAP_NET_ADMIN)) {
			up(&pegasus->sem);
			return -EPERM;
		}
		write_mii_word(pegasus, pegasus->phy, data[1] & 0x1f, data[2]);
		res = 0;
		break;
	default:
		res = -EOPNOTSUPP;
	}
	up(&pegasus->sem);
	return res;
}

static void pegasus_set_multicast(struct net_device *net)
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
	}

	pegasus->flags |= ETH_REGS_CHANGE;
	ctrl_callback(pegasus->ctrl_urb);

	netif_wake_queue(net);
}

static __u8 mii_phy_probe(pegasus_t * pegasus)
{
	int i;
	__u16 tmp;

	for (i = 0; i < 32; i++) {
		read_mii_word(pegasus, i, MII_BMSR, &tmp);
		if (tmp == 0 || tmp == 0xffff || (tmp & BMSR_MEDIA) == 0)
			continue;
		else
			return i;
	}

	return 0xff;
}

static inline void setup_pegasus_II(pegasus_t * pegasus)
{
	set_register(pegasus, Reg1d, 0);
	set_register(pegasus, Reg7b, 2);
	if (pegasus->features & HAS_HOME_PNA && mii_mode)
		set_register(pegasus, Reg81, 6);
	else
		set_register(pegasus, Reg81, 2);
}

static void *pegasus_probe(struct usb_device *dev, unsigned int ifnum,
			   const struct usb_device_id *id)
{
	struct net_device *net;
	pegasus_t *pegasus;
	int dev_index = id - pegasus_ids;

	if (usb_set_configuration(dev, dev->config[0].bConfigurationValue)) {
		err("usb_set_configuration() failed");
		return NULL;
	}
	if (!(pegasus = kmalloc(sizeof(struct pegasus), GFP_KERNEL))) {
		err("out of memory allocating device structure");
		return NULL;
	}

	usb_get_dev(dev);
	memset(pegasus, 0, sizeof(struct pegasus));
	pegasus->dev_index = dev_index;
	init_waitqueue_head(&pegasus->ctrl_wait);

	pegasus->ctrl_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pegasus->ctrl_urb) {
		kfree(pegasus);
		return NULL;
	}
	pegasus->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pegasus->rx_urb) {
		usb_free_urb(pegasus->ctrl_urb);
		kfree(pegasus);
		return NULL;
	}
	pegasus->tx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pegasus->tx_urb) {
		usb_free_urb(pegasus->rx_urb);
		usb_free_urb(pegasus->ctrl_urb);
		kfree(pegasus);
		return NULL;
	}
	pegasus->intr_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pegasus->intr_urb) {
		usb_free_urb(pegasus->tx_urb);
		usb_free_urb(pegasus->rx_urb);
		usb_free_urb(pegasus->ctrl_urb);
		kfree(pegasus);
		return NULL;
	}

	net = init_etherdev(NULL, 0);
	if (!net) {
		usb_free_urb(pegasus->tx_urb);
		usb_free_urb(pegasus->rx_urb);
		usb_free_urb(pegasus->ctrl_urb);
		kfree(pegasus);
		return NULL;
	}

	init_MUTEX(&pegasus->sem);
	tasklet_init(&pegasus->rx_tl, rx_fixup, (unsigned long)pegasus);
	
	down(&pegasus->sem);
	pegasus->usb = dev;
	pegasus->net = net;
	SET_MODULE_OWNER(net);
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

	pegasus->features = usb_dev_id[dev_index].private;
#ifdef	PEGASUS_USE_INTR
	get_interrupt_interval(pegasus);
#endif
	if (reset_mac(pegasus)) {
		err("can't reset MAC");
		unregister_netdev(pegasus->net);
		usb_free_urb(pegasus->tx_urb);
		usb_free_urb(pegasus->rx_urb);
		usb_free_urb(pegasus->ctrl_urb);
		kfree(pegasus->net);
		kfree(pegasus);
		pegasus = NULL;
		goto exit;
	}

	info("%s: %s", net->name, usb_dev_id[dev_index].name);

	set_ethernet_addr(pegasus);

	if (pegasus->features & PEGASUS_II) {
		info("setup Pegasus II specific registers");
		setup_pegasus_II(pegasus);
	}

	pegasus->phy = mii_phy_probe(pegasus);
	if (pegasus->phy == 0xff) {
		warn("can't locate MII phy, using default");
		pegasus->phy = 1;
	}
exit:
	up(&pegasus->sem);
	return pegasus;
}

static void pegasus_disconnect(struct usb_device *dev, void *ptr)
{
	struct pegasus *pegasus = ptr;

	if (!pegasus) {
		warn("unregistering non-existant device");
		return;
	}

	pegasus->flags |= PEGASUS_UNPLUG;
	unregister_netdev(pegasus->net);
	usb_put_dev(dev);
	usb_unlink_urb(pegasus->intr_urb);
	usb_unlink_urb(pegasus->tx_urb);
	usb_unlink_urb(pegasus->rx_urb);
	usb_unlink_urb(pegasus->ctrl_urb);
	usb_free_urb(pegasus->intr_urb);
	usb_free_urb(pegasus->tx_urb);
	usb_free_urb(pegasus->rx_urb);
	usb_free_urb(pegasus->ctrl_urb);
	if (pegasus->rx_skb)
		dev_kfree_skb(pegasus->rx_skb);
	kfree(pegasus->net);
	kfree(pegasus);
	pegasus = NULL;
}

static struct usb_driver pegasus_driver = {
	name:		driver_name,
	probe:		pegasus_probe,
	disconnect:	pegasus_disconnect,
	id_table:	pegasus_ids,
};

int __init pegasus_init(void)
{
	info(DRIVER_VERSION ":" DRIVER_DESC);
	return usb_register(&pegasus_driver);
}

void __exit pegasus_exit(void)
{
	usb_deregister(&pegasus_driver);
}

module_init(pegasus_init);
module_exit(pegasus_exit);
