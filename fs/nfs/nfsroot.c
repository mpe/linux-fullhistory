/*
 *  linux/fs/nfs/nfsroot.c -- version 2.1
 *
 *  Copyright (C) 1995  Gero Kuhlmann <gero@gkminix.han.de>
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
 *
 *
 *	Known bugs and caveats:
 *
 *	- BOOTP code doesn't handle multiple network interfaces properly.
 *	  For now, it uses the first one, trying to prefer ethernet-like
 *	  devices. Not critical as diskless stations are usually single-homed.
 *
 */


/* Define this to allow debugging output */
#undef NFSROOT_DEBUG
#undef NFSROOT_MORE_DEBUG

/* Define the timeout for waiting for a RARP/BOOTP reply */
#define CONF_BASE_TIMEOUT	(HZ*5)	/* Initial timeout: 5 seconds */
#define CONF_RETRIES	 	10	/* 10 retries */
#define CONF_TIMEOUT_RANDOM	(HZ)	/* Maximum amount of randomization */
#define CONF_TIMEOUT_MULT	*5/4	/* Speed of timeout growth */
#define CONF_TIMEOUT_MAX	(HZ*30)	/* Maximum allowed timeout */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>

#include <asm/param.h>
#include <linux/utsname.h>
#include <linux/in.h>
#include <linux/if.h>
#include <linux/inet.h>
#include <linux/net.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#ifdef CONFIG_AX25
#include <net/ax25.h>	/* For AX25_P_IP */
#endif
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/route.h>
#include <linux/nfs.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/in.h>
#include <net/route.h>
#include <net/sock.h>
#include <linux/random.h>
#include <linux/fcntl.h>


/* Range of privileged ports */
#define STARTPORT	600
#define ENDPORT		1023
#define NPORTS		(ENDPORT - STARTPORT + 1)


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
static struct sockaddr_in myaddr;	/* My IP address */
static struct sockaddr_in server;	/* Server IP address */
static struct sockaddr_in gateway;	/* Gateway IP address */
static struct sockaddr_in netmask;	/* Netmask for local subnet */

/* BOOTP/RARP variables */

static int bootp_flag;			/* User said: Use BOOTP! */
static int rarp_flag;			/* User said: Use RARP! */
static int bootp_dev_count = 0;		/* Number of devices allowing BOOTP */
static int rarp_dev_count = 0;		/* Number of devices allowing RARP */

#if defined(CONFIG_RNFS_BOOTP) || defined(CONFIG_RNFS_RARP)
#define CONFIG_RNFS_DYNAMIC		/* Enable dynamic IP config */
volatile static int pkt_arrived;	/* BOOTP/RARP packet detected */

#define ARRIVED_BOOTP 1
#define ARRIVED_RARP 2
#endif

/* NFS-related data */
static struct nfs_mount_data nfs_data;	/* NFS mount info */
static char nfs_path[NFS_MAXPATHLEN];	/* Name of directory to mount */
static int nfs_port;			/* Port to connect to for NFS service */

/* Macro for formatting of addresses in debug dumps */
#define IN_NTOA(x) (((x) == INADDR_NONE) ? "none" : in_ntoa(x))

/* Yes, we use sys_socket, but there's no include file for it */
extern asmlinkage int sys_socket(int family, int type, int protocol);


/***************************************************************************

			Device Handling Subroutines

 ***************************************************************************/

