/* $Id: ioctl32.c,v 1.8 1997/06/04 13:05:15 jj Exp $
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/ioctl.h>
#include <linux/if.h>
#include <linux/malloc.h>
#include <linux/hdreg.h>
#include <linux/md.h>
#include <linux/kd.h>
#include <linux/route.h>
#include <linux/netlink.h>

#include <asm/types.h>
#include <asm/uaccess.h>

/* As gcc will warn about casting u32 to some ptr, we have to cast it to
 * unsigned long first, and that's what is A() for.
 * You just do (void *)A(x), instead of having to type (void *)((unsigned long)x)
 * or instead of just (void *)x, which will produce warnings.
 */
#define A(x) ((unsigned long)x)

extern asmlinkage int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);

static int w_long(unsigned int fd, unsigned int cmd, u32 arg)
{
	unsigned long old_fs = get_fs();
	int err;
	unsigned long val;
	
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&val);
	set_fs (old_fs);
	if (!err && put_user(val, (u32 *)A(arg)))
		return -EFAULT;
	return err;
}
 
struct ifmap32 {
	u32 mem_start;
	u32 mem_end;
	unsigned short base_addr;
	unsigned char irq;
	unsigned char dma;
	unsigned char port;
};

struct ifreq32 {
#define IFHWADDRLEN     6
#define IFNAMSIZ        16
        union {
                char    ifrn_name[IFNAMSIZ];            /* if name, e.g. "en0" */
        } ifr_ifrn;
        union {
                struct  sockaddr ifru_addr;
                struct  sockaddr ifru_dstaddr;
                struct  sockaddr ifru_broadaddr;
                struct  sockaddr ifru_netmask;
                struct  sockaddr ifru_hwaddr;
                short   ifru_flags;
                int     ifru_ivalue;
                int     ifru_mtu;
                struct  ifmap32 ifru_map;
                char    ifru_slave[IFNAMSIZ];   /* Just fits the size */
                __kernel_caddr_t32 ifru_data;
        } ifr_ifru;
};

struct ifconf32 {
        int     ifc_len;                        /* size of buffer       */
        __kernel_caddr_t32  ifcbuf;
};

static inline int dev_ifconf(unsigned int fd, u32 arg)
{
	struct ifconf32 ifc32;
	struct ifconf ifc;
	struct ifreq32 *ifr32;
	struct ifreq *ifr;
	unsigned long old_fs;
	unsigned int i, j;
	int err;

	if (copy_from_user(&ifc32, (struct ifconf32 *)A(arg), sizeof(struct ifconf32)))
		return -EFAULT;
	ifc.ifc_len = ((ifc32.ifc_len / sizeof (struct ifreq32)) + 1) * sizeof (struct ifreq);
	ifc.ifc_buf = kmalloc (ifc.ifc_len, GFP_KERNEL);
	if (!ifc.ifc_buf) return -ENOMEM;
	ifr = ifc.ifc_req;
	ifr32 = (struct ifreq32 *)A(ifc32.ifcbuf);
	for (i = 0; i < ifc32.ifc_len; i += sizeof (struct ifreq32)) {
		if (copy_from_user(ifr++, ifr32++, sizeof (struct ifreq32))) {
			kfree (ifc.ifc_buf);
			return -EFAULT;
		}
	}
	old_fs = get_fs(); set_fs (KERNEL_DS);
	err = sys_ioctl (fd, SIOCGIFCONF, (unsigned long)&ifc);	
	set_fs (old_fs);
	if (!err) {
		ifr = ifc.ifc_req;
		ifr32 = (struct ifreq32 *)A(ifc32.ifcbuf);
		for (i = 0, j = 0; i < ifc32.ifc_len && j < ifc.ifc_len;
		     i += sizeof (struct ifreq32), j += sizeof (struct ifreq)) {
			if (copy_to_user(ifr32++, ifr++, sizeof (struct ifreq32))) {
				err = -EFAULT;
				break;
			}
		}
		if (!err) {
			if (i <= ifc32.ifc_len)
				ifc32.ifc_len = i;
			else
				ifc32.ifc_len = i - sizeof (struct ifreq32);
			if (copy_to_user((struct ifconf32 *)A(arg), &ifc32, sizeof(struct ifconf32)))
				err = -EFAULT;
		}
	}
	kfree (ifc.ifc_buf);
	return err;
}

