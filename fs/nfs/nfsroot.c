/*
 *  linux/fs/nfs/nfsroot.c
 *
 *  Copyright (C) 1995  Gero Kuhlmann <gero@gkminix.han.de>
 *
 *  Allow an NFS filesystem to be mounted as root. The way this works
 *  is to first determine the local IP address via RARP. Then handle
 *  the RPC negotiation with the system which replied to the RARP. The
 *  actual mounting is done later, when init() is running. In addition
 *  it's possible to avoid using RARP if the necessary addresses are
 *  provided on the kernel command line. This is necessary to use boot-
 *  roms which use bootp instead of RARP.
 *
 *	Changes:
 *
 *	Alan Cox	:	Removed get_address name clash with FPU.
 *	Alan Cox	:	Reformatted a bit.
 *	Michael Rausch  :	Fixed recognition of an incoming RARP answer.
 *
 */


/* Define this to allow debugging output */
#undef NFSROOT_DEBUG 1

/* Define the timeout for waiting for a RARP reply */
#define RARP_TIMEOUT	30	/* 30 seconds */
#define RARP_RETRIES	 5	/* 5 retries */

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


/* Range of privileged ports */
#define STARTPORT	600
#define ENDPORT		1023
#define NPORTS		(ENDPORT - STARTPORT + 1)



struct open_dev {
	struct device *dev;
	unsigned short old_flags;
	struct open_dev *next;
};

static struct open_dev *open_base = NULL;
static struct device *root_dev = NULL;
static struct sockaddr_in myaddr;	/* My IP address */
static struct sockaddr_in server;	/* Server IP address */
static struct sockaddr_in gateway;	/* Gateway IP address */
static struct sockaddr_in netmask;	/* Netmask for local subnet */
static struct nfs_mount_data nfs_data;	/* NFS mount info */
static char nfs_path[NFS_MAXPATHLEN];	/* Name of directory to mount */
static int nfs_port;			/* Port to connect to for NFS service */



/***************************************************************************

			Device Handling Subroutines

 ***************************************************************************/

/*
 * Setup and initialize all network devices
 */
