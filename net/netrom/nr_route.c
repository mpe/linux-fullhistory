/*
 *	NET/ROM release 003
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 1.2.1 or higher/ NET3.029
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	NET/ROM 001	Jonathan(G4KLX)	First attempt.
 *	NET/ROM	003	Jonathan(G4KLX)	Use SIOCADDRT/SIOCDELRT ioctl values
 *					for NET/ROM routes.
 *			Alan Cox(GW4PTS) Added the firewall hooks.
 *
 *	TO DO
 *	Sort out the which pointer when shuffling entries in the routes
 *	section. Also reset the which pointer when a route becomes "good"
 *	again, ie when a NODES broadcast is processed via calls to
 *	nr_add_node().
 */
 
#include <linux/config.h>
#ifdef CONFIG_NETROM
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/arp.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/firewall.h>
#include <net/netrom.h>

static int nr_neigh_no = 1;
static int nr_route_on = 1;

static struct nr_node  *nr_node_list  = NULL;
static struct nr_neigh *nr_neigh_list = NULL;

/*
 *	Add a new route to a node, and in the process add the node and the
 *	neighbour if it is new.
 */
static int nr_add_node(ax25_address *nr, const char *mnemonic, ax25_address *ax25,
	ax25_digi *ax25_digi, struct device *dev, int quality, int obs_count)
{
	struct nr_node  *nr_node;
	struct nr_neigh *nr_neigh;
	struct nr_route nr_route;
	unsigned long flags;
	int i, found;
	
	for (nr_node = nr_node_list; nr_node != NULL; nr_node = nr_node->next)
		if (ax25cmp(nr, &nr_node->callsign) == 0)
			break;

	for (nr_neigh = nr_neigh_list; nr_neigh != NULL; nr_neigh = nr_neigh->next)
		if (ax25cmp(ax25, &nr_neigh->callsign) == 0 && nr_neigh->dev == dev)
			break;

	if (quality == 0 && nr_neigh != NULL && nr_node != NULL)
		return 0;

	if (nr_neigh == NULL) {
		if ((nr_neigh = (struct nr_neigh *)kmalloc(sizeof(*nr_neigh), GFP_ATOMIC)) == NULL)
			return -ENOMEM;

		nr_neigh->callsign = *ax25;
		nr_neigh->digipeat = NULL;
		nr_neigh->dev      = dev;
		nr_neigh->quality  = nr_default.quality;
		nr_neigh->locked   = 0;
		nr_neigh->count    = 0;
		nr_neigh->number   = nr_neigh_no++;

		if (ax25_digi != NULL) {
			if ((nr_neigh->digipeat = kmalloc(sizeof(*ax25_digi), GFP_KERNEL)) == NULL) {
				kfree_s(nr_neigh, sizeof(*nr_neigh));
				return -ENOMEM;
			}
			*nr_neigh->digipeat = *ax25_digi;
		}
			
		save_flags(flags);
		cli();

		nr_neigh->next = nr_neigh_list;
		nr_neigh_list  = nr_neigh;

		restore_flags(flags);
	}

	if (nr_node == NULL) {
		if ((nr_node = (struct nr_node *)kmalloc(sizeof(*nr_node), GFP_ATOMIC)) == NULL)
			return -ENOMEM;

		nr_node->callsign = *nr;
		memcpy(&nr_node->mnemonic, mnemonic, sizeof(nr_node->mnemonic));

		nr_node->which = 0;
		nr_node->count = 1;

		nr_node->routes[0].quality   = quality;
		nr_node->routes[0].obs_count = obs_count;
		nr_node->routes[0].neighbour = nr_neigh->number;
			
		save_flags(flags);
		cli();

		nr_node->next = nr_node_list;
		nr_node_list  = nr_node;

		restore_flags(flags);
		
		nr_neigh->count++;

		return 0;
	}

	for (found = 0, i = 0; i < nr_node->count; i++) {
		if (nr_node->routes[i].neighbour == nr_neigh->number) {
			nr_node->routes[i].quality   = quality;
			nr_node->routes[i].obs_count = obs_count;
			found = 1;
			break;
		}
	}

	if (!found) {
		/* We have space at the bottom, slot it in */
		if (nr_node->count < 3) {
			nr_node->routes[2] = nr_node->routes[1];
			nr_node->routes[1] = nr_node->routes[0];

			nr_node->routes[0].quality   = quality;
			nr_node->routes[0].obs_count = obs_count;
			nr_node->routes[0].neighbour = nr_neigh->number;
				
			nr_node->count++;
			nr_neigh->count++;
		} else {
			/* It must be better than the worst */
			if (quality > nr_node->routes[2].quality) {
				nr_node->routes[2].quality   = quality;
				nr_node->routes[2].obs_count = obs_count;
				nr_node->routes[2].neighbour = nr_neigh->number;

				nr_neigh->count++;
			}
		}
	}

	/* Now re-sort the routes in quality order */
	switch (nr_node->count) {
		case 3:
			if (nr_node->routes[1].quality > nr_node->routes[0].quality) {
				switch (nr_node->which) {
					case 0:  nr_node->which = 1; break;
					case 1:  nr_node->which = 0; break;
					default: break;
				}
				nr_route           = nr_node->routes[0];
				nr_node->routes[0] = nr_node->routes[1];
				nr_node->routes[1] = nr_route;
			}
			if (nr_node->routes[2].quality > nr_node->routes[1].quality) {
				switch (nr_node->which) {
					case 1:  nr_node->which = 2; break;
					case 2:  nr_node->which = 1; break;
					default: break;
				}
				nr_route           = nr_node->routes[1];
				nr_node->routes[1] = nr_node->routes[2];
				nr_node->routes[2] = nr_route;
			}
		case 2:
			if (nr_node->routes[1].quality > nr_node->routes[0].quality) {
				switch (nr_node->which) {
					case 0:  nr_node->which = 1; break;
					case 1:  nr_node->which = 0; break;
					default: break;
				}
				nr_route           = nr_node->routes[0];
				nr_node->routes[0] = nr_node->routes[1];
				nr_node->routes[1] = nr_route;
			}
		case 1:
			break;
	}

	for (i = 0; i < nr_node->count; i++) {
		if (nr_node->routes[i].neighbour == nr_neigh->number) {
			if (i < nr_node->which)
				nr_node->which = i;
			break;
		}
	}

	return 0;
}

