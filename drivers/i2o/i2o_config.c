/*
 *	I2O Configuration Interface Driver
 *
 *	(C) Copyright 1999   Red Hat Software
 *	
 *	Written by Alan Cox, Building Number Three Ltd
 *
 *      Modified 04/20/1999 by Deepak Saxena
 *         - Added basic ioctl() support
 *      Modified 06/07/1999 by Deepak Saxena
 *         - Added software download ioctl (still testing)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 * 	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/i2o.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/spinlock.h>

#include "i2o_proc.h"

static int i2o_cfg_token = 0;
static int i2o_cfg_context = -1;
static void *page_buf;
static void *i2o_buffer;
static int i2o_ready;
static int i2o_pagelen;
static int i2o_error;
static int cfg_inuse;
static int i2o_eof;
static spinlock_t i2o_config_lock = SPIN_LOCK_UNLOCKED;
struct wait_queue *i2o_wait_queue;

static int ioctl_getiops(unsigned long);
static int ioctl_gethrt(unsigned long);
static int ioctl_getlct(unsigned long);
static int ioctl_parms(unsigned long, unsigned int);
static int ioctl_html(unsigned long);
static int ioctl_swdl(unsigned long);
static int ioctl_swul(unsigned long);
static int ioctl_swdel(unsigned long);

/*
 *	This is the callback for any message we have posted. The message itself
 *	will be returned to the message pool when we return from the IRQ
 *
 *	This runs in irq context so be short and sweet.
 */
static void i2o_cfg_reply(struct i2o_handler *h, struct i2o_controller *c, struct i2o_message *m)
{
	i2o_cfg_token = I2O_POST_WAIT_OK;

	return;
}

/*
 *	Each of these describes an i2o message handler. They are
 *	multiplexed by the i2o_core code
 */
 
struct i2o_handler cfg_handler=
{
	i2o_cfg_reply,
	"Configuration",
	0
};

static long long cfg_llseek(struct file *file, long long offset, int origin)
{
	return -ESPIPE;
}

/* i2ocontroller/i2odevice/page/?data */

static ssize_t cfg_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	printk(KERN_INFO "i2o_config write not yet supported\n");

	return 0;
}

/* To be written for event management support */
static ssize_t cfg_read(struct file *file, char *buf, size_t count, loff_t *ptr)
{
	return 0;
}

static int cfg_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	int ret;

	/* Only 1 token, so lock... */
	spin_lock(&i2o_config_lock);

	switch(cmd)
	{	
		case I2OGETIOPS:
			ret = ioctl_getiops(arg);
			break;

		case I2OHRTGET:
			ret = ioctl_gethrt(arg);
			break;

		case I2OLCTGET:
			ret = ioctl_getlct(arg);
			break;

		case I2OPARMSET:
			ret = ioctl_parms(arg, I2OPARMSET);
			break;

		case I2OPARMGET:
			ret = ioctl_parms(arg, I2OPARMGET);
			break;

		case I2OSWDL:
			ret = ioctl_swdl(arg);
			break;

		case I2OSWUL:
			ret = ioctl_swul(arg);
			break;

		case I2OSWDEL:
			ret = ioctl_swdel(arg);
			break;

		case I2OHTML:
			ret = ioctl_html(arg);
			break;

		default:
			ret = -EINVAL;
	}

	spin_unlock(&i2o_config_lock);
	return ret;
}

int ioctl_getiops(unsigned long arg)
{
	u8 *user_iop_table = (u8*)arg;
	struct i2o_controller *c = NULL;
	int i;
	u8 foo[MAX_I2O_CONTROLLERS];

	if(!access_ok(VERIFY_WRITE, user_iop_table,  MAX_I2O_CONTROLLERS))
		return -EFAULT;

	for(i = 0; i < MAX_I2O_CONTROLLERS; i++)
	{
		c = i2o_find_controller(i);
		if(c)
		{
			printk(KERN_INFO "ioctl: iop%d found\n", i);
			foo[i] = 1;
			i2o_unlock_controller(c);
		}
		else
		{
			printk(KERN_INFO "ioctl: iop%d not found\n", i);
			foo[i] = 0;
		}
	}

	__copy_to_user(user_iop_table, foo, MAX_I2O_CONTROLLERS);
	return 0;
}

