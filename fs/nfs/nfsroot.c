/*
 *  linux/fs/nfs/nfsroot.c -- version 2.3
 *
 *  Copyright (C) 1995, 1996  Gero Kuhlmann <gero@gkminix.han.de>
 *
 *  For parts of this file:
 *  Copyright (C) 1996  Martin Mares <mj@k332.feld.cvut.cz>
 *
 *  Allow an NFS filesystem to be mounted as root. The way this works is:
 *     (1) Determine the local IP address via RARP or BOOTP or from the
 *         kernel command line.
 *     (2) Handle RPC negotiation with the system which replied to RARP or
 *         was reported as a boot server by BOOTP or manually.
 *     (3) The actual mounting is done later, when init() is running.
 *
 *
 *	Changes:
 *
 *	Alan Cox	:	Removed get_address name clash with FPU.
 *	Alan Cox	:	Reformatted a bit.
 *	Gero Kuhlmann	:	Code cleanup
 *	Michael Rausch  :	Fixed recognition of an incoming RARP answer.
 *	Martin Mares	: (2.0)	Auto-configuration via BOOTP supported.
 *	Martin Mares	:	Manual selection of interface & BOOTP/RARP.
 *	Martin Mares	:	Using network routes instead of host routes,
 *				allowing the default configuration to be used
 *				for normal operation of the host.
 *	Martin Mares	:	Randomized timer with exponential backoff
 *				installed to minimize network congestion.
 *	Martin Mares	:	Code cleanup.
 *	Martin Mares	: (2.1)	BOOTP and RARP made configuration options.
 *	Martin Mares	:	Server hostname generation fixed.
 *	Gerd Knorr	:	Fixed wired inode handling
 *	Martin Mares	: (2.2)	"0.0.0.0" addresses from command line ignored.
 *	Martin Mares	:	RARP replies not tested for server address.
 *	Gero Kuhlmann	: (2.3) Some bug fixes and code cleanup again (please
 *				send me your new patches _before_ bothering
 *				Linus so that I don' always have to cleanup
 *				_afterwards_ - thanks)
 *	Gero Kuhlmann	:	Last changes of Martin Mares undone.
 *	Gero Kuhlmann	: 	RARP replies are tested for specified server
 *				again. However, it's now possible to have
 *				different RARP and NFS servers.
 *	Gero Kuhlmann	:	"0.0.0.0" addresses from command line are
 *				now mapped to INADDR_NONE.
 *	Gero Kuhlmann	:	Fixed a bug which prevented BOOTP path name
 *				from being used (thanks to Leo Spiekman)
 *	Andy Walker	:	Allow to specify the NFS server in nfs_root
 *				without giving a path name
 *	Swen Th=FCmmler	:	Allow to specify the NFS options in nfs_root
 *				without giving a path name. Fix BOOTP request
 *				for domainname (domainname is NIS domain, not
 *				DNS domain!). Skip dummy devices for BOOTP.
 *	Jacek Zapala	:	Fixed a bug which prevented server-ip address
 *				from nfsroot parameter from being used.
 *	Olaf Kirch	:	Adapted to new NFS code.
 *
 */


/* Define this to allow debugging output */
#undef NFSROOT_DEBUG
#undef NFSROOT_BOOTP_DEBUG


#include <linux/config.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/random.h>
#include <linux/fcntl.h>

#include <asm/param.h>
#include <linux/utsname.h>
#include <linux/in.h>
#include <linux/if.h>
#include <linux/inet.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#ifdef CONFIG_AX25
#include <net/ax25.h>	/* For AX25_P_IP */
#endif
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/socket.h>
#include <linux/route.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/in.h>
#include <net/route.h>
#include <net/sock.h>

#include <asm/segment.h>
#include <asm/uaccess.h>

#define NFSDBG_FACILITY		NFSDBG_ROOT
/* Range of privileged ports */
#define STARTPORT		600
#define ENDPORT			1023
#define NPORTS			(ENDPORT - STARTPORT + 1)


/* Define the timeout for waiting for a RARP/BOOTP reply */
#define CONF_BASE_TIMEOUT	(HZ*5)	/* Initial timeout: 5 seconds */
#define CONF_RETRIES	 	10	/* 10 retries */
#define CONF_TIMEOUT_RANDOM	(HZ)	/* Maximum amount of randomization */
#define CONF_TIMEOUT_MULT	*5/4	/* Speed of timeout growth */
#define CONF_TIMEOUT_MAX	(HZ*30)	/* Maximum allowed timeout */


/* List of open devices */
struct open_dev {
	struct device *dev;
	unsigned short old_flags;
	struct open_dev *next;
};

static struct open_dev *open_base = NULL;


/* IP configuration */
static struct device *root_dev = NULL;	/* Device selected for booting */
static char user_dev_name[IFNAMSIZ];	/* Name of user-selected boot device */
static __u32 myaddr;			/* My IP address */
static __u32 servaddr;			/* Server IP address */
static __u32 gateway;			/* Gateway IP address */
static __u32 netmask;			/* Netmask for local subnet */


/* BOOTP/RARP variables */
static int bootp_flag;			/* User said: Use BOOTP! */
static int rarp_flag;			/* User said: Use RARP! */
static int bootp_dev_count = 0;		/* Number of devices allowing BOOTP */
static int rarp_dev_count = 0;		/* Number of devices allowing RARP */
static __u32 rarp_serv;			/* IP address of RARP server */

#if defined(CONFIG_RNFS_BOOTP) || defined(CONFIG_RNFS_RARP)
#define CONFIG_RNFS_DYNAMIC		/* Enable dynamic IP config */
static volatile int pkt_arrived;	/* BOOTP/RARP packet detected */

#define ARRIVED_BOOTP	1
#define ARRIVED_RARP	2
#endif


/* NFS-related data */
static struct nfs_mount_data nfs_data;		/* NFS mount info */
static char	nfs_path[NFS_MAXPATHLEN];	/* Name of directory to mount */
static int	nfs_port;			/* Port to connect to for NFS */
static int	mount_port;			/* Mount daemon port number */


