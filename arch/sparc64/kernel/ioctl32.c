/* $Id: ioctl32.c,v 1.55 1998/11/17 07:43:17 davem Exp $
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997  Jakub Jelinek  (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
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
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/vt.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fd.h>
#include <linux/if_ppp.h>
#include <linux/mtio.h>
#include <linux/cdrom.h>
#include <linux/loop.h>
#include <linux/auto_fs.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <linux/fb.h>
#include <linux/ext2_fs.h>

#include <scsi/scsi.h>
/* Ugly hack. */
#undef __KERNEL__
#include <scsi/scsi_ioctl.h>
#define __KERNEL__
#include <scsi/sg.h>

#include <asm/types.h>
#include <asm/uaccess.h>
#include <asm/fbio.h>
#include <asm/kbio.h>
#include <asm/vuid_event.h>
#include <asm/rtc.h>
#include <asm/openpromio.h>
#include <asm/envctrl.h>
#include <asm/audioio.h>

#include <linux/soundcard.h>

/* Use this to get at 32-bit user passed pointers. 
   See sys_sparc32.c for description about these. */
#define A(__x) ((unsigned long)(__x))
#define AA(__x)				\
({	unsigned long __ret;		\
	__asm__ ("srl	%0, 0, %0"	\
		 : "=r" (__ret)		\
		 : "0" (__x));		\
	__ret;				\
})

/* Aiee. Someone does not find a difference between int and long */
#define EXT2_IOC32_GETFLAGS               _IOR('f', 1, int)
#define EXT2_IOC32_SETFLAGS               _IOW('f', 2, int)
#define EXT2_IOC32_GETVERSION             _IOR('v', 1, int)
#define EXT2_IOC32_SETVERSION             _IOW('v', 2, int)

extern asmlinkage int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);

static int w_long(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	int err;
	unsigned long val;
	
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&val);
	set_fs (old_fs);
	if (!err && put_user(val, (u32 *)arg))
		return -EFAULT;
	return err;
}
 
static int rw_long(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	int err;
	unsigned long val;
	
	if(get_user(val, (u32 *)arg))
		return -EFAULT;
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&val);
	set_fs (old_fs);
	if (!err && put_user(val, (u32 *)arg))
		return -EFAULT;
	return err;
}

static int do_ext2_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case EXT2_IOC32_GETFLAGS: cmd = EXT2_IOC_GETFLAGS; break;
	case EXT2_IOC32_SETFLAGS: cmd = EXT2_IOC_SETFLAGS; break;
	case EXT2_IOC32_GETVERSION: cmd = EXT2_IOC_GETVERSION; break;
	case EXT2_IOC32_SETVERSION: cmd = EXT2_IOC_SETVERSION; break;
	}
	return sys_ioctl(fd, cmd, arg);
}
 
struct timeval32 {
	int tv_sec;
	int tv_usec;
};

static int do_siocgstamp(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct timeval32 *up = (struct timeval32 *)arg;
	struct timeval ktv;
	mm_segment_t old_fs = get_fs();
	int err;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&ktv);
	set_fs(old_fs);
	if(!err) {
		err = put_user(ktv.tv_sec, &up->tv_sec);
		err |= __put_user(ktv.tv_usec, &up->tv_usec);
	}
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

static inline int dev_ifconf(unsigned int fd, unsigned long arg)
{
	struct ifconf32 ifc32;
	struct ifconf ifc;
	struct ifreq32 *ifr32;
	struct ifreq *ifr;
	mm_segment_t old_fs;
	unsigned int i, j;
	int err;

	if (copy_from_user(&ifc32, (struct ifconf32 *)arg, sizeof(struct ifconf32)))
		return -EFAULT;

	if(ifc32.ifcbuf == 0) {
		ifc32.ifc_len = 0;
		ifc.ifc_len = 0;
		ifc.ifc_buf = NULL;
	} else {
		ifc.ifc_len = ((ifc32.ifc_len / sizeof (struct ifreq32)) + 1) *
			sizeof (struct ifreq);
		ifc.ifc_buf = kmalloc (ifc.ifc_len, GFP_KERNEL);
		if (!ifc.ifc_buf)
			return -ENOMEM;
	}
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
			if (copy_to_user((struct ifconf32 *)arg, &ifc32, sizeof(struct ifconf32)))
				err = -EFAULT;
		}
	}
	if(ifc.ifc_buf != NULL)
		kfree (ifc.ifc_buf);
	return err;
}

static inline int dev_ifsioc(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ifreq ifr;
	mm_segment_t old_fs;
	int err;
	
	switch (cmd) {
	case SIOCSIFMAP:
		err = copy_from_user(&ifr, (struct ifreq32 *)arg, sizeof(ifr.ifr_name));
		err |= __get_user(ifr.ifr_map.mem_start, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.mem_start));
		err |= __get_user(ifr.ifr_map.mem_end, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.mem_end));
		err |= __get_user(ifr.ifr_map.base_addr, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.base_addr));
		err |= __get_user(ifr.ifr_map.irq, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.irq));
		err |= __get_user(ifr.ifr_map.dma, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.dma));
		err |= __get_user(ifr.ifr_map.port, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.port));
		if (err)
			return -EFAULT;
		break;
	case SIOCGPPPSTATS:
	case SIOCGPPPCSTATS:
	case SIOCGPPPVER:
		if (copy_from_user(&ifr, (struct ifreq32 *)arg, sizeof(struct ifreq32)))
			return -EFAULT;
		ifr.ifr_data = (__kernel_caddr_t)get_free_page(GFP_KERNEL);
		if (!ifr.ifr_data)
			return -EAGAIN;
		break;
	default:
		if (copy_from_user(&ifr, (struct ifreq32 *)arg, sizeof(struct ifreq32)))
			return -EFAULT;
		break;
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
		case SIOCGIFINDEX:
		case SIOCGIFADDR:
		case SIOCGIFBRDADDR:
		case SIOCGIFDSTADDR:
		case SIOCGIFNETMASK:
			if (copy_to_user((struct ifreq32 *)arg, &ifr, sizeof(struct ifreq32)))
				return -EFAULT;
			break;
		case SIOCGPPPSTATS:
		case SIOCGPPPCSTATS:
		case SIOCGPPPVER:
		{
			u32 data;
			int len;

			__get_user(data, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_data));
			if(cmd == SIOCGPPPVER)
				len = strlen(PPP_VERSION) + 1;
			else if(cmd == SIOCGPPPCSTATS)
				len = sizeof(struct ppp_comp_stats);
			else
				len = sizeof(struct ppp_stats);

			if (copy_to_user((char *)A(data), ifr.ifr_data, len))
				return -EFAULT;
			break;
		}
		case SIOCGIFMAP:
			err = copy_to_user((struct ifreq32 *)arg, &ifr, sizeof(ifr.ifr_name));
			err |= __put_user(ifr.ifr_map.mem_start, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.mem_start));
			err |= __put_user(ifr.ifr_map.mem_end, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.mem_end));
			err |= __put_user(ifr.ifr_map.base_addr, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.base_addr));
			err |= __put_user(ifr.ifr_map.irq, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.irq));
			err |= __put_user(ifr.ifr_map.dma, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.dma));
			err |= __put_user(ifr.ifr_map.port, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.port));
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

static inline int routing_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct rtentry r;
	char devname[16];
	u32 rtdev;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	ret = copy_from_user (&r.rt_dst, &(((struct rtentry32 *)arg)->rt_dst), 3 * sizeof(struct sockaddr));
	ret |= __get_user (r.rt_flags, &(((struct rtentry32 *)arg)->rt_flags));
	ret |= __get_user (r.rt_metric, &(((struct rtentry32 *)arg)->rt_metric));
	ret |= __get_user (r.rt_mtu, &(((struct rtentry32 *)arg)->rt_mtu));
	ret |= __get_user (r.rt_window, &(((struct rtentry32 *)arg)->rt_window));
	ret |= __get_user (r.rt_irtt, &(((struct rtentry32 *)arg)->rt_irtt));
	ret |= __get_user (rtdev, &(((struct rtentry32 *)arg)->rt_dev));
	if (rtdev) {
		ret |= copy_from_user (devname, (char *)A(rtdev), 15);
		r.rt_dev = devname; devname[15] = 0;
	} else
		r.rt_dev = 0;
	if (ret)
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_ioctl (fd, cmd, (long)&r);
	set_fs (old_fs);
	return ret;
}

struct hd_geometry32 {
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	u32 start;
};
                        
static inline int hdio_getgeo(unsigned int fd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct hd_geometry geo;
	int err;
	
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, HDIO_GETGEO, (unsigned long)&geo);
	set_fs (old_fs);
	if (!err) {
		err = copy_to_user ((struct hd_geometry32 *)arg, &geo, 4);
		err |= __put_user (geo.start, &(((struct hd_geometry32 *)arg)->start));
	}
	return err;
}

struct  fbcmap32 {
	int             index;          /* first element (0 origin) */
	int             count;
	u32		red;
	u32		green;
	u32		blue;
};

#define FBIOPUTCMAP32	_IOW('F', 3, struct fbcmap32)
#define FBIOGETCMAP32	_IOW('F', 4, struct fbcmap32)