int ioctl_gethrt(unsigned long arg)
{
	struct i2o_controller *c;
	struct i2o_cmd_hrtlct *cmd = (struct i2o_cmd_hrtlct*)arg;
	struct i2o_cmd_hrtlct kcmd;
	pi2o_hrt hrt;
	u32 msg[6];
	u32 *workspace;
	int len;
	int token;
	u32 reslen;
	int ret = 0;

	if(copy_from_user(&kcmd, cmd, sizeof(struct i2o_cmd_hrtlct)))
		return -EFAULT;

	if(get_user(reslen, kcmd.reslen) < 0)
		return -EFAULT;

	if(kcmd.resbuf == NULL)
		return -EFAULT;

	c = i2o_find_controller(kcmd.iop);
	if(!c)
		return -ENXIO;

	workspace = kmalloc(8192, GFP_KERNEL);
	hrt = (pi2o_hrt)workspace;
	if(workspace==NULL)
	{
		i2o_unlock_controller(c);
		return -ENOMEM;
	}

	memset(workspace, 0, 8192);

	msg[0]= SIX_WORD_MSG_SIZE| SGL_OFFSET_4;
	msg[1]= I2O_CMD_HRT_GET<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2]= (u32)cfg_handler.context;
	msg[3]= 0;
	msg[4]= (0xD0000000 | 8192);
	msg[5]= virt_to_phys(workspace);

	token = i2o_post_wait(c, ADAPTER_TID, msg, 6*4, &i2o_cfg_token,2);
	if(token == I2O_POST_WAIT_TIMEOUT)
	{
		kfree(workspace);
		i2o_unlock_controller(c);
		return -ETIMEDOUT;
	}
	i2o_unlock_controller(c);

	len = 8 + ((hrt->entry_len * hrt->num_entries) << 2);
	/* We did a get user...so assuming mem is ok...is this bad? */
	put_user(len, kcmd.reslen);
	if(len > reslen)
		ret = -ENOBUFS;	
	if(copy_to_user(kcmd.resbuf, (void*)hrt, len))
		ret = -EINVAL;

	kfree(workspace);
	return ret;
}

int ioctl_getlct(unsigned long arg)
{
	struct i2o_controller *c;
	struct i2o_cmd_hrtlct *cmd = (struct i2o_cmd_hrtlct*)arg;
	struct i2o_cmd_hrtlct kcmd;
	pi2o_lct lct;
	u32 msg[9];
	u32 *workspace;
	int len;
	int token;
	int ret = 0;
	u32 reslen;

	if(copy_from_user(&kcmd, cmd, sizeof(struct i2o_cmd_hrtlct)))
		return -EFAULT;

	if(get_user(reslen, kcmd.reslen) < 0)
		return -EFAULT;

	if(kcmd.resbuf == NULL)
		return -EFAULT;

	c = i2o_find_controller(kcmd.iop);
	if(!c)
		return -ENXIO;

	workspace = kmalloc(8192, GFP_KERNEL);
	lct = (pi2o_lct)workspace;
	if(workspace==NULL)
	{
		i2o_unlock_controller(c);
		return -ENOMEM;
	}

	memset(workspace, 0, 8192);

	msg[0]= EIGHT_WORD_MSG_SIZE | SGL_OFFSET_6;
	msg[1]= I2O_CMD_LCT_NOTIFY<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2]= (u32)cfg_handler.context;
	msg[3]= 0;
	msg[4]= 0xFFFFFFFF;
	msg[5]= 0;
	msg[6]= (0xD0000000 | 8192);
	msg[7]= virt_to_phys(workspace);

	token = i2o_post_wait(c, ADAPTER_TID, msg, 8*4, &i2o_cfg_token,2);
	if(token == I2O_POST_WAIT_TIMEOUT)
	{
		kfree(workspace);
		i2o_unlock_controller(c);
		return -ETIMEDOUT;
	}
	i2o_unlock_controller(c);

	len = (unsigned int)lct->table_size << 2;
	put_user(len, kcmd.reslen);
	if(len > reslen)
		ret = -ENOBUFS;	
	else if(copy_to_user(kcmd.resbuf, (void*)lct, len))
		ret = -EINVAL;

	kfree(workspace);
	return ret;
}