/* Yes, we use sys_socket, but there's no include file for it */
extern asmlinkage int sys_socket(int family, int type, int protocol);



/***************************************************************************

			Device Handling Subroutines

 ***************************************************************************/

/*
 * Setup and initialize all network devices. If there is a user-preferred
 * interface, ignore all other interfaces.
 */
static int root_dev_open(void)
{
	struct open_dev *openp, **last;
	struct device *dev;
	unsigned short old_flags;

	last = &open_base;
	for (dev = dev_base; dev != NULL; dev = dev->next) {
		if (dev->type < ARPHRD_SLIP &&
		    dev->family == AF_INET &&
		    !(dev->flags & (IFF_LOOPBACK | IFF_POINTOPOINT)) &&
		    (0 != strncmp(dev->name, "dummy", 5)) &&
		    (!user_dev_name[0] || !strcmp(dev->name, user_dev_name))) {
			/* First up the interface */
			old_flags = dev->flags;
			dev->flags = IFF_UP | IFF_BROADCAST | IFF_RUNNING;
			if (!(old_flags & IFF_UP) && dev_open(dev)) {
				dev->flags = old_flags;
				continue;
			}
			openp = (struct open_dev *) kmalloc(sizeof(struct open_dev),
						GFP_ATOMIC);
			if (openp == NULL)
				continue;
			openp->dev = dev;
			openp->old_flags = old_flags;
			*last = openp;
			last = &openp->next;
			bootp_dev_count++;
			if (!(dev->flags & IFF_NOARP))
				rarp_dev_count++;
			dprintk("Root-NFS: Opened %s\n", dev->name);
		}
	}
	*last = NULL;

	if (!bootp_dev_count && !rarp_dev_count) {
		printk(KERN_ERR "Root-NFS: Unable to open at least one network device\n");
		return -1;
	}
	return 0;
}

static inline void
set_sockaddr(struct sockaddr_in *sin, __u32 addr, __u16 port)
{
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = addr;
	sin->sin_port = port;
}

static int
root_dev_chg_route(int op, struct device *dev, __u32 dest, __u32 mask, __u32 gw)
{
	struct rtentry	route;
	unsigned long	oldfs;
	int		err;

	route.rt_dev = dev->name;
	route.rt_mtu = dev->mtu;
	route.rt_flags = RTF_UP;
	set_sockaddr((struct sockaddr_in *) &route.rt_dst, dest & mask, 0);
	set_sockaddr((struct sockaddr_in *) &route.rt_genmask, mask, 0);

	if (gw != 0) {
		set_sockaddr((struct sockaddr_in *) &route.rt_gateway, gw, 0);
		route.rt_flags |= RTF_GATEWAY;
		if ((gw ^ myaddr) & netmask) {
			printk(KERN_ERR "Root-NFS: Gateway not on local network!\n");
			return -ENETUNREACH;
		}
	}

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = ip_rt_ioctl(op, &route);
	set_fs(oldfs);
	printk(KERN_NOTICE "%s route %s %s %s: res %d\n",
			(op == SIOCADDRT? "add" : "del"),
			in_ntoa(dest), in_ntoa(mask), in_ntoa(gw), err);

	return err;
}

static int
root_dev_add_route(struct device *dev, __u32 dest, __u32 mask, __u32 gateway)
{
	return root_dev_chg_route(SIOCADDRT, dev, dest, mask, gateway);
}

static int
root_dev_del_route(struct device *dev, __u32 dest, __u32 mask, __u32 gateway)
{
	return root_dev_chg_route(SIOCDELRT, dev, dest, mask, gateway);
}

/*
 *  Restore the state of all devices. However, keep the root device open
 *  for the upcoming mount.
 */
static void root_dev_close(void)
{
	struct open_dev *openp;
	struct open_dev *nextp;

	openp = open_base;
	while (openp != NULL) {
		nextp = openp->next;
		openp->next = NULL;
		if (openp->dev != root_dev) {
			if (!(openp->old_flags & IFF_UP))
				dev_close(openp->dev);
			openp->dev->flags = openp->old_flags;
		}
		kfree_s(openp, sizeof(struct open_dev));
		openp = nextp;
	}
}



/***************************************************************************

			      RARP Subroutines

 ***************************************************************************/

#ifdef CONFIG_RNFS_RARP

extern void arp_send(int type, int ptype, unsigned long target_ip,
		     struct device *dev, unsigned long src_ip,
		     unsigned char *dest_hw, unsigned char *src_hw,
		     unsigned char *target_hw);

static int root_rarp_recv(struct sk_buff *skb, struct device *dev,
			  struct packet_type *pt);


static struct packet_type rarp_packet_type = {
	0,			/* Should be: __constant_htons(ETH_P_RARP)
				 * - but this _doesn't_ come out constant! */
	NULL,			/* Listen to all devices */
	root_rarp_recv,
	NULL,
	NULL
};


/*
 *  Register the packet type for RARP
 */
static void root_rarp_open(void)
{
	rarp_packet_type.type = htons(ETH_P_RARP);
	dev_add_pack(&rarp_packet_type);
}


/*
 *  Deregister the RARP packet type
 */
static void root_rarp_close(void)
{
	rarp_packet_type.type = htons(ETH_P_RARP);
	dev_remove_pack(&rarp_packet_type);
}


/*
 *  Receive RARP packets.
 */
