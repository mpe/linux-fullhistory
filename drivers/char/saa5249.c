/*
 *	Cleaned up to use existing videodev interface and allow the idea
 *	of multiple teletext decoders on the video4linux iface. Changed i2c
 *	to cover addressing clashes on device busses. It's also rebuilt so
 *	you can add arbitary multiple teletext devices to Linux video4linux
 *	now (well 32 anyway).
 *
 *	Alan Cox <Alan.Cox@linux.org>
 *
 *	The original driver was heavily modified to match the i2c interface
 *	It was truncated to use the WinTV boards, too.
 *
 *	Copyright (c) 1998 Richard Guenther <richard.guenther@student.uni-tuebingen.de>
 *
 * $Id: saa5249.c,v 1.1 1998/03/30 22:23:23 alan Exp $
 *
 *	Derived From
 *
 * vtx.c:
 * This is a loadable character-device-driver for videotext-interfaces
 * (aka teletext). Please check the Makefile/README for a list of supported
 * interfaces.
 *
 * Copyright (c) 1994-97 Martin Buck  <martin-2.buck@student.uni-ulm.de>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <stdarg.h>
#include <linux/i2c.h>
#include <linux/videotext.h>
#include <linux/videodev.h>

#define VTX_VER_MAJ 1
#define VTX_VER_MIN 7



#define NUM_DAUS 4
#define NUM_BUFS 8
#define IF_NAME "SAA5249"

static const int disp_modes[8][3] = 
{
	{ 0x46, 0x03, 0x03 },	/* DISPOFF */
	{ 0x46, 0xcc, 0xcc },	/* DISPNORM */
	{ 0x44, 0x0f, 0x0f },	/* DISPTRANS */
	{ 0x46, 0xcc, 0x46 },	/* DISPINS */
	{ 0x44, 0x03, 0x03 },	/* DISPOFF, interlaced */
	{ 0x44, 0xcc, 0xcc },	/* DISPNORM, interlaced */
	{ 0x44, 0x0f, 0x0f },	/* DISPTRANS, interlaced */
	{ 0x44, 0xcc, 0x46 }	/* DISPINS, interlaced */
};



#define PAGE_WAIT 30				/* Time in jiffies between requesting page and */
						/* checking status bits */
#define PGBUF_EXPIRE 1500			/* Time in jiffies to wait before retransmitting */
						/* page regardless of infobits */
typedef struct {
	u8 pgbuf[VTX_VIRTUALSIZE];		/* Page-buffer */
	u8 laststat[10];			/* Last value of infobits for DAU */
	u8 sregs[7];				/* Page-request registers */
	unsigned long expire;			/* Time when page will be expired */
	unsigned clrfound : 1;			/* VTXIOCCLRFOUND has been called */
	unsigned stopped : 1;			/* VTXIOCSTOPDAU has been called */
} vdau_t;

struct saa5249_device
{
	vdau_t vdau[NUM_DAUS];			/* Data for virtual DAUs (the 5249 only has one */
						/* real DAU, so we have to simulate some more) */
	int vtx_use_count;
	int is_searching[NUM_DAUS];
	int disp_mode;
	int virtual_mode;
	struct i2c_bus *bus;
};


#define CCTWR 34		/* I²C write/read-address of vtx-chip */
#define CCTRD 35
#define NOACK_REPEAT 10		/* Retry access this many times on failure */
#define CLEAR_DELAY 5		/* Time in jiffies required to clear a page */
#define I2C_TIMEOUT 300		/* open/close/SDA-check timeout in jiffies */
#define READY_TIMEOUT 3		/* Time in jiffies to wait for ready signal of I²C-bus interface */
#define INIT_DELAY 500		/* Time in usec to wait at initialization of CEA interface */
#define START_DELAY 10		/* Time in usec to wait before starting write-cycle (CEA) */

#define VTX_DEV_MINOR 0

/* General defines and debugging support */

#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define RESCHED \
        do { \
          if (current->need_resched) \
            schedule(); \
        } while (0)

static struct video_device saa_template;	/* Declared near bottom */

/*
 *	We do most of the hard work when we become a device on the i2c.
 */
 