static inline int fbiogetputcmap(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct fbcmap f;
	int ret;
	char red[256], green[256], blue[256];
	u32 r, g, b;
	mm_segment_t old_fs = get_fs();
	
	ret = get_user(f.index, &(((struct fbcmap32 *)arg)->index));
	ret |= __get_user(f.count, &(((struct fbcmap32 *)arg)->count));
	ret |= __get_user(r, &(((struct fbcmap32 *)arg)->red));
	ret |= __get_user(g, &(((struct fbcmap32 *)arg)->green));
	ret |= __get_user(b, &(((struct fbcmap32 *)arg)->blue));
	if (ret)
		return -EFAULT;
	if ((f.index < 0) || (f.index > 255)) return -EINVAL;
	if (f.index + f.count > 256)
		f.count = 256 - f.index;
	if (cmd == FBIOPUTCMAP32) {
		ret = copy_from_user (red, (char *)A(r), f.count);
		ret |= copy_from_user (green, (char *)A(g), f.count);
		ret |= copy_from_user (blue, (char *)A(b), f.count);
		if (ret)
			return -EFAULT;
	}
	f.red = red; f.green = green; f.blue = blue;
	set_fs (KERNEL_DS);
	ret = sys_ioctl (fd, (cmd == FBIOPUTCMAP32) ? FBIOPUTCMAP_SPARC : FBIOGETCMAP_SPARC, (long)&f);
	set_fs (old_fs);
	if (!ret && cmd == FBIOGETCMAP32) {
		ret = copy_to_user ((char *)A(r), red, f.count);
		ret |= copy_to_user ((char *)A(g), green, f.count);
		ret |= copy_to_user ((char *)A(b), blue, f.count);
	}
	return ret;
}

struct fbcursor32 {
	short set;		/* what to set, choose from the list above */
	short enable;		/* cursor on/off */
	struct fbcurpos pos;	/* cursor position */
	struct fbcurpos hot;	/* cursor hot spot */
	struct fbcmap32 cmap;	/* color map info */
	struct fbcurpos size;	/* cursor bit map size */
	u32	image;		/* cursor image bits */
	u32	mask;		/* cursor mask bits */
};
	
#define FBIOSCURSOR32	_IOW('F', 24, struct fbcursor32)
#define FBIOGCURSOR32	_IOW('F', 25, struct fbcursor32)

static inline int fbiogscursor(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct fbcursor f;
	int ret;
	char red[2], green[2], blue[2];
	char image[128], mask[128];
	u32 r, g, b;
	u32 m, i;
	mm_segment_t old_fs = get_fs();
	
	ret = copy_from_user (&f, (struct fbcursor32 *)arg, 2 * sizeof (short) + 2 * sizeof(struct fbcurpos));
	ret |= __get_user(f.size.fbx, &(((struct fbcursor32 *)arg)->size.fbx));
	ret |= __get_user(f.size.fby, &(((struct fbcursor32 *)arg)->size.fby));
	ret |= __get_user(f.cmap.index, &(((struct fbcursor32 *)arg)->cmap.index));
	ret |= __get_user(f.cmap.count, &(((struct fbcursor32 *)arg)->cmap.count));
	ret |= __get_user(r, &(((struct fbcursor32 *)arg)->cmap.red));
	ret |= __get_user(g, &(((struct fbcursor32 *)arg)->cmap.green));
	ret |= __get_user(b, &(((struct fbcursor32 *)arg)->cmap.blue));
	ret |= __get_user(m, &(((struct fbcursor32 *)arg)->mask));
	ret |= __get_user(i, &(((struct fbcursor32 *)arg)->image));
	if (ret)
		return -EFAULT;
	if (f.set & FB_CUR_SETCMAP) {
		if ((uint) f.size.fby > 32)
			return -EINVAL;
		ret = copy_from_user (mask, (char *)A(m), f.size.fby * 4);
		ret |= copy_from_user (image, (char *)A(i), f.size.fby * 4);
		if (ret)
			return -EFAULT;
		f.image = image; f.mask = mask;
	}
	if (f.set & FB_CUR_SETCMAP) {
		ret = copy_from_user (red, (char *)A(r), 2);
		ret |= copy_from_user (green, (char *)A(g), 2);
		ret |= copy_from_user (blue, (char *)A(b), 2);
		if (ret)
			return -EFAULT;
		f.cmap.red = red; f.cmap.green = green; f.cmap.blue = blue;
	}
	set_fs (KERNEL_DS);
	ret = sys_ioctl (fd, FBIOSCURSOR, (long)&f);
	set_fs (old_fs);
	return ret;
}

struct fb_fix_screeninfo32 {
	char			id[16];
        __kernel_caddr_t32	smem_start;
	__u32			smem_len;
	__u32			type;
	__u32			type_aux;
	__u32			visual;
	__u16			xpanstep;
	__u16			ypanstep;
	__u16			ywrapstep;
	__u32			line_length;
        __kernel_caddr_t32	mmio_start;
	__u32			mmio_len;
	__u32			accel;
	__u16			reserved[3];
};

struct fb_cmap32 {
	__u32			start;
	__u32			len;
	__kernel_caddr_t32	red;
	__kernel_caddr_t32	green;
	__kernel_caddr_t32	blue;
	__kernel_caddr_t32	transp;
};

static int fb_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	u32 red = 0, green = 0, blue = 0, transp = 0;
	struct fb_fix_screeninfo fix;
	struct fb_cmap cmap;
	void *karg;
	int err = 0;

	memset(&cmap, 0, sizeof(cmap));
	switch (cmd) {
	case FBIOGET_FSCREENINFO:
		karg = &fix;
		break;
	case FBIOGETCMAP:
	case FBIOPUTCMAP:
		karg = &cmap;
		err = __get_user(cmap.start, &((struct fb_cmap32 *)arg)->start);
		err |= __get_user(cmap.len, &((struct fb_cmap32 *)arg)->len);
		err |= __get_user(red, &((struct fb_cmap32 *)arg)->red);
		err |= __get_user(green, &((struct fb_cmap32 *)arg)->green);
		err |= __get_user(blue, &((struct fb_cmap32 *)arg)->blue);
		err |= __get_user(transp, &((struct fb_cmap32 *)arg)->transp);
		if (err)
			goto out;
		err = -ENOMEM;
		cmap.red = kmalloc(cmap.len * sizeof(__u16), GFP_KERNEL);
		if (!cmap.red)
			goto out;
		cmap.green = kmalloc(cmap.len * sizeof(__u16), GFP_KERNEL);
		if (!cmap.green)
			goto out;
		cmap.blue = kmalloc(cmap.len * sizeof(__u16), GFP_KERNEL);
		if (!cmap.blue)
			goto out;
		if (transp) {
			cmap.transp = kmalloc(cmap.len * sizeof(__u16), GFP_KERNEL);
			if (!cmap.transp)
				goto out;
		} else
			cmap.transp = NULL;
			
		if (cmd == FBIOGETCMAP)
			break;

		err = __copy_from_user(cmap.red, (char *)A(red), cmap.len * sizeof(__u16));
		err |= __copy_from_user(cmap.green, (char *)A(green), cmap.len * sizeof(__u16));
		err |= __copy_from_user(cmap.blue, (char *)A(blue), cmap.len * sizeof(__u16));
		if (cmap.transp) err |= __copy_from_user(cmap.transp, (char *)A(transp), cmap.len * sizeof(__u16));
		if (err)
			goto out;
		break;
	default:
		do {
			static int count = 0;
			if (++count <= 20)
				printk("%s: Unknown fb ioctl cmd fd(%d) "
				       "cmd(%08x) arg(%08lx)\n",
				       __FUNCTION__, fd, cmd, arg);
		} while(0);
		return -ENOSYS;
	}
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)karg);
	set_fs(old_fs);
	if (err)
		goto out;
	switch (cmd) {
	case FBIOGET_FSCREENINFO:
		err = __copy_to_user((char *)((struct fb_fix_screeninfo32 *)arg)->id, (char *)fix.id, sizeof(fix.id));
		err |= __put_user((__u32)(unsigned long)fix.smem_start, &((struct fb_fix_screeninfo32 *)arg)->smem_start);
		err |= __put_user(fix.smem_len, &((struct fb_fix_screeninfo32 *)arg)->smem_len);
		err |= __put_user(fix.type, &((struct fb_fix_screeninfo32 *)arg)->type);
		err |= __put_user(fix.type_aux, &((struct fb_fix_screeninfo32 *)arg)->type_aux);
		err |= __put_user(fix.visual, &((struct fb_fix_screeninfo32 *)arg)->visual);
		err |= __put_user(fix.xpanstep, &((struct fb_fix_screeninfo32 *)arg)->xpanstep);
		err |= __put_user(fix.ypanstep, &((struct fb_fix_screeninfo32 *)arg)->ypanstep);
		err |= __put_user(fix.ywrapstep, &((struct fb_fix_screeninfo32 *)arg)->ywrapstep);
		err |= __put_user(fix.line_length, &((struct fb_fix_screeninfo32 *)arg)->line_length);
		err |= __put_user((__u32)(unsigned long)fix.mmio_start, &((struct fb_fix_screeninfo32 *)arg)->mmio_start);
		err |= __put_user(fix.mmio_len, &((struct fb_fix_screeninfo32 *)arg)->mmio_len);
		err |= __put_user(fix.accel, &((struct fb_fix_screeninfo32 *)arg)->accel);
		err |= __copy_to_user((char *)((struct fb_fix_screeninfo32 *)arg)->reserved, (char *)fix.reserved, sizeof(fix.reserved));
		break;
	case FBIOGETCMAP:
		err = __copy_to_user((char *)A(red), cmap.red, cmap.len * sizeof(__u16));
		err |= __copy_to_user((char *)A(green), cmap.blue, cmap.len * sizeof(__u16));
		err |= __copy_to_user((char *)A(blue), cmap.blue, cmap.len * sizeof(__u16));
		if (cmap.transp)
			err |= __copy_to_user((char *)A(transp), cmap.transp, cmap.len * sizeof(__u16));
		break;
	case FBIOPUTCMAP:
		break;
	}