static int root_rarp_recv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	struct arphdr *rarp = (struct arphdr *)skb->h.raw;
	unsigned char *rarp_ptr = (unsigned char *) (rarp + 1);
	unsigned long sip, tip;
	unsigned char *sha, *tha;		/* s for "source", t for "target" */

	/* If this test doesn't pass, it's not IP, or we should ignore it anyway */
	if (rarp->ar_hln != dev->addr_len || dev->type != ntohs(rarp->ar_hrd)) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}

	/* If it's not a RARP reply, delete it. */
	if (rarp->ar_op != htons(ARPOP_RREPLY)) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}

	/* If it's not ethernet or AX25, delete it. */
	if ((rarp->ar_pro != htons(ETH_P_IP) && dev->type != ARPHRD_AX25) ||
#ifdef CONFIG_AX25
	   (rarp->ar_pro != htons(AX25_P_IP) && dev->type == ARPHRD_AX25) ||
#endif
	    rarp->ar_pln != 4) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}

	/* Extract variable width fields */
	sha = rarp_ptr;
	rarp_ptr += dev->addr_len;
	memcpy(&sip, rarp_ptr, 4);
	rarp_ptr += 4;
	tha = rarp_ptr;
	rarp_ptr += dev->addr_len;
	memcpy(&tip, rarp_ptr, 4);

	/* Discard packets which are not meant for us. */
	if (memcmp(tha, dev->dev_addr, dev->addr_len)) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	/* Discard packets which are not from specified server. */
	if (rarp_flag && !bootp_flag &&
	    rarp_serv != INADDR_NONE &&
	    rarp_serv != sip) {
		kfree_skb(skb, FREE_READ);
		return 0;
	}

	/*
	 * The packet is what we were looking for. Setup the global
	 * variables.
	 */
	cli();
	if (pkt_arrived) {
		sti();
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	pkt_arrived = ARRIVED_RARP;
	sti();
	root_dev = dev;

	if (myaddr == INADDR_NONE)
		myaddr = tip;
	if (servaddr == INADDR_NONE)
		servaddr = sip;
	kfree_skb(skb, FREE_READ);
	return 0;
}


/*
 *  Send RARP request packet over all devices which allow RARP.
 */
static void root_rarp_send(void)
{
	struct open_dev *openp;
	struct device *dev;
	int num = 0;

	for (openp = open_base; openp != NULL; openp = openp->next) {
		dev = openp->dev;
		if (!(dev->flags & IFF_NOARP)) {
			arp_send(ARPOP_RREQUEST, ETH_P_RARP, 0, dev, 0, NULL,
				 dev->dev_addr, dev->dev_addr);
			num++;
		}
	}
}
#endif



/***************************************************************************

			     BOOTP Subroutines

 ***************************************************************************/

#ifdef CONFIG_RNFS_BOOTP

static struct device *bootp_dev = NULL;	/* Device selected as best BOOTP target */

static struct socket *bootp_xmit_sock;	/* BOOTP send socket */
static struct socket *bootp_recv_sock;	/* BOOTP receive socket */

struct bootp_pkt {		/* BOOTP packet format */
	u8 op;			/* 1=request, 2=reply */
	u8 htype;		/* HW address type */
	u8 hlen;		/* HW address length */
	u8 hops;		/* Used only by gateways */
	u32 xid;		/* Transaction ID */
	u16 secs;		/* Seconds since we started */
	u16 flags;		/* Just what is says */
	u32 client_ip;		/* Client's IP address if known */
	u32 your_ip;		/* Assigned IP address */
	u32 server_ip;		/* Server's IP address */
	u32 relay_ip;		/* IP address of BOOTP relay */
	u8 hw_addr[16];		/* Client's HW address */
	u8 serv_name[64];	/* Server host name */
	u8 boot_file[128];	/* Name of boot file */
	u8 vendor_area[128];	/* Area for extensions */
};

#define BOOTP_REQUEST 1
#define BOOTP_REPLY 2

static struct bootp_pkt *xmit_bootp;	/* Packet being transmitted */
static struct bootp_pkt *recv_bootp;	/* Packet being received */

static int bootp_have_route = 0;	/* BOOTP route installed */


/*
 *  Free BOOTP packet buffers
 */
static void root_free_bootp(void)
{
	if (xmit_bootp) {
		kfree_s(xmit_bootp, sizeof(struct bootp_pkt));
		xmit_bootp = NULL;
	}
	if (recv_bootp) {
		kfree_s(recv_bootp, sizeof(struct bootp_pkt));
		recv_bootp = NULL;
	}
}


/*
 *  Allocate memory for BOOTP packet buffers
 */
static inline int root_alloc_bootp(void)
{
	if (!(xmit_bootp = kmalloc(sizeof(struct bootp_pkt), GFP_KERNEL)) ||
	    !(recv_bootp = kmalloc(sizeof(struct bootp_pkt), GFP_KERNEL))) {
		printk("BOOTP: Out of memory!");
		return -1;
	}
	return 0;
}


/*
 *  Create default route for BOOTP sending
 */
static int root_add_bootp_route(void)
{
	if (root_dev_add_route(bootp_dev, 0, 0, 0) < 0) {
		printk(KERN_ERR "BOOTP: Failed to add route\n");
		return -1;
	}
	bootp_have_route = 1;
	return 0;
}


/*
 *  Delete default route for BOOTP sending
 */
static int root_del_bootp_route(void)
{
	if (bootp_have_route && root_dev_del_route(bootp_dev, 0, 0, 0) < 0) {
		printk(KERN_ERR "BOOTP: Deleting of route failed!\n");
		return -1;
	}
	bootp_have_route = 0;
	return 0;
}


/*
 *  Open UDP socket.
 */
static int root_open_udp_sock(struct socket **sock)
{
	int	err;

	if ((err = sock_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP, sock)) < 0)
		printk(KERN_ERR "BOOTP: Cannot open UDP socket!\n");
	return err;
}


/*
 *  Connect UDP socket.
 */
static int root_connect_udp_sock(struct socket *sock, u32 addr, u16 port)
{
	struct sockaddr_in sa;
	int result;

	set_sockaddr(&sa, htonl(addr), htonl(port));
	result = sock->ops->connect(sock, (struct sockaddr *) &sa, sizeof(sa), 0);
	if (result < 0) {
		printk(KERN_ERR "BOOTP: connect() failed\n");
		return -1;
	}
	return 0;
}


/*
 *  Bind UDP socket.
 */
static int root_bind_udp_sock(struct socket *sock, u32 addr, u16 port)
{
	struct sockaddr_in sa;
	int result;

	set_sockaddr(&sa, htonl(addr), htonl(port));
	result = sock->ops->bind(sock, (struct sockaddr *) &sa, sizeof(sa));
	if (result < 0) {
		printk(KERN_ERR "BOOTP: bind() failed\n");
		return -1;
	}
	return 0;
}


