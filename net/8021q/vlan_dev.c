/*
 * INET		802.1Q VLAN
 *		Ethernet-type device handling.
 *
 * Authors:	Ben Greear <greearb@candelatech.com>
 *              Please send support related email to: vlan@scry.wanfear.com
 *              VLAN Home Page: http://www.candelatech.com/~greear/vlan.html
 * 
 * Fixes:       Mar 22 2001: Martin Bokaemper <mbokaemper@unispherenetworks.com>
 *                - reset skb->pkt_type on incoming packets when MAC was changed
 *                - see that changed MAC is saddr for outgoing packets
 *              Oct 20, 2001:  Ard van Breeman:
 *                - Fix MC-list, finally.
 *                - Flush MC-list on VLAN destroy.
 *                
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/in.h>
#include <linux/init.h>
#include <asm/uaccess.h> /* for copy_from_user */
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/datalink.h>
#include <net/p8022.h>
#include <net/arp.h>
#include <linux/brlock.h>

#include "vlan.h"
#include "vlanproc.h"
#include <linux/if_vlan.h>
#include <net/ip.h>

struct net_device_stats *vlan_dev_get_stats(struct net_device *dev)
{
	return &(((struct vlan_dev_info *)(dev->priv))->dev_stats);
}


/*
 *	Rebuild the Ethernet MAC header. This is called after an ARP
 *	(or in future other address resolution) has completed on this
 *	sk_buff. We now let ARP fill in the other fields.
 *
 *	This routine CANNOT use cached dst->neigh!
 *	Really, it is used only when dst->neigh is wrong.
 *
 * TODO:  This needs a checkup, I'm ignorant here. --BLG
 */
int vlan_dev_rebuild_header(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct vlan_ethhdr *veth = (struct vlan_ethhdr *)(skb->data);

	switch (veth->h_vlan_encapsulated_proto) {
#ifdef CONFIG_INET
	case __constant_htons(ETH_P_IP):

		/* TODO:  Confirm this will work with VLAN headers... */
		return arp_find(veth->h_dest, skb);
#endif	
	default:
		printk(VLAN_DBG
		       "%s: unable to resolve type %X addresses.\n", 
		       dev->name, (int)veth->h_vlan_encapsulated_proto);
	 
		memcpy(veth->h_source, dev->dev_addr, ETH_ALEN);
		break;
	};

	return 0;
}

/*
 *	Determine the packet's protocol ID. The rule here is that we 
 *	assume 802.3 if the type field is short enough to be a length.
 *	This is normal practice and works for any 'now in use' protocol.
 *
 *  Also, at this point we assume that we ARE dealing exclusively with
 *  VLAN packets, or packets that should be made into VLAN packets based
 *  on a default VLAN ID.
 *
 *  NOTE:  Should be similar to ethernet/eth.c.
 *
 *  SANITY NOTE:  This method is called when a packet is moving up the stack
 *                towards userland.  To get here, it would have already passed
 *                through the ethernet/eth.c eth_type_trans() method.
 *  SANITY NOTE 2: We are referencing to the VLAN_HDR frields, which MAY be
 *                 stored UNALIGNED in the memory.  RISC systems don't like
 *                 such cases very much...
 *  SANITY NOTE 2a:  According to Dave Miller & Alexey, it will always be aligned,
 *                 so there doesn't need to be any of the unaligned stuff.  It has
 *                 been commented out now...  --Ben
 *
 */
int vlan_skb_recv(struct sk_buff *skb, struct net_device *dev,
                  struct packet_type* ptype)
{
	unsigned char *rawp = NULL;
	struct vlan_hdr *vhdr = (struct vlan_hdr *)(skb->data);
	unsigned short vid;
	struct net_device_stats *stats;
	unsigned short vlan_TCI;
	unsigned short proto;

	/* vlan_TCI = ntohs(get_unaligned(&vhdr->h_vlan_TCI)); */
	vlan_TCI = ntohs(vhdr->h_vlan_TCI);

	vid = (vlan_TCI & 0xFFF);

#ifdef VLAN_DEBUG
	printk(VLAN_DBG __FUNCTION__ ": skb: %p vlan_id: %hx\n",
	       skb, vid);
#endif