out:	if (cmap.red) kfree(cmap.red);
	if (cmap.green) kfree(cmap.green);
	if (cmap.blue) kfree(cmap.blue);
	if (cmap.transp) kfree(cmap.transp);
	return err;
}

static int hdio_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	unsigned long kval;
	unsigned int *uvp;
	int error;

	set_fs(KERNEL_DS);
	error = sys_ioctl(fd, cmd, (long)&kval);
	set_fs(old_fs);

	if(error == 0) {
		uvp = (unsigned int *)arg;
		if(put_user(kval, uvp))
			error = -EFAULT;
	}
	return error;
}

struct floppy_struct32 {
	unsigned int	size;
	unsigned int	sect;
	unsigned int	head;
	unsigned int	track;
	unsigned int	stretch;
	unsigned char	gap;
	unsigned char	rate;
	unsigned char	spec1;
	unsigned char	fmt_gap;
	const __kernel_caddr_t32 name;
};

struct floppy_drive_params32 {
	char		cmos;
	u32		max_dtr;
	u32		hlt;
	u32		hut;
	u32		srt;
	u32		spinup;
	u32		spindown;
	unsigned char	spindown_offset;
	unsigned char	select_delay;
	unsigned char	rps;
	unsigned char	tracks;
	u32		timeout;
	unsigned char	interleave_sect;
	struct floppy_max_errors max_errors;
	char		flags;
	char		read_track;
	short		autodetect[8];
	int		checkfreq;
	int		native_format;
};

struct floppy_drive_struct32 {
	signed char	flags;
	u32		spinup_date;
	u32		select_date;
	u32		first_read_date;
	short		probed_format;
	short		track;
	short		maxblock;
	short		maxtrack;
	int		generation;
	int		keep_data;
	int		fd_ref;
	int		fd_device;
	int		last_checked;
	__kernel_caddr_t32 dmabuf;
	int		bufblocks;
};

struct floppy_fdc_state32 {
	int		spec1;
	int		spec2;
	int		dtr;
	unsigned char	version;
	unsigned char	dor;
	u32		address;
	unsigned int	rawcmd:2;
	unsigned int	reset:1;
	unsigned int	need_configure:1;
	unsigned int	perp_mode:2;
	unsigned int	has_fifo:1;
	unsigned int	driver_version;
	unsigned char	track[4];
};

struct floppy_write_errors32 {
	unsigned int	write_errors;
	u32		first_error_sector;
	int		first_error_generation;
	u32		last_error_sector;
	int		last_error_generation;
	unsigned int	badness;
};

#define FDSETPRM32 _IOW(2, 0x42, struct floppy_struct32) 
#define FDDEFPRM32 _IOW(2, 0x43, struct floppy_struct32) 
#define FDGETPRM32 _IOR(2, 0x04, struct floppy_struct32)
#define FDSETDRVPRM32 _IOW(2, 0x90, struct floppy_drive_params32)
#define FDGETDRVPRM32 _IOR(2, 0x11, struct floppy_drive_params32)
#define FDGETDRVSTAT32 _IOR(2, 0x12, struct floppy_drive_struct32)
#define FDPOLLDRVSTAT32 _IOR(2, 0x13, struct floppy_drive_struct32)
#define FDGETFDCSTAT32 _IOR(2, 0x15, struct floppy_fdc_state32)
#define FDWERRORGET32  _IOR(2, 0x17, struct floppy_write_errors32)

static struct {
	unsigned int	cmd32;
	unsigned int	cmd;
} fd_ioctl_trans_table[] = {
	{ FDSETPRM32, FDSETPRM },
	{ FDDEFPRM32, FDDEFPRM },
	{ FDGETPRM32, FDGETPRM },
	{ FDSETDRVPRM32, FDSETDRVPRM },
	{ FDGETDRVPRM32, FDGETDRVPRM },
	{ FDGETDRVSTAT32, FDGETDRVSTAT },
	{ FDPOLLDRVSTAT32, FDPOLLDRVSTAT },
	{ FDGETFDCSTAT32, FDGETFDCSTAT },
	{ FDWERRORGET32, FDWERRORGET }
};

#define NR_FD_IOCTL_TRANS (sizeof(fd_ioctl_trans_table)/sizeof(fd_ioctl_trans_table[0]))