/*
 * Setup and initialize all network devices. If there is a user-prefered
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
#ifdef NFSROOT_DEBUG
	printk(KERN_NOTICE "Root-NFS: Opened %s\n", dev->name);
#endif
		}
	}
	*last = NULL;

	if (!bootp_dev_count && !rarp_dev_count) {
		printk(KERN_ERR "Root-NFS: Unable to open at least one network device\n");
		return -1;
	}
	return 0;
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

	/* If this test doesn't pass, its not IP, or we should ignore it anyway */
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
	if (server.sin_addr.s_addr != INADDR_NONE &&
	    server.sin_addr.s_addr != sip) {
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

	if (myaddr.sin_addr.s_addr == INADDR_NONE) {
		myaddr.sin_family = dev->family;
		myaddr.sin_addr.s_addr = tip;
	}
	if (server.sin_addr.s_addr == INADDR_NONE) {
		server.sin_family = dev->family;
		server.sin_addr.s_addr = sip;
	}
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

static int bootp_xmit_fd = -1;		/* Socket descriptor for transmit */
static struct socket *bootp_xmit_sock;	/* The socket itself */
static int bootp_recv_fd = -1;		/* Socket descriptor for receive */
static struct socket *bootp_recv_sock;	/* The socket itself */

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
	struct rtentry route;

	memset(&route, 0, sizeof(route));
	route.rt_dev = bootp_dev->name;
	route.rt_mss = bootp_dev->mtu;
	route.rt_flags = RTF_UP;
	((struct sockaddr_in *) &(route.rt_dst)) -> sin_addr.s_addr = 0;
	((struct sockaddr_in *) &(route.rt_dst)) -> sin_family = AF_INET;
	((struct sockaddr_in *) &(route.rt_genmask)) -> sin_addr.s_addr = 0;
	((struct sockaddr_in *) &(route.rt_genmask)) -> sin_family = AF_INET;
	if (ip_rt_new(&route)) {
		printk(KERN_ERR "BOOTP: Adding of route failed!\n");
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
	struct rtentry route;

	if (!bootp_have_route)
		return 0;
	memset(&route, 0, sizeof(route));
	((struct sockaddr_in *) &(route.rt_dst)) -> sin_addr.s_addr = 0;
	((struct sockaddr_in *) &(route.rt_genmask)) -> sin_addr.s_addr = 0;
	if (ip_rt_kill(&route)) {
		printk(KERN_ERR "BOOTP: Deleting of route failed!\n");
		return -1;
	}
	bootp_have_route = 0;
	return 0;
}


/*
 *  Open UDP socket.
 */
static int root_open_udp_sock(int *fd, struct socket **sock)
{
	struct file *file;
	struct inode *inode;

	*fd = sys_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (*fd >= 0) {
		file = current->files->fd[*fd];
		inode = file->f_inode;
		*sock = &inode->u.socket_i;
		return 0;
	}

	printk(KERN_ERR "BOOTP: Cannot open UDP socket!\n");
	return -1;
}


/*
 *  Connect UDP socket.
 */
static int root_connect_udp_sock(struct socket *sock, u32 addr, u16 port)
{
	struct sockaddr_in sa;
	int result;

	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(addr);
	sa.sin_port = htons(port);
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

	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(addr);
	sa.sin_port = htons(port);
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
	msg.msg_name = NULL;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_accrights = NULL;
	result = sock->ops->sendmsg(sock, &msg, size, 0, 0);
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
	msg.msg_name = NULL;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_accrights = NULL;
	msg.msg_namelen = 0;
	result = sock->ops->recvmsg(sock, &msg, size, O_NONBLOCK, 0, &msg.msg_namelen);
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
	*e++ = 15;		/* Domain name request */
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
	if (bootp_xmit_fd != -1)
		sys_close(bootp_xmit_fd);
	if (bootp_recv_fd != -1)
		sys_close(bootp_recv_fd);
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
#ifdef NFSROOT_DEBUG
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
	if (root_open_udp_sock(&bootp_xmit_fd, &bootp_xmit_sock) ||
	    root_open_udp_sock(&bootp_recv_fd, &bootp_recv_sock))
		return -1;

	/* Bind/connect the sockets */
	((struct sock *) bootp_xmit_sock->data) -> broadcast = 1;
	((struct sock *) bootp_xmit_sock->data) -> reuse = 1;
	((struct sock *) bootp_recv_sock->data) -> reuse = 1;
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
	u8 *c;
	static int got_bootp_domain = 0;

#ifdef NFSROOT_MORE_DEBUG
	printk("BOOTP: Got extension %02x",*ext);
	for(c=ext+2; c<ext+2+ext[1]; c++)
		printk(" %02x", *c);
	printk("\n");
#endif

	switch (*ext++) {
		case 1:		/* Subnet mask */
			if (netmask.sin_addr.s_addr == INADDR_NONE)
				memcpy(&netmask.sin_addr.s_addr, ext+1, 4);
			break;
		case 3:		/* Default gateway */
			if (gateway.sin_addr.s_addr == INADDR_NONE)
				memcpy(&gateway.sin_addr.s_addr, ext+1, 4);
			break;
		case 12:	/* Host name */
			if (root_bootp_string(system_utsname.nodename, ext+1, *ext, __NEW_UTS_LEN)) {
				c = strchr(system_utsname.nodename, '.');
				if (c) {
					*c++ = 0;
					if (!system_utsname.domainname[0]) {
						strcpy(system_utsname.domainname, c);
						got_bootp_domain = 1;
					}
				}
			}
			break;
		case 15:	/* Domain name */
			if (got_bootp_domain && *ext && ext[1])
				system_utsname.domainname[0] = '\0';
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
#ifdef NFSROOT_DEBUG
		printk("?");
#endif
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
	myaddr.sin_addr.s_addr = recv_bootp->your_ip;
	server.sin_addr.s_addr = recv_bootp->server_ip;

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
	u32 timeout, jiff;
	u32 start_jiffies;
	int selected = 0;

	/* Check devices */

#ifdef CONFIG_RNFS_BOOTP
	if (bootp_flag) {
		if (bootp_dev_count)
			selected = 1;
		else {
			printk(KERN_ERR "BOOTP: No suitable device found.\n");
			bootp_flag = 0;
		}
	}
#else
	bootp_flag = 0;
#endif

#ifdef CONFIG_RNFS_RARP
	if (rarp_flag) {
		if (rarp_dev_count)
			selected = 1;
		else {
			printk(KERN_ERR "RARP: No suitable device found.\n");
			rarp_flag = 0;
		}
	}
#else
	rarp_flag = 0;
#endif

	/* If neither BOOTP nor RARP was selected manually, use both of them */
	if (!selected) {
#ifdef CONFIG_RNFS_BOOTP
		if (bootp_dev_count)
			bootp_flag = 1;
#endif
#ifdef CONFIG_RNFS_RARP
		if (rarp_dev_count)
			rarp_flag = 1;
#endif
		if (!bootp_flag && !rarp_flag)
			return -1;
	}

	/* Setup RARP and BOOTP protocols */
#ifdef CONFIG_RNFS_RARP
	if (rarp_flag)
		root_rarp_open();
#endif
#ifdef CONFIG_RNFS_BOOTP
	if (bootp_flag && root_bootp_open()) {
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
		if (bootp_flag && root_bootp_send(jiffies - start_jiffies)) {
			printk("...BOOTP failed!\n");
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
			printk("timed out!\n");
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

	printk("OK\n");
	printk(KERN_NOTICE "Root-NFS: Got %s answer from %s, ",
		(pkt_arrived == ARRIVED_BOOTP) ? "BOOTP" : "RARP",
		in_ntoa(server.sin_addr.s_addr));
	printk("my address is %s\n", in_ntoa(myaddr.sin_addr.s_addr));

	return 0;
}

#endif

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
 *  Prepare the NFS data structure and parse any options
 */
static int root_nfs_parse(char *name)
{
	char buf[NFS_MAXPATHLEN];
	char *cp, *options, *val;

	/* It is possible to override the server IP number here */
	if (*name >= '0' && *name <= '9' && (cp = strchr(name, ':')) != NULL) {
		*cp++ = '\0';
		server.sin_addr.s_addr = in_aton(name);
		name = cp;
	}

	/* Set the name of the directory to mount */
	cp = in_ntoa(myaddr.sin_addr.s_addr);
	strncpy(buf, name, 255);
	if ((options = strchr(buf, ',')))
		*options++ = '\0';
	if (strlen(buf) + strlen(cp) > NFS_MAXPATHLEN) {
		printk(KERN_ERR "Root-NFS: Pathname for remote directory too long\n");
		return -1;
	}
	sprintf(nfs_path, buf, cp);

	/* Set some default values */
	nfs_port          = -1;
	nfs_data.version  = NFS_MOUNT_VERSION;
	nfs_data.flags    = 0;
	nfs_data.rsize    = NFS_DEF_FILE_IO_BUFFER_SIZE;
	nfs_data.wsize    = NFS_DEF_FILE_IO_BUFFER_SIZE;
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
#ifdef NFSROOT_DEBUG
static void root_nfs_print(void)
{
	printk(KERN_NOTICE "Root-NFS: IP config: dev=%s, ",
		root_dev ? root_dev->name : "none");
	printk("local=%s, ", IN_NTOA(myaddr.sin_addr.s_addr));
	printk("server=%s, ", IN_NTOA(server.sin_addr.s_addr));
	printk("gw=%s, ", IN_NTOA(gateway.sin_addr.s_addr));
	printk("mask=%s, ", IN_NTOA(netmask.sin_addr.s_addr));
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
}
#endif


/*
 *  Parse any IP configuration options (the "nfsaddrs" parameter).
 *  The parameter consists of option fields separated by colons. It can start
 *  with device name and possibly with auto-config type ("bootp" or "rarp")
 *  followed by own IP address, IP address of the server, IP address of
 *  default gateway, local netmask and a host name. Any of the addresses can
 *  be blank to indicate that default value should be used.
 */
static void root_nfs_ip_config(char *addrs)
{
	char *cp, *ip, *dp;
	int num = 0;

	/* Clear all addresses and strings */
	myaddr.sin_family = server.sin_family =
	    gateway.sin_family = netmask.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = server.sin_addr.s_addr =
	    gateway.sin_addr.s_addr = netmask.sin_addr.s_addr = INADDR_NONE;
	system_utsname.nodename[0] = '\0';
	system_utsname.domainname[0] = '\0';
	user_dev_name[0] = '\0';
	bootp_flag = rarp_flag = 0;

	/* Check for device name and BOOTP/RARP flags */
	ip = addrs;
	while (ip && *ip && (*ip < '0' || *ip > '9')) {
		if ((cp = strchr(ip, ':')))
			*cp++ = '\0';
		if (*ip) {
			if (!strcmp(ip, "rarp"))
				rarp_flag = 1;
			else if (!strcmp(ip, "bootp"))
				bootp_flag = 1;
			else if (!user_dev_name[0]) {
				strncpy(user_dev_name, ip, IFNAMSIZ);
				user_dev_name[IFNAMSIZ-1] = '\0';
			}
		}
		ip = cp;
	}

	/* Parse the IP addresses */
	while (ip && *ip) {
		if ((cp = strchr(ip, ':')))
			*cp++ = '\0';
		if (strlen(ip) > 0) {
			switch (num) {
			case 0:
				myaddr.sin_addr.s_addr = in_aton(ip);
				break;
			case 1:
				server.sin_addr.s_addr = in_aton(ip);
				break;
			case 2:
				gateway.sin_addr.s_addr = in_aton(ip);
				break;
			case 3:
				netmask.sin_addr.s_addr = in_aton(ip);
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
			default:
				break;
			}
		}
		ip = cp;
		num++;
	}
#ifdef NFSROOT_DEBUG
	printk(KERN_NOTICE "Root-NFS: IP options: dev=%s, RARP=%d, BOOTP=%d, ",
		user_dev_name[0] ? user_dev_name : "none",
		rarp_flag,
		bootp_flag);
	printk("local=%s, ", IN_NTOA(myaddr.sin_addr.s_addr));
	printk("server=%s, ", IN_NTOA(server.sin_addr.s_addr));
	printk("gw=%s, ", IN_NTOA(gateway.sin_addr.s_addr));
	printk("mask=%s, ", IN_NTOA(netmask.sin_addr.s_addr));
	printk("host=%s, domain=%s\n",
		system_utsname.nodename[0] ? system_utsname.nodename : "none",
		system_utsname.domainname[0] ? system_utsname.domainname : "none");
#endif
}


/*
 *  Set the interface address and configure a route to the server.
 */
static int root_nfs_setup(void)
{
	struct rtentry route;

	/* Set the default system name in case none was previously found */
	if (!system_utsname.nodename[0]) {
		strncpy(system_utsname.nodename, in_ntoa(myaddr.sin_addr.s_addr), __NEW_UTS_LEN);
		system_utsname.nodename[__NEW_UTS_LEN] = '\0';
	}

	/* Setup the server hostname */
	strncpy(nfs_data.hostname, in_ntoa(server.sin_addr.s_addr), 255);

	/* Set the correct netmask */
	if (netmask.sin_addr.s_addr == INADDR_NONE)
		netmask.sin_addr.s_addr = ip_get_mask(myaddr.sin_addr.s_addr);

	/* Setup the device correctly */
	root_dev->family     = myaddr.sin_family;
	root_dev->pa_addr    = myaddr.sin_addr.s_addr;
	root_dev->pa_mask    = netmask.sin_addr.s_addr;
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
	memset(&route, 0, sizeof(route));	/* Local subnet route */
	route.rt_dev = root_dev->name;
	route.rt_mss = root_dev->mtu;
	route.rt_flags = RTF_UP;
	*((struct sockaddr_in *) &(route.rt_dst)) = myaddr;
	(((struct sockaddr_in *) &(route.rt_dst))) -> sin_addr.s_addr &= netmask.sin_addr.s_addr;
	*((struct sockaddr_in *) &(route.rt_genmask)) = netmask;
	if (ip_rt_new(&route)) {
		printk(KERN_ERR "Root-NFS: Adding of local route failed!\n");
		return -1;
	}

	if (gateway.sin_addr.s_addr != INADDR_NONE) {	/* Default route */
		(((struct sockaddr_in *) &(route.rt_dst))) -> sin_addr.s_addr = 0;
		(((struct sockaddr_in *) &(route.rt_genmask))) -> sin_addr.s_addr = 0;
		*((struct sockaddr_in *) &(route.rt_gateway)) = gateway;
		route.rt_flags |= RTF_GATEWAY;
		if ((gateway.sin_addr.s_addr ^ myaddr.sin_addr.s_addr) & netmask.sin_addr.s_addr) {
			printk(KERN_ERR "Root-NFS: Gateway not on local network!\n");
			return -1;
		}
		if (ip_rt_new(&route)) {
			printk(KERN_ERR "Root-NFS: Adding of default route failed!\n");
			return -1;
		}
	} else if ((server.sin_addr.s_addr ^ myaddr.sin_addr.s_addr) & netmask.sin_addr.s_addr) {
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
	/*
	 * Get local and server IP address. First check for network config
	 * parameters in the command line parameter.
	 */
	root_nfs_ip_config(nfsaddrs);

	/* Parse NFS options */
	if (root_nfs_parse(nfsname))
		return -1;

	/* Setup all network devices */
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
	if ((myaddr.sin_addr.s_addr == INADDR_NONE ||
	     server.sin_addr.s_addr == INADDR_NONE ||
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
			/* I hope this cannot happen */
			printk(KERN_ERR "Root-NFS: 1 == 0!\n");
			root_dev_close();
			return -1;
		}
	}
	/*
	 * Close all network devices except the device which connects to
	 * server
	 */
	root_dev_close();

	/* Setup devices and routes */
	if (root_nfs_setup())
		return -1;

#ifdef NFSROOT_DEBUG
	root_nfs_print();
#endif

	return 0;
}


/***************************************************************************

	       Routines to actually mount the root directory

 ***************************************************************************/

static struct file nfs_file;		/* File descriptor containing socket */
static struct inode nfs_inode;		/* Inode containing socket */
static int *rpc_packet = NULL;		/* RPC packet */


/*
 *  Open a UDP socket.
 */
static int root_nfs_open(void)
{
	struct file *filp;

	/* Open the socket */
	if ((nfs_data.fd = sys_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		printk(KERN_ERR "Root-NFS: Cannot open UDP socket!\n");
		return -1;
	}
	/*
	 * Copy the file and inode data area so that we can remove the
	 * file lateron without killing the socket. After all this the
	 * closing routine just needs to remove the file pointer from the
	 * init-task descriptor.
	 */
	filp = current->files->fd[nfs_data.fd];
	memcpy(&nfs_file, filp, sizeof(struct file));
	nfs_file.f_next = nfs_file.f_prev = NULL;
	current->files->fd[nfs_data.fd] = &nfs_file;
	filp->f_count = 0;		/* Free the file descriptor */

	memcpy(&nfs_inode, nfs_file.f_inode, sizeof(struct inode));
	nfs_inode.i_hash_next = nfs_inode.i_hash_prev = NULL;
	nfs_inode.i_next = nfs_inode.i_prev = NULL;
	clear_inode(nfs_file.f_inode);
	nfs_file.f_inode = &nfs_inode;
	nfs_inode.u.socket_i.inode = &nfs_inode;
	nfs_file.private_data = NULL;

	return 0;
}


/*
 *  Close the UDP file descriptor. The main part of preserving the socket
 *  has already been done after opening it. Now we have to remove the file
 *  descriptor from the init task.
 */
static void root_nfs_close(int close_all)
{
	/* Remove the file from the list of open files */
	current->files->fd[nfs_data.fd] = NULL;
	if (current->files->count > 0)
		current->files->count--;

	/* Clear memory used by the RPC packet */
	if (rpc_packet != NULL)
		kfree_s(rpc_packet, nfs_data.wsize + 1024);

	/*
	 * In case of an error we also have to close the socket again
	 * (sigh)
	 */
	if (close_all) {
		nfs_inode.u.socket_i.inode = NULL;	/* The inode is already
							 * cleared */
		if (nfs_file.f_op->release)
			nfs_file.f_op->release(&nfs_inode, &nfs_file);
	}
}


/*
 *  Find a suitable listening port and bind to it
 */
static int root_nfs_bind(void)
{
	int res = -1;
	short port = STARTPORT;
	struct sockaddr_in *sin = &myaddr;
	int i;

	if (nfs_inode.u.socket_i.ops->bind) {
		for (i = 0; i < NPORTS && res < 0; i++) {
			sin->sin_port = htons(port++);
			if (port > ENDPORT) {
				port = STARTPORT;
			}
			res = nfs_inode.u.socket_i.ops->bind(&nfs_inode.u.socket_i,
							     (struct sockaddr *)sin, sizeof(struct sockaddr_in));
		}
	}
	if (res < 0) {
		printk(KERN_ERR "Root-NFS: Cannot find a suitable listening port\n");
		root_nfs_close(1);
		return -1;
	}
#ifdef NFSROOT_DEBUG
	printk(KERN_NOTICE "Root-NFS: Binding to listening port %d\n", port);
#endif
	return 0;
}


/*
 *  Send an RPC request and wait for the answer
 */
static int *root_nfs_call(int *end)
{
	struct file *filp;
	struct socket *sock;
	int dummylen;
	static struct nfs_server s = {
		&nfs_file,		/* struct file *	 */
		0,			/* struct rsock *	 */
		{
		    0, "",
		},			/* toaddr		 */
		0,			/* lock			 */
		NULL,			/* wait queue		 */
		NFS_MOUNT_SOFT,		/* flags		 */
		0, 0,			/* rsize, wsize		 */
		0,			/* timeo		 */
		0,			/* retrans		 */
		3 * HZ, 60 * HZ, 30 * HZ, 60 * HZ, "\0"
	};

	filp = &nfs_file;
	sock = &((filp->f_inode)->u.socket_i);

	/* extract the other end of the socket into s->toaddr */
	sock->ops->getname(sock, &(s.toaddr), &dummylen, 1);
	((struct sockaddr_in *) &s.toaddr)->sin_port   = server.sin_port;
	((struct sockaddr_in *) &s.toaddr)->sin_family = server.sin_family;
	((struct sockaddr_in *) &s.toaddr)->sin_addr.s_addr = server.sin_addr.s_addr;

	s.rsock = rpc_makesock(filp);
	s.flags = nfs_data.flags;
	s.rsize = nfs_data.rsize;
	s.wsize = nfs_data.wsize;
	s.timeo = nfs_data.timeo * HZ / 10;
	s.retrans = nfs_data.retrans;
	strcpy(s.hostname, nfs_data.hostname);

	/*
	 * First connect the UDP socket to a server port, then send the
	 * packet out, and finally check wether the answer is OK.
	 */
	if (nfs_inode.u.socket_i.ops->connect &&
	    nfs_inode.u.socket_i.ops->connect(&nfs_inode.u.socket_i,
						(struct sockaddr *) &server,
						sizeof(struct sockaddr_in),
						nfs_file.f_flags) < 0)
		            return NULL;
	if (nfs_rpc_call(&s, rpc_packet, end, nfs_data.wsize) < 0)
		return NULL;
	return rpc_verify(rpc_packet);
}


/*
 *  Create an RPC packet header
 */
static int *root_nfs_header(int proc, int program, int version)
{
	int groups[] = { 0, NOGROUP };

	if (rpc_packet == NULL) {
		if (!(rpc_packet = kmalloc(nfs_data.wsize + 1024, GFP_NFS))) {
			printk(KERN_ERR "Root-NFS: Cannot allocate UDP buffer\n");
			return NULL;
		}
	}
	return rpc_header(rpc_packet, proc, program, version, 0, 0, groups);
}


/*
 *  Query server portmapper for the port of a daemon program
 */
static int root_nfs_get_port(int program, int version)
{
	int *p;

	/* Prepare header for portmap request */
	server.sin_port = htons(NFS_PMAP_PORT);
	p = root_nfs_header(NFS_PMAP_PROC, NFS_PMAP_PROGRAM, NFS_PMAP_VERSION);
	if (!p)
		return -1;

	/* Set arguments for portmapper */
	*p++ = htonl(program);
	*p++ = htonl(version);
	*p++ = htonl(IPPROTO_UDP);
	*p++ = 0;

	/* Send request to server portmapper */
	if ((p = root_nfs_call(p)) == NULL)
		return -1;

	return ntohl(*p);
}


/*
 *  Get portnumbers for mountd and nfsd from server
 */
static int root_nfs_ports(void)
{
	int port;

	if (nfs_port < 0) {
		if ((port = root_nfs_get_port(NFS_NFS_PROGRAM, NFS_NFS_VERSION)) < 0) {
			printk(KERN_ERR "Root-NFS: Unable to get nfsd port number from server, using default\n");
			port = NFS_NFS_PORT;
		}
		nfs_port = port;
#ifdef NFSROOT_DEBUG
		printk(KERN_NOTICE "Root-NFS: Portmapper on server returned %d as nfsd port\n", port);
#endif
	}
	if ((port = root_nfs_get_port(NFS_MOUNT_PROGRAM, NFS_MOUNT_VERSION)) < 0) {
		printk(KERN_ERR "Root-NFS: Unable to get mountd port number from server, using default\n");
		port = NFS_MOUNT_PORT;
	}
	server.sin_port = htons(port);
#ifdef NFSROOT_DEBUG
	printk(KERN_NOTICE "Root-NFS: Portmapper on server returned %d as mountd port\n", port);
#endif

	return 0;
}


/*
 *  Get a file handle from the server for the directory which is to be
 *  mounted
 */
static int root_nfs_get_handle(void)
{
	int len, status, *p;

	/* Prepare header for mountd request */
	p = root_nfs_header(NFS_MOUNT_PROC, NFS_MOUNT_PROGRAM, NFS_MOUNT_VERSION);
	if (!p) {
		root_nfs_close(1);
		return -1;
	}
	/* Set arguments for mountd */
	len = strlen(nfs_path);
	*p++ = htonl(len);
	memcpy(p, nfs_path, len);
	len = (len + 3) >> 2;
	p[len] = 0;
	p += len;

	/* Send request to server mountd */
	if ((p = root_nfs_call(p)) == NULL) {
		root_nfs_close(1);
		return -1;
	}
	status = ntohl(*p++);
	if (status == 0) {
		nfs_data.root = *((struct nfs_fh *) p);
		printk(KERN_NOTICE "Root-NFS: Got file handle for %s via RPC\n", nfs_path);
	} else {
		printk(KERN_ERR "Root-NFS: Server returned error %d while mounting %s\n",
			status, nfs_path);
		root_nfs_close(1);
		return -1;
	}

	return 0;
}


/*
 *  Now actually mount the given directory
 */
static int root_nfs_do_mount(struct super_block *sb)
{
	/* First connect to the nfsd port on the server */
	server.sin_port = htons(nfs_port);
	nfs_data.addr = server;
	if (nfs_inode.u.socket_i.ops->connect &&
	    nfs_inode.u.socket_i.ops->connect(&nfs_inode.u.socket_i,
						(struct sockaddr *) &server,
						sizeof(struct sockaddr_in),
						nfs_file.f_flags) < 0) {
		root_nfs_close(1);
		return -1;
	}
	/* Now (finally ;-)) read the super block for mounting */
	if (nfs_read_super(sb, &nfs_data, 1) == NULL) {
		root_nfs_close(1);
		return -1;
	}
	return 0;
}


/*
 *  Get the NFS port numbers and file handle, and then read the super-
 *  block for mounting.
 */
int nfs_root_mount(struct super_block *sb)
{
	if (root_nfs_open() < 0)
		return -1;
	if (root_nfs_bind() < 0)
		return -1;
	if (root_nfs_ports() < 0)
		return -1;
	if (root_nfs_get_handle() < 0)
		return -1;
	if (root_nfs_do_mount(sb) < 0)
		return -1;
	root_nfs_close(0);
	return 0;
}