	/* Ok, we will find the correct VLAN device, strip the header,
	 * and then go on as usual.
	 */

	/* we have 12 bits of vlan ID. */
	/* If it's NULL, we will tag it to be junked below */
	skb->dev = find_802_1Q_vlan_dev(dev, vid);

	if (!skb->dev) {
#ifdef VLAN_DEBUG
		printk(VLAN_DBG __FUNCTION__ ": ERROR:	No net_device for VID: %i on dev: %s [%i]\n",
		       (unsigned int)(vid), dev->name, dev->ifindex);
#endif
		kfree_skb(skb);
		return -1;
	}

	/* Bump the rx counters for the VLAN device. */
	stats = vlan_dev_get_stats(skb->dev);
	stats->rx_packets++;
	stats->rx_bytes += skb->len;

	skb_pull(skb, VLAN_HLEN); /* take off the VLAN header (4 bytes currently) */

	/* Ok, lets check to make sure the device (dev) we
	 * came in on is what this VLAN is attached to.
	 */

	if (dev != VLAN_DEV_INFO(skb->dev)->real_dev) {
#ifdef VLAN_DEBUG
		printk(VLAN_DBG __FUNCTION__ ": dropping skb: %p because came in on wrong device, dev: %s  real_dev: %s, skb_dev: %s\n",
		       skb, dev->name, VLAN_DEV_INFO(skb->dev)->real_dev->name, skb->dev->name);
#endif
		kfree_skb(skb);
		stats->rx_errors++;
		return -1;
	}

	/*
	 * Deal with ingress priority mapping.
	 */
	skb->priority = VLAN_DEV_INFO(skb->dev)->ingress_priority_map[(ntohs(vhdr->h_vlan_TCI) >> 13) & 0x7];

#ifdef VLAN_DEBUG
	printk(VLAN_DBG __FUNCTION__ ": priority: %lu  for TCI: %hu (hbo)\n",
	       (unsigned long)(skb->priority), ntohs(vhdr->h_vlan_TCI));
#endif

	/* The ethernet driver already did the pkt_type calculations
	 * for us...
	 */
	switch (skb->pkt_type) {
	case PACKET_BROADCAST: /* Yeah, stats collect these together.. */
		// stats->broadcast ++; // no such counter :-(
	case PACKET_MULTICAST:
		stats->multicast++;
		break;
	case PACKET_OTHERHOST: 
		/* Our lower layer thinks this is not local, let's make sure.
		 * This allows the VLAN to have a different MAC than the underlying
		 * device, and still route correctly.
		 */
		if (memcmp(skb->mac.ethernet->h_dest, skb->dev->dev_addr, ETH_ALEN) == 0) {
			/* It is for our (changed) MAC-address! */
			skb->pkt_type = PACKET_HOST;
		}
		break;
	default:
		break;
	};

	/*  Was a VLAN packet, grab the encapsulated protocol, which the layer
	 * three protocols care about.
	 */
	/* proto = get_unaligned(&vhdr->h_vlan_encapsulated_proto); */
	proto = vhdr->h_vlan_encapsulated_proto;

	skb->protocol = proto;
	if (ntohs(proto) >= 1536) {
		/* place it back on the queue to be handled by
		 * true layer 3 protocols.
		 */

		/* See if we are configured to re-write the VLAN header
		 * to make it look like ethernet...
		 */
		skb = vlan_check_reorder_header(skb);

		/* Can be null if skb-clone fails when re-ordering */
		if (skb) {
			netif_rx(skb);
		} else {
			/* TODO:  Add a more specific counter here. */
			stats->rx_errors++;
		}
		return 0;
	}

	rawp = skb->data;

	/*
	 * This is a magic hack to spot IPX packets. Older Novell breaks
	 * the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 * layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 * won't work for fault tolerant netware but does for the rest.
	 */
	if (*(unsigned short *)rawp == 0xFFFF) {
		skb->protocol = __constant_htons(ETH_P_802_3);
		/* place it back on the queue to be handled by true layer 3 protocols.
		 */

		/* See if we are configured to re-write the VLAN header
		 * to make it look like ethernet...
		 */
		skb = vlan_check_reorder_header(skb);

		/* Can be null if skb-clone fails when re-ordering */
		if (skb) {
			netif_rx(skb);
		} else {
			/* TODO:  Add a more specific counter here. */
			stats->rx_errors++;
		}
		return 0;
	}