static int ioctl_parms(unsigned long arg, unsigned int type)
{
	int ret = 0;
	struct i2o_controller *c;
	struct i2o_cmd_psetget *cmd = (struct i2o_cmd_psetget*)arg;
	struct i2o_cmd_psetget kcmd;
	u32 msg[9];
	u32 reslen;
	int token;
	u8 *ops;
	u8 *res;
	u16 *res16;	
	u32 *res32;
	u16 count;
	int len;
	int i,j;

	u32 i2o_cmd = (type == I2OPARMGET ? 
				I2O_CMD_UTIL_PARAMS_GET :
				I2O_CMD_UTIL_PARAMS_SET);

	if(copy_from_user(&kcmd, cmd, sizeof(struct i2o_cmd_psetget)))
		return -EFAULT;

	if(get_user(reslen, kcmd.reslen))
		return -EFAULT;

	c = i2o_find_controller(kcmd.iop);
	if(!c)
		return -ENXIO;

	ops = (u8*)kmalloc(kcmd.oplen, GFP_KERNEL);
	if(!ops)
	{
		i2o_unlock_controller(c);
		return -ENOMEM;
	}

	if(copy_from_user(ops, kcmd.opbuf, kcmd.oplen))
	{
		i2o_unlock_controller(c);
		kfree(ops);
		return -EFAULT;
	}

	/*
	 * It's possible to have a _very_ large table
	 * and that the user asks for all of it at once...
	 */
	res = (u8*)kmalloc(65536, GFP_KERNEL);
	if(!res)
	{
		i2o_unlock_controller(c);
		kfree(ops);
		return -ENOMEM;
	}

	res16 = (u16*)res;

	msg[0]=NINE_WORD_MSG_SIZE|SGL_OFFSET_5;
	msg[1]=i2o_cmd<<24|HOST_TID<<12|cmd->tid;
	msg[2]=(u32)cfg_handler.context;
	msg[3]=0;
	msg[4]=0;
	msg[5]=0x54000000|kcmd.oplen;
	msg[6]=virt_to_bus(ops);
	msg[7]=0xD0000000|(65536);
	msg[8]=virt_to_bus(res);

	/*
	 * Parm set sometimes takes a little while for some reason
	 */
	token = i2o_post_wait(c, kcmd.tid, msg, 9*4, &i2o_cfg_token,10);
	if(token == I2O_POST_WAIT_TIMEOUT)
	{
		i2o_unlock_controller(c);
		kfree(ops);
		kfree(res);
		return -ETIMEDOUT;
	}
	i2o_unlock_controller(c);
	kfree(ops);

	/* 
  	 * Determine required size...there's got to be a quicker way? 
	 * Dump data to syslog for debugging failures
	 */
	count = res16[0];
	printk(KERN_INFO "%0#6x\n%0#6x\n", res16[0], res16[1]);
	len = 4;
	res16 += 2;
	for(i = 0; i < count; i++ )
	{
		len += res16[0] << 2;	/* BlockSize field in ResultBlock */
		res32 = (u32*)res16;
		for(j = 0; j < res16[0]; j++)
			printk(KERN_INFO "%0#10x\n", res32[j]);
		res16 += res16[0] << 1;	/* Shift to next block */
	}

	put_user(len, kcmd.reslen);
	if(len > reslen)
		ret = -ENOBUFS;
	else if(copy_to_user(cmd->resbuf, res, len))
		ret = -EFAULT;

	kfree(res);

	return ret;
}

