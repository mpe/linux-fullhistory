/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		FDDI-type device handling.
 *
 * Version:	@(#)fddi.c	1.0.0	08/12/96
 *
 * Authors:	Lawrence V. Stefani, <stefani@lkg.dec.com>
 *
 *		fddi.c is based on previous eth.c and tr.c work by
 *			Ross Biro, <bir7@leland.Stanford.Edu>
 *			Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *			Mark Evans, <evansmp@uhura.aston.ac.uk>
 *			Florian La Roche, <rzsfl@rz.uni-sb.de>
 *			Alan Cox, <gw4pts@gw4pts.ampr.org>
 * 
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/fddidevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <net/arp.h>
#include <net/sock.h>

/*
 * Create the FDDI MAC header for an arbitrary protocol layer
 *
 * saddr=NULL	means use device source address
 * daddr=NULL	means leave destination address (eg unresolved arp)
 */

int fddi_header(
	struct sk_buff	*skb,
	struct device	*dev,
	unsigned short	type,
	void			*daddr,
	void			*saddr,
	unsigned		len
	)

	{
	struct fddihdr *fddi = (struct fddihdr *)skb_push(skb, FDDI_K_SNAP_HLEN);

	/* Fill in frame header - assume 802.2 SNAP frames for now */

	fddi->fc					 = FDDI_FC_K_ASYNC_LLC_DEF;
	fddi->hdr.llc_snap.dsap		 = FDDI_EXTENDED_SAP;
	fddi->hdr.llc_snap.ssap		 = FDDI_EXTENDED_SAP;
	fddi->hdr.llc_snap.ctrl		 = FDDI_UI_CMD;
	fddi->hdr.llc_snap.oui[0]	 = 0x00;
	fddi->hdr.llc_snap.oui[1]	 = 0x00;
	fddi->hdr.llc_snap.oui[2]	 = 0x00;
	fddi->hdr.llc_snap.ethertype = htons(type);

	/* Set the source and destination hardware addresses */
	 
	if (saddr != NULL)
		memcpy(fddi->saddr, saddr, dev->addr_len);
	else
		memcpy(fddi->saddr, dev->dev_addr, dev->addr_len);

	if (daddr != NULL)
		{
		memcpy(fddi->daddr, daddr, dev->addr_len);
		return(FDDI_K_SNAP_HLEN);
		}
	return(-FDDI_K_SNAP_HLEN);
	}


/*
 * Rebuild the FDDI MAC header. This is called after an ARP
 * (or in future other address resolution) has completed on
 * this sk_buff.  We now let ARP fill in the other fields.
 */
 
int fddi_rebuild_header(
	void			*buff,
	struct device	*dev,
	unsigned long	dest,
	struct sk_buff	*skb
	)

	{
	struct fddihdr *fddi = (struct fddihdr *)buff;

	/* Only ARP/IP is currently supported */
	 
	if (fddi->hdr.llc_snap.ethertype != htons(ETH_P_IP))
		{
		printk("fddi_rebuild_header: Don't know how to resolve type %04X addresses?\n", (unsigned int)htons(fddi->hdr.llc_snap.ethertype));
		return(0);
		}

	/* Try to get ARP to resolve the header and fill destination address */

	if (arp_find(fddi->daddr, dest, dev, dev->pa_addr, skb))
		return(1);
	else
		return(0);
	}


/*
 * Determine the packet's protocol ID and fill in skb fields.
 * This routine is called before an incoming packet is passed
 * up.  It's used to fill in specific skb fields and to set
 * the proper pointer to the start of packet data (skb->data).
 */
 
unsigned short fddi_type_trans(
	struct sk_buff	*skb,
	struct device	*dev
	)

	{
	struct fddihdr *fddi = (struct fddihdr *)skb->data;

	/*
	 * Set mac.raw field to point to FC byte, set data field to point
	 * to start of packet data.  Assume 802.2 SNAP frames for now.
	 */

	skb->mac.raw = skb->data;			/* point to frame control (FC) */
	skb_pull(skb, FDDI_K_SNAP_HLEN);	/* adjust for 21 byte header */

	/* Set packet type based on destination address and flag settings */
			
	if (*fddi->daddr & 0x01)
		{
		if (memcmp(fddi->daddr, dev->broadcast, FDDI_K_ALEN) == 0)
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_MULTICAST;
		}
	
	else if (dev->flags & IFF_PROMISC)
		{
		if (memcmp(fddi->daddr, dev->dev_addr, FDDI_K_ALEN))
			skb->pkt_type = PACKET_OTHERHOST;
		}

	/* Assume 802.2 SNAP frames, for now */

	return(fddi->hdr.llc_snap.ethertype);
	}