	/*
	 *	Real 802.2 LLC
	 */
	skb->protocol = __constant_htons(ETH_P_802_2);
	/* place it back on the queue to be handled by upper layer protocols.
	 */

	/* See if we are configured to re-write the VLAN header
	 * to make it look like ethernet...
	 */
	skb = vlan_check_reorder_header(skb);

	/* Can be null if skb-clone fails when re-ordering */
	if (skb) {
		netif_rx(skb);
	} else {
		/* TODO:  Add a more specific counter here. */
		stats->rx_errors++;
	}
	return 0;
}

/*
 *	Create the VLAN header for an arbitrary protocol layer 
 *
 *	saddr=NULL	means use device source address
 *	daddr=NULL	means leave destination address (eg unresolved arp)
 *
 *  This is called when the SKB is moving down the stack towards the
 *  physical devices.
 */
int vlan_dev_hard_header(struct sk_buff *skb, struct net_device *dev,
                         unsigned short type, void *daddr, void *saddr,
                         unsigned len)
{
	struct vlan_hdr *vhdr;
	unsigned short veth_TCI = 0;
	int rc = 0;
	int build_vlan_header = 0;
	struct net_device *vdev = dev; /* save this for the bottom of the method */

#ifdef VLAN_DEBUG
	printk(VLAN_DBG __FUNCTION__ ": skb: %p type: %hx len: %x vlan_id: %hx, daddr: %p\n",
	       skb, type, len, VLAN_DEV_INFO(dev)->vlan_id, daddr);
#endif

	/* build vlan header only if re_order_header flag is NOT set.  This
	 * fixes some programs that get confused when they see a VLAN device
	 * sending a frame that is VLAN encoded (the consensus is that the VLAN
	 * device should look completely like an Ethernet device when the
	 * REORDER_HEADER flag is set)	The drawback to this is some extra 
	 * header shuffling in the hard_start_xmit.  Users can turn off this
	 * REORDER behaviour with the vconfig tool.
	 */
	build_vlan_header = ((VLAN_DEV_INFO(dev)->flags & 1) == 0);

	if (build_vlan_header) {
		vhdr = (struct vlan_hdr *) skb_push(skb, VLAN_HLEN);

		/* build the four bytes that make this a VLAN header. */

		/* Now, construct the second two bytes. This field looks something
		 * like:
		 * usr_priority: 3 bits	 (high bits)
		 * CFI		 1 bit
		 * VLAN ID	 12 bits (low bits)
		 *
		 */
		veth_TCI = VLAN_DEV_INFO(dev)->vlan_id;
		veth_TCI |= vlan_dev_get_egress_qos_mask(dev, skb);

		vhdr->h_vlan_TCI = htons(veth_TCI);

		/*
		 *  Set the protocol type.
		 *  For a packet of type ETH_P_802_3 we put the length in here instead.
		 *  It is up to the 802.2 layer to carry protocol information.
		 */

		if (type != ETH_P_802_3) {
			vhdr->h_vlan_encapsulated_proto = htons(type);
		} else {
			vhdr->h_vlan_encapsulated_proto = htons(len);
		}
	}

	/* Before delegating work to the lower layer, enter our MAC-address */
	if (saddr == NULL)
		saddr = dev->dev_addr;

	dev = VLAN_DEV_INFO(dev)->real_dev;

	/* MPLS can send us skbuffs w/out enough space.	 This check will grow the
	 * skb if it doesn't have enough headroom.  Not a beautiful solution, so
	 * I'll tick a counter so that users can know it's happening...	 If they
	 * care...
	 */

	/* NOTE:  This may still break if the underlying device is not the final
	 * device (and thus there are more headers to add...)  It should work for
	 * good-ole-ethernet though.
	 */
	if (skb_headroom(skb) < dev->hard_header_len) {
		struct sk_buff *sk_tmp = skb;
		skb = skb_realloc_headroom(sk_tmp, dev->hard_header_len);
		kfree_skb(sk_tmp);
		if (skb == NULL) {
			struct net_device_stats *stats = vlan_dev_get_stats(vdev);
			stats->tx_dropped++;
			return -ENOMEM;
		}
		VLAN_DEV_INFO(vdev)->cnt_inc_headroom_on_tx++;
#ifdef VLAN_DEBUG
		printk(VLAN_DBG __FUNCTION__ ": %s: had to grow skb.\n", vdev->name);
#endif
	}

	if (build_vlan_header) {
		/* Now make the underlying real hard header */
		rc = dev->hard_header(skb, dev, ETH_P_8021Q, daddr, saddr, len + VLAN_HLEN);

		if (rc > 0) {
			rc += VLAN_HLEN;
		} else if (rc < 0) {
			rc -= VLAN_HLEN;
		}
	} else {
		/* If here, then we'll just make a normal looking ethernet frame,
		 * but, the hard_start_xmit method will insert the tag (it has to
		 * be able to do this for bridged and other skbs that don't come
		 * down the protocol stack in an orderly manner.
		 */
		rc = dev->hard_header(skb, dev, type, daddr, saddr, len);
	}

	return rc;
}

