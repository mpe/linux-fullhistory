/* $Id: ioctl.c,v 1.2 1997/09/04 00:59:22 davem Exp $
 * ioctl.c: Solaris ioctl emulation.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * Streams & timod emulation based on code
 * Copyright (C) 1995, 1996 Mike Jagdis (jaggy@purplet.demon.co.uk)
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/ioctl.h>
#include <linux/fs.h>

#include <asm/uaccess.h>
#include <asm/termios.h>

#include "conv.h"

extern char * getname32(u32 filename);
#define putname32 putname

extern asmlinkage int sys_ioctl(unsigned int fd, unsigned int cmd, 
	unsigned long arg);
extern asmlinkage int sys32_ioctl(unsigned int fd, unsigned int cmd,
	u32 arg);
asmlinkage int solaris_ioctl(unsigned int fd, unsigned int cmd, u32 arg);

/* termio* stuff {{{ */

struct solaris_termios {
	u32	c_iflag;
	u32	c_oflag;
	u32	c_cflag;
	u32	c_lflag;
	u8	c_cc[19];
};

struct solaris_termio {
	u16	c_iflag;
	u16	c_oflag;
	u16	c_cflag;
	u16	c_lflag;
	s8	c_line;
	u8	c_cc[8];
};

struct solaris_termiox {
	u16	x_hflag;
	u16	x_cflag;
	u16	x_rflag[5];
	u16	x_sflag;
};

static u32 solaris_to_linux_cflag(u32 cflag)
{
	cflag &= 0x7fdff000;
	if (cflag & 0x200000) {
		int baud = cflag & 0xf;
		cflag &= ~0x20000f;
		switch (baud) {
		case 0: baud = B57600; break;
		case 1: baud = B76800; break;
		case 2: baud = B115200; break;
		case 3: baud = B153600; break;
		case 4: baud = B230400; break;
		case 5: baud = B307200; break;
		case 6: baud = B460800; break;
		}
		cflag |= CBAUDEX | baud;
	}
	return cflag;
}

static u32 linux_to_solaris_cflag(u32 cflag)
{
	cflag &= ~(CMSPAR | CIBAUD);
	if (cflag & CBAUDEX) {
		int baud = cflag & CBAUD;
		cflag &= ~CBAUD;
		switch (baud) {
		case B57600: baud = 0; break;
		case B76800: baud = 1; break;
		case B115200: baud = 2; break;
		case B153600: baud = 3; break;
		case B230400: baud = 4; break;
		case B307200: baud = 5; break;
		case B460800: baud = 6; break;
		case B614400: baud = 7; break;
		case B921600: baud = 8; break;
		/* case B1843200: baud = 9; break; */
		}
		cflag |= 0x200000 | baud;
	}
	return cflag;
}

static inline int linux_to_solaris_termio(unsigned int fd, unsigned int cmd, u32 arg)
{
	int ret;
	
	ret = sys_ioctl(fd, cmd, A(arg));
	if (!ret) {
		u32 cflag;
		
		if (__get_user (cflag, &((struct solaris_termio *)A(arg))->c_cflag))
			return -EFAULT;
		cflag = linux_to_solaris_cflag(cflag);
		if (__put_user (cflag, &((struct solaris_termio *)A(arg))->c_cflag))
			return -EFAULT;
	}
	return ret;
}

static int solaris_to_linux_termio(unsigned int fd, unsigned int cmd, u32 arg)
{
	int ret;
	struct solaris_termio s;
	unsigned long old_fs = get_fs();
	
	if (copy_from_user (&s, (struct solaris_termio *)A(arg), sizeof(struct solaris_termio)))
		return -EFAULT;
	s.c_cflag = solaris_to_linux_cflag(s.c_cflag);
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, cmd, (unsigned long)&s);
	set_fs(old_fs);
	return ret;
}

static inline int linux_to_solaris_termios(unsigned int fd, unsigned int cmd, u32 arg)
{
	int ret;
	struct solaris_termios s;
	unsigned long old_fs = get_fs();

	set_fs(KERNEL_DS);	
	ret = sys_ioctl(fd, cmd, (unsigned long)&s);
	set_fs(old_fs);
	if (!ret) {
		if (put_user (s.c_iflag, &((struct solaris_termios *)A(arg))->c_iflag) ||
		    __put_user (s.c_oflag, &((struct solaris_termios *)A(arg))->c_oflag) ||
		    __put_user (linux_to_solaris_cflag(s.c_cflag), &((struct solaris_termios *)A(arg))->c_cflag) ||
		    __put_user (s.c_lflag, &((struct solaris_termios *)A(arg))->c_lflag) ||
		    __copy_to_user (((struct solaris_termios *)A(arg))->c_cc, s.c_cc, 16) ||
		    __clear_user (((struct solaris_termios *)A(arg))->c_cc + 16, 2))
			return -EFAULT;
	}
	return ret;
}