static int saa5249_attach(struct i2c_device *device)
{
	int pgbuf;
	int err;
	struct video_device *vd;
	struct saa5249_device *t;
	/* Only attach these chips to the BT848 bus for now */
	
	if(device->bus->id!=I2C_BUSID_BT848)
		return -EINVAL;
	
	strcpy(device->name, IF_NAME);
	
	/*
	 *	Now create a video4linux device
	 */
	 
	vd=(struct video_device *)kmalloc(sizeof(struct video_device), GFP_KERNEL);
	if(vd==NULL)
		return -ENOMEM;
	
	memcpy(vd, &saa_template, sizeof(*vd));
	
	/*
	 *	Attach an saa5249 device
	 */
	
	t=(struct saa5249_device *)kmalloc(sizeof(struct saa5249_device), GFP_KERNEL);
	if(t==NULL)
	{
		kfree(vd);
		return -ENOMEM;
	}
	
	memset(t, 0, sizeof(*t));
	
	for (pgbuf = 0; pgbuf < NUM_DAUS; pgbuf++) 
	{
		memset(t->vdau[pgbuf].pgbuf, ' ', sizeof(t->vdau[0].pgbuf));
		memset(t->vdau[pgbuf].sregs, 0, sizeof(t->vdau[0].sregs));
		memset(t->vdau[pgbuf].laststat, 0, sizeof(t->vdau[0].laststat));
		t->vdau[pgbuf].expire = 0;
		t->vdau[pgbuf].clrfound = TRUE;
		t->vdau[pgbuf].stopped = TRUE;
		t->is_searching[pgbuf] = FALSE;
	}
	vd->priv=t;		 
	device->data=vd;
	
	/*
	 *	Register it
	 */

	if((err=video_register_device(vd, VFL_TYPE_VTX))<0)
	{
		kfree(t);
		kfree(vd);
		return err;
	}
	t->bus = device->bus;
	return 0;
}

static int saa5249_detach(struct i2c_device *device)
{
	struct video_device *vd=device->data;
	video_unregister_device(vd);
	kfree(vd->priv);
	kfree(vd);
	return 0;
}

static int saa5249_command(struct i2c_device *device,
			     unsigned int cmd, void *arg)
{
	return -EINVAL;
}

/* new I2C driver support */

static struct i2c_driver i2c_driver_videotext = 
{
	IF_NAME,		/* name */
	I2C_DRIVERID_VIDEOTEXT, /* in i2c.h */
	34, 35,			/* Addr range */
	saa5249_attach,
	saa5249_detach,
	saa5249_command
};

/*
 *	Wait the given number of jiffies (10ms). This calls the scheduler, so the actual
 *	delay may be longer.
 */

static void jdelay(unsigned long delay) 
{
	sigset_t oldblocked = current->blocked;

	spin_lock_irq(&current->sigmask_lock);
	sigfillset(&current->blocked);
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(delay);

	spin_lock_irq(&current->sigmask_lock);
	current->blocked = oldblocked;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
}


/* Send arbitrary number of bytes to I²C-bus. Start & stop handshaking is done by this routine.
 * adr should be address of I²C-device, varargs-list of values to send must be terminated by -1
 * Returns -1 if I²C-device didn't send acknowledge, 0 otherwise
 */

static int i2c_senddata(struct saa5249_device *t, int adr, ...) 
{
	int val, loop;
	va_list argp;

	for (loop = 0; loop <= NOACK_REPEAT; loop++) 
	{
		i2c_start(t->bus);
		if (i2c_sendbyte(t->bus, adr, 0) != 0)
			goto loopend;

		va_start(argp, adr);
		while ((val = va_arg(argp, int)) != -1) 
		{
			if (val < 0 || val > 255) 
			{
				printk(KERN_ERR "vtx: internal error in i2c_senddata\n");
				break;
			}
			if (i2c_sendbyte(t->bus, val, 0) != 0)
				goto loopend;
		}
		va_end(argp);
		i2c_stop(t->bus);
		return 0;
loopend:
		i2c_stop(t->bus);
	}
	va_end(argp);
	return -1;
}


/* Send count number of bytes from buffer buf to I²C-device adr. Start & stop handshaking is
 * done by this routine. If uaccess is TRUE, data is read from user-space with get_user.
 * Returns -1 if I²C-device didn't send acknowledge, 0 otherwise
 */