/*
 *  Send UDP packet.
 */
static inline int root_send_udp(struct socket *sock, void *buf, int size)
{
	u32 oldfs;
	int result;
	struct msghdr msg;
	struct iovec iov;

	oldfs = get_fs();
	set_fs(get_ds());
	iov.iov_base = buf;
	iov.iov_len = size;
	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	result = sock_sendmsg(sock, &msg, size);
	set_fs(oldfs);

	return (result != size);
}


/*
 *  Try to receive UDP packet.
 */
static inline int root_recv_udp(struct socket *sock, void *buf, int size)
{
	u32 oldfs;
	int result;
	struct msghdr msg;
	struct iovec iov;

	oldfs = get_fs();
	set_fs(get_ds());
	iov.iov_base = buf;
	iov.iov_len = size;
	memset(&msg, 0, sizeof(msg));
	msg.msg_flags = MSG_DONTWAIT;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	result = sock_recvmsg(sock, &msg, size, MSG_DONTWAIT);
	set_fs(oldfs);
	return result;
}


/*
 *  Initialize BOOTP extension fields in the request.
 */
static void root_bootp_init_ext(u8 *e)
{
	*e++ = 99;		/* RFC1048 Magic Cookie */
	*e++ = 130;
	*e++ = 83;
	*e++ = 99;
	*e++ = 1;		/* Subnet mask request */
	*e++ = 4;
	e += 4;
	*e++ = 3;		/* Default gateway request */
	*e++ = 4;
	e += 4;
	*e++ = 12;		/* Host name request */
	*e++ = 32;
	e += 32;
	*e++ = 40;		/* NIS Domain name request */
	*e++ = 32;
	e += 32;
	*e++ = 17;		/* Boot path */
	*e++ = 32;
	e += 32;
	*e = 255;		/* End of the list */
}


/*
 *  Deinitialize the BOOTP mechanism.
 */
static void root_bootp_close(void)
{
	if (bootp_xmit_sock)
		sock_release(bootp_xmit_sock);
	if (bootp_recv_sock)
		sock_release(bootp_recv_sock);
	root_del_bootp_route();
	root_free_bootp();
}


/*
 *  Initialize the BOOTP mechanism.
 */
static int root_bootp_open(void)
{
	struct open_dev *openp;
	struct device *dev, *best_dev;

	/*
	 * Select the best interface for BOOTP. We try to select a first
	 * Ethernet-like interface. It's shame I know no simple way how to send
	 * BOOTP's to all interfaces, but it doesn't apply to usual diskless
	 * stations as they don't have multiple interfaces.
	 */

	best_dev = NULL;
	for (openp = open_base; openp != NULL; openp = openp->next) {
		dev = openp->dev;
		if (dev->flags & IFF_BROADCAST) {
			if (!best_dev ||
			   ((best_dev->flags & IFF_NOARP) && !(dev->flags & IFF_NOARP)))
				best_dev = dev;
			}
		}

	if (!best_dev) {
		printk(KERN_ERR "BOOTP: This cannot happen!\n");
		return -1;
	}
	bootp_dev = best_dev;

	/* Allocate memory for BOOTP packets */
	if (root_alloc_bootp())
		return -1;

	/* Construct BOOTP request */
	memset(xmit_bootp, 0, sizeof(struct bootp_pkt));
	xmit_bootp->op = BOOTP_REQUEST;
	get_random_bytes(&xmit_bootp->xid, sizeof(xmit_bootp->xid));
	xmit_bootp->htype = best_dev->type;
	xmit_bootp->hlen = best_dev->addr_len;
	memcpy(xmit_bootp->hw_addr, best_dev->dev_addr, best_dev->addr_len);
	root_bootp_init_ext(xmit_bootp->vendor_area);

#ifdef NFSROOT_BOOTP_DEBUG
	{
		int x;
		printk(KERN_NOTICE "BOOTP: XID=%08x, DE=%s, HT=%02x, HL=%02x, HA=",
			xmit_bootp->xid,
			best_dev->name,
			xmit_bootp->htype,
			xmit_bootp->hlen);
		for(x=0; x<xmit_bootp->hlen; x++)
			printk("%02x", xmit_bootp->hw_addr[x]);
		printk("\n");
	}
#endif

	/* Create default route to that interface */
	if (root_add_bootp_route())
		return -1;

	/* Open the sockets */
	if (root_open_udp_sock(&bootp_xmit_sock) ||
	    root_open_udp_sock(&bootp_recv_sock))
		return -1;

	/* Bind/connect the sockets */
	bootp_xmit_sock->sk->broadcast = 1;
	bootp_xmit_sock->sk->reuse = 1;
	bootp_recv_sock->sk->reuse = 1;
	if (root_bind_udp_sock(bootp_recv_sock, INADDR_ANY, 68) ||
	    root_bind_udp_sock(bootp_xmit_sock, INADDR_ANY, 68) ||
	    root_connect_udp_sock(bootp_xmit_sock, INADDR_BROADCAST, 67))
		return -1;

	return 0;
}


/*
 *  Send BOOTP request.
 */
static int root_bootp_send(u32 jiffies)
{
	xmit_bootp->secs = htons(jiffies / HZ);
	return root_send_udp(bootp_xmit_sock, xmit_bootp, sizeof(struct bootp_pkt));
}


/*
 *  Copy BOOTP-supplied string if not already set.
 */
static int root_bootp_string(char *dest, char *src, int len, int max)
{
	if (*dest || !len)
		return 0;
	if (len > max-1)
		len = max-1;
	strncpy(dest, src, len);
	dest[len] = '\0';
	return 1;
}


/*
 *  Process BOOTP extension.
 */