int ioctl_html(unsigned long arg)
{
	struct i2o_html *cmd = (struct i2o_html*)arg;
	struct i2o_html kcmd;
	struct i2o_controller *c;
	u8 *res = NULL;
	void *query = NULL;
	int ret = 0;
	int token;
	u32 len;
	u32 reslen;
	u32 msg[MSG_FRAME_SIZE/4];

	if(copy_from_user(&kcmd, cmd, sizeof(struct i2o_html)))
	{
		printk(KERN_INFO "i2o_config: can't copy html cmd\n");
		return -EFAULT;
	}

	if(get_user(reslen, kcmd.reslen) < 0)
	{
		printk(KERN_INFO "i2o_config: can't copy html reslen\n");
		return -EFAULT;
	}

	if(!kcmd.resbuf)		
	{
		printk(KERN_INFO "i2o_config: NULL html buffer\n");
		return -EFAULT;
	}

	c = i2o_find_controller(kcmd.iop);
	if(!c)
		return -ENXIO;

	if(kcmd.qlen) /* Check for post data */
	{
		query = kmalloc(kcmd.qlen, GFP_KERNEL);
		if(!query)
		{
			i2o_unlock_controller(c);
			return -ENOMEM;
		}
		if(copy_from_user(query, kcmd.qbuf, kcmd.qlen))
		{
			i2o_unlock_controller(c);
			printk(KERN_INFO "i2o_config: could not get query\n");
			kfree(query);
			return -EFAULT;
		}
	}

	res = kmalloc(4096, GFP_KERNEL);
	if(!res)
	{
		i2o_unlock_controller(c);
		return -ENOMEM;
	}

	msg[1] = (I2O_CMD_UTIL_CONFIG_DIALOG << 24)|HOST_TID<<12|kcmd.tid;
	msg[2] = i2o_cfg_context;
	msg[3] = 0;
	msg[4] = kcmd.page;
	msg[5] = 0xD0000000|4096;
	msg[6] = virt_to_bus(res);
	if(!kcmd.qlen) /* Check for post data */
		msg[0] = SEVEN_WORD_MSG_SIZE|SGL_OFFSET_5;
	else
	{
		msg[0] = NINE_WORD_MSG_SIZE|SGL_OFFSET_5;
		msg[5] = 0x50000000|4096;
		msg[7] = 0xD4000000|(kcmd.qlen);
		msg[8] = virt_to_phys(query);
	}

	token = i2o_post_wait(c, cmd->tid, msg, 9*4, &i2o_cfg_token, 10);
	if(token == I2O_POST_WAIT_TIMEOUT)
	{
		i2o_unlock_controller(c);
		kfree(res);
		if(kcmd.qlen) kfree(query);

		return -ETIMEDOUT;
	}
	i2o_unlock_controller(c);

	len = strnlen(res, 8192);
	put_user(len, kcmd.reslen);
	if(len > reslen)
		ret = -ENOMEM;
	if(copy_to_user(kcmd.resbuf, res, len))
		ret = -EFAULT;

	kfree(res);
	if(kcmd.qlen) 
		kfree(query);

	return ret;
}

int ioctl_swdl(unsigned long arg)
{
	struct i2o_sw_xfer kxfer;
	struct i2o_sw_xfer *pxfer = (struct i2o_sw_xfer *)arg;
	unsigned char maxfrag = 0, curfrag = 0;
	unsigned char buffer[8192];
	u32 msg[MSG_FRAME_SIZE/4];
	unsigned int token = 0, diff = 0, swlen = 0, swxfer = 0;
	struct i2o_controller *c;
	int foo = 0;

	printk("*** foo%d ***\n", foo++);
	if(copy_from_user(&kxfer, pxfer, sizeof(struct i2o_sw_xfer)))
	{
		printk( "i2o_config: can't copy i2o_sw cmd @ %p\n", pxfer);
		return -EFAULT;
	}
	printk("*** foo%d ***\n", foo++);

	printk("Attempting to copy swlen from %p\n", kxfer.swlen);
	if(get_user(swlen, kxfer.swlen) < 0)
	{
		printk( "i2o_config: can't copy swlen\n");
		return -EFAULT;
	}
	printk("*** foo%d ***\n", foo++);

	maxfrag = swlen >> 13;	// Transfer in 8k fragments

	printk("Attempting to write maxfrag @ %p\n", kxfer.maxfrag);
	if(put_user(maxfrag, kxfer.maxfrag) < 0)
	{
		printk( "i2o_config: can't write maxfrag\n");
		return -EFAULT;
	}
	printk("*** foo%d ***\n", foo++);

	printk("Attempting to write curfrag @ %p\n", kxfer.curfrag);
	if(put_user(curfrag, kxfer.curfrag) < 0)
	{
		printk( "i2o_config: can't write curfrag\n");
		return -EFAULT;
	}
	printk("*** foo%d ***\n", foo++);

	if(!kxfer.buf)
	{
		printk( "i2o_config: NULL software buffer\n");
		return -EFAULT;
	}
	printk("*** foo%d ***\n", foo++);
	
	// access_ok doesn't check for NULL...
	if(!access_ok(VERIFY_READ, kxfer.buf, swlen))
	{
                printk( "i2o_config: Cannot read sw buffer\n");
                return -EFAULT;
	}
	printk("*** foo%d ***\n", foo++);

	c = i2o_find_controller(kxfer.iop);
	if(!c)
		return -ENXIO;
	printk("*** foo%d ***\n", foo++);

	msg[0]= EIGHT_WORD_MSG_SIZE| SGL_OFFSET_7;
	msg[1]= I2O_CMD_SW_DOWNLOAD<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2]= (u32)cfg_handler.context;
	msg[3]= 0;
	msg[4]= ((u32)kxfer.dl_flags)<<24|((u32)kxfer.sw_type)<<16|((u32)maxfrag)<<8|((u32)curfrag);
	msg[5]= swlen;
	msg[6]= kxfer.sw_id;
	msg[7]= (0xD0000000 | 8192);
	msg[8]= virt_to_phys(buffer);

	printk("*** foo%d ***\n", foo++);

	//
	// Loop through all fragments but last and transfer them...
	// We already checked memory, so from now we assume it's all good
	//
	for(curfrag = 0; curfrag < maxfrag-1; curfrag++)
	{
		printk("Transfering fragment %d\n", curfrag);

		msg[4] |= (u32)curfrag;

		__copy_from_user(buffer, kxfer.buf, 8192);
		swxfer += 8129;

		// Yes...that's one minute, but the spec states that
		// transfers take a long time, and I've seen just how
		// long they can take.
		token = i2o_post_wait(c, ADAPTER_TID, msg, 6*4, &i2o_cfg_token,60);
		if( token == I2O_POST_WAIT_TIMEOUT )	// Something very wrong
		{
			printk("Timeout downloading software");
			return -ETIMEDOUT;
		}

		__put_user(curfrag, kxfer.curfrag);
	}

	// Last frag is special case since it's not exactly 8K
	diff = swlen - swxfer;
	msg[4] |= (u32)maxfrag;
	msg[7] = (0xD0000000 | diff);
	__copy_from_user(buffer, kxfer.buf, 8192);
	token = i2o_post_wait(c, ADAPTER_TID, msg, 6*4, &i2o_cfg_token,60);
	if( token == I2O_POST_WAIT_TIMEOUT )	// Something very wrong
	{
		printk("Timeout downloading software");
		return -ETIMEDOUT;
	}
	__put_user(curfrag, kxfer.curfrag);

	return 0;
}