static int i2c_sendbuf(struct saa5249_device *t, int adr, int reg, int count, u8 *buf, int uaccess) 
{
	int pos, loop;
	u8 val;

	for (loop = 0; loop <= NOACK_REPEAT; loop++) 
	{
		i2c_start(t->bus);
		if (i2c_sendbyte(t->bus, adr, 0) != 0 || i2c_sendbyte(t->bus, reg, 0) != 0)
			goto loopend;
		for (pos = 0; pos < count; pos++) 
		{
			/* FIXME: FAULT WITH CLI/SPINLOCK ?? */
			if (uaccess)
				get_user(val, buf + pos);
			else
				val = buf[pos];
			if (i2c_sendbyte(t->bus, val, 0) != 0)
				goto loopend;
			RESCHED;
		}
		i2c_stop(t->bus);
		return 0;
loopend:
		i2c_stop(t->bus);
	}
	return -1;
}


/* Get count number of bytes from I²C-device at address adr, store them in buf. Start & stop
 * handshaking is done by this routine, ack will be sent after the last byte to inhibit further
 * sending of data. If uaccess is TRUE, data is written to user-space with put_user.
 * Returns -1 if I²C-device didn't send acknowledge, 0 otherwise
 */

static int i2c_getdata(struct saa5249_device *t, int adr, int count, u8 *buf, int uaccess) 
{
	int pos, loop, val;

	for (loop = 0; loop <= NOACK_REPEAT; loop++) 
	{
		i2c_start(t->bus);
		if (i2c_sendbyte(t->bus, adr, 1) != 0)
			goto loopend;
		for (pos = 0; pos < count; pos++) 
		{
			val = i2c_readbyte(t->bus, (pos==count-1) ? 1 : 0);
			if (uaccess) 
				put_user(val, buf + pos);
			else
				buf[pos] = val;
			RESCHED;
		}
		i2c_stop(t->bus);
		return 0;
loopend:
		i2c_stop(t->bus);
	}
	return -1;
}


/*
 *	Standard character-device-driver functions
 */