static int fd_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	void *karg = NULL;
	unsigned int kcmd = 0;
	int i, err;

	for (i = 0; i < NR_FD_IOCTL_TRANS; i++)
		if (cmd == fd_ioctl_trans_table[i].cmd32) {
			kcmd = fd_ioctl_trans_table[i].cmd;
			break;
		}
	if (!kcmd)
		return -EINVAL;

	switch (cmd) {
		case FDSETPRM32:
		case FDDEFPRM32:
		case FDGETPRM32:
		{
			struct floppy_struct *f;

			f = karg = kmalloc(GFP_KERNEL, sizeof(struct floppy_struct));
			if (!karg)
				return -ENOMEM;
			if (cmd == FDGETPRM32)
				break;
			err = __get_user(f->size, &((struct floppy_struct32 *)arg)->size);
			err |= __get_user(f->sect, &((struct floppy_struct32 *)arg)->sect);
			err |= __get_user(f->head, &((struct floppy_struct32 *)arg)->head);
			err |= __get_user(f->track, &((struct floppy_struct32 *)arg)->track);
			err |= __get_user(f->stretch, &((struct floppy_struct32 *)arg)->stretch);
			err |= __get_user(f->gap, &((struct floppy_struct32 *)arg)->gap);
			err |= __get_user(f->rate, &((struct floppy_struct32 *)arg)->rate);
			err |= __get_user(f->spec1, &((struct floppy_struct32 *)arg)->spec1);
			err |= __get_user(f->fmt_gap, &((struct floppy_struct32 *)arg)->fmt_gap);
			err |= __get_user((u64)f->name, &((struct floppy_struct32 *)arg)->name);
			if (err)
				goto out;
			break;
		}
		case FDSETDRVPRM32:
		case FDGETDRVPRM32:
		{
			struct floppy_drive_params *f;

			f = karg = kmalloc(GFP_KERNEL, sizeof(struct floppy_drive_params));
			if (!karg)
				return -ENOMEM;
			if (cmd == FDGETDRVPRM32)
				break;
			err = __get_user(f->cmos, &((struct floppy_drive_params32 *)arg)->cmos);
			err |= __get_user(f->max_dtr, &((struct floppy_drive_params32 *)arg)->max_dtr);
			err |= __get_user(f->hlt, &((struct floppy_drive_params32 *)arg)->hlt);
			err |= __get_user(f->hut, &((struct floppy_drive_params32 *)arg)->hut);
			err |= __get_user(f->srt, &((struct floppy_drive_params32 *)arg)->srt);
			err |= __get_user(f->spinup, &((struct floppy_drive_params32 *)arg)->spinup);
			err |= __get_user(f->spindown, &((struct floppy_drive_params32 *)arg)->spindown);
			err |= __get_user(f->spindown_offset, &((struct floppy_drive_params32 *)arg)->spindown_offset);
			err |= __get_user(f->select_delay, &((struct floppy_drive_params32 *)arg)->select_delay);
			err |= __get_user(f->rps, &((struct floppy_drive_params32 *)arg)->rps);
			err |= __get_user(f->tracks, &((struct floppy_drive_params32 *)arg)->tracks);
			err |= __get_user(f->timeout, &((struct floppy_drive_params32 *)arg)->timeout);
			err |= __get_user(f->interleave_sect, &((struct floppy_drive_params32 *)arg)->interleave_sect);
			err |= __copy_from_user(&f->max_errors, &((struct floppy_drive_params32 *)arg)->max_errors, sizeof(f->max_errors));
			err |= __get_user(f->flags, &((struct floppy_drive_params32 *)arg)->flags);
			err |= __get_user(f->read_track, &((struct floppy_drive_params32 *)arg)->read_track);
			err |= __copy_from_user(f->autodetect, ((struct floppy_drive_params32 *)arg)->autodetect, sizeof(f->autodetect));
			err |= __get_user(f->checkfreq, &((struct floppy_drive_params32 *)arg)->checkfreq);
			err |= __get_user(f->native_format, &((struct floppy_drive_params32 *)arg)->native_format);
			if (err)
				goto out;
			break;
		}
		case FDGETDRVSTAT32:
		case FDPOLLDRVSTAT32:
			karg = kmalloc(GFP_KERNEL, sizeof(struct floppy_drive_struct));
			if (!karg)
				return -ENOMEM;
			break;
		case FDGETFDCSTAT32:
			karg = kmalloc(GFP_KERNEL, sizeof(struct floppy_fdc_state));
			if (!karg)
				return -ENOMEM;
			break;
		case FDWERRORGET32:
			karg = kmalloc(GFP_KERNEL, sizeof(struct floppy_write_errors));
			if (!karg)
				return -ENOMEM;
			break;
		default:
			return -EINVAL;
	}
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, kcmd, (unsigned long)karg);
	set_fs (old_fs);
	if (err)
		goto out;
	switch (cmd) {
		case FDGETPRM32:
		{
			struct floppy_struct *f = karg;

			err = __put_user(f->size, &((struct floppy_struct32 *)arg)->size);
			err |= __put_user(f->sect, &((struct floppy_struct32 *)arg)->sect);
			err |= __put_user(f->head, &((struct floppy_struct32 *)arg)->head);
			err |= __put_user(f->track, &((struct floppy_struct32 *)arg)->track);
			err |= __put_user(f->stretch, &((struct floppy_struct32 *)arg)->stretch);
			err |= __put_user(f->gap, &((struct floppy_struct32 *)arg)->gap);
			err |= __put_user(f->rate, &((struct floppy_struct32 *)arg)->rate);
			err |= __put_user(f->spec1, &((struct floppy_struct32 *)arg)->spec1);
			err |= __put_user(f->fmt_gap, &((struct floppy_struct32 *)arg)->fmt_gap);
			err |= __put_user((u64)f->name, &((struct floppy_struct32 *)arg)->name);
			break;
		}
		case FDGETDRVPRM32:
		{
			struct floppy_drive_params *f = karg;

			err = __put_user(f->cmos, &((struct floppy_drive_params32 *)arg)->cmos);
			err |= __put_user(f->max_dtr, &((struct floppy_drive_params32 *)arg)->max_dtr);
			err |= __put_user(f->hlt, &((struct floppy_drive_params32 *)arg)->hlt);
			err |= __put_user(f->hut, &((struct floppy_drive_params32 *)arg)->hut);
			err |= __put_user(f->srt, &((struct floppy_drive_params32 *)arg)->srt);
			err |= __put_user(f->spinup, &((struct floppy_drive_params32 *)arg)->spinup);
			err |= __put_user(f->spindown, &((struct floppy_drive_params32 *)arg)->spindown);
			err |= __put_user(f->spindown_offset, &((struct floppy_drive_params32 *)arg)->spindown_offset);
			err |= __put_user(f->select_delay, &((struct floppy_drive_params32 *)arg)->select_delay);
			err |= __put_user(f->rps, &((struct floppy_drive_params32 *)arg)->rps);
			err |= __put_user(f->tracks, &((struct floppy_drive_params32 *)arg)->tracks);
			err |= __put_user(f->timeout, &((struct floppy_drive_params32 *)arg)->timeout);
			err |= __put_user(f->interleave_sect, &((struct floppy_drive_params32 *)arg)->interleave_sect);
			err |= __copy_to_user(&((struct floppy_drive_params32 *)arg)->max_errors, &f->max_errors, sizeof(f->max_errors));
			err |= __put_user(f->flags, &((struct floppy_drive_params32 *)arg)->flags);
			err |= __put_user(f->read_track, &((struct floppy_drive_params32 *)arg)->read_track);
			err |= __copy_to_user(((struct floppy_drive_params32 *)arg)->autodetect, f->autodetect, sizeof(f->autodetect));
			err |= __put_user(f->checkfreq, &((struct floppy_drive_params32 *)arg)->checkfreq);
			err |= __put_user(f->native_format, &((struct floppy_drive_params32 *)arg)->native_format);
			break;
		}
		case FDGETDRVSTAT32:
		case FDPOLLDRVSTAT32:
		{
			struct floppy_drive_struct *f = karg;

			err = __put_user(f->flags, &((struct floppy_drive_struct32 *)arg)->flags);
			err |= __put_user(f->spinup_date, &((struct floppy_drive_struct32 *)arg)->spinup_date);
			err |= __put_user(f->select_date, &((struct floppy_drive_struct32 *)arg)->select_date);
			err |= __put_user(f->first_read_date, &((struct floppy_drive_struct32 *)arg)->first_read_date);
			err |= __put_user(f->probed_format, &((struct floppy_drive_struct32 *)arg)->probed_format);
			err |= __put_user(f->track, &((struct floppy_drive_struct32 *)arg)->track);
			err |= __put_user(f->maxblock, &((struct floppy_drive_struct32 *)arg)->maxblock);
			err |= __put_user(f->maxtrack, &((struct floppy_drive_struct32 *)arg)->maxtrack);
			err |= __put_user(f->generation, &((struct floppy_drive_struct32 *)arg)->generation);
			err |= __put_user(f->keep_data, &((struct floppy_drive_struct32 *)arg)->keep_data);
			err |= __put_user(f->fd_ref, &((struct floppy_drive_struct32 *)arg)->fd_ref);
			err |= __put_user(f->fd_device, &((struct floppy_drive_struct32 *)arg)->fd_device);
			err |= __put_user(f->last_checked, &((struct floppy_drive_struct32 *)arg)->last_checked);
			err |= __put_user((u64)f->dmabuf, &((struct floppy_drive_struct32 *)arg)->dmabuf);
			err |= __put_user((u64)f->bufblocks, &((struct floppy_drive_struct32 *)arg)->bufblocks);
			break;
		}
		case FDGETFDCSTAT32:
		{
			struct floppy_fdc_state *f = karg;

			err = __put_user(f->spec1, &((struct floppy_fdc_state32 *)arg)->spec1);
			err |= __put_user(f->spec2, &((struct floppy_fdc_state32 *)arg)->spec2);
			err |= __put_user(f->dtr, &((struct floppy_fdc_state32 *)arg)->dtr);
			err |= __put_user(f->version, &((struct floppy_fdc_state32 *)arg)->version);
			err |= __put_user(f->dor, &((struct floppy_fdc_state32 *)arg)->dor);
			err |= __put_user(f->address, &((struct floppy_fdc_state32 *)arg)->address);
			err |= __copy_to_user((char *)&((struct floppy_fdc_state32 *)arg)->address
			    		   + sizeof(((struct floppy_fdc_state32 *)arg)->address),
					   (char *)&f->address + sizeof(f->address), sizeof(int));
			err |= __put_user(f->driver_version, &((struct floppy_fdc_state32 *)arg)->driver_version);
			err |= __copy_to_user(((struct floppy_fdc_state32 *)arg)->track, f->track, sizeof(f->track));
			break;
		}
		case FDWERRORGET32:
		{
			struct floppy_write_errors *f = karg;

			err = __put_user(f->write_errors, &((struct floppy_write_errors32 *)arg)->write_errors);
			err |= __put_user(f->first_error_sector, &((struct floppy_write_errors32 *)arg)->first_error_sector);
			err |= __put_user(f->first_error_generation, &((struct floppy_write_errors32 *)arg)->first_error_generation);
			err |= __put_user(f->last_error_sector, &((struct floppy_write_errors32 *)arg)->last_error_sector);
			err |= __put_user(f->last_error_generation, &((struct floppy_write_errors32 *)arg)->last_error_generation);
			err |= __put_user(f->badness, &((struct floppy_write_errors32 *)arg)->badness);
			break;
		}
		default:
			break;
	}
out:	if (karg) kfree(karg);
	return err;
}

struct ppp_option_data32 {
	__kernel_caddr_t32	ptr;
	__u32			length;
	int			transmit;
};
#define PPPIOCSCOMPRESS32	_IOW('t', 77, struct ppp_option_data32)

struct ppp_idle32 {
	__kernel_time_t32 xmit_idle;
	__kernel_time_t32 recv_idle;
};
#define PPPIOCGIDLE32		_IOR('t', 63, struct ppp_idle32)

static int ppp_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct ppp_option_data32 data32;
	struct ppp_option_data data;
	struct ppp_idle32 idle32;
	struct ppp_idle idle;
	unsigned int kcmd;
	void *karg;
	int err = 0;

	switch (cmd) {
	case PPPIOCGIDLE32:
		kcmd = PPPIOCGIDLE;
		karg = &idle;
		break;
	case PPPIOCSCOMPRESS32:
		if (copy_from_user(&data32, (struct ppp_option_data32 *)arg, sizeof(struct ppp_option_data32)))
			return -EFAULT;
		data.ptr = kmalloc (data32.length, GFP_KERNEL);
		if (!data.ptr)
			return -ENOMEM;
		if (copy_from_user(data.ptr, (__u8 *)A(data32.ptr), data32.length)) {
			kfree(data.ptr);
			return -EFAULT;
		}
		data.length = data32.length;
		data.transmit = data32.transmit;
		kcmd = PPPIOCSCOMPRESS;
		karg = &data;
		break;
	default:
		do {
			static int count = 0;
			if (++count <= 20)
				printk("ppp_ioctl: Unknown cmd fd(%d) "
				       "cmd(%08x) arg(%08x)\n",
				       (int)fd, (unsigned int)cmd, (unsigned int)arg);
		} while(0);
		return -EINVAL;
	}
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, kcmd, (unsigned long)karg);
	set_fs (old_fs);
	switch (cmd) {
	case PPPIOCGIDLE32:
		if (err)
			return err;
		idle32.xmit_idle = idle.xmit_idle;
		idle32.recv_idle = idle.recv_idle;
		if (copy_to_user((struct ppp_idle32 *)arg, &idle32, sizeof(struct ppp_idle32)))
			return -EFAULT;
		break;
	case PPPIOCSCOMPRESS32:
		kfree(data.ptr);
		break;
	default:
		break;
	}
	return err;
}