static inline int dev_ifsioc(unsigned int fd, unsigned int cmd, u32 arg)
{
	struct ifreq ifr;
	unsigned long old_fs;
	int err;
	
	if (cmd == SIOCSIFMAP) {
		if (copy_from_user(&ifr, (struct ifreq32 *)A(arg), sizeof(ifr.ifr_name)) ||
		    __get_user(ifr.ifr_map.mem_start, &(((struct ifreq32 *)A(arg))->ifr_ifru.ifru_map.mem_start)) ||
		    __get_user(ifr.ifr_map.mem_end, &(((struct ifreq32 *)A(arg))->ifr_ifru.ifru_map.mem_end)) ||
		    __get_user(ifr.ifr_map.base_addr, &(((struct ifreq32 *)A(arg))->ifr_ifru.ifru_map.base_addr)) ||
		    __get_user(ifr.ifr_map.irq, &(((struct ifreq32 *)A(arg))->ifr_ifru.ifru_map.irq)) ||
		    __get_user(ifr.ifr_map.dma, &(((struct ifreq32 *)A(arg))->ifr_ifru.ifru_map.dma)) ||
		    __get_user(ifr.ifr_map.port, &(((struct ifreq32 *)A(arg))->ifr_ifru.ifru_map.port)))
			return -EFAULT;
	} else {
		if (copy_from_user(&ifr, (struct ifreq32 *)A(arg), sizeof(struct ifreq32)))
			return -EFAULT;
	}
	old_fs = get_fs();
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, cmd, (unsigned long)&ifr);
	set_fs (old_fs);
	if (!err) {
		switch (cmd) {
		case SIOCGIFFLAGS:
		case SIOCGIFMETRIC:
		case SIOCGIFMTU:
		case SIOCGIFMEM:
		case SIOCGIFHWADDR:
		case SIOGIFINDEX:
		case SIOCGIFADDR:
		case SIOCGIFBRDADDR:
		case SIOCGIFDSTADDR:
		case SIOCGIFNETMASK:
			if (copy_to_user((struct ifreq32 *)A(arg), &ifr, sizeof(struct ifreq32)))
				return -EFAULT;
			break;
		case SIOCGIFMAP:
			if (copy_to_user((struct ifreq32 *)A(arg), &ifr, sizeof(ifr.ifr_name)) ||
			    __put_user(ifr.ifr_map.mem_start, &(((struct ifreq32 *)A(arg))->ifr_ifru.ifru_map.mem_start)) ||
			    __put_user(ifr.ifr_map.mem_end, &(((struct ifreq32 *)A(arg))->ifr_ifru.ifru_map.mem_end)) ||
			    __put_user(ifr.ifr_map.base_addr, &(((struct ifreq32 *)A(arg))->ifr_ifru.ifru_map.base_addr)) ||
			    __put_user(ifr.ifr_map.irq, &(((struct ifreq32 *)A(arg))->ifr_ifru.ifru_map.irq)) ||
			    __put_user(ifr.ifr_map.dma, &(((struct ifreq32 *)A(arg))->ifr_ifru.ifru_map.dma)) ||
			    __put_user(ifr.ifr_map.port, &(((struct ifreq32 *)A(arg))->ifr_ifru.ifru_map.port)))
				return -EFAULT;
			break;
		}
	}
	return err;
}

struct rtentry32 {
        u32   		rt_pad1;
        struct sockaddr rt_dst;         /* target address               */
        struct sockaddr rt_gateway;     /* gateway addr (RTF_GATEWAY)   */
        struct sockaddr rt_genmask;     /* target network mask (IP)     */
        unsigned short  rt_flags;
        short           rt_pad2;
        u32   		rt_pad3;
        unsigned char   rt_tos;
        unsigned char   rt_class;
        short           rt_pad4;
        short           rt_metric;      /* +1 for binary compatibility! */
        /* char * */ u32 rt_dev;        /* forcing the device at add    */
        u32   		rt_mtu;         /* per route MTU/Window         */
        u32   		rt_window;      /* Window clamping              */
        unsigned short  rt_irtt;        /* Initial RTT                  */

};