/* To be written */
int ioctl_swul(unsigned long arg)
{
	return -EINVAL;
}

/* To be written */
int ioctl_swdel(unsigned long arg)
{
	return 0;
}

static int cfg_open(struct inode *inode, struct file *file)
{
	/* 
         * Should support multiple management users
         */
	MOD_INC_USE_COUNT;
	return 0;
}

static int cfg_release(struct inode *inode, struct file *file)
{
	MOD_DEC_USE_COUNT;
	return 0;
}


static struct file_operations config_fops =
{
	cfg_llseek,
	cfg_read,
	cfg_write,
	NULL,
	NULL /*cfg_poll*/,
	cfg_ioctl,
	NULL,		/* No mmap */
	cfg_open,
	NULL,		/* No flush */
	cfg_release
};

static struct miscdevice i2o_miscdev = {
	I2O_MINOR,
	"i2octl",
	&config_fops
};	

#ifdef MODULE
int init_module(void)
#else
__init int i2o_config_init(void)
#endif
{
	printk(KERN_INFO "i2o configuration manager v 0.02\n");
	
	if((page_buf = kmalloc(4096, GFP_KERNEL))==NULL)
	{
		printk(KERN_ERR "i2o_config: no memory for page buffer.\n");
		return -ENOBUFS;
	}
	if(misc_register(&i2o_miscdev)==-1)
	{
		printk(KERN_ERR "i2o_config: can't register device.\n");
		kfree(page_buf);
		return -EBUSY;
	}
	/*
	 *	Install our handler
	 */
	if(i2o_install_handler(&cfg_handler)<0)
	{
		kfree(page_buf);
		printk(KERN_ERR "i2o_config: handler register failed.\n");
		misc_deregister(&i2o_miscdev);
		return -EBUSY;
	}
	/*
	 *	The low 16bits of the transaction context must match this
	 *	for everything we post. Otherwise someone else gets our mail
	 */
	i2o_cfg_context = cfg_handler.context;
	return 0;
}

#ifdef MODULE

void cleanup_module(void)
{
	misc_deregister(&i2o_miscdev);
	
	if(page_buf)
		kfree(page_buf);
	if(i2o_cfg_context != -1)
		i2o_remove_handler(&cfg_handler);
	if(i2o_buffer)
		kfree(i2o_buffer);
}
 
EXPORT_NO_SYMBOLS;
MODULE_AUTHOR("Red Hat Software");
MODULE_DESCRIPTION("I2O Configuration");

#endif