static void root_do_bootp_ext(u8 *ext)
{
#ifdef NFSROOT_BOOTP_DEBUG
	u8 *c;

	printk("BOOTP: Got extension %02x",*ext);
	for(c=ext+2; c<ext+2+ext[1]; c++)
		printk(" %02x", *c);
	printk("\n");
#endif

	switch (*ext++) {
		case 1:		/* Subnet mask */
			if (netmask == INADDR_NONE)
				memcpy(&netmask, ext+1, 4);
			break;
		case 3:		/* Default gateway */
			if (gateway == INADDR_NONE)
				memcpy(&gateway, ext+1, 4);
			break;
		case 12:	/* Host name */
			root_bootp_string(system_utsname.nodename, ext+1, *ext, __NEW_UTS_LEN);
			break;
		case 40:	/* NIS Domain name */
			root_bootp_string(system_utsname.domainname, ext+1, *ext, __NEW_UTS_LEN);
			break;
		case 17:	/* Root path */
			root_bootp_string(nfs_path, ext+1, *ext, NFS_MAXPATHLEN);
			break;
	}
}


/*
 *  Receive BOOTP request.
 */
static void root_bootp_recv(void)
{
	int len;
	u8 *ext, *end, *opt;

	len = root_recv_udp(bootp_recv_sock, recv_bootp, sizeof(struct bootp_pkt));
	if (len < 0)
		return;

	/* Check consistency of incoming packet */
	if (len < 300 ||			/* See RFC 1542:2.1 */
	    recv_bootp->op != BOOTP_REPLY ||
	    recv_bootp->htype != xmit_bootp->htype ||
	    recv_bootp->hlen != xmit_bootp->hlen ||
	    recv_bootp->xid != xmit_bootp->xid) {
		dprintk("?");
		return;
		}

	/* Record BOOTP packet arrival in the global variables */
	cli();
	if (pkt_arrived) {
		sti();
		return;
	}
	pkt_arrived = ARRIVED_BOOTP;
	sti();
	root_dev = bootp_dev;

	/* Extract basic fields */
	myaddr = recv_bootp->your_ip;
	if (servaddr==INADDR_NONE)
		servaddr = recv_bootp->server_ip;

	/* Parse extensions */
	if (recv_bootp->vendor_area[0] == 99 &&	/* Check magic cookie */
	    recv_bootp->vendor_area[1] == 130 &&
	    recv_bootp->vendor_area[2] == 83 &&
	    recv_bootp->vendor_area[3] == 99) {
		ext = &recv_bootp->vendor_area[4];
		end = (u8 *) recv_bootp + len;
		while (ext < end && *ext != 255) {
			if (*ext == 0)		/* Padding */
				ext++;
			else {
				opt = ext;
				ext += ext[1] + 2;
				if (ext <= end)
					root_do_bootp_ext(opt);
			}
		}
	}
}
#endif



/***************************************************************************

			Dynamic configuration of IP.

 ***************************************************************************/

#ifdef CONFIG_RNFS_DYNAMIC

/*
 *  Determine client and server IP numbers and appropriate device by using
 *  the RARP and BOOTP protocols.
 */
static int root_auto_config(void)
{
	int retries;
	unsigned long timeout, jiff;
	unsigned long start_jiffies;

	/*
	 * If neither BOOTP nor RARP was selected, return with an error. This
	 * routine gets only called when some pieces of information are mis-
	 * sing, and without BOOTP and RARP we are not able to get that in-
	 * formation.
	 */
	if (!bootp_flag && !rarp_flag) {
		printk(KERN_ERR "Root-NFS: Neither RARP nor BOOTP selected.\n");
		return -1;
	}

#ifdef CONFIG_RNFS_BOOTP
	if (bootp_flag && !bootp_dev_count) {
		printk(KERN_ERR "Root-NFS: No suitable device for BOOTP found.\n");
		bootp_flag = 0;
	}
#else
	bootp_flag = 0;
#endif

#ifdef CONFIG_RNFS_RARP
	if (rarp_flag && !rarp_dev_count) {
		printk(KERN_ERR "Root-NFS: No suitable device for RARP found.\n");
		rarp_flag = 0;
	}
#else
	rarp_flag = 0;
#endif

	if (!bootp_flag && !rarp_flag)
		/* Error message already printed */
		return -1;

	/*
	 * Setup RARP and BOOTP protocols
	 */
#ifdef CONFIG_RNFS_RARP
	if (rarp_flag)
		root_rarp_open();
#endif
#ifdef CONFIG_RNFS_BOOTP
	if (bootp_flag && root_bootp_open() < 0) {
		root_bootp_close();
		return -1;
	}
#endif

	/*
	 * Send requests and wait, until we get an answer. This loop
	 * seems to be a terrible waste of CPU time, but actually there is
	 * only one process running at all, so we don't need to use any
	 * scheduler functions.
	 * [Actually we could now, but the nothing else running note still 
	 *  applies.. - AC]
	 */
	printk(KERN_NOTICE "Sending %s%s%s requests...",
		bootp_flag ? "BOOTP" : "",
		bootp_flag && rarp_flag ? " and " : "",
		rarp_flag ? "RARP" : "");
	start_jiffies = jiffies;
	retries = CONF_RETRIES;
	get_random_bytes(&timeout, sizeof(timeout));
	timeout = CONF_BASE_TIMEOUT + (timeout % (unsigned) CONF_TIMEOUT_RANDOM);
	for(;;) {
#ifdef CONFIG_RNFS_BOOTP
		if (bootp_flag && root_bootp_send(jiffies - start_jiffies) < 0) {
			printk(" BOOTP failed!\n");
			root_bootp_close();
			bootp_flag = 0;
			if (!rarp_flag)
				break;
		}
#endif
#ifdef CONFIG_RNFS_RARP
		if (rarp_flag)
			root_rarp_send();
#endif
		printk(".");
		jiff = jiffies + timeout;
		while (jiffies < jiff && !pkt_arrived)
#ifdef CONFIG_RNFS_BOOTP
			root_bootp_recv();
#else
			;
#endif
		if (pkt_arrived)
			break;
		if (! --retries) {
			printk(" timed out!\n");
			break;
		}
		timeout = timeout CONF_TIMEOUT_MULT;
		if (timeout > CONF_TIMEOUT_MAX)
			timeout = CONF_TIMEOUT_MAX;
	}

#ifdef CONFIG_RNFS_RARP
	if (rarp_flag)
		root_rarp_close();
#endif
#ifdef CONFIG_RNFS_BOOTP
	if (bootp_flag)
		root_bootp_close();
#endif

	if (!pkt_arrived)
		return -1;

	printk(" OK\n");
	printk(KERN_NOTICE "Root-NFS: Got %s answer from %s, ",
		(pkt_arrived == ARRIVED_BOOTP) ? "BOOTP" : "RARP",
		in_ntoa(servaddr));
	printk("my address is %s\n", in_ntoa(myaddr));

	return 0;
}
#endif