int vlan_dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device_stats *stats = vlan_dev_get_stats(dev);
	struct vlan_ethhdr *veth = (struct vlan_ethhdr *)(skb->data);

	/* Handle non-VLAN frames if they are sent to us, for example by DHCP.
	 *
	 * NOTE: THIS ASSUMES DIX ETHERNET, SPECIFICALLY NOT SUPPORTING
	 * OTHER THINGS LIKE FDDI/TokenRing/802.3 SNAPs...
	 */

	if (veth->h_vlan_proto != __constant_htons(ETH_P_8021Q)) {
		/* This is not a VLAN frame...but we can fix that! */
		unsigned short veth_TCI = 0;
		VLAN_DEV_INFO(dev)->cnt_encap_on_xmit++;

#ifdef VLAN_DEBUG
		printk(VLAN_DBG __FUNCTION__ ": proto to encap: 0x%hx (hbo)\n",
		       htons(veth->h_vlan_proto));
#endif

		if (skb_headroom(skb) < VLAN_HLEN) {
			struct sk_buff *sk_tmp = skb;
			skb = skb_realloc_headroom(sk_tmp, VLAN_HLEN);
			kfree_skb(sk_tmp);
			if (skb == NULL) {
				stats->tx_dropped++;
				return -ENOMEM;
			}
			VLAN_DEV_INFO(dev)->cnt_inc_headroom_on_tx++;
		} else {
			if (!(skb = skb_unshare(skb, GFP_ATOMIC))) {
				printk(KERN_ERR "vlan: failed to unshare skbuff\n");
				stats->tx_dropped++;
				return -ENOMEM;
			}
		}
		veth = (struct vlan_ethhdr *)skb_push(skb, VLAN_HLEN);

		/* Move the mac addresses to the beginning of the new header. */
		memmove(skb->data, skb->data + VLAN_HLEN, 12);

		/* first, the ethernet type */
		/* put_unaligned(__constant_htons(ETH_P_8021Q), &veth->h_vlan_proto); */
		veth->h_vlan_proto = __constant_htons(ETH_P_8021Q);

		/* Now, construct the second two bytes. This field looks something
		 * like:
		 * usr_priority: 3 bits	 (high bits)
		 * CFI		 1 bit
		 * VLAN ID	 12 bits (low bits)
		 */
		veth_TCI = VLAN_DEV_INFO(dev)->vlan_id;
		veth_TCI |= vlan_dev_get_egress_qos_mask(dev, skb);

		veth->h_vlan_TCI = htons(veth_TCI);
	}

	skb->dev = VLAN_DEV_INFO(dev)->real_dev;

