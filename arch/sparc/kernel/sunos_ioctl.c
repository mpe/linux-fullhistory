/* $Id: sunos_ioctl.c,v 1.8 1995/11/26 04:07:39 davem Exp $
 * sunos_ioctl.c: The Linux Operating system: SunOS ioctl compatibility.
 * 
 * Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/termios.h>
#include <linux/ioctl.h>
#include <linux/route.h>
#include <linux/sockios.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/fs.h>

extern asmlinkage int sys_ioctl(unsigned int, unsigned int, unsigned long);

asmlinkage int sunos_ioctl (int fd, unsigned long cmd, unsigned long arg)
{
	struct file *filp;
	
	if (fd >= NR_OPEN || !(filp = current->files->fd [fd]))
		return -EBADF;

	/* SunOS networking ioctls. */
	switch (cmd) {
	case _IOW('r', 10, struct rtentry):
		return sys_ioctl(fd, SIOCADDRT, arg);
	case _IOW('r', 11, struct rtentry):
		return sys_ioctl(fd, SIOCDELRT, arg);
	case _IOW('i', 12, struct ifreq):
		return sys_ioctl(fd, SIOCSIFADDR, arg);
	case _IORW('i', 13, struct ifreq):
		return sys_ioctl(fd, SIOCGIFADDR, arg);
	case _IOW('i', 14, struct ifreq):
		return sys_ioctl(fd, SIOCSIFDSTADDR, arg);
	case _IORW('i', 15, struct ifreq):
		return sys_ioctl(fd, SIOCGIFDSTADDR, arg);
	case _IOW('i', 16, struct ifreq):
		return sys_ioctl(fd, SIOCSIFFLAGS, arg);
	case _IORW('i', 17, struct ifreq):
		return sys_ioctl(fd, SIOCGIFFLAGS, arg);
	case _IOW('i', 18, struct ifreq):
		return sys_ioctl(fd, SIOCSIFMEM, arg);
	case _IORW('i', 19, struct ifreq):
		return sys_ioctl(fd, SIOCGIFMEM, arg);
	case _IORW('i', 20, struct ifreq):
		return sys_ioctl(fd, SIOCGIFCONF, arg);
	case _IOW('i', 21, struct ifreq): /* SIOCSIFMTU */
		return sys_ioctl(fd, SIOCSIFMTU, arg);
	case _IORW('i', 22, struct ifreq): /* SIOCGIFMTU */
		return sys_ioctl(fd, SIOCGIFMTU, arg);

	case _IORW('i', 23, struct ifreq):
		return sys_ioctl(fd, SIOCGIFBRDADDR, arg);
	case _IOW('i', 24, struct ifreq):
		return sys_ioctl(fd, SIOCGIFBRDADDR, arg);
	case _IORW('i', 25, struct ifreq):
		return sys_ioctl(fd, SIOCGIFNETMASK, arg);
	case _IOW('i', 26, struct ifreq):
		return sys_ioctl(fd, SIOCSIFNETMASK, arg);
	case _IORW('i', 27, struct ifreq):
		return sys_ioctl(fd, SIOCGIFMETRIC, arg);
	case _IOW('i', 28, struct ifreq):
		return sys_ioctl(fd, SIOCSIFMETRIC, arg);

	case _IOW('i', 30, struct arpreq):
		return sys_ioctl(fd, SIOCSARP, arg);
	case _IOW('i', 31, struct arpreq):
		return sys_ioctl(fd, SIOCGARP, arg);
	case _IOW('i', 32, struct arpreq):
		return sys_ioctl(fd, SIOCGARP, arg);

	case _IOW('i', 40, struct ifreq): /* SIOCUPPER */
	case _IOW('i', 41, struct ifreq): /* SIOCLOWER */
	case _IOW('i', 44, struct ifreq): /* SIOCSETSYNC */
	case _IOW('i', 45, struct ifreq): /* SIOCGETSYNC */
	case _IOW('i', 46, struct ifreq): /* SIOCSSDSTATS */
	case _IOW('i', 47, struct ifreq): /* SIOCSSESTATS */
	case _IOW('i', 48, struct ifreq): /* SIOCSPROMISC */
		return -EINVAL;

	case _IOW('i', 49, struct ifreq):
		return sys_ioctl(fd, SIOCADDMULTI, arg);
	case _IOW('i', 50, struct ifreq):
		return sys_ioctl(fd, SIOCDELMULTI, arg);

	/* FDDI interface ioctls, unsupported. */
	case _IOW('i', 51, struct ifreq): /* SIOCFDRESET */
	case _IOW('i', 52, struct ifreq): /* SIOCFDSLEEP */
	case _IOW('i', 53, struct ifreq): /* SIOCSTRTFMWAR */
	case _IOW('i', 54, struct ifreq): /* SIOCLDNSTRTFW */
	case _IOW('i', 55, struct ifreq): /* SIOCGETFDSTAT */
	case _IOW('i', 56, struct ifreq): /* SIOCFDNMIINT */
	case _IOW('i', 57, struct ifreq): /* SIOCFDEXUSER */
	case _IOW('i', 58, struct ifreq): /* SIOCFDGNETMAP */
	case _IOW('i', 59, struct ifreq): /* SIOCFDGIOCTL */
		return -EINVAL;
	}

	return sys_ioctl (fd, cmd, arg);
}