/* Get default netmask - used to be exported from net/ipv4 */
static unsigned long
ip_get_mask(unsigned long addr)
{
	if (!addr)
		return 0;
	addr = ntohl(addr);
	if (IN_CLASSA(addr))
		return htonl(IN_CLASSA_NET);
	if (IN_CLASSB(addr))
		return htonl(IN_CLASSB_NET);
	if (IN_CLASSC(addr))
		return htonl(IN_CLASSC_NET);
	return 0;
}

/***************************************************************************

			     Parsing of options

 ***************************************************************************/


/*
 *  The following integer options are recognized
 */
static struct nfs_int_opts {
	char *name;
	int  *val;
} root_int_opts[] = {
	{ "port",	&nfs_port },
	{ "rsize",	&nfs_data.rsize },
	{ "wsize",	&nfs_data.wsize },
	{ "timeo",	&nfs_data.timeo },
	{ "retrans",	&nfs_data.retrans },
	{ "acregmin",	&nfs_data.acregmin },
	{ "acregmax",	&nfs_data.acregmax },
	{ "acdirmin",	&nfs_data.acdirmin },
	{ "acdirmax",	&nfs_data.acdirmax },
	{ NULL,		NULL }
};


/*
 *  And now the flag options
 */
static struct nfs_bool_opts {
	char *name;
	int  and_mask;
	int  or_mask;
} root_bool_opts[] = {
	{ "soft",	~NFS_MOUNT_SOFT,	NFS_MOUNT_SOFT },
	{ "hard",	~NFS_MOUNT_SOFT,	0 },
	{ "intr",	~NFS_MOUNT_INTR,	NFS_MOUNT_INTR },
	{ "nointr",	~NFS_MOUNT_INTR,	0 },
	{ "posix",	~NFS_MOUNT_POSIX,	NFS_MOUNT_POSIX },
	{ "noposix",	~NFS_MOUNT_POSIX,	0 },
	{ "cto",	~NFS_MOUNT_NOCTO,	0 },
	{ "nocto",	~NFS_MOUNT_NOCTO,	NFS_MOUNT_NOCTO },
	{ "ac",		~NFS_MOUNT_NOAC,	0 },
	{ "noac",	~NFS_MOUNT_NOAC,	NFS_MOUNT_NOAC },
	{ NULL,		0,			0 }
};


/*
 *  Prepare the NFS data structure and parse any options. This tries to
 *  set as many values in the nfs_data structure as known right now.
 */
static int root_nfs_name(char *name)
{
	char buf[NFS_MAXPATHLEN];
	char *cp, *cq, *options, *val;
	int octets = 0;

	/* It is possible to override the server IP number here */
	cp = cq = name;
	while (octets < 4) {
		while (*cp >= '0' && *cp <= '9')
			cp++;
		if (cp == cq || cp - cq > 3)
			break;
		if (*cp == '.' || octets == 3)
			octets++;
		if (octets < 4)
			cp++;
		cq = cp;
	}
	if (octets == 4 && (*cp == ':' || *cp == '\0')) {
		if (*cp == ':')
			*cp++ = '\0';
		servaddr = in_aton(name);
		name = cp;
	}

	/* Clear the nfs_data structure and setup the server hostname */
	memset(&nfs_data, 0, sizeof(nfs_data));
	strncpy(nfs_data.hostname, in_ntoa(servaddr),
						sizeof(nfs_data.hostname)-1);

	/* Set the name of the directory to mount */
	if (nfs_path[0] == '\0' || strncmp(name, "default", 7))
		strncpy(buf, name, NFS_MAXPATHLEN);
	else
		strncpy(buf, nfs_path, NFS_MAXPATHLEN);
	if ((options = strchr(buf, ',')))
		*options++ = '\0';
	if (!strcmp(buf, "default"))
		strcpy(buf, NFS_ROOT);
	cp = in_ntoa(myaddr);
	if (strlen(buf) + strlen(cp) > NFS_MAXPATHLEN) {
		printk(KERN_ERR "Root-NFS: Pathname for remote directory too long.\n");
		return -1;
	}
	/* update nfs_path with path from nfsroot=... command line parameter */
	if (*buf)
		sprintf(nfs_path, buf, cp);

	/* Set some default values */
	nfs_port          = -1;
	nfs_data.version  = NFS_MNT_VERSION;
	nfs_data.flags    = NFS_MOUNT_NONLM;	/* No lockd in nfs root yet */
	nfs_data.rsize    = NFS_DEF_FILE_IO_BUFFER_SIZE;
	nfs_data.wsize    = NFS_DEF_FILE_IO_BUFFER_SIZE;
	nfs_data.bsize	  = 0;
	nfs_data.timeo    = 7;
	nfs_data.retrans  = 3;
	nfs_data.acregmin = 3;
	nfs_data.acregmax = 60;
	nfs_data.acdirmin = 30;
	nfs_data.acdirmax = 60;

	/* Process any options */
	if (options) {
		cp = strtok(options, ",");
		while (cp) {
			if ((val = strchr(cp, '='))) {
				struct nfs_int_opts *opts = root_int_opts;
				*val++ = '\0';
				while (opts->name && strcmp(opts->name, cp))
					opts++;
				if (opts->name)
					*(opts->val) = (int) simple_strtoul(val, NULL, 10);
			} else {
				struct nfs_bool_opts *opts = root_bool_opts;
				while (opts->name && strcmp(opts->name, cp))
					opts++;
				if (opts->name) {
					nfs_data.flags &= opts->and_mask;
					nfs_data.flags |= opts->or_mask;
				}
			}
			cp = strtok(NULL, ",");
		}
	}
	return 0;
}