static int root_dev_open(void)
{
	struct open_dev *openp;
	struct device *dev;
	unsigned short old_flags;
	int num = 0;

	for (dev = dev_base; dev != NULL; dev = dev->next) {
		if (dev->type < ARPHRD_SLIP &&
		    dev->family == AF_INET &&
		    !(dev->flags & (IFF_LOOPBACK | IFF_POINTOPOINT))) {
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
			openp->next = open_base;
			open_base = openp;
			num++;
		}
	}

	if (num == 0) {
		printk(KERN_ERR "NFS: Unable to open at least one network device\n");
		return -1;
	}
#ifdef NFSROOT_DEBUG
	printk(KERN_NOTICE "NFS: Opened %d network interfaces\n", num);
#endif
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
	if (root_dev != NULL) {
		sti();
		kfree_skb(skb, FREE_READ);
		return 0;
	}
	root_dev = dev;
	sti();

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
static int root_rarp_send(void)
{
	struct open_dev *openp;
	struct device *dev;
	int num = 0;

	/* always print this message so user knows what's going on... */
	printk(KERN_NOTICE "NFS: Sending RARP request...\n");
	for (openp = open_base; openp != NULL; openp = openp->next) {
		dev = openp->dev;
		if (!(dev->flags & IFF_NOARP)) {
			arp_send(ARPOP_RREQUEST, ETH_P_RARP, 0, dev, 0, NULL,
				 dev->dev_addr, dev->dev_addr);
			num++;
		}
	}

	if (num == 0) {
		printk(KERN_ERR "NFS: Couldn't find device to send RARP request to\n");
		return -1;
	}
	return 0;
}


/*
 *  Determine client and server IP numbers and appropriate device by using
 *  the RARP protocol.
 */
static int do_rarp(void)
{
	int retries = 0;
	unsigned long timeout = 0;
	volatile struct device **root_dev_ptr = (volatile struct device **) &root_dev;

	/* Setup RARP protocol */
	root_rarp_open();

	/*
	 * Send RARP request and wait, until we get an answer. This loop
	 * seems to be a terrible waste of cpu time, but actually there is
	 * no process running at all, so we don't need to use any
	 * scheduler functions.
	 * [Actually we could now, but the nothing else running note still 
	 *  applies.. - AC]
	 */
	for (retries = 0; retries < RARP_RETRIES && *root_dev_ptr == NULL; retries++) {
		if (root_rarp_send() < 0)
			break;
		timeout = jiffies + (RARP_TIMEOUT * HZ);
		while (jiffies < timeout && *root_dev_ptr == NULL)
			;
	}

	root_rarp_close();
	if (*root_dev_ptr == NULL && timeout > 0) {
		printk(KERN_ERR "NFS: Timed out while waiting for RARP answer\n");
		return -1;
	}
	printk(KERN_NOTICE "NFS: ");
	printk("Got RARP answer from %s, ", in_ntoa(server.sin_addr.s_addr));
	printk("my address is %s\n", in_ntoa(myaddr.sin_addr.s_addr));

	return 0;
}




/***************************************************************************

			Routines to setup NFS

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

	/* Set the default system name in case none was previously found */
	if (!system_utsname.nodename[0]) {
		strncpy(system_utsname.nodename, in_ntoa(myaddr.sin_addr.s_addr), __NEW_UTS_LEN);
		system_utsname.nodename[__NEW_UTS_LEN] = '\0';
	}
	/* It is possible to override the host IP number here */
	if (*name >= '0' && *name <= '9' && (cp = strchr(name, ':')) != NULL) {
		*cp++ = '\0';
		server.sin_addr.s_addr = in_aton(name);
		name = cp;
	}

	/* Setup the server hostname */
	cp = in_ntoa(server.sin_addr.s_addr);
	strncpy(nfs_data.hostname, cp, 255);

	/* Set the name of the directory to mount */
	cp = in_ntoa(myaddr.sin_addr.s_addr);
	strncpy(buf, name, 255);
	if ((options = strchr(buf, ',')))
		*options++ = '\0';
	if (strlen(buf) + strlen(cp) > NFS_MAXPATHLEN) {
		printk(KERN_ERR "NFS: Pathname for remote directory too long\n");
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
static void root_nfs_print(void)
{
#ifdef NFSROOT_DEBUG
	printk(KERN_NOTICE "NFS: Mounting %s on server %s as root\n",
		nfs_path, nfs_data.hostname);
	printk(KERN_NOTICE "NFS:     rsize = %d, wsize = %d, timeo = %d, retrans = %d\n",
		nfs_data.rsize, nfs_data.wsize, nfs_data.timeo, nfs_data.retrans);
	printk(KERN_NOTICE "NFS:     acreg (min,max) = (%d,%d), acdir (min,max) = (%d,%d)\n",
		nfs_data.acregmin, nfs_data.acregmax,
		nfs_data.acdirmin, nfs_data.acdirmax);
	printk(KERN_NOTICE "NFS:     port = %d, flags = %08x\n",
		nfs_port, nfs_data.flags);
#endif
}


/*
 *  Parse any IP addresses
 */
static void root_nfs_addrs(char *addrs)
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

	/*
	 * Parse the address field. It contains 4 IP addresses which are
	 * separated by colons: Field 0 = my own address Field 1 = server
	 * address Field 2 = gateway address Field 3 = netmask address
	 * Field 4 = client host name
	 */
	ip = addrs;
	while (ip && *ip) {
		if ((cp = strchr(ip, ':')))
			*cp++ = '\0';
		if (strlen(ip) > 0) {
#ifdef NFSROOT_DEBUG
			printk(KERN_NOTICE "NFS: IP address num %d is \"%s\"\n", num, ip);
#endif
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
}


/*
 *  Set the interface address and configure a route to the server.
 */
static void root_nfs_setup(void)
{
	struct rtentry route;

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
	 * the server is on our own local network, so a host route is
	 * sufficient. Otherwise we first have to create a host route to
	 * the gateway, and then setup a gatewayed host route to the
	 * server. Note that it's not possible to setup a network route
	 * because we don't know the network mask of the server network.
	 */
	memset(&route, 0, sizeof(route));
	route.rt_dev = root_dev->name;
	route.rt_mss = root_dev->mtu;
	route.rt_flags = RTF_HOST | RTF_UP;
	*((struct sockaddr_in *) &(route.rt_genmask)) = netmask;

	if (gateway.sin_addr.s_addr == INADDR_NONE ||
	    gateway.sin_addr.s_addr == server.sin_addr.s_addr ||
	    !((server.sin_addr.s_addr ^ root_dev->pa_addr) & root_dev->pa_mask)) {
		*((struct sockaddr_in *) &(route.rt_dst)) = server;
		ip_rt_new(&route);
	} else {
		*((struct sockaddr_in *) &(route.rt_dst)) = gateway;
		ip_rt_new(&route);
		route.rt_flags |= RTF_GATEWAY;
		*((struct sockaddr_in *) &(route.rt_gateway)) = gateway;
		*((struct sockaddr_in *) &(route.rt_dst)) = server;
		ip_rt_new(&route);
	}
}


/*
 *  Get the necessary IP addresses and prepare for mounting the required
 *  NFS filesystem.
 */
int nfs_root_init(char *nfsname, char *nfsaddrs)
{
	/* Setup all network devices */
	if (root_dev_open() < 0)
		return -1;

	/*
	 * Get local and server IP address. First check for addresses in
	 * command line parameter. If one of the IP addresses is missing,
	 * or there's more than one network interface in the system, use
	 * RARP to get the missing values and routing information. If all
	 * addresses are given, the best way to find a proper routing is
	 * to use icmp echo requests ("ping"), but that would add a lot of
	 * code to this module, which is only really necessary in the rare
	 * case of multiple ethernet devices in the (diskless) system and
	 * if the server is on another subnet (otherwise RARP can serve as
	 * a ping substitute). If only one device is installed the routing
	 * is obvious.
	 */
	root_nfs_addrs(nfsaddrs);
	if ((myaddr.sin_addr.s_addr == INADDR_NONE ||
	     server.sin_addr.s_addr == INADDR_NONE ||
	     (open_base != NULL && open_base->next != NULL)) && do_rarp() < 0) {
		root_dev_close();
		return -1;
	}
	if (root_dev == NULL) {
		if (open_base != NULL && open_base->next == NULL) {
			root_dev = open_base->dev;
		} else {
			printk(KERN_ERR "NFS: Unable to find routing to server\n");
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
	 * Initialize the global variables necessary for NFS. The server
	 * directory is actually mounted after init() has been started.
	 */
	if (root_nfs_parse(nfsname) < 0)
		return -1;
	root_nfs_print();
	root_nfs_setup();
	return 0;
}




/***************************************************************************

		Routines to actually mount the root directory

 ***************************************************************************/

static struct file nfs_file;		/* File descriptor containing socket */
static struct inode nfs_inode;		/* Inode containing socket */
static int *rpc_packet = NULL;		/* RPC packet */

extern asmlinkage int sys_socket(int family, int type, int protocol);


/*
 *  Open a UDP socket.
 */
static int root_nfs_open(void)
{
	struct file *filp;

	/* Open the socket */
	if ((nfs_data.fd = sys_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		printk(KERN_ERR "NFS: Cannot open UDP socket\n");
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
		printk(KERN_ERR "NFS: Cannot find a suitable listening port\n");
		root_nfs_close(1);
		return -1;
	}
#ifdef NFSROOT_DEBUG
	printk(KERN_NOTICE "NFS: Binding to listening port %d\n", port);
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
			printk(KERN_ERR "NFS: Cannot allocate UDP buffer\n");
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
			printk(KERN_ERR "NFS: Unable to get nfsd port number from server, using default\n");
			port = NFS_NFS_PORT;
		}
		nfs_port = port;
#ifdef NFSROOT_DEBUG
		printk(KERN_NOTICE "NFS: Portmapper on server returned %d as nfsd port\n", port);
#endif
	}
	if ((port = root_nfs_get_port(NFS_MOUNT_PROGRAM, NFS_MOUNT_VERSION)) < 0) {
		printk(KERN_ERR "NFS: Unable to get mountd port number from server, using default\n");
		port = NFS_MOUNT_PORT;
	}
	server.sin_port = htons(port);
#ifdef NFSROOT_DEBUG
	printk(KERN_NOTICE "NFS: Portmapper on server returned %d as mountd port\n", port);
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
		printk(KERN_NOTICE "NFS: ");
		printk("Got file handle for %s via RPC\n", nfs_path);
	} else {
		printk(KERN_ERR "NFS: Server returned error %d while mounting %s\n",
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