struct mtget32 {
	__u32	mt_type;
	__u32	mt_resid;
	__u32	mt_dsreg;
	__u32	mt_gstat;
	__u32	mt_erreg;
	__kernel_daddr_t32	mt_fileno;
	__kernel_daddr_t32	mt_blkno;
};
#define MTIOCGET32	_IOR('m', 2, struct mtget32)

struct mtpos32 {
	__u32	mt_blkno;
};
#define MTIOCPOS32	_IOR('m', 3, struct mtpos32)

struct mtconfiginfo32 {
	__u32	mt_type;
	__u32	ifc_type;
	__u16	irqnr;
	__u16	dmanr;
	__u16	port;
	__u32	debug;
	__u32	have_dens:1;
	__u32	have_bsf:1;
	__u32	have_fsr:1;
	__u32	have_bsr:1;
	__u32	have_eod:1;
	__u32	have_seek:1;
	__u32	have_tell:1;
	__u32	have_ras1:1;
	__u32	have_ras2:1;
	__u32	have_ras3:1;
	__u32	have_qfa:1;
	__u32	pad1:5;
	char	reserved[10];
};
#define	MTIOCGETCONFIG32	_IOR('m', 4, struct mtconfiginfo32)
#define	MTIOCSETCONFIG32	_IOW('m', 5, struct mtconfiginfo32)

static int mt_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct mtconfiginfo info;
	struct mtget get;
	struct mtpos pos;
	unsigned long kcmd;
	void *karg;
	int err = 0;

	switch(cmd) {
	case MTIOCPOS32:
		kcmd = MTIOCPOS;
		karg = &pos;
		break;
	case MTIOCGET32:
		kcmd = MTIOCGET;
		karg = &get;
		break;
	case MTIOCGETCONFIG32:
		kcmd = MTIOCGETCONFIG;
		karg = &info;
		break;
	case MTIOCSETCONFIG32:
		kcmd = MTIOCSETCONFIG;
		karg = &info;
		err = __get_user(info.mt_type, &((struct mtconfiginfo32 *)arg)->mt_type);
		err |= __get_user(info.ifc_type, &((struct mtconfiginfo32 *)arg)->ifc_type);
		err |= __get_user(info.irqnr, &((struct mtconfiginfo32 *)arg)->irqnr);
		err |= __get_user(info.dmanr, &((struct mtconfiginfo32 *)arg)->dmanr);
		err |= __get_user(info.port, &((struct mtconfiginfo32 *)arg)->port);
		err |= __get_user(info.debug, &((struct mtconfiginfo32 *)arg)->debug);
		err |= __copy_from_user((char *)&info.debug + sizeof(info.debug),
				     (char *)&((struct mtconfiginfo32 *)arg)->debug
				     + sizeof(((struct mtconfiginfo32 *)arg)->debug), sizeof(__u32));
		if (err)
			return -EFAULT;
		break;
	default:
		do {
			static int count = 0;
			if (++count <= 20)
				printk("mt_ioctl: Unknown cmd fd(%d) "
				       "cmd(%08x) arg(%08x)\n",
				       (int)fd, (unsigned int)cmd, (unsigned int)arg);
		} while(0);
		return -EINVAL;
	}
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, kcmd, (unsigned long)karg);
	set_fs (old_fs);
	if (err)
		return err;
	switch (cmd) {
	case MTIOCPOS32:
		if (__put_user(pos.mt_blkno, &((struct mtpos32 *)arg)->mt_blkno))
			return -EFAULT;
		break;
	case MTIOCGET32:
		err = __put_user(get.mt_type, &((struct mtget32 *)arg)->mt_type);
		err |= __put_user(get.mt_resid, &((struct mtget32 *)arg)->mt_resid);
		err |= __put_user(get.mt_dsreg, &((struct mtget32 *)arg)->mt_dsreg);
		err |= __put_user(get.mt_gstat, &((struct mtget32 *)arg)->mt_gstat);
		err |= __put_user(get.mt_erreg, &((struct mtget32 *)arg)->mt_erreg);
		err |= __put_user(get.mt_fileno, &((struct mtget32 *)arg)->mt_fileno);
		err |= __put_user(get.mt_blkno, &((struct mtget32 *)arg)->mt_blkno);
		break;
	case MTIOCGETCONFIG32:
		err = __put_user(info.mt_type, &((struct mtconfiginfo32 *)arg)->mt_type);
		err |= __put_user(info.ifc_type, &((struct mtconfiginfo32 *)arg)->ifc_type);
		err |= __put_user(info.irqnr, &((struct mtconfiginfo32 *)arg)->irqnr);
		err |= __put_user(info.dmanr, &((struct mtconfiginfo32 *)arg)->dmanr);
		err |= __put_user(info.port, &((struct mtconfiginfo32 *)arg)->port);
		err |= __put_user(info.debug, &((struct mtconfiginfo32 *)arg)->debug);
		err |= __copy_to_user((char *)&((struct mtconfiginfo32 *)arg)->debug
			    		   + sizeof(((struct mtconfiginfo32 *)arg)->debug),
					   (char *)&info.debug + sizeof(info.debug), sizeof(__u32));
		break;
	case MTIOCSETCONFIG32:
		break;
	}
	return err;
}

struct cdrom_read32 {
	int			cdread_lba;
	__kernel_caddr_t32	cdread_bufaddr;
	int			cdread_buflen;
};

struct cdrom_read_audio32 {
	union cdrom_addr	addr;
	u_char			addr_format;
	int			nframes;
	__kernel_caddr_t32	buf;
};

static int cdrom_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct cdrom_read cdread;
	struct cdrom_read_audio cdreadaudio;
	__kernel_caddr_t32 addr;
	char *data = 0;
	void *karg;
	int err = 0;

	switch(cmd) {
	case CDROMREADMODE2:
	case CDROMREADMODE1:
	case CDROMREADRAW:
	case CDROMREADCOOKED:
		karg = &cdread;
		err = __get_user(cdread.cdread_lba, &((struct cdrom_read32 *)arg)->cdread_lba);
		err |= __get_user(addr, &((struct cdrom_read32 *)arg)->cdread_bufaddr);
		err |= __get_user(cdread.cdread_buflen, &((struct cdrom_read32 *)arg)->cdread_buflen);
		if (err)
			return -EFAULT;
		data = kmalloc(cdread.cdread_buflen, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		cdread.cdread_bufaddr = data;
		break;
	case CDROMREADAUDIO:
		karg = &cdreadaudio;
		err = copy_from_user(&cdreadaudio.addr, &((struct cdrom_read_audio32 *)arg)->addr, sizeof(cdreadaudio.addr));
		err |= __get_user(cdreadaudio.addr_format, &((struct cdrom_read_audio32 *)arg)->addr_format);
		err |= __get_user(cdreadaudio.nframes, &((struct cdrom_read_audio32 *)arg)->nframes); 
		err |= __get_user(addr, &((struct cdrom_read_audio32 *)arg)->buf);
		if (err)
			return -EFAULT;
		data = kmalloc(cdreadaudio.nframes * 2352, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		cdreadaudio.buf = data;
		break;
	default:
		do {
			static int count = 0;
			if (++count <= 20)
				printk("cdrom_ioctl: Unknown cmd fd(%d) "
				       "cmd(%08x) arg(%08x)\n",
				       (int)fd, (unsigned int)cmd, (unsigned int)arg);
		} while(0);
		return -EINVAL;
	}
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, cmd, (unsigned long)karg);
	set_fs (old_fs);
	if (err)
		goto out;
	switch (cmd) {
	case CDROMREADMODE2:
	case CDROMREADMODE1:
	case CDROMREADRAW:
	case CDROMREADCOOKED:
		err = copy_to_user((char *)A(addr), data, cdread.cdread_buflen);
		break;
	case CDROMREADAUDIO:
		err = copy_to_user((char *)A(addr), data, cdreadaudio.nframes * 2352);
		break;
	default:
		break;
	}
out:	if (data) kfree(data);
	return err;
}

struct loop_info32 {
	int			lo_number;      /* ioctl r/o */
	__kernel_dev_t32	lo_device;      /* ioctl r/o */
	unsigned int		lo_inode;       /* ioctl r/o */
	__kernel_dev_t32	lo_rdevice;     /* ioctl r/o */
	int			lo_offset;
	int			lo_encrypt_type;
	int			lo_encrypt_key_size;    /* ioctl w/o */
	int			lo_flags;       /* ioctl r/o */
	char			lo_name[LO_NAME_SIZE];
	unsigned char		lo_encrypt_key[LO_KEY_SIZE]; /* ioctl w/o */
	unsigned int		lo_init[2];
	char			reserved[4];
};