/*
 *  Tell the user what's going on.
 */
#ifdef NFSROOT_BOOTP
static void root_nfs_print(void)
{
#define IN_NTOA(x) (((x) == INADDR_NONE) ? "none" : in_ntoa(x))

	printk(KERN_NOTICE "Root-NFS: IP config: dev=%s, ",
		root_dev ? root_dev->name : "none");
	printk("local=%s, ", IN_NTOA(myaddr));
	printk("server=%s, ", IN_NTOA(servaddr));
	printk("gw=%s, ", IN_NTOA(gateway));
	printk("mask=%s, ", IN_NTOA(netmask));
	printk("host=%s, domain=%s\n",
		system_utsname.nodename[0] ? system_utsname.nodename : "none",
		system_utsname.domainname[0] ? system_utsname.domainname : "none");
	printk(KERN_NOTICE "Root-NFS: Mounting %s on server %s as root\n",
		nfs_path, nfs_data.hostname);
	printk(KERN_NOTICE "Root-NFS:     rsize = %d, wsize = %d, timeo = %d, retrans = %d\n",
		nfs_data.rsize, nfs_data.wsize, nfs_data.timeo, nfs_data.retrans);
	printk(KERN_NOTICE "Root-NFS:     acreg (min,max) = (%d,%d), acdir (min,max) = (%d,%d)\n",
		nfs_data.acregmin, nfs_data.acregmax,
		nfs_data.acdirmin, nfs_data.acdirmax);
	printk(KERN_NOTICE "Root-NFS:     port = %d, flags = %08x\n",
		nfs_port, nfs_data.flags);

#undef IN_NTOA
}
#endif


/*
 *  Decode any IP configuration options in the "nfsaddrs" kernel command
 *  line parameter. It consists of option fields separated by colons in
 *  the following order:
 *
 *  <client-ip>:<server-ip>:<gw-ip>:<netmask>:<host name>:<device>:<bootp|rarp>
 *
 *  Any of the fields can be empty which means to use a default value:
 *	<client-ip>	- address given by BOOTP or RARP
 *	<server-ip>	- address of host returning BOOTP or RARP packet
 *	<gw-ip>		- none, or the address returned by BOOTP
 *	<netmask>	- automatically determined from <client-ip>, or the
 *			  one returned by BOOTP
 *	<host name>	- <client-ip> in ASCII notation, or the name returned
 *			  by BOOTP
 *	<device>	- use all available devices for RARP and the first
 *			  one for BOOTP
 *	<bootp|rarp>	- use both protocols to determine my own address
 */
static void root_nfs_addrs(char *addrs)
{
	char *cp, *ip, *dp;
	int num = 0;

	/* Clear all addresses and strings */
	myaddr = servaddr = rarp_serv = gateway = netmask = INADDR_NONE;
	system_utsname.nodename[0] = '\0';
	system_utsname.domainname[0] = '\0';
	user_dev_name[0] = '\0';
	bootp_flag = rarp_flag = 1;

	/* The following is just a shortcut for automatic IP configuration */
	if (!strcmp(addrs, "bootp")) {
		rarp_flag = 0;
		return;
	} else if (!strcmp(addrs, "rarp")) {
		bootp_flag = 0;
		return;
	} else if (!strcmp(addrs, "both")) {
		return;
	}

	/* Parse the whole string */
	ip = addrs;
	while (ip && *ip) {
		if ((cp = strchr(ip, ':')))
			*cp++ = '\0';
		if (strlen(ip) > 0) {
			dprintk("Root-NFS: Config string num %d is \"%s\"\n",
								num, ip);
			switch (num) {
			case 0:
				if ((myaddr = in_aton(ip)) == INADDR_ANY)
					myaddr = INADDR_NONE;
				break;
			case 1:
				if ((servaddr = in_aton(ip)) == INADDR_ANY)
					servaddr = INADDR_NONE;
				break;
			case 2:
				if ((gateway = in_aton(ip)) == INADDR_ANY)
					gateway = INADDR_NONE;
				break;
			case 3:
				if ((netmask = in_aton(ip)) == INADDR_ANY)
					netmask = INADDR_NONE;
				break;
			case 4:
				if ((dp = strchr(ip, '.'))) {
					*dp++ = '\0';
					strncpy(system_utsname.domainname, dp, __NEW_UTS_LEN);
					system_utsname.domainname[__NEW_UTS_LEN] = '\0';
				}
				strncpy(system_utsname.nodename, ip, __NEW_UTS_LEN);
				system_utsname.nodename[__NEW_UTS_LEN] = '\0';
				break;
			case 5:
				strncpy(user_dev_name, ip, IFNAMSIZ);
				user_dev_name[IFNAMSIZ-1] = '\0';
				break;
			case 6:
				if (!strcmp(ip, "rarp"))
					bootp_flag = 0;
				else if (!strcmp(ip, "bootp"))
					rarp_flag = 0;
				else if (strcmp(ip, "both"))
					bootp_flag = rarp_flag = 0;
				break;
			default:
				break;
			}
		}
		ip = cp;
		num++;
	}
	rarp_serv = servaddr;
}


/*
 *  Set the interface address and configure a route to the server.
 */
static int root_nfs_setup(void)
{
	/* Set the default system name in case none was previously found */
	if (!system_utsname.nodename[0]) {
		strncpy(system_utsname.nodename, in_ntoa(myaddr), __NEW_UTS_LEN);
		system_utsname.nodename[__NEW_UTS_LEN] = '\0';
	}

	/* Set the correct netmask */
	if (netmask == INADDR_NONE)
		netmask = ip_get_mask(myaddr);

	/* Setup the device correctly */
	root_dev->family     = AF_INET;
	root_dev->pa_addr    = myaddr;
	root_dev->pa_mask    = netmask;
	root_dev->pa_brdaddr = root_dev->pa_addr | ~root_dev->pa_mask;
	root_dev->pa_dstaddr = 0;

	/*
	 * Now add a route to the server. If there is no gateway given,
	 * the server is on the same subnet, so we establish only a route to
	 * the local network. Otherwise we create a route to the gateway (the
	 * same local network router as in the former case) and then setup a
	 * gatewayed default route. Note that this gives sufficient network
	 * setup even for full system operation in all common cases.
	 */
	if (root_dev_add_route(root_dev, myaddr, netmask, 0))
	{
		printk(KERN_ERR "Root-NFS: Adding of local route failed!\n");
		return -1;
	}

	if (gateway != INADDR_NONE) {	/* Default route */
		if (root_dev_add_route(root_dev, INADDR_ANY, INADDR_ANY, gateway)) {
			printk(KERN_ERR "Root-NFS: Adding of default route failed!\n");
			return -1;
		}
	} else if ((servaddr ^ myaddr) & netmask) {
		printk(KERN_ERR "Root-NFS: Boot server not on local network and no default gateway configured!\n");
		return -1;
	}

	return 0;
}