#ifdef VLAN_DEBUG
	printk(VLAN_DBG __FUNCTION__ ": about to send skb: %p to dev: %s\n",
	       skb, skb->dev->name);
	printk(VLAN_DBG "  %2hx.%2hx.%2hx.%2xh.%2hx.%2hx %2hx.%2hx.%2hx.%2hx.%2hx.%2hx %4hx %4hx %4hx\n",
	       veth->h_dest[0], veth->h_dest[1], veth->h_dest[2], veth->h_dest[3], veth->h_dest[4], veth->h_dest[5],
	       veth->h_source[0], veth->h_source[1], veth->h_source[2], veth->h_source[3], veth->h_source[4], veth->h_source[5],
	       veth->h_vlan_proto, veth->h_vlan_TCI, veth->h_vlan_encapsulated_proto);
#endif

	dev_queue_xmit(skb);
	stats->tx_packets++; /* for statics only */
	stats->tx_bytes += skb->len;
	return 0;
}

int vlan_dev_change_mtu(struct net_device *dev, int new_mtu)
{
	/* TODO: gotta make sure the underlying layer can handle it,
	 * maybe an IFF_VLAN_CAPABLE flag for devices?
	 */
	if (VLAN_DEV_INFO(dev)->real_dev->mtu < new_mtu)
		return -ERANGE;

	dev->mtu = new_mtu;

	return new_mtu;
}

int vlan_dev_open(struct net_device *dev)
{
	if (!(VLAN_DEV_INFO(dev)->real_dev->flags & IFF_UP))
		return -ENETDOWN;

	return 0;
}

int vlan_dev_stop(struct net_device *dev)
{
	vlan_flush_mc_list(dev);
	return 0;
}

int vlan_dev_init(struct net_device *dev)
{
	/* TODO:  figure this out, maybe do nothing?? */
	return 0;
}

void vlan_dev_destruct(struct net_device *dev)
{
	if (dev) {
		vlan_flush_mc_list(dev);
		if (dev->priv) {
			dev_put(VLAN_DEV_INFO(dev)->real_dev);
			if (VLAN_DEV_INFO(dev)->dent) {
				printk(KERN_ERR __FUNCTION__ ": dent is NOT NULL!\n");

				/* If we ever get here, there is a serious bug
				 * that must be fixed.
				 */
			}

			kfree(dev->priv);

			VLAN_FMEM_DBG("dev->priv free, addr: %p\n", dev->priv);
			dev->priv = NULL;
		}
	}
}

int vlan_dev_set_ingress_priority(char *dev_name, __u32 skb_prio, short vlan_prio)
{
	struct net_device *dev = dev_get_by_name(dev_name);

	if (dev) {
		if (dev->priv_flags & IFF_802_1Q_VLAN) {
			/* see if a priority mapping exists.. */
			VLAN_DEV_INFO(dev)->ingress_priority_map[vlan_prio & 0x7] = skb_prio;
			dev_put(dev);
			return 0;
		}

		dev_put(dev);
	}
	return -EINVAL;
}

int vlan_dev_set_egress_priority(char *dev_name, __u32 skb_prio, short vlan_prio)
{
	struct net_device *dev = dev_get_by_name(dev_name);
	struct vlan_priority_tci_mapping *mp = NULL;
	struct vlan_priority_tci_mapping *np;
   
	if (dev) {
		if (dev->priv_flags & IFF_802_1Q_VLAN) {
			/* See if a priority mapping exists.. */
			mp = VLAN_DEV_INFO(dev)->egress_priority_map[skb_prio & 0xF];
			while (mp) {
				if (mp->priority == skb_prio) {
					mp->vlan_qos = ((vlan_prio << 13) & 0xE000);
					dev_put(dev);
					return 0;
				}
			}

			/* Create a new mapping then. */
			mp = VLAN_DEV_INFO(dev)->egress_priority_map[skb_prio & 0xF];
			np = kmalloc(sizeof(struct vlan_priority_tci_mapping), GFP_KERNEL);
			if (np) {
				np->next = mp;
				np->priority = skb_prio;
				np->vlan_qos = ((vlan_prio << 13) & 0xE000);
				VLAN_DEV_INFO(dev)->egress_priority_map[skb_prio & 0xF] = np;
				dev_put(dev);
				return 0;
			} else {
				dev_put(dev);
				return -ENOBUFS;
			}
		}
		dev_put(dev);
	}
	return -EINVAL;
}