static inline int routing_ioctl(unsigned int fd, unsigned int cmd, u32 arg)
{
	struct rtentry r;
	char devname[16];
	u32 rtdev;
	int ret;
	unsigned long old_fs = get_fs();
	
	if (get_user (r.rt_pad1, &(((struct rtentry32 *)A(arg))->rt_pad1)) ||
	    copy_from_user (&r.rt_dst, &(((struct rtentry32 *)A(arg))->rt_dst), 3 * sizeof(struct sockaddr)) ||
	    __get_user (r.rt_flags, &(((struct rtentry32 *)A(arg))->rt_flags)) ||
	    __get_user (r.rt_pad2, &(((struct rtentry32 *)A(arg))->rt_pad2)) ||
	    __get_user (r.rt_pad3, &(((struct rtentry32 *)A(arg))->rt_pad3)) ||
	    __get_user (r.rt_tos, &(((struct rtentry32 *)A(arg))->rt_tos)) ||
	    __get_user (r.rt_class, &(((struct rtentry32 *)A(arg))->rt_class)) ||
	    __get_user (r.rt_pad4, &(((struct rtentry32 *)A(arg))->rt_pad4)) ||
	    __get_user (r.rt_metric, &(((struct rtentry32 *)A(arg))->rt_metric)) ||
	    __get_user (r.rt_mtu, &(((struct rtentry32 *)A(arg))->rt_mtu)) ||
	    __get_user (r.rt_window, &(((struct rtentry32 *)A(arg))->rt_window)) ||
	    __get_user (r.rt_irtt, &(((struct rtentry32 *)A(arg))->rt_irtt)) ||
	    __get_user (rtdev, &(((struct rtentry32 *)A(arg))->rt_dev)) ||
	    (rtdev && copy_from_user (devname, (char *)A(rtdev), 15)))
		return -EFAULT;
	if (rtdev) {
		r.rt_dev = devname; devname[15] = 0;
	} else
		r.rt_dev = 0;
	set_fs (KERNEL_DS);
	ret = sys_ioctl (fd, cmd, (long)&r);
	set_fs (old_fs);
	return ret;
}

struct nlmsghdr32 {
        u32   		nlmsg_len;      /* Length of message including header */
        u32   		nlmsg_type;     /* Message type */
        u32   		nlmsg_seq;      /* Sequence number */
        u32   		nlmsg_pid;      /* Sending process PID */
        unsigned char   nlmsg_data[0];
};

struct in_rtmsg32 {
        struct in_addr  rtmsg_prefix;
        struct in_addr  rtmsg_gateway;
        unsigned        rtmsg_flags;
        u32   		rtmsg_mtu;
        u32   		rtmsg_window;
        unsigned short  rtmsg_rtt;
        short           rtmsg_metric;
        unsigned char   rtmsg_tos;
        unsigned char   rtmsg_class;
        unsigned char   rtmsg_prefixlen;
        unsigned char   rtmsg_reserved;
        int             rtmsg_ifindex;
};

struct in_ifmsg32 {
        struct sockaddr ifmsg_lladdr;
        struct in_addr  ifmsg_prefix;
        struct in_addr  ifmsg_brd;
        unsigned        ifmsg_flags;
        u32   		ifmsg_mtu;
        short           ifmsg_metric;
        unsigned char   ifmsg_prefixlen;
        unsigned char   ifmsg_reserved;
        int             ifmsg_index;
        char            ifmsg_name[16];
};