static int solaris_to_linux_termios(unsigned int fd, unsigned int cmd, u32 arg)
{
	int ret;
	struct solaris_termios s;
	unsigned long old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, TCGETS, (unsigned long)&s);
	set_fs(old_fs);
	if (ret) return ret;
	if (put_user (s.c_iflag, &((struct solaris_termios *)A(arg))->c_iflag) ||
	    __put_user (s.c_oflag, &((struct solaris_termios *)A(arg))->c_oflag) ||
	    __put_user (s.c_cflag, &((struct solaris_termios *)A(arg))->c_cflag) ||
	    __put_user (s.c_lflag, &((struct solaris_termios *)A(arg))->c_lflag) ||
	    __copy_from_user (s.c_cc, ((struct solaris_termios *)A(arg))->c_cc, 16))
		return -EFAULT;
	s.c_cflag = solaris_to_linux_cflag(s.c_cflag);
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, cmd, (unsigned long)&s);
	set_fs(old_fs);
	return ret;
}

static inline int solaris_T(unsigned int fd, unsigned int cmd, u32 arg)
{
	switch (cmd & 0xff) {
	case 1: /* TCGETA */
		return linux_to_solaris_termio(fd, TCGETA, arg);
	case 2: /* TCSETA */
		return solaris_to_linux_termio(fd, TCSETA, arg);
	case 3: /* TCSETAW */
		return solaris_to_linux_termio(fd, TCSETAW, arg);
	case 4: /* TCSETAF */
		return solaris_to_linux_termio(fd, TCSETAF, arg);
	case 5: /* TCSBRK */
		return sys_ioctl(fd, TCSBRK, arg);
	case 6: /* TCXONC */
		return sys_ioctl(fd, TCXONC, arg);
	case 7: /* TCFLSH */
		return sys_ioctl(fd, TCFLSH, arg);
	case 13: /* TCGETS */
		return linux_to_solaris_termios(fd, TCGETS, arg);
	case 14: /* TCSETS */
		return solaris_to_linux_termios(fd, TCSETS, arg);
	case 15: /* TCSETSW */
		return solaris_to_linux_termios(fd, TCSETSW, arg);
	case 16: /* TCSETSF */
		return solaris_to_linux_termios(fd, TCSETSF, arg);
	case 103: /* TIOCSWINSZ */
		return sys_ioctl(fd, TIOCSWINSZ, arg);
	case 104: /* TIOCGWINSZ */
		return sys_ioctl(fd, TIOCGWINSZ, arg);
	}
	return -ENOSYS;
}

static inline int solaris_t(unsigned int fd, unsigned int cmd, u32 arg)
{
	switch (cmd & 0xff) {
	case 20: /* TIOCGPGRP */
		return sys_ioctl(fd, TIOCGPGRP, arg);
	case 21: /* TIOCSPGRP */
		return sys_ioctl(fd, TIOCSPGRP, arg);
	}
	return -ENOSYS;
}

/* }}} */

/* A pseudo STREAMS support {{{ */

struct strioctl {
	int cmd, timeout, len;
	u32 data;
};

static inline int solaris_S(unsigned int fd, unsigned int cmd, u32 arg)
{
	char *p;
	int ret;
	unsigned long old_fs;
	struct strioctl si;
	
	switch (cmd & 0xff) {
	case 1: /* I_NREAD */
		return -ENOSYS;
	case 2: /* I_PUSH */
		p = getname32 (arg);
		if (IS_ERR (p))
			return PTR_ERR(p);
		putname32 (p);
		return 0;
	case 3: /* I_POP */
		return 0;
	case 5: /* I_FLUSH */
		return 0;
	case 8: /* I_STR */
		if (copy_from_user (&si, (struct strioctl *)A(arg), sizeof(struct strioctl)))
			return -EFAULT;
		switch ((si.cmd >> 8) & 0xff) {
		case 'T':
		default:
			return solaris_ioctl(fd, si.cmd, si.data);
		}
	case 9: /* I_SETSIG */
		return sys_ioctl(fd, FIOSETOWN, current->pid);
	case 10: /* I_GETSIG */
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		sys_ioctl(fd, FIOGETOWN, (unsigned long)&ret);
		set_fs(old_fs);
		if (ret == current->pid) return 0x3ff;
		else return -EINVAL;
	case 11: /* I_FIND */
		p = getname32 (arg);
		if (IS_ERR (p))
			return PTR_ERR(p);
		ret = !strcmp(p, "timod");
		putname32 (p);
		return ret;
	}
	return -ENOSYS;
}
/* }}} */

asmlinkage int solaris_ioctl(unsigned int fd, unsigned int cmd, u32 arg)
{
	struct file * filp;
	int error = -EBADF;

	lock_kernel();
	if(fd >= NR_OPEN) goto out;

	filp = current->files->fd[fd];
	if(!filp) goto out;

	if (!filp->f_op || !filp->f_op->ioctl) {
		error = sys_ioctl (fd, cmd, (unsigned long)arg);
		goto out;
	}
	
	error = -EFAULT;
	switch ((cmd >> 8) & 0xff) {
	case 'S': error = solaris_S(fd, cmd, arg); break;
	case 'T': error = solaris_T(fd, cmd, arg); break;
	case 't': error = solaris_t(fd, cmd, arg); break;
	default:
		error = -ENOSYS;
		break;
	}
out:
	if (error == -ENOSYS) {
		unsigned char c = cmd>>8;
		
		if (c < ' ' || c > 126) c = '.';
		printk("solaris_ioctl: Unknown cmd fd(%d) cmd(%08x '%c') arg(%08x)\n",
		       (int)fd, (unsigned int)cmd, c, (unsigned int)arg);
		error = -EINVAL;
	}
	unlock_kernel();
	return error;
}