static void nr_remove_node(struct nr_node *nr_node)
{
	struct nr_node *s;
	unsigned long flags;
	
	save_flags(flags);
	cli();

	if ((s = nr_node_list) == nr_node) {
		nr_node_list = nr_node->next;
		restore_flags(flags);
		kfree_s(nr_node, sizeof(struct nr_node));
		return;
	}

	while (s != NULL && s->next != NULL) {
		if (s->next == nr_node) {
			s->next = nr_node->next;
			restore_flags(flags);
			kfree_s(nr_node, sizeof(struct nr_node));
			return;
		}

		s = s->next;
	}

	restore_flags(flags);
}

static void nr_remove_neigh(struct nr_neigh *nr_neigh)
{
	struct nr_neigh *s;
	unsigned long flags;
	
	save_flags(flags);
	cli();

	if ((s = nr_neigh_list) == nr_neigh) {
		nr_neigh_list = nr_neigh->next;
		restore_flags(flags);
		if (nr_neigh->digipeat != NULL)
			kfree_s(nr_neigh->digipeat, sizeof(ax25_digi));
		kfree_s(nr_neigh, sizeof(struct nr_neigh));
		return;
	}

	while (s != NULL && s->next != NULL) {
		if (s->next == nr_neigh) {
			s->next = nr_neigh->next;
			restore_flags(flags);
			if (nr_neigh->digipeat != NULL)
				kfree_s(nr_neigh->digipeat, sizeof(ax25_digi));
			kfree_s(nr_neigh, sizeof(struct nr_neigh));
			return;
		}

		s = s->next;
	}

	restore_flags(flags);
}

/*
 *	"Delete" a node. Strictly speaking remove a route to a node. The node
 *	is only deleted if no routes are left to it.
 */
static int nr_del_node(ax25_address *callsign, ax25_address *neighbour, struct device *dev)
{
	struct nr_node  *nr_node;
	struct nr_neigh *nr_neigh;
	int i;
	
	for (nr_node = nr_node_list; nr_node != NULL; nr_node = nr_node->next)
		if (ax25cmp(callsign, &nr_node->callsign) == 0)
			break;

	if (nr_node == NULL) return -EINVAL;

	for (nr_neigh = nr_neigh_list; nr_neigh != NULL; nr_neigh = nr_neigh->next)
		if (ax25cmp(neighbour, &nr_neigh->callsign) == 0 && nr_neigh->dev == dev)
			break;

	if (nr_neigh == NULL) return -EINVAL;
	
	for (i = 0; i < nr_node->count; i++) {
		if (nr_node->routes[i].neighbour == nr_neigh->number) {
			nr_neigh->count--;

			if (nr_neigh->count == 0 && !nr_neigh->locked)
				nr_remove_neigh(nr_neigh);
				
			nr_node->count--;
			
			if (nr_node->count == 0) {
				nr_remove_node(nr_node);
			} else {
				switch (i) {
					case 0:
						nr_node->routes[0] = nr_node->routes[1];
					case 1:
						nr_node->routes[1] = nr_node->routes[2];
					case 2:
						break;
				}
			}

			return 0;
		}
	}

	return -EINVAL;
}