static inline int rtmsg_ioctl(unsigned int fd, u32 arg)
{
	struct {
		struct nlmsghdr n;
		union {
			struct in_rtmsg rt;
			struct in_ifmsg iff;
			struct in_rtctlmsg ctl;
			struct in_rtrulemsg rule;
		} u;
	} nn;
	char *p;
	int ret;
	unsigned long old_fs = get_fs();
	
	if (get_user (nn.n.nlmsg_len, &(((struct nlmsghdr32 *)A(arg))->nlmsg_len)) ||
	    __get_user (nn.n.nlmsg_type, &(((struct nlmsghdr32 *)A(arg))->nlmsg_type)) ||
	    __get_user (nn.n.nlmsg_seq, &(((struct nlmsghdr32 *)A(arg))->nlmsg_seq)) ||
	    __get_user (nn.n.nlmsg_pid, &(((struct nlmsghdr32 *)A(arg))->nlmsg_pid)) ||
	    __get_user (nn.n.nlmsg_data[0], &(((struct nlmsghdr32 *)A(arg))->nlmsg_data[0])))
		return -EFAULT;
	p = ((char *)(&nn.n)) + sizeof(struct nlmsghdr);
	arg += sizeof(struct nlmsghdr32);
	switch (nn.n.nlmsg_type) {
	case RTMSG_NEWRULE:
	case RTMSG_DELRULE:
		if (nn.n.nlmsg_len < sizeof(struct nlmsghdr32) + sizeof(struct in_rtrulemsg)
		    - sizeof(struct in_rtmsg) + sizeof(struct in_rtmsg32))
			return -EINVAL;
		if (copy_from_user (p, (struct in_rtrulemsg *)A(arg), sizeof(struct in_rtrulemsg) - sizeof(struct in_rtmsg)))
			return -EFAULT;
		nn.n.nlmsg_len = sizeof(struct nlmsghdr) + sizeof(struct in_rtrulemsg);
		p += sizeof (struct in_rtrulemsg) - sizeof(struct in_rtmsg);
		arg += sizeof (struct in_rtrulemsg) - sizeof(struct in_rtmsg);
		goto newroute;
	case RTMSG_NEWROUTE:
	case RTMSG_DELROUTE:
		if (nn.n.nlmsg_len < sizeof(struct nlmsghdr32) + sizeof(struct in_rtmsg))
			return -EINVAL;
		nn.n.nlmsg_len = sizeof(struct nlmsghdr) + sizeof(struct in_rtmsg);
newroute:
		if (copy_from_user (p, (struct in_rtmsg32 *)A(arg), 2*sizeof(struct in_addr) + sizeof(unsigned)) ||
	    	    __get_user (((struct in_rtmsg *)p)->rtmsg_mtu, &((struct in_rtmsg32 *)A(arg))->rtmsg_mtu) ||
	    	    __get_user (((struct in_rtmsg *)p)->rtmsg_window, &((struct in_rtmsg32 *)A(arg))->rtmsg_window) ||
		    copy_from_user (&(((struct in_rtmsg *)p)->rtmsg_rtt), &((struct in_rtmsg32 *)A(arg))->rtmsg_rtt, 
			2 * sizeof(short) + 4 + sizeof(int)))
			return -EFAULT;
		break;
	case RTMSG_NEWDEVICE:
	case RTMSG_DELDEVICE:
		if (nn.n.nlmsg_len < sizeof(struct nlmsghdr32) + sizeof(struct in_ifmsg))
			return -EINVAL;
		nn.n.nlmsg_len = sizeof(struct nlmsghdr) + sizeof(struct in_ifmsg);
		if (copy_from_user (p, (struct in_ifmsg32 *)A(arg), 
			sizeof(struct sockaddr) + 2*sizeof(struct in_addr) + sizeof(unsigned)) ||
	    	    __get_user (((struct in_ifmsg *)p)->ifmsg_mtu, &((struct in_ifmsg32 *)A(arg))->ifmsg_mtu) ||
		    copy_from_user (&(((struct in_ifmsg *)p)->ifmsg_metric), &((struct in_ifmsg32 *)A(arg))->ifmsg_metric, 
			sizeof(short) + 2 + sizeof(int) + 16))
			return -EFAULT;
		break;
	case RTMSG_CONTROL:
		if (nn.n.nlmsg_len < sizeof(struct nlmsghdr32) + sizeof(struct in_rtctlmsg))
			return -EINVAL;
		nn.n.nlmsg_len = sizeof(struct nlmsghdr) + sizeof(struct in_rtctlmsg);
		if (copy_from_user (p, (struct in_rtctlmsg *)A(arg), sizeof(struct in_rtctlmsg)))
			return -EFAULT;
		break;
	}
	set_fs (KERNEL_DS);
	ret = sys_ioctl (fd, SIOCRTMSG, (long)&(nn.n));
	set_fs (old_fs);
	return ret;
}

struct hd_geometry32 {
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	u32 start;
};
                        
static inline int hdio_getgeo(unsigned int fd, u32 arg)
{
	unsigned long old_fs = get_fs();
	struct hd_geometry geo;
	int err;
	
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, HDIO_GETGEO, (unsigned long)&geo);
	set_fs (old_fs);
	if (!err) {
		if (copy_to_user ((struct hd_geometry32 *)A(arg), &geo, 4) ||
		    __put_user (geo.start, &(((struct hd_geometry32 *)A(arg))->start)))
			return -EFAULT;
	}
	return err;
}