static int loop_status(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct loop_info l;
	int err = 0;

	switch(cmd) {
	case LOOP_SET_STATUS:
		err = get_user(l.lo_number, &((struct loop_info32 *)arg)->lo_number);
		err |= __get_user(l.lo_device, &((struct loop_info32 *)arg)->lo_device);
		err |= __get_user(l.lo_inode, &((struct loop_info32 *)arg)->lo_inode);
		err |= __get_user(l.lo_rdevice, &((struct loop_info32 *)arg)->lo_rdevice);
		err |= __copy_from_user((char *)&l.lo_offset, (char *)&((struct loop_info32 *)arg)->lo_offset,
					   8 + (unsigned long)l.lo_init - (unsigned long)&l.lo_offset);
		if (err)
			return -EFAULT;
		set_fs (KERNEL_DS);
		err = sys_ioctl (fd, cmd, (unsigned long)&l);
		set_fs (old_fs);
		break;
	case LOOP_GET_STATUS:
		set_fs (KERNEL_DS);
		err = sys_ioctl (fd, cmd, (unsigned long)&l);
		set_fs (old_fs);
		if (!err) {
			err = put_user(l.lo_number, &((struct loop_info32 *)arg)->lo_number);
			err |= __put_user(l.lo_device, &((struct loop_info32 *)arg)->lo_device);
			err |= __put_user(l.lo_inode, &((struct loop_info32 *)arg)->lo_inode);
			err |= __put_user(l.lo_rdevice, &((struct loop_info32 *)arg)->lo_rdevice);
			err |= __copy_to_user((char *)&((struct loop_info32 *)arg)->lo_offset,
					   (char *)&l.lo_offset, (unsigned long)l.lo_init - (unsigned long)&l.lo_offset);
		}
		break;
	}
	return err;
}

extern int tty_ioctl(struct inode * inode, struct file * file, unsigned int cmd, unsigned long arg);

static int vt_check(struct file *file)
{
	struct tty_struct *tty;
	struct inode *inode = file->f_dentry->d_inode;
	
	if (file->f_op->ioctl != tty_ioctl)
		return -EINVAL;
	                
	tty = (struct tty_struct *)file->private_data;
	if (tty_paranoia_check(tty, inode->i_rdev, "tty_ioctl"))
		return -EINVAL;
	                                                
	if (tty->driver.ioctl != vt_ioctl)
		return -EINVAL;
	
	/*
	 * To have permissions to do most of the vt ioctls, we either have
	 * to be the owner of the tty, or super-user.
	 */
	if (current->tty == tty || suser())
		return 1;
	return 0;                                                    
}

struct consolefontdesc32 {
	unsigned short charcount;       /* characters in font (256 or 512) */
	unsigned short charheight;      /* scan lines per character (1-32) */
	u32 chardata;			/* font data in expanded form */
};

static int do_fontx_ioctl(struct file *file, int cmd, struct consolefontdesc32 *user_cfd)
{
	struct consolefontdesc cfdarg;
	struct console_font_op op;
	int i, perm;

	perm = vt_check(file);
	if (perm < 0) return perm;
	
	if (copy_from_user(&cfdarg, user_cfd, sizeof(struct consolefontdesc32))) 
		return -EFAULT;
	
	cfdarg.chardata = (unsigned char *)A(((struct consolefontdesc32 *)&cfdarg)->chardata);
 	
	switch (cmd) {
	case PIO_FONTX:
		if (!perm)
			return -EPERM;
		op.op = KD_FONT_OP_SET;
		op.flags = 0;
		op.width = 8;
		op.height = cfdarg.charheight;
		op.charcount = cfdarg.charcount;
		op.data = cfdarg.chardata;
		return con_font_op(fg_console, &op);
	case GIO_FONTX:
		if (!cfdarg.chardata)
			return 0;
		op.op = KD_FONT_OP_GET;
		op.flags = 0;
		op.width = 8;
		op.height = cfdarg.charheight;
		op.charcount = cfdarg.charcount;
		op.data = cfdarg.chardata;
		i = con_font_op(fg_console, &op);
		if (i)
			return i;
		cfdarg.charheight = op.height;
		cfdarg.charcount = op.charcount;
		((struct consolefontdesc32 *)&cfdarg)->chardata	= (unsigned long)cfdarg.chardata;
		if (copy_to_user(user_cfd, &cfdarg, sizeof(struct consolefontdesc32)))
			return -EFAULT;
		return 0;
	}
	return -EINVAL;
}

struct console_font_op32 {
	unsigned int op;        /* operation code KD_FONT_OP_* */
	unsigned int flags;     /* KD_FONT_FLAG_* */
	unsigned int width, height;     /* font size */
	unsigned int charcount;
	u32 data;    /* font data with height fixed to 32 */
};
                                        
static int do_kdfontop_ioctl(struct file *file, struct console_font_op32 *fontop)
{
	struct console_font_op op;
	int perm = vt_check(file), i;
	struct vt_struct *vt;
	
	if (perm < 0) return perm;
	
	if (copy_from_user(&op, (void *) fontop, sizeof(struct console_font_op32)))
		return -EFAULT;
	if (!perm && op.op != KD_FONT_OP_GET)
		return -EPERM;
	op.data = (unsigned char *)A(((struct console_font_op32 *)&op)->data);
	op.flags |= KD_FONT_FLAG_OLD;
	vt = (struct vt_struct *)((struct tty_struct *)file->private_data)->driver_data;
	i = con_font_op(vt->vc_num, &op);
	if (i) return i;
	((struct console_font_op32 *)&op)->data = (unsigned long)op.data;
	if (copy_to_user((void *) fontop, &op, sizeof(struct console_font_op32)))
		return -EFAULT;
	return 0;
}

struct unimapdesc32 {
	unsigned short entry_ct;
	u32 entries;
};

static int do_unimap_ioctl(struct file *file, int cmd, struct unimapdesc32 *user_ud)
{
	struct unimapdesc32 tmp;
	int perm = vt_check(file);
	
	if (perm < 0) return perm;
	if (copy_from_user(&tmp, user_ud, sizeof tmp))
		return -EFAULT;
	switch (cmd) {
	case PIO_UNIMAP:
		if (!perm) return -EPERM;
		return con_set_unimap(fg_console, tmp.entry_ct, (struct unipair *)A(tmp.entries));
	case GIO_UNIMAP:
		return con_get_unimap(fg_console, tmp.entry_ct, &(user_ud->entry_ct), (struct unipair *)A(tmp.entries));
	}
	return 0;
}

asmlinkage int sys32_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct file * filp;
	int error = -EBADF;

	lock_kernel();
	filp = fcheck(fd);
	if(!filp)
		goto out;

	if (!filp->f_op || !filp->f_op->ioctl) {
		error = sys_ioctl (fd, cmd, arg);
		goto out;
	}
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
	case SIOCGIFINDEX:
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
	case SIOCSIFPFLAGS:
	case SIOCGIFPFLAGS:
	case SIOCGPPPSTATS:
	case SIOCGPPPCSTATS:
	case SIOCGPPPVER:
		error = dev_ifsioc(fd, cmd, arg);
		goto out;
		
	case SIOCADDRT:
	case SIOCDELRT:
		error = routing_ioctl(fd, cmd, arg);
		goto out;

	case SIOCRTMSG: /* Note SIOCRTMSG is no longer, so this is safe and
			 * the user would have seen just an -EINVAL anyways.
			 */
		error = -EINVAL;
		goto out;

	case SIOCGSTAMP:
		/* Sorry, timeval in the kernel is different now. */
		error = do_siocgstamp(fd, cmd, arg);
		goto out;

	case HDIO_GETGEO:
		error = hdio_getgeo(fd, arg);
		goto out;
		
	case BLKRAGET:
	case BLKGETSIZE:
	case 0x1260:
		/* The mkswap binary hard codes it to Intel value :-((( */
		if(cmd == 0x1260)
			cmd = BLKGETSIZE;
		error = w_long(fd, cmd, arg);
		goto out;
		
	case FBIOPUTCMAP32:
	case FBIOGETCMAP32:
		error = fbiogetputcmap(fd, cmd, arg);
		goto out;
		
	case FBIOSCURSOR32:
		error = fbiogscursor(fd, cmd, arg);
		goto out;

	case FBIOGET_FSCREENINFO:
	case FBIOGETCMAP:
	case FBIOPUTCMAP:
		error = fb_ioctl_trans(fd, cmd, arg);
		goto out;

	case HDIO_GET_KEEPSETTINGS:
	case HDIO_GET_UNMASKINTR:
	case HDIO_GET_DMA:
	case HDIO_GET_32BIT:
	case HDIO_GET_MULTCOUNT:
	case HDIO_GET_NOWERR:
	case HDIO_GET_NICE:
		error = hdio_ioctl_trans(fd, cmd, arg);
		goto out;

	case FDSETPRM32:
	case FDDEFPRM32:
	case FDGETPRM32:
	case FDSETDRVPRM32:
	case FDGETDRVPRM32:
	case FDGETDRVSTAT32:
	case FDPOLLDRVSTAT32:
	case FDGETFDCSTAT32:
	case FDWERRORGET32:
		error = fd_ioctl_trans(fd, cmd, arg);
		goto out;

	case PPPIOCGIDLE32:
	case PPPIOCSCOMPRESS32:
		error = ppp_ioctl_trans(fd, cmd, arg);
		goto out;

	case MTIOCGET32:
	case MTIOCPOS32:
	case MTIOCGETCONFIG32:
	case MTIOCSETCONFIG32:
		error = mt_ioctl_trans(fd, cmd, arg);
		goto out;

	case CDROMREADMODE2:
	case CDROMREADMODE1:
	case CDROMREADRAW:
	case CDROMREADCOOKED:
	case CDROMREADAUDIO:
	case CDROMREADALL:
		error = cdrom_ioctl_trans(fd, cmd, arg);
		goto out;
		
	case LOOP_SET_STATUS:
	case LOOP_GET_STATUS:
		error = loop_status(fd, cmd, arg);
		goto out;