/*
 *	Lock a neighbour with a quality.
 */
static int nr_add_neigh(ax25_address *callsign, struct device *dev, unsigned int quality)
{
	struct nr_neigh *nr_neigh;
	unsigned long flags;

	for (nr_neigh = nr_neigh_list; nr_neigh != NULL; nr_neigh = nr_neigh->next) {
		if (ax25cmp(callsign, &nr_neigh->callsign) == 0 && nr_neigh->dev == dev) {
			nr_neigh->quality = quality;
			nr_neigh->locked  = 1;
			return 0;
		}
	}

	if ((nr_neigh = (struct nr_neigh *)kmalloc(sizeof(*nr_neigh), GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	nr_neigh->callsign = *callsign;
	nr_neigh->digipeat = NULL;
	nr_neigh->dev      = dev;
	nr_neigh->quality  = quality;
	nr_neigh->locked   = 1;
	nr_neigh->count    = 0;
	nr_neigh->number   = nr_neigh_no++;

	save_flags(flags);
	cli();
			
	nr_neigh->next = nr_neigh_list;
	nr_neigh_list  = nr_neigh;

	restore_flags(flags);

	return 0;	
}

/*
 *	"Delete" a neighbour. The neighbour is only removed if the number
 *	of nodes that may use it is zero.
 */
static int nr_del_neigh(ax25_address *callsign, struct device *dev, unsigned int quality)
{
	struct nr_neigh *nr_neigh;

	for (nr_neigh = nr_neigh_list; nr_neigh != NULL; nr_neigh = nr_neigh->next)
		if (ax25cmp(callsign, &nr_neigh->callsign) == 0 && nr_neigh->dev == dev)
			break;

	if (nr_neigh == NULL) return -EINVAL;

	nr_neigh->quality = quality;
	nr_neigh->locked  = 0;

	if (nr_neigh->count == 0)
		nr_remove_neigh(nr_neigh);

	return 0;
}

/*
 *	Decrement the obsolescence count by one. If a route is reduced to a
 *	count of zero, remove it. Also remove any unlocked neighbours with
 *	zero nodes routing via it.
 */
static int nr_dec_obs(void)
{
	struct nr_neigh *t, *nr_neigh;
	struct nr_node  *s, *nr_node;
	int i;

	nr_node = nr_node_list;

	while (nr_node != NULL) {
		s       = nr_node;
		nr_node = nr_node->next;

		for (i = 0; i < s->count; i++) {
			switch (s->routes[i].obs_count) {

			case 0:		/* A locked entry */
				break;

			case 1:		/* From 1 -> 0 */
				nr_neigh = nr_neigh_list;

				while (nr_neigh != NULL) {
					t        = nr_neigh;
					nr_neigh = nr_neigh->next;

					if (t->number == s->routes[i].neighbour) {
						t->count--;
						
						if (t->count == 0 && !t->locked)
							nr_remove_neigh(t);

						break;
					}
				}

				s->count--;

				switch (i) {
					case 0:
						s->routes[0] = s->routes[1];
					case 1:
						s->routes[1] = s->routes[2];
					case 2:
						break;
				}
				break;

			default:
				s->routes[i].obs_count--;
				break;

			}
		}

		if (s->count <= 0)
			nr_remove_node(s);
	}

	return 0;
}

/*
 *	A device has been removed. Remove its routes and neighbours.
 */
void nr_rt_device_down(struct device *dev)
{
	struct nr_neigh *s, *nr_neigh = nr_neigh_list;
	struct nr_node  *t, *nr_node;
	int i;

	while (nr_neigh != NULL) {
		s        = nr_neigh;
		nr_neigh = nr_neigh->next;
		
		if (s->dev == dev) {
			nr_node = nr_node_list;

			while (nr_node != NULL) {
				t       = nr_node;
				nr_node = nr_node->next;
				
				for (i = 0; i < t->count; i++) {
					if (t->routes[i].neighbour == s->number) {
						t->count--;

						switch (i) {
							case 0:
								t->routes[0] = t->routes[1];
							case 1:
								t->routes[1] = t->routes[2];
							case 2:
								break;
						}
					}
				}
				
				if (t->count <= 0)
					nr_remove_node(t);
			}
			
			nr_remove_neigh(s);
		}
	}
}

/*
 *	Check that the device given is a valid AX.25 interface that is "up".
 *	Or a valid ethernet interface with an AX.25 callsign binding.
 */
static struct device *nr_ax25_dev_get(char *devname)
{
	struct device *dev;

	if ((dev = dev_get(devname)) == NULL)
		return NULL;

	if ((dev->flags & IFF_UP) && dev->type == ARPHRD_AX25)
		return dev;

#ifdef CONFIG_BPQETHER
	if ((dev->flags & IFF_UP) && dev->type == ARPHRD_ETHER)
		if (ax25_bpq_get_addr(dev) != NULL)
			return dev;
#endif
	
	return NULL;
}

/*
 *	Find the first active NET/ROM device, usually "nr0".
 */
struct device *nr_dev_first(void)
{
	struct device *dev, *first = NULL;

	for (dev = dev_base; dev != NULL; dev = dev->next)
		if ((dev->flags & IFF_UP) && dev->type == ARPHRD_NETROM)
			if (first == NULL || strncmp(dev->name, first->name, 3) < 0)
				first = dev;

	return first;
}

/*
 *	Find the NET/ROM device for the given callsign.
 */
struct device *nr_dev_get(ax25_address *addr)
{
	struct device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next)
		if ((dev->flags & IFF_UP) && dev->type == ARPHRD_NETROM && ax25cmp(addr, (ax25_address *)dev->dev_addr) == 0)
			return dev;
	
	return NULL;
}

/*
 *	Handle the ioctls that control the routing functions.
 */
int nr_rt_ioctl(unsigned int cmd, void *arg)
{
	struct nr_route_struct nr_route;
	struct device *dev;
	int err;
	long opt = 0;

	switch (cmd) {

		case SIOCADDRT:
			if ((err = verify_area(VERIFY_READ, arg, sizeof(struct nr_route_struct))) != 0)
				return err;
			memcpy_fromfs(&nr_route, arg, sizeof(struct nr_route_struct));
			if ((dev = nr_ax25_dev_get(nr_route.device)) == NULL)
				return -EINVAL;
			switch (nr_route.type) {
				case NETROM_NODE:
					return nr_add_node(&nr_route.callsign,
						nr_route.mnemonic,
						&nr_route.neighbour,
						NULL, dev, nr_route.quality,
						nr_route.obs_count);
				case NETROM_NEIGH:
					return nr_add_neigh(&nr_route.callsign,
						dev, nr_route.quality);
				default:
					return -EINVAL;
			}

		case SIOCDELRT:
			if ((err = verify_area(VERIFY_READ, arg, sizeof(struct nr_route_struct))) != 0)
				return err;
			memcpy_fromfs(&nr_route, arg, sizeof(struct nr_route_struct));
			if ((dev = nr_ax25_dev_get(nr_route.device)) == NULL)
				return -EINVAL;
			switch (nr_route.type) {
				case NETROM_NODE:
					return nr_del_node(&nr_route.callsign,
						&nr_route.neighbour, dev);
				case NETROM_NEIGH:
					return nr_del_neigh(&nr_route.callsign,
						dev, nr_route.quality);
				default:
					return -EINVAL;
			}

		case SIOCNRDECOBS:
			return nr_dec_obs();
			
		case SIOCNRRTCTL:
			if ((err = verify_area(VERIFY_READ, arg, sizeof(int))) != 0)
				return err;
			opt = get_fs_long((void *)arg);
			nr_route_on = opt ? 1 : 0;
			return 0;
	}

	return 0;
}

/*
 * 	A level 2 link has timed out, therefore it appears to be a poor link,
 *	then don't use that neighbour until it is reset.
 */
void nr_link_failed(ax25_address *callsign, struct device *dev)
{
	struct nr_neigh *nr_neigh;
	struct nr_node  *nr_node;

	for (nr_neigh = nr_neigh_list; nr_neigh != NULL; nr_neigh = nr_neigh->next)
		if (ax25cmp(&nr_neigh->callsign, callsign) == 0 && nr_neigh->dev == dev)
			break;
			
	if (nr_neigh == NULL) return;
	
	for (nr_node = nr_node_list; nr_node != NULL; nr_node = nr_node->next)
		if (nr_node->which >= nr_node->count && nr_node->routes[nr_node->which].neighbour == nr_neigh->number)
			nr_node->which++;
}

/*
 *	Route a frame to an appropriate AX.25 connection. A NULL ax25_cb
 *	indicates an internally generated frame.
 */

int nr_route_frame(struct sk_buff *skb, ax25_cb *ax25)
{
	ax25_address *nr_src, *nr_dest;
	struct nr_neigh *nr_neigh;
	struct nr_node  *nr_node;
	struct device *dev;
	unsigned char *dptr;
	
#ifdef CONFIG_FIREWALL

	if(ax25 && call_in_firewall(PF_NETROM, skb->dev, skb->data, NULL)!=FW_ACCEPT)
		return 0;
	if(!ax25 && call_out_firewall(PF_NETROM, skb->dev, skb->data, NULL)!=FW_ACCEPT)
		return 0;
#endif
	nr_src  = (ax25_address *)(skb->data + 0);
	nr_dest = (ax25_address *)(skb->data + 7);

	if (ax25 != NULL)
		nr_add_node(nr_src, "", &ax25->dest_addr, ax25->digipeat, ax25->device, 0, nr_default.obs_count);

	if ((dev = nr_dev_get(nr_dest)) != NULL)	/* Its for me */
		return nr_rx_frame(skb, dev);

	if (!nr_route_on && ax25 != NULL)
		return 0;

	/* Its Time-To-Live has expired */
	if (--skb->data[14] == 0)
		return 0;

	for (nr_node = nr_node_list; nr_node != NULL; nr_node = nr_node->next)
		if (ax25cmp(nr_dest, &nr_node->callsign) == 0)
			break;

	if (nr_node == NULL || nr_node->which >= nr_node->count)
		return 0;

	for (nr_neigh = nr_neigh_list; nr_neigh != NULL; nr_neigh = nr_neigh->next)
		if (nr_neigh->number == nr_node->routes[nr_node->which].neighbour)
			break;
			
	if (nr_neigh == NULL)
		return 0;

	if ((dev = nr_dev_first()) == NULL)
		return 0;

#ifdef CONFIG_FIREWALL
	if(ax25 && call_fw_firewall(PF_NETROM, skb->dev, skb->data, NULL)!=FW_ACCEPT)
		return 0;
#endif

	dptr  = skb_push(skb, 1);
	*dptr = AX25_P_NETROM;

	ax25_send_frame(skb, (ax25_address *)dev->dev_addr, &nr_neigh->callsign, nr_neigh->digipeat, nr_neigh->dev);

	return 1;
}

int nr_nodes_get_info(char *buffer, char **start, off_t offset,
		      int length, int dummy)
{
	struct nr_node *nr_node;
	int len     = 0;
	off_t pos   = 0;
	off_t begin = 0;
	int i;
  
	cli();

	len += sprintf(buffer, "callsign  mnemonic w n qual obs neigh qual obs neigh qual obs neigh\n");

	for (nr_node = nr_node_list; nr_node != NULL; nr_node = nr_node->next) {
		len += sprintf(buffer + len, "%-9s %-7s  %d %d",
			ax2asc(&nr_node->callsign),
			nr_node->mnemonic,
			nr_node->which + 1,
			nr_node->count);			

		for (i = 0; i < nr_node->count; i++) {
			len += sprintf(buffer + len, "  %3d   %d %05d",
				nr_node->routes[i].quality,
				nr_node->routes[i].obs_count,
				nr_node->routes[i].neighbour);
		}

		len += sprintf(buffer + len, "\n");

		pos = begin + len;

		if (pos < offset) {
			len   = 0;
			begin = pos;
		}
		
		if (pos > offset + length)
			break;
	}

	sti();

	*start = buffer + (offset - begin);
	len   -= (offset - begin);

	if (len > length) len = length;

	return(len);
} 

int nr_neigh_get_info(char *buffer, char **start, off_t offset,
		      int length, int dummy)
{
	struct nr_neigh *nr_neigh;
	int len     = 0;
	off_t pos   = 0;
	off_t begin = 0;
  
	cli();

	len += sprintf(buffer, "addr  callsign  dev  qual lock count\n");

	for (nr_neigh = nr_neigh_list; nr_neigh != NULL; nr_neigh = nr_neigh->next) {
		len += sprintf(buffer + len, "%05d %-9s %-4s  %3d    %d   %3d\n",
			nr_neigh->number,
			ax2asc(&nr_neigh->callsign),
			nr_neigh->dev ? nr_neigh->dev->name : "???",
			nr_neigh->quality,
			nr_neigh->locked,
			nr_neigh->count);

		pos = begin + len;

		if (pos < offset) {
			len   = 0;
			begin = pos;
		}
		
		if (pos > offset + length)
			break;
	}

	sti();

	*start = buffer + (offset - begin);
	len   -= (offset - begin);

	if (len > length) len = length;

	return(len);
} 

#endif