/* Flags are defined in the vlan_dev_info class in include/linux/if_vlan.h file. */
int vlan_dev_set_vlan_flag(char *dev_name, __u32 flag, short flag_val)
{
	struct net_device *dev = dev_get_by_name(dev_name);

	if (dev) {
		if (dev->priv_flags & IFF_802_1Q_VLAN) {
			/* verify flag is supported */
			if (flag == 1) {
				if (flag_val) {
					VLAN_DEV_INFO(dev)->flags |= 1;
				} else {
					VLAN_DEV_INFO(dev)->flags &= ~1;
				}
				dev_put(dev);
				return 0;
			} else {
				printk(KERN_ERR __FUNCTION__ ": flag %i is not valid.\n",
				       (int)(flag));
				dev_put(dev);
				return -EINVAL;
			}
		} else {
			printk(KERN_ERR __FUNCTION__
			       ": %s is not a vlan device, priv_flags: %hX.\n",
			       dev->name, dev->priv_flags);
			dev_put(dev);
		}
	} else {
		printk(KERN_ERR __FUNCTION__ ": Could not find device: %s\n", dev_name);
	}

	return -EINVAL;
}

int vlan_dev_set_mac_address(struct net_device *dev, void *addr_struct_p)
{
	struct sockaddr *addr = (struct sockaddr *)(addr_struct_p);
	int i;

	if (netif_running(dev))
		return -EBUSY;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	printk("%s: Setting MAC address to ", dev->name);
	for (i = 0; i < 6; i++)
		printk(" %2.2x", dev->dev_addr[i]);
	printk(".\n");

	if (memcmp(VLAN_DEV_INFO(dev)->real_dev->dev_addr,
		   dev->dev_addr,
		   dev->addr_len) != 0) {
		if (!(VLAN_DEV_INFO(dev)->real_dev->flags & IFF_PROMISC)) {
			int flgs = VLAN_DEV_INFO(dev)->real_dev->flags;

			/* Increment our in-use promiscuity counter */
			dev_set_promiscuity(VLAN_DEV_INFO(dev)->real_dev, 1);

			/* Make PROMISC visible to the user. */
			flgs |= IFF_PROMISC;
			printk("VLAN (%s):  Setting underlying device (%s) to promiscious mode.\n",
			       dev->name, VLAN_DEV_INFO(dev)->real_dev->name);
			dev_change_flags(VLAN_DEV_INFO(dev)->real_dev, flgs);
		}
	} else {
		printk("VLAN (%s):  Underlying device (%s) has same MAC, not checking promiscious mode.\n",
		       dev->name, VLAN_DEV_INFO(dev)->real_dev->name);
	}

	return 0;
}

