/*
 *            Telephony registration for Linux
 *
 *              (c) Copyright 1999 Red Hat Software Inc.
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Author:      Alan Cox, <alan@redhat.com>
 *
 * Fixes:       Mar 01 2000 Thomas Sparr, <thomas.l.sparr@telia.com>
 *              phone_register_device now works with unit!=PHONE_UNIT_ANY
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/phonedev.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/kmod.h>


#define PHONE_NUM_DEVICES	256

/*
 *    Active devices 
 */

static struct phone_device *phone_device[PHONE_NUM_DEVICES];

/*
 *    Open a phone device.
 */

static int phone_open(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	int err;
	struct phone_device *p;

	if (minor >= PHONE_NUM_DEVICES)
		return -ENODEV;

	p = phone_device[minor];
	if (p == NULL) {
		char modname[32];

		sprintf(modname, "char-major-%d-%d", PHONE_MAJOR, minor);
		request_module(modname);
		p = phone_device[minor];
		if (p == NULL)
			return -ENODEV;
	}
	if (p->open) {
		err = p->open(p, file);	/* Tell the device it is open */
		if (err)
			return err;
	}
	file->f_op = p->f_op;
	return 0;
}

/*
 *    Telephony For Linux device drivers request registration here.
 */

int phone_register_device(struct phone_device *p, int unit)
{
	int base;
	int end;
	int i;

	base = 0;
	end = PHONE_NUM_DEVICES - 1;

	if (unit != PHONE_UNIT_ANY) {
		base = unit;
		end = unit + 1;  /* enter the loop at least one time */
	}
	for (i = base; i < end; i++) {
		if (phone_device[i] == NULL) {
			phone_device[i] = p;
			p->minor = i;
			MOD_INC_USE_COUNT;
			return 0;
		}
	}
	return -ENFILE;
}

/*
 *    Unregister an unused Telephony for linux device
 */

void phone_unregister_device(struct phone_device *pfd)
{
	if (phone_device[pfd->minor] != pfd)
		panic("phone: bad unregister");
	phone_device[pfd->minor] = NULL;
	MOD_DEC_USE_COUNT;
}


static struct file_operations phone_fops =
{
	open:		phone_open,
};

/*
 *	Board init functions
 */
 
extern int ixj_init(void);

/*
 *    Initialise Telephony for linux
 */

int telephony_init(void)
{
	printk(KERN_INFO "Linux telephony interface: v1.00\n");
	if (register_chrdev(PHONE_MAJOR, "telephony", &phone_fops)) {
		printk("phonedev: unable to get major %d\n", PHONE_MAJOR);
		return -EIO;
	}
	/*
	 *    Init kernel installed drivers
	 */
#ifdef CONFIG_PHONE_IXJ
	ixj_init();	 
#endif
	return 0;
}

#ifdef MODULE
int init_module(void)
{
	return telephony_init();
}

void cleanup_module(void)
{
	unregister_chrdev(PHONE_MAJOR, "telephony");
}

#endif

EXPORT_SYMBOL(phone_register_device);
EXPORT_SYMBOL(phone_unregister_device);