#define AUTOFS_IOC_SETTIMEOUT32 _IOWR(0x93,0x64,unsigned int)
	case AUTOFS_IOC_SETTIMEOUT32:
		error = rw_long(fd, AUTOFS_IOC_SETTIMEOUT, arg);
		goto out;
		
	case PIO_FONTX:
	case GIO_FONTX:
		error = do_fontx_ioctl(filp, cmd, (struct consolefontdesc32 *)arg);
		goto out;
		
	case PIO_UNIMAP:
	case GIO_UNIMAP:
		error = do_unimap_ioctl(filp, cmd, (struct unimapdesc32 *)arg);
		goto out;

	case KDFONTOP:
		error = do_kdfontop_ioctl(filp, (struct console_font_op32 *)arg);
		goto out;
		
	case EXT2_IOC32_GETFLAGS:
	case EXT2_IOC32_SETFLAGS:
	case EXT2_IOC32_GETVERSION:
	case EXT2_IOC32_SETVERSION:
		error = do_ext2_ioctl(fd, cmd, arg);
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
	
	/* Big F */
	case FBIOGTYPE:
	case FBIOSATTR:
	case FBIOGATTR:
	case FBIOSVIDEO:
	case FBIOGVIDEO:
	case FBIOGCURSOR32: /* This is not implemented yet. Later it should be converted... */
	case FBIOSCURPOS:
	case FBIOGCURPOS:
	case FBIOGCURMAX:

	case FBIOGET_VSCREENINFO:
	case FBIOPUT_VSCREENINFO:
	case FBIOPAN_DISPLAY:
	case FBIOGET_FCURSORINFO:
	case FBIOGET_VCURSORINFO:
	case FBIOPUT_VCURSORINFO:
	case FBIOGET_CURSORSTATE:
	case FBIOPUT_CURSORSTATE:
	case FBIOGET_CON2FBMAP:
	case FBIOPUT_CON2FBMAP:

	/* Little f */
	case FIOCLEX:
	case FIONCLEX:
	case FIOASYNC:
	case FIONBIO:
	case FIONREAD: /* This is also TIOCINQ */
	
	/* 0x00 */
	case FIBMAP:
	case FIGETBSZ:
	
	/* 0x03 -- HD/IDE ioctl's used by hdparm and friends.
	 *         Some need translations, these do not.
	 */
	case HDIO_GET_IDENTITY:
	case HDIO_SET_DMA:
	case HDIO_SET_KEEPSETTINGS:
	case HDIO_SET_UNMASKINTR:
	case HDIO_SET_NOWERR:
	case HDIO_SET_32BIT:
	case HDIO_SET_MULTCOUNT:
	case HDIO_DRIVE_CMD:
	case HDIO_SET_PIO_MODE:
	case HDIO_SCAN_HWIF:
	case HDIO_SET_NICE:
	case BLKROSET:
	case BLKROGET:

	/* 0x02 -- Floppy ioctls */
	case FDMSGON:
	case FDMSGOFF:
	case FDSETEMSGTRESH:
	case FDFLUSH:
	case FDWERRORCLR:
	case FDSETMAXERRS:
	case FDGETMAXERRS:
	case FDGETDRVTYP:
	case FDEJECT:
	case FDCLRPRM:
	case FDFMTBEG:
	case FDFMTEND:
	case FDRESET:
	case FDTWADDLE:
	case FDFMTTRK:
	case FDRAWCMD:

	/* 0x12 */
	case BLKRRPART:
	case BLKFLSBUF:
	case BLKRASET:
	
	/* 0x09 */
	case REGISTER_DEV:
	case REGISTER_DEV_NEW:
	case START_MD:
	case STOP_MD:
	
	/* Big K */
	case PIO_FONT:
	case GIO_FONT:
	case KDSIGACCEPT:
	case KDGETKEYCODE:
	case KDSETKEYCODE:
	case KIOCSOUND:
	case KDMKTONE:
	case KDGKBTYPE:
	case KDSETMODE:
	case KDGETMODE:
	case KDSKBMODE:
	case KDGKBMODE:
	case KDSKBMETA:
	case KDGKBMETA:
	case KDGKBENT:
	case KDSKBENT:
	case KDGKBSENT:
	case KDSKBSENT:
	case KDGKBDIACR:
	case KDSKBDIACR:
	case KDGKBLED:
	case KDSKBLED:
	case KDGETLED:
	case KDSETLED:
	case GIO_SCRNMAP:
	case PIO_SCRNMAP:
	case GIO_UNISCRNMAP:
	case PIO_UNISCRNMAP:
	case PIO_FONTRESET:
	case PIO_UNIMAPCLR:
	
	/* Little k */
	case KIOCTYPE:
	case KIOCLAYOUT:
	case KIOCGTRANS:
	case KIOCTRANS:
	case KIOCCMD:
	case KIOCSDIRECT:
	case KIOCSLED:
	case KIOCGLED:
	case KIOCSRATE:
	case KIOCGRATE:
	
	/* Big S */
	case SCSI_IOCTL_GET_IDLUN:
	case SCSI_IOCTL_DOORLOCK:
	case SCSI_IOCTL_DOORUNLOCK:
	case SCSI_IOCTL_TEST_UNIT_READY:
	case SCSI_IOCTL_TAGGED_ENABLE:
	case SCSI_IOCTL_TAGGED_DISABLE:
	case SCSI_IOCTL_GET_BUS_NUMBER:
	case SCSI_IOCTL_SEND_COMMAND:
	
	/* Big V */
	case VT_SETMODE:
	case VT_GETMODE:
	case VT_GETSTATE:
	case VT_OPENQRY:
	case VT_ACTIVATE:
	case VT_WAITACTIVE:
	case VT_RELDISP:
	case VT_DISALLOCATE:
	case VT_RESIZE:
	case VT_RESIZEX:
	case VT_LOCKSWITCH:
	case VT_UNLOCKSWITCH:
	
	/* Little v */
	case VUIDSFORMAT:
	case VUIDGFORMAT:

	/* Little p (/dev/rtc, /dev/envctrl, etc.) */
	case RTCGET:
	case RTCSET:
	case I2CIOCSADR:
	case I2CIOCGADR:

	/* Little m */
	case MTIOCTOP:

	/* OPENPROMIO, SunOS/Solaris only, the NetBSD one's have
	 * embedded pointers in the arg which we'd need to clean up...
	 */
	case OPROMGETOPT:
	case OPROMSETOPT:
	case OPROMNXTOPT:
	case OPROMSETOPT2:
	case OPROMNEXT:
	case OPROMCHILD:
	case OPROMGETPROP:
	case OPROMNXTPROP:
	case OPROMU2P:
	case OPROMGETCONS:
	case OPROMGETFBNAME:
	case OPROMGETBOOTARGS:

	/* Socket level stuff */
	case FIOSETOWN:
	case SIOCSPGRP:
	case FIOGETOWN:
	case SIOCGPGRP:
	case SIOCATMARK:
	case SIOCSIFLINK:
	case SIOCSIFENCAP:
	case SIOCGIFENCAP:
	case SIOCSIFBR:
	case SIOCGIFBR:
	case SIOCSARP:
	case SIOCGARP:
	case SIOCDARP:
#if 0 /* XXX No longer exist in new routing code. XXX */
	case OLD_SIOCSARP:
	case OLD_SIOCGARP:
	case OLD_SIOCDARP:
#endif
	case SIOCSRARP:
	case SIOCGRARP:
	case SIOCDRARP:
	case SIOCADDDLCI:
	case SIOCDELDLCI:
	
	/* SG stuff */
	case SG_SET_TIMEOUT:
	case SG_GET_TIMEOUT:
	case SG_EMULATED_HOST:
	case SG_SET_TRANSFORM:
	case SG_GET_TRANSFORM:

	/* PPP stuff */
	case PPPIOCGFLAGS:
	case PPPIOCSFLAGS:
	case PPPIOCGASYNCMAP:
	case PPPIOCSASYNCMAP:
	case PPPIOCGUNIT:
	case PPPIOCGRASYNCMAP:
	case PPPIOCSRASYNCMAP:
	case PPPIOCGMRU:
	case PPPIOCSMRU:
	case PPPIOCSMAXCID:
	case PPPIOCGXASYNCMAP:
	case PPPIOCSXASYNCMAP:
	case PPPIOCXFERUNIT:
	case PPPIOCGNPMODE:
	case PPPIOCSNPMODE:
	case PPPIOCGDEBUG:
	case PPPIOCSDEBUG:

	/* CDROM stuff */
	case CDROMPAUSE:
	case CDROMRESUME:
	case CDROMPLAYMSF:
	case CDROMPLAYTRKIND:
	case CDROMREADTOCHDR:
	case CDROMREADTOCENTRY:
	case CDROMSTOP:
	case CDROMSTART:
	case CDROMEJECT:
	case CDROMVOLCTRL:
	case CDROMSUBCHNL:
	case CDROMEJECT_SW:
	case CDROMMULTISESSION:
	case CDROM_GET_MCN:
	case CDROMRESET:
	case CDROMVOLREAD:
	case CDROMSEEK:
	case CDROMPLAYBLK:
	case CDROMCLOSETRAY:
	case CDROM_SET_OPTIONS:
	case CDROM_CLEAR_OPTIONS:
	case CDROM_SELECT_SPEED:
	case CDROM_SELECT_DISC:
	case CDROM_MEDIA_CHANGED:
	case CDROM_DRIVE_STATUS:
	case CDROM_DISC_STATUS:
	case CDROM_CHANGER_NSLOTS:
	
	/* Big L */
	case LOOP_SET_FD:
	case LOOP_CLR_FD:
	
	/* Big A */
	case AUDIO_GETINFO:
	case AUDIO_SETINFO:
	case AUDIO_DRAIN:
	case AUDIO_GETDEV:
	case AUDIO_GETDEV_SUNOS:
	case AUDIO_FLUSH:

	/* Big Q for sound/OSS */
	case SNDCTL_SEQ_RESET:
	case SNDCTL_SEQ_SYNC:
	case SNDCTL_SYNTH_INFO:
	case SNDCTL_SEQ_CTRLRATE:
	case SNDCTL_SEQ_GETOUTCOUNT:
	case SNDCTL_SEQ_GETINCOUNT:
	case SNDCTL_SEQ_PERCMODE:
	case SNDCTL_FM_LOAD_INSTR:
	case SNDCTL_SEQ_TESTMIDI:
	case SNDCTL_SEQ_RESETSAMPLES:
	case SNDCTL_SEQ_NRSYNTHS:
	case SNDCTL_SEQ_NRMIDIS:
	case SNDCTL_MIDI_INFO:
	case SNDCTL_SEQ_THRESHOLD:
	case SNDCTL_SYNTH_MEMAVL:
	case SNDCTL_FM_4OP_ENABLE:
	case SNDCTL_SEQ_PANIC:
	case SNDCTL_SEQ_OUTOFBAND:
	case SNDCTL_SEQ_GETTIME:
	case SNDCTL_SYNTH_ID:
	case SNDCTL_SYNTH_CONTROL:
	case SNDCTL_SYNTH_REMOVESAMPLE:

	/* Big T for sound/OSS */
	case SNDCTL_TMR_TIMEBASE:
	case SNDCTL_TMR_START:
	case SNDCTL_TMR_STOP:
	case SNDCTL_TMR_CONTINUE:
	case SNDCTL_TMR_TEMPO:
	case SNDCTL_TMR_SOURCE:
	case SNDCTL_TMR_METRONOME:
	case SNDCTL_TMR_SELECT:

	/* Little m for sound/OSS */
	case SNDCTL_MIDI_PRETIME:
	case SNDCTL_MIDI_MPUMODE:
	case SNDCTL_MIDI_MPUCMD:

	/* Big P for sound/OSS */
	case SNDCTL_DSP_RESET:
	case SNDCTL_DSP_SYNC:
	case SNDCTL_DSP_SPEED:
	case SNDCTL_DSP_STEREO:
	case SNDCTL_DSP_GETBLKSIZE:
	case SNDCTL_DSP_CHANNELS:
	case SOUND_PCM_WRITE_FILTER:
	case SNDCTL_DSP_POST:
	case SNDCTL_DSP_SUBDIVIDE:
	case SNDCTL_DSP_SETFRAGMENT:
	case SNDCTL_DSP_GETFMTS:
	case SNDCTL_DSP_SETFMT:
	case SNDCTL_DSP_GETOSPACE:
	case SNDCTL_DSP_GETISPACE:
	case SNDCTL_DSP_NONBLOCK:
	case SNDCTL_DSP_GETCAPS:
	case SNDCTL_DSP_GETTRIGGER:
	case SNDCTL_DSP_SETTRIGGER:
	case SNDCTL_DSP_GETIPTR:
	case SNDCTL_DSP_GETOPTR:
	/* case SNDCTL_DSP_MAPINBUF: XXX needs translation */
	/* case SNDCTL_DSP_MAPOUTBUF: XXX needs translation */
	case SNDCTL_DSP_SETSYNCRO:
	case SNDCTL_DSP_SETDUPLEX:
	case SNDCTL_DSP_GETODELAY:
	case SNDCTL_DSP_PROFILE:

	case SOUND_PCM_READ_RATE:
	case SOUND_PCM_READ_CHANNELS:
	case SOUND_PCM_READ_BITS:
	case SOUND_PCM_READ_FILTER:

	/* Big C for sound/OSS */
	case SNDCTL_COPR_RESET:
	case SNDCTL_COPR_LOAD:
	case SNDCTL_COPR_RDATA:
	case SNDCTL_COPR_RCODE:
	case SNDCTL_COPR_WDATA:
	case SNDCTL_COPR_WCODE:
	case SNDCTL_COPR_RUN:
	case SNDCTL_COPR_HALT:
	case SNDCTL_COPR_SENDMSG:
	case SNDCTL_COPR_RCVMSG:

	/* Big M for sound/OSS */
	case SOUND_MIXER_READ_VOLUME:
	case SOUND_MIXER_READ_BASS:
	case SOUND_MIXER_READ_TREBLE:
	case SOUND_MIXER_READ_SYNTH:
	case SOUND_MIXER_READ_PCM:
	case SOUND_MIXER_READ_SPEAKER:
	case SOUND_MIXER_READ_LINE:
	case SOUND_MIXER_READ_MIC:
	case SOUND_MIXER_READ_CD:
	case SOUND_MIXER_READ_IMIX:
	case SOUND_MIXER_READ_ALTPCM:
	case SOUND_MIXER_READ_RECLEV:
	case SOUND_MIXER_READ_IGAIN:
	case SOUND_MIXER_READ_OGAIN:
	case SOUND_MIXER_READ_LINE1:
	case SOUND_MIXER_READ_LINE2:
	case SOUND_MIXER_READ_LINE3:
	case SOUND_MIXER_READ_MUTE:
	/* case SOUND_MIXER_READ_ENHANCE: same value as READ_MUTE */
	/* case SOUND_MIXER_READ_LOUD: same value as READ_MUTE */
	case SOUND_MIXER_READ_RECSRC:
	case SOUND_MIXER_READ_DEVMASK:
	case SOUND_MIXER_READ_RECMASK:
	case SOUND_MIXER_READ_STEREODEVS:
	case SOUND_MIXER_READ_CAPS:

	case SOUND_MIXER_WRITE_VOLUME:
	case SOUND_MIXER_WRITE_BASS:
	case SOUND_MIXER_WRITE_TREBLE:
	case SOUND_MIXER_WRITE_SYNTH:
	case SOUND_MIXER_WRITE_PCM:
	case SOUND_MIXER_WRITE_SPEAKER:
	case SOUND_MIXER_WRITE_LINE:
	case SOUND_MIXER_WRITE_MIC:
	case SOUND_MIXER_WRITE_CD:
	case SOUND_MIXER_WRITE_IMIX:
	case SOUND_MIXER_WRITE_ALTPCM:
	case SOUND_MIXER_WRITE_RECLEV:
	case SOUND_MIXER_WRITE_IGAIN:
	case SOUND_MIXER_WRITE_OGAIN:
	case SOUND_MIXER_WRITE_LINE1:
	case SOUND_MIXER_WRITE_LINE2:
	case SOUND_MIXER_WRITE_LINE3:
	case SOUND_MIXER_WRITE_MUTE:
	/* case SOUND_MIXER_WRITE_ENHANCE: same value as WRITE_MUTE */
	/* case SOUND_MIXER_WRITE_LOUD: same value as WRITE_MUTE */
	case SOUND_MIXER_WRITE_RECSRC:

	case SOUND_MIXER_INFO:
	case SOUND_OLD_MIXER_INFO:
	case SOUND_MIXER_ACCESS:
	case SOUND_MIXER_PRIVATE1:
	case SOUND_MIXER_PRIVATE2:
	case SOUND_MIXER_PRIVATE3:
	case SOUND_MIXER_PRIVATE4:
	case SOUND_MIXER_PRIVATE5:
	case SOUND_MIXER_GETLEVELS:
	case SOUND_MIXER_SETLEVELS:

	case OSS_GETVERSION:

	/* AUTOFS */
	case AUTOFS_IOC_READY:
	case AUTOFS_IOC_FAIL:
	case AUTOFS_IOC_CATATONIC:
	case AUTOFS_IOC_PROTOVER:
	case AUTOFS_IOC_EXPIRE:
	
		error = sys_ioctl (fd, cmd, arg);
		goto out;

	default:
		do {
			static int count = 0;
			if (++count <= 20)
				printk("sys32_ioctl: Unknown cmd fd(%d) "
				       "cmd(%08x) arg(%08x)\n",
				       (int)fd, (unsigned int)cmd, (unsigned int)arg);
		} while(0);
		error = -EINVAL;
		break;
	}
out:
	unlock_kernel();
	return error;
}