asmlinkage int sys32_ioctl(unsigned int fd, unsigned int cmd, u32 arg)
{
	struct file * filp;
	int error = -EBADF;

	lock_kernel();
	if (fd >= NR_OPEN || !(filp = current->files->fd[fd]))
		goto out;
	if (!filp->f_op || !filp->f_op->ioctl) {
		error = sys_ioctl (fd, cmd, (unsigned long)arg);
		goto out;
	}
	error = -EFAULT;
	switch (cmd) {
	case SIOCGIFCONF:
		error = dev_ifconf(fd, arg);
		goto out;
		
	case SIOCGIFFLAGS:
	case SIOCSIFFLAGS:
	case SIOCGIFMETRIC:
	case SIOCSIFMETRIC:
	case SIOCGIFMTU:
	case SIOCSIFMTU:
	case SIOCGIFMEM:
	case SIOCSIFMEM:
	case SIOCGIFHWADDR:
	case SIOCSIFHWADDR:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
	case SIOGIFINDEX:
	case SIOCGIFMAP:
	case SIOCSIFMAP:
	case SIOCGIFADDR:
	case SIOCSIFADDR:
	case SIOCGIFBRDADDR:
	case SIOCSIFBRDADDR:
	case SIOCGIFDSTADDR:
	case SIOCSIFDSTADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFNETMASK:
		error = dev_ifsioc(fd, cmd, arg);
		goto out;
		
	case SIOCADDRT:
	case SIOCDELRT:
		error = routing_ioctl(fd, cmd, arg);
		goto out;

	case SIOCRTMSG:
		error = rtmsg_ioctl(fd, arg);
		goto out;
		
	case HDIO_GETGEO:
		error = hdio_getgeo(fd, arg);
		goto out;
		
	case BLKRAGET:
	case BLKGETSIZE:
		error = w_long(fd, cmd, arg);
		goto out;

	/* List here exlicitly which ioctl's are known to have
	 * compatable types passed or none at all...
	 */

	/* Bit T */
	case TCGETA:
	case TCSETA:
	case TCSETAW:
	case TCSETAF:
	case TCSBRK:
	case TCXONC:
	case TCFLSH:
	case TCGETS:
	case TCSETS:
	case TCSETSW:
	case TCSETSF:
	case TIOCLINUX:

	/* Little t */
	case TIOCGETD:
	case TIOCSETD:
	case TIOCEXCL:
	case TIOCNXCL:
	case TIOCCONS:
	case TIOCGSOFTCAR:
	case TIOCSSOFTCAR:
	case TIOCSWINSZ:
	case TIOCGWINSZ:
	case TIOCMGET:
	case TIOCMBIC:
	case TIOCMBIS:
	case TIOCMSET:
	case TIOCPKT:
	case TIOCNOTTY:
	case TIOCSTI:
	case TIOCOUTQ:
	case TIOCSPGRP:
	case TIOCGPGRP:
	case TIOCSCTTY:

	/* Little f */
	case FIOCLEX:
	case FIONCLEX:
	case FIOASYNC:
	case FIONBIO:
	case FIONREAD: /* This is also TIOCINQ */
	
	/* 0x12 */
	case BLKRRPART:
	case BLKFLSBUF:
	case BLKRASET:
	
	/* 0x09 */
	case REGISTER_DEV:
	case START_MD:
	case STOP_MD:
	
	/* Big K */
	case PIO_FONT:
	case GIO_FONT:
	case KDSIGACCEPT:
	case KDGETKEYCODE:
	case KDSETKEYCODE:

	/* Socket level stuff */
	case FIOSETOWN:
	case SIOCSPGRP:
	case FIOGETOWN:
	case SIOCGPGRP:
	case SIOCATMARK:
	case SIOCGSTAMP:
	case SIOCSIFLINK:
	case SIOCSIFENCAP:
	case SIOCGIFENCAP:
	case SIOCSIFBR:
	case SIOCGIFBR:
	case SIOCSARP:
	case SIOCGARP:
	case SIOCDARP:
	case SIOCADDDLCI:
	case SIOCDELDLCI:
		error = sys_ioctl (fd, cmd, (unsigned long)arg);
		goto out;
		break;

	default:
		printk("sys32_ioctl: Unknown cmd fd(%d) cmd(%08x) arg(%08x)\n",
		       (int)fd, (unsigned int)cmd, (unsigned int)arg);
		error = -EINVAL;
		goto out;
		break;
	}
out:
	unlock_kernel();
	return error;
}