static int saa5249_ioctl(struct video_device *vd, unsigned int cmd, void *arg) 
{
	struct saa5249_device *t=vd->priv;
	static int virtual_mode = FALSE;

	switch(cmd) 
	{
		case VTXIOCGETINFO: 
		{
			vtx_info_t info;
			info.version_major = VTX_VER_MAJ;
			info.version_minor = VTX_VER_MIN;
			info.numpages = NUM_DAUS;
			/*info.cct_type = CCT_TYPE;*/
			if(copy_to_user((void*)arg, &info, sizeof(vtx_info_t)))
				return -EFAULT;
			return 0;
		}

		case VTXIOCCLRPAGE: 
		{
			vtx_pagereq_t req;
      
			if(copy_from_user(&req, (void*)arg, sizeof(vtx_pagereq_t)))
				return -EFAULT;
			if (req.pgbuf < 0 || req.pgbuf >= NUM_DAUS)
				return -EINVAL;
			memset(t->vdau[req.pgbuf].pgbuf, ' ', sizeof(t->vdau[0].pgbuf));
			t->vdau[req.pgbuf].clrfound = TRUE;
			return 0;
		}

		case VTXIOCCLRFOUND: 
		{
			vtx_pagereq_t req;
      
			if(copy_from_user(&req, (void*)arg, sizeof(vtx_pagereq_t)))
				return -EFAULT;
			if (req.pgbuf < 0 || req.pgbuf >= NUM_DAUS)
				return -EINVAL;
			t->vdau[req.pgbuf].clrfound = TRUE;
			return 0;
		}

		case VTXIOCPAGEREQ: 
		{
			vtx_pagereq_t req;
			if(copy_from_user(&req, (void*)arg, sizeof(vtx_pagereq_t)))
				return -EFAULT;
			if (!(req.pagemask & PGMASK_PAGE))
				req.page = 0;
			if (!(req.pagemask & PGMASK_HOUR))
				req.hour = 0;
			if (!(req.pagemask & PGMASK_MINUTE))
				req.minute = 0;
			if (req.page < 0 || req.page > 0x8ff) /* 7FF ?? */
				return -EINVAL;
			req.page &= 0x7ff;
			if (req.hour < 0 || req.hour > 0x3f || req.minute < 0 || req.minute > 0x7f ||
				req.pagemask < 0 || req.pagemask >= PGMASK_MAX || req.pgbuf < 0 || req.pgbuf >= NUM_DAUS)
				return -EINVAL;
			t->vdau[req.pgbuf].sregs[0] = (req.pagemask & PG_HUND ? 0x10 : 0) | (req.page / 0x100);
			t->vdau[req.pgbuf].sregs[1] = (req.pagemask & PG_TEN ? 0x10 : 0) | ((req.page / 0x10) & 0xf);
			t->vdau[req.pgbuf].sregs[2] = (req.pagemask & PG_UNIT ? 0x10 : 0) | (req.page & 0xf);
			t->vdau[req.pgbuf].sregs[3] = (req.pagemask & HR_TEN ? 0x10 : 0) | (req.hour / 0x10);
			t->vdau[req.pgbuf].sregs[4] = (req.pagemask & HR_UNIT ? 0x10 : 0) | (req.hour & 0xf);
			t->vdau[req.pgbuf].sregs[5] = (req.pagemask & MIN_TEN ? 0x10 : 0) | (req.minute / 0x10);
			t->vdau[req.pgbuf].sregs[6] = (req.pagemask & MIN_UNIT ? 0x10 : 0) | (req.minute & 0xf);
			t->vdau[req.pgbuf].stopped = FALSE;
			t->vdau[req.pgbuf].clrfound = TRUE;
			t->is_searching[req.pgbuf] = TRUE;
			return 0;
		}

		case VTXIOCGETSTAT: 
		{
			vtx_pagereq_t req;
			u8 infobits[10];
			vtx_pageinfo_t info;
			int a;

			if(copy_from_user(&req, (void*)arg, sizeof(vtx_pagereq_t)))
				return -EFAULT;
			if (req.pgbuf < 0 || req.pgbuf >= NUM_DAUS)
				return -EINVAL;
			if (!t->vdau[req.pgbuf].stopped) 
			{
				if (i2c_senddata(t, CCTWR, 2, 0, -1) ||
					i2c_sendbuf(t, CCTWR, 3, sizeof(t->vdau[0].sregs), t->vdau[req.pgbuf].sregs, FALSE) ||
					i2c_senddata(t, CCTWR, 8, 0, 25, 0, ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', -1) ||
					i2c_senddata(t, CCTWR, 2, 0, t->vdau[req.pgbuf].sregs[0] | 8, -1) ||
					i2c_senddata(t, CCTWR, 8, 0, 25, 0, -1))
					return -EIO;
				jdelay(PAGE_WAIT);
				if (i2c_getdata(t, CCTRD, 10, infobits, FALSE))
					return -EIO;

				if (!(infobits[8] & 0x10) && !(infobits[7] & 0xf0) &&	/* check FOUND-bit */
					(memcmp(infobits, t->vdau[req.pgbuf].laststat, sizeof(infobits)) || 
					time_after_eq(jiffies, t->vdau[req.pgbuf].expire)))
				{		/* check if new page arrived */
					if (i2c_senddata(t, CCTWR, 8, 0, 0, 0, -1) ||
						i2c_getdata(t, CCTRD, VTX_PAGESIZE, t->vdau[req.pgbuf].pgbuf, FALSE))
						return -EIO;
					t->vdau[req.pgbuf].expire = jiffies + PGBUF_EXPIRE;
					memset(t->vdau[req.pgbuf].pgbuf + VTX_PAGESIZE, ' ', VTX_VIRTUALSIZE - VTX_PAGESIZE);
					if (t->virtual_mode) 
					{
						/* Packet X/24 */
						if (i2c_senddata(t, CCTWR, 8, 0, 0x20, 0, -1) ||
							i2c_getdata(t, CCTRD, 40, t->vdau[req.pgbuf].pgbuf + VTX_PAGESIZE + 20 * 40, FALSE))
							return -EIO;
						/* Packet X/27/0 */
						if (i2c_senddata(t, CCTWR, 8, 0, 0x21, 0, -1) ||
							i2c_getdata(t, CCTRD, 40, t->vdau[req.pgbuf].pgbuf + VTX_PAGESIZE + 16 * 40, FALSE))
							return -EIO;
						/* Packet 8/30/0...8/30/15
						 * FIXME: AFAIK, the 5249 does hamming-decoding for some bytes in packet 8/30,
						 *        so we should undo this here.
						 */
						if (i2c_senddata(t, CCTWR, 8, 0, 0x22, 0, -1) ||
							i2c_getdata(t, CCTRD, 40, t->vdau[req.pgbuf].pgbuf + VTX_PAGESIZE + 23 * 40, FALSE))
							return -EIO;
					}
					t->vdau[req.pgbuf].clrfound = FALSE;
					memcpy(t->vdau[req.pgbuf].laststat, infobits, sizeof(infobits));
				}
				else
				{
					memcpy(infobits, t->vdau[req.pgbuf].laststat, sizeof(infobits));
				}
			}
			else
			{
				memcpy(infobits, t->vdau[req.pgbuf].laststat, sizeof(infobits));
			}

			info.pagenum = ((infobits[8] << 8) & 0x700) | ((infobits[1] << 4) & 0xf0) | (infobits[0] & 0x0f);
			if (info.pagenum < 0x100)
				info.pagenum += 0x800;
			info.hour = ((infobits[5] << 4) & 0x30) | (infobits[4] & 0x0f);
			info.minute = ((infobits[3] << 4) & 0x70) | (infobits[2] & 0x0f);
			info.charset = ((infobits[7] >> 1) & 7);
			info.delete = !!(infobits[3] & 8);
			info.headline = !!(infobits[5] & 4);
			info.subtitle = !!(infobits[5] & 8);
			info.supp_header = !!(infobits[6] & 1);
			info.update = !!(infobits[6] & 2);
			info.inter_seq = !!(infobits[6] & 4);
			info.dis_disp = !!(infobits[6] & 8);
			info.serial = !!(infobits[7] & 1);
			info.notfound = !!(infobits[8] & 0x10);
			info.pblf = !!(infobits[9] & 0x20);
			info.hamming = 0;
			for (a = 0; a <= 7; a++) 
			{
				if (infobits[a] & 0xf0) 
				{
					info.hamming = 1;
					break;
				}
			}
			if (t->vdau[req.pgbuf].clrfound)
				info.notfound = 1;
			if(copy_to_user(req.buffer, &info, sizeof(vtx_pageinfo_t)))
				return -EFAULT;
			if (!info.hamming && !info.notfound) 
			{
				t->is_searching[req.pgbuf] = FALSE;
			}
			return 0;
		}

		case VTXIOCGETPAGE: 
		{
			vtx_pagereq_t req;
			int start, end;

			if(copy_from_user(&req, (void*)arg, sizeof(vtx_pagereq_t)))
				return -EFAULT;
			if (req.pgbuf < 0 || req.pgbuf >= NUM_DAUS || req.start < 0 ||
				req.start > req.end || req.end >= (virtual_mode ? VTX_VIRTUALSIZE : VTX_PAGESIZE))
				return -EINVAL;
			if(copy_to_user(req.buffer, &t->vdau[req.pgbuf].pgbuf[req.start], req.end - req.start + 1))
				return -EFAULT;
				
			 /* 
			  *	Always read the time directly from SAA5249
			  */
			  
			if (req.start <= 39 && req.end >= 32) 
			{
				start = MAX(req.start, 32);
				end = MIN(req.end, 39);
				if (i2c_senddata(t, CCTWR, 8, 0, 0, start, -1) ||
					i2c_getdata(t, CCTRD, end - start + 1, req.buffer + start - req.start, TRUE))
					return -EIO;
			}
			/* Insert the current header if DAU is still searching for a page */
			if (req.start <= 31 && req.end >= 7 && t->is_searching[req.pgbuf]) 
			{
				start = MAX(req.start, 7);
				end = MIN(req.end, 31);
				if (i2c_senddata(t, CCTWR, 8, 0, 0, start, -1) ||
					i2c_getdata(t, CCTRD, end - start + 1, req.buffer + start - req.start, TRUE))
					return -EIO;
			}
			return 0;
		}

		case VTXIOCSTOPDAU: 
		{
			vtx_pagereq_t req;

			if(copy_from_user(&req, (void*)arg, sizeof(vtx_pagereq_t)))
				return -EFAULT;
			if (req.pgbuf < 0 || req.pgbuf >= NUM_DAUS)
				return -EINVAL;
			t->vdau[req.pgbuf].stopped = TRUE;
			t->is_searching[req.pgbuf] = FALSE;
			return 0;
		}

		case VTXIOCPUTPAGE: 
		case VTXIOCSETDISP: 
		case VTXIOCPUTSTAT: 
			return 0;
			
		case VTXIOCCLRCACHE: 
		{
			if (i2c_senddata(t ,CCTWR, 0, NUM_DAUS, 0, 8, -1) || i2c_senddata(t, CCTWR, 11,
				' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
				' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ', -1))
				return -EIO;
			if (i2c_senddata(t, CCTWR, 3, 0x20, -1))
				return -EIO;
			jdelay(10 * CLEAR_DELAY);			/* I have no idea how long we have to wait here */
			return 0;
		}

		case VTXIOCSETVIRT: 
		{
			/* The SAA5249 has virtual-row reception turned on always */
			t->virtual_mode = (int)arg;
			return 0;
		}
	}
	return -EINVAL;
}


static int saa5249_open(struct video_device *vd, int nb) 
{
	struct saa5249_device *t=vd->priv;
	int pgbuf;

	if (t->bus==NULL) 
		return -ENODEV;

	if (i2c_senddata(t, CCTWR, 0, 0, -1) ||		/* Select R11 */
						/* Turn off parity checks (we do this ourselves) */
		i2c_senddata(t, CCTWR, 1, disp_modes[t->disp_mode][0], 0, -1) ||
						/* Display TV-picture, no virtual rows */
		i2c_senddata(t, CCTWR, 4, NUM_DAUS, disp_modes[t->disp_mode][1], disp_modes[t->disp_mode][2], 7, -1)) /* Set display to page 4 */
	
	{
		return -EIO;
	}

	for (pgbuf = 0; pgbuf < NUM_DAUS; pgbuf++) 
	{
		memset(t->vdau[pgbuf].pgbuf, ' ', sizeof(t->vdau[0].pgbuf));
		memset(t->vdau[pgbuf].sregs, 0, sizeof(t->vdau[0].sregs));
		memset(t->vdau[pgbuf].laststat, 0, sizeof(t->vdau[0].laststat));
		t->vdau[pgbuf].expire = 0;
		t->vdau[pgbuf].clrfound = TRUE;
		t->vdau[pgbuf].stopped = TRUE;
		t->is_searching[pgbuf] = FALSE;
	}
	t->virtual_mode=FALSE;
	MOD_INC_USE_COUNT;
	return 0;
}



static void saa5249_release(struct video_device *vd) 
{
	struct saa5249_device *t=vd->priv;
	i2c_senddata(t, CCTWR, 1, 0x20, -1);		/* Turn off CCT */
	i2c_senddata(t, CCTWR, 5, 3, 3, -1);		/* Turn off TV-display */
	MOD_DEC_USE_COUNT;
	return;
}

static long saa5249_write(struct video_device *v, const char *buf, unsigned long l, int nb)
{
	return -EINVAL;
}

static long saa5249_read(struct video_device *v, char *buf, unsigned long l, int nb)
{
	return -EINVAL;
}

int init_saa_5249(struct video_init *v)
{
	printk(KERN_INFO "SAA5249 driver (" IF_NAME " interface) for VideoText version %d.%d\n",
			VTX_VER_MAJ, VTX_VER_MIN);
	i2c_register_driver(&i2c_driver_videotext);

	return 0;
}

static struct video_device saa_template=
{
	IF_NAME,
	VID_TYPE_TELETEXT,	/*| VID_TYPE_TUNER ?? */
	VID_HARDWARE_SAA5249,
	saa5249_open,
	saa5249_release,
	saa5249_read,
	saa5249_write,
	NULL,	/* poll */
	saa5249_ioctl,
	NULL,
	NULL,
	NULL,
	0,
	0
};

#ifdef MODULE

/*
 *	Routines for loadable modules
 */

int init_module(void) 
{
	init_saa_5249(NULL);
	return 0;
}


void cleanup_module(void) 
{
	i2c_unregister_driver(&i2c_driver_videotext);
}

#endif