/** Taken from Gleb + Lennert's VLAN code, and modified... */
void vlan_dev_set_multicast_list(struct net_device *vlan_dev)
{
	struct dev_mc_list *dmi;
	struct net_device *real_dev;
	int inc;

	if (vlan_dev && (vlan_dev->priv_flags & IFF_802_1Q_VLAN)) {
		/* Then it's a real vlan device, as far as we can tell.. */
		real_dev = VLAN_DEV_INFO(vlan_dev)->real_dev;

		/* compare the current promiscuity to the last promisc we had.. */
		inc = vlan_dev->promiscuity - VLAN_DEV_INFO(vlan_dev)->old_promiscuity;
		if (inc) {
			printk(KERN_INFO "%s: dev_set_promiscuity(master, %d)\n",
			       vlan_dev->name, inc);
			dev_set_promiscuity(real_dev, inc); /* found in dev.c */
			VLAN_DEV_INFO(vlan_dev)->old_promiscuity = vlan_dev->promiscuity;
		}

		inc = vlan_dev->allmulti - VLAN_DEV_INFO(vlan_dev)->old_allmulti;
		if (inc) {
			printk(KERN_INFO "%s: dev_set_allmulti(master, %d)\n",
			       vlan_dev->name, inc);
			dev_set_allmulti(real_dev, inc); /* dev.c */
			VLAN_DEV_INFO(vlan_dev)->old_allmulti = vlan_dev->allmulti;
		}

		/* looking for addresses to add to master's list */
		for (dmi = vlan_dev->mc_list; dmi != NULL; dmi = dmi->next) {
			if (vlan_should_add_mc(dmi, VLAN_DEV_INFO(vlan_dev)->old_mc_list)) {
				dev_mc_add(real_dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
				printk(KERN_INFO "%s: add %.2x:%.2x:%.2x:%.2x:%.2x:%.2x mcast address to master interface\n",
				       vlan_dev->name,
				       dmi->dmi_addr[0],
				       dmi->dmi_addr[1],
				       dmi->dmi_addr[2],
				       dmi->dmi_addr[3],
				       dmi->dmi_addr[4],
				       dmi->dmi_addr[5]);
			}
		}

		/* looking for addresses to delete from master's list */
		for (dmi = VLAN_DEV_INFO(vlan_dev)->old_mc_list; dmi != NULL; dmi = dmi->next) {
			if (vlan_should_add_mc(dmi, vlan_dev->mc_list)) {
				/* if we think we should add it to the new list, then we should really
				 * delete it from the real list on the underlying device.
				 */
				dev_mc_delete(real_dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
				printk(KERN_INFO "%s: del %.2x:%.2x:%.2x:%.2x:%.2x:%.2x mcast address from master interface\n",
				       vlan_dev->name,
				       dmi->dmi_addr[0],
				       dmi->dmi_addr[1],
				       dmi->dmi_addr[2],
				       dmi->dmi_addr[3],
				       dmi->dmi_addr[4],
				       dmi->dmi_addr[5]);
			}
		}

		/* save multicast list */
		vlan_copy_mc_list(vlan_dev->mc_list, VLAN_DEV_INFO(vlan_dev));
	}
}

/** dmi is a single entry into a dev_mc_list, a single node.  mc_list is
 *  an entire list, and we'll iterate through it.
 */
int vlan_should_add_mc(struct dev_mc_list *dmi, struct dev_mc_list *mc_list)
{
	struct dev_mc_list *idmi;

	for (idmi = mc_list; idmi != NULL; ) {
		if (vlan_dmi_equals(dmi, idmi)) {
			if (dmi->dmi_users > idmi->dmi_users)
				return 1;
			else
				return 0;
		} else {
			idmi = idmi->next;
		}
	}

	return 1;
}

void vlan_copy_mc_list(struct dev_mc_list *mc_list, struct vlan_dev_info *vlan_info)
{
	struct dev_mc_list *dmi, *new_dmi;

	vlan_destroy_mc_list(vlan_info->old_mc_list);
	vlan_info->old_mc_list = NULL;

	for (dmi = mc_list; dmi != NULL; dmi = dmi->next) {
		new_dmi = kmalloc(sizeof(*new_dmi), GFP_ATOMIC);
		if (new_dmi == NULL) {
			printk(KERN_ERR "vlan: cannot allocate memory. "
			       "Multicast may not work properly from now.\n");
			return;
		}

		/* Copy whole structure, then make new 'next' pointer */
		*new_dmi = *dmi;
		new_dmi->next = vlan_info->old_mc_list;
		vlan_info->old_mc_list = new_dmi;
	}
}

void vlan_flush_mc_list(struct net_device *dev)
{
	struct dev_mc_list *dmi = dev->mc_list;

	while (dmi) {
		dev_mc_delete(dev, dmi->dmi_addr, dmi->dmi_addrlen, 0);
		printk(KERN_INFO "%s: del %.2x:%.2x:%.2x:%.2x:%.2x:%.2x mcast address from vlan interface\n",
		       dev->name,
		       dmi->dmi_addr[0],
		       dmi->dmi_addr[1],
		       dmi->dmi_addr[2],
		       dmi->dmi_addr[3],
		       dmi->dmi_addr[4],
		       dmi->dmi_addr[5]);
		dmi = dev->mc_list;
	}

	/* dev->mc_list is NULL by the time we get here. */
	vlan_destroy_mc_list(VLAN_DEV_INFO(dev)->old_mc_list);
	VLAN_DEV_INFO(dev)->old_mc_list = NULL;
}