/*
 *  Get the necessary IP addresses and prepare for mounting the required
 *  NFS filesystem.
 */
int nfs_root_init(char *nfsname, char *nfsaddrs)
{
#ifdef NFSROOT_DEBUG
	nfs_debug |= NFSDBG_ROOT;
#endif

	/*
	 * Decode IP addresses and other configuration info contained
	 * in the nfsaddrs string (which came from the kernel command
	 * line).
	 */
	root_nfs_addrs(nfsaddrs);

	/*
	 * Setup all network devices
	 */
	if (root_dev_open() < 0)
		return -1;

	/*
	 * If the config information is insufficient (e.g., our IP address or
	 * IP address of the boot server is missing or we have multiple network
	 * interfaces and no default was set), use BOOTP or RARP to get the
	 * missing values.
	 *
	 * Note that we don't try to set up correct routes for multiple
	 * interfaces (could be solved by trying icmp echo requests), because
	 * it's only necessary in the rare case of multiple ethernet devices
	 * in the (diskless) system and if the server is on another subnet.
	 * If only one interface is installed, the routing is obvious.
	 */
	if ((myaddr == INADDR_NONE ||
	     servaddr == INADDR_NONE ||
	     (open_base != NULL && open_base->next != NULL))
#ifdef CONFIG_RNFS_DYNAMIC
		&& root_auto_config() < 0
#endif
	   ) {
		root_dev_close();
		return -1;
	}
	if (root_dev == NULL) {
		if (open_base != NULL && open_base->next == NULL) {
			root_dev = open_base->dev;
		} else {
			printk(KERN_ERR "Root-NFS: Multiple devices and no server\n");
			root_dev_close();
			return -1;
		}
	}

	/*
	 * Close all network devices except the device which connects to
	 * server
	 */
	root_dev_close();

	/*
	 * Decode the root directory path name and NFS options from
	 * the kernel command line. This has to go here in order to
	 * be able to use the client IP address for the remote root
	 * directory (necessary for pure RARP booting).
	 */
	if (root_nfs_name(nfsname) < 0)
		return -1;

	/*
	 * Setup devices and routes. The server directory is actually
	 * mounted after init() has been started.
	 */
	if (root_nfs_setup() < 0)
		return -1;

#ifdef NFSROOT_DEBUG
	root_nfs_print();
#endif

	return 0;
}


/***************************************************************************

	       Routines to actually mount the root directory

 ***************************************************************************/
/*
 *  Query server portmapper for the port of a daemon program
 */
static int root_nfs_getport(int program, int version)
{
	struct sockaddr_in	sin;

printk(KERN_NOTICE "Looking up port of RPC %d/%d on %s\n",
	program, version, in_ntoa(servaddr));
	set_sockaddr(&sin, servaddr, 0);
	return rpc_getport_external(&sin, program, version, IPPROTO_UDP);
}


/*
 *  Get portnumbers for mountd and nfsd from server
 *  The RPC layer does support portmapper queries; the only reason to
 *  keep this code is that we may want to use fallback ports. But is there
 *  actually someone who does not run portmap?
 */
static int root_nfs_ports(void)
{
	int	port;

	if (nfs_port < 0) {
		if ((port = root_nfs_getport(NFS_PROGRAM, NFS_VERSION)) < 0) {
			printk(KERN_ERR "Root-NFS: Unable to get nfsd port "
					"number from server, using default\n");
			port = NFS_PORT;
		}
		nfs_port = port;
		dprintk("Root-NFS: Portmapper on server returned %d "
			"as nfsd port\n", port);
	}

	if ((port = root_nfs_getport(NFS_MNT_PROGRAM, NFS_MNT_VERSION)) < 0) {
		printk(KERN_ERR "Root-NFS: Unable to get mountd port "
				"number from server, using default\n");
		port = NFS_MNT_PORT;
	}

	mount_port = htons(port);
	dprintk("Root-NFS: Portmapper on server returned %d "
		"as mountd port\n", port);

	return 0;
}


/*
 *  Get a file handle from the server for the directory which is to be
 *  mounted
 */
static int root_nfs_get_handle(void)
{
	struct sockaddr_in sin;
	int		status;

	set_sockaddr(&sin, servaddr, mount_port);
	status = nfs_mount(&sin, nfs_path, &nfs_data.root);
	if (status < 0)
		printk(KERN_ERR "Root-NFS: Server returned error %d "
				"while mounting %s\n", status, nfs_path);

	return status;
}


/*
 *  Now actually mount the given directory
 */
static int root_nfs_do_mount(struct super_block *sb)
{
	/* Pass the server address to NFS */
	set_sockaddr((struct sockaddr_in *) &nfs_data.addr, servaddr, nfs_port);

	/* Now (finally ;-)) read the super block for mounting */
	if (nfs_read_super(sb, &nfs_data, 1) == NULL)
		return -1;
	return 0;
}


/*
 *  Get the NFS port numbers and file handle, and then read the super-
 *  block for mounting.
 */
int nfs_root_mount(struct super_block *sb)
{
	if (root_nfs_ports() < 0)
		return -1;
	if (root_nfs_get_handle() < 0)
		return -1;
	if (root_nfs_do_mount(sb) < 0)
		return -1;
	return 0;
}
