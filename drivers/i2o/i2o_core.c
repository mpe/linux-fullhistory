/* 
 * Core I2O structure managment 
 * 
 * (C) Copyright 1999   Red Hat Software 
 *	
 * Written by Alan Cox, Building Number Three Ltd 
 * 
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License 
 * as published by the Free Software Foundation; either version 
 * 2 of the License, or (at your option) any later version.  
 * 
 * A lot of the I2O message side code from this is taken from the 
 * Red Creek RCPCI45 adapter driver by Red Creek Communications 
 *
 * Fixes by: 
 *		Philipp Rumpf 
 *		Juha Sievänen <Juha.Sievanen@cs.Helsinki.FI> 
 *		Auvo Häkkinen <Auvo.Hakkinen@cs.Helsinki.FI> 
 *		Deepak Saxena <deepak@plexity.net> 
 * 
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include <linux/i2o.h>

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/spinlock.h>

#include <linux/bitops.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/timer.h>

#include <asm/io.h>

#include "i2o_lan.h"

// #define DRIVERDEBUG
// #define DEBUG_IRQ

/*
 *	Size of the I2O module table
 */
 
static struct i2o_handler *i2o_handlers[MAX_I2O_MODULES];
static struct i2o_controller *i2o_controllers[MAX_I2O_CONTROLLERS];
int i2o_num_controllers = 0;
static int core_context = 0;
static int reply_flag = 0;

extern int i2o_online_controller(struct i2o_controller *c);
static int i2o_init_outbound_q(struct i2o_controller *c);
static void i2o_core_reply(struct i2o_handler *, struct i2o_controller *,
			   struct i2o_message *);
static int i2o_add_management_user(struct i2o_device *, struct i2o_handler *);
static int i2o_remove_management_user(struct i2o_device *, struct i2o_handler *);
static int i2o_quiesce_system(void);
static int i2o_enable_system(void);
static void i2o_dump_message(u32 *);

static int i2o_lct_get(struct i2o_controller *);
static int i2o_hrt_get(struct i2o_controller *);

static void i2o_sys_init(void);
static void i2o_sys_shutdown(void);

static int i2o_build_sys_table(void);
static int i2o_systab_send(struct i2o_controller *c);

/*
 * I2O System Table.  Contains information about
 * all the IOPs in the system.  Used to inform IOPs
 * about each other's existence.
 *
 * sys_tbl_ver is the CurrentChangeIndicator that is
 * used by IOPs to track changes.
 */
static struct i2o_sys_tbl *sys_tbl = NULL;
static int sys_tbl_ind = 0;
static int sys_tbl_len = 0;

#ifdef MODULE
/* 
 * Function table to send to bus specific layers
 * See <include/linux/i2o.h> for explanation of this
 */
static struct i2o_core_func_table i2o_core_functions =
{
	i2o_install_controller,
	i2o_activate_controller,
	i2o_find_controller,
	i2o_unlock_controller,
	i2o_run_queue,
	i2o_delete_controller
};

#ifdef CONFIG_I2O_PCI_MODULE
extern int i2o_pci_core_attach(struct i2o_core_func_table *);
extern void i2o_pci_core_detach(void);
#endif /* CONFIG_I2O_PCI_MODULE */

#endif /* MODULE */

/*
 * Structures and definitions for synchronous message posting.
 * See i2o_post_wait() for description.
 */ 
struct i2o_post_wait_data
{
	int status;
	u32 id;
	wait_queue_head_t *wq;
	struct i2o_post_wait_data *next;
};
static struct i2o_post_wait_data *post_wait_queue = NULL;
static u32 post_wait_id = 0;	// Unique ID for each post_wait
static spinlock_t post_wait_lock = SPIN_LOCK_UNLOCKED;
static void i2o_post_wait_complete(u32, int);

/* Message handler */ 
static struct i2o_handler i2o_core_handler =
{
	(void *)i2o_core_reply,
	"I2O core layer",
	0
};


/*
 *	I2O configuration spinlock. This isnt a big deal for contention
 *	so we have one only
 */
 
static spinlock_t i2o_configuration_lock = SPIN_LOCK_UNLOCKED;

/*
 * I2O Core reply handler
 *
 * Only messages this should see are i2o_post_wait() replies
 */
void i2o_core_reply(struct i2o_handler *h, struct i2o_controller *c,
		    struct i2o_message *m)
{
	u32 *msg=(u32 *)m;
	u32 status;
	u32 context = msg[3];

#if 0
	i2o_report_status(KERN_INFO, "i2o_core", msg);
#endif
	
	if (msg[0] & (1<<13)) // Fail bit is set
        {
                printk(KERN_ERR "IOP failed to process the msg:\n");
                printk(KERN_ERR "  Cmd = 0x%02X, InitiatorTid = %d, TargetTid =%d\n",
		       (msg[1] >> 24) & 0xFF, (msg[1] >> 12) & 0xFFF, msg[1] &
		       0xFFF);
                printk(KERN_ERR "  FailureCode = 0x%02X\n  Severity = 0x%02X\n"
		       "LowestVersion = 0x%02X\n  HighestVersion = 0x%02X\n",
		       msg[4] >> 24, (msg[4] >> 16) & 0xFF,
		       (msg[4] >> 8) & 0xFF, msg[4] & 0xFF);
                printk(KERN_ERR "  FailingHostUnit = 0x%04X\n  FailingIOP = 0x%03X\n",
		       msg[5] >> 16, msg[5] & 0xFFF);
                return;
        }

	if(msg[2]&0x80000000)	// Post wait message
	{
		if (msg[4] >> 24)
		{
			/* 0x40000000 is used as an error report supress bit */
			if((msg[2]&0x40000000)==0)
				i2o_report_status(KERN_WARNING, "i2o_core: post_wait reply", msg);
			status = -(msg[4] & 0xFFFF);
		}
		else
			status = I2O_POST_WAIT_OK;
	
		i2o_post_wait_complete(context, status);
	}
}

/*
 *	Install an I2O handler - these handle the asynchronous messaging
 *	from the card once it has initialised.
 */
 
int i2o_install_handler(struct i2o_handler *h)
{
	int i;
	spin_lock(&i2o_configuration_lock);
	for(i=0;i<MAX_I2O_MODULES;i++)
	{
		if(i2o_handlers[i]==NULL)
		{
			h->context = i;
			i2o_handlers[i]=h;
			spin_unlock(&i2o_configuration_lock);
			return 0;
		}
	}
	spin_unlock(&i2o_configuration_lock);
	return -ENOSPC;
}

int i2o_remove_handler(struct i2o_handler *h)
{
	i2o_handlers[h->context]=NULL;
	return 0;
}
	

/*
 *	Each I2O controller has a chain of devices on it - these match
 *	the useful parts of the LCT of the board.
 */
 
int i2o_install_device(struct i2o_controller *c, struct i2o_device *d)
{
	int i;

	spin_lock(&i2o_configuration_lock);
	d->controller=c;
	d->owner=NULL;
	d->next=c->devices;
	c->devices=d;
	*d->dev_name = 0;
	d->owner = NULL;

	for(i = 0; i < I2O_MAX_MANAGERS; i++)
		d->managers[i] = NULL;

	spin_unlock(&i2o_configuration_lock);
	return 0;
}

/* we need this version to call out of i2o_delete_controller */

int __i2o_delete_device(struct i2o_device *d)
{
	struct i2o_device **p;

	p=&(d->controller->devices);

	/*
	 *	Hey we have a driver!
	 */
	 
	if(d->owner)
		return -EBUSY;

	/*
	 *	Seek, locate
	 */
	 			
	while(*p!=NULL)
	{
		if(*p==d)
		{
			/*
			 *	Destroy
			 */
			*p=d->next;
			kfree(d);
			return 0;
		}
		p=&((*p)->next);
	}
	printk(KERN_ERR "i2o_delete_device: passed invalid device.\n");
	return -EINVAL;
}

int i2o_delete_device(struct i2o_device *d)
{
	int ret;

	spin_lock(&i2o_configuration_lock);

	ret = __i2o_delete_device(d);

	spin_unlock(&i2o_configuration_lock);

	return ret;
}

/*
 *	Add and remove controllers from the I2O controller list
 */
 
int i2o_install_controller(struct i2o_controller *c)
{
	int i;
	spin_lock(&i2o_configuration_lock);
	for(i=0;i<MAX_I2O_CONTROLLERS;i++)
	{
		if(i2o_controllers[i]==NULL)
		{
			i2o_controllers[i]=c;
			c->next=i2o_controller_chain;
			i2o_controller_chain=c;
			c->unit = i;
			c->page_frame = NULL;
			c->hrt = NULL;
			c->lct = NULL;
			c->status_block = NULL;
//			printk(KERN_INFO "lct @ %p hrt @ %p status @ %p",
//					c->lct, c->hrt, c->status_block);
			sprintf(c->name, "i2o/iop%d", i);
			i2o_num_controllers++;
			spin_unlock(&i2o_configuration_lock);
			return 0;
		}
	}
	printk(KERN_ERR "No free i2o controller slots.\n");
	spin_unlock(&i2o_configuration_lock);
	return -EBUSY;
}

int i2o_delete_controller(struct i2o_controller *c)
{
	struct i2o_controller **p;
	int users;

#ifdef DRIVERDEBUG
	printk(KERN_INFO "Deleting controller iop%d\n", c->unit);
#endif

	spin_lock(&i2o_configuration_lock);
	if((users=atomic_read(&c->users)))
	{
		printk(KERN_INFO "I2O: %d users for controller iop%d\n", users, c->unit);
		spin_unlock(&i2o_configuration_lock);
		return -EBUSY;
	}
	while(c->devices)
	{
		if(__i2o_delete_device(c->devices)<0)
		{
			/* Shouldnt happen */
			spin_unlock(&i2o_configuration_lock);
			return -EBUSY;
		}
	}

	p=&i2o_controller_chain;

	while(*p)
	{
		if(*p==c)
		{
			/* Ask the IOP to switch to HOLD state */
			if (i2o_clear_controller(c) < 0)
				printk("Unable to clear iop%d\n", c->unit);

			/* Release IRQ */
			c->destructor(c);

			*p=c->next;
			spin_unlock(&i2o_configuration_lock);

//			printk(KERN_INFO "hrt %p lct %p page_frame %p status_block %p\n",
//				c->hrt, c->lct, c->page_frame, c->status_block);
			if(c->page_frame)
				kfree(c->page_frame);
			if(c->hrt)
				kfree(c->hrt);
			if(c->lct)
				kfree(c->lct);
			if(c->status_block)
				kfree(c->status_block);

			kfree(c);

			i2o_controllers[c->unit]=NULL;

			i2o_num_controllers--;
#ifdef DRIVERDEBUG
			printk(KERN_INFO "iop deleted\n");
#endif
			return 0;
		}
		p=&((*p)->next);
	}
	spin_unlock(&i2o_configuration_lock);
	printk(KERN_ERR "i2o_delete_controller: bad pointer!\n");
	return -ENOENT;
}

void i2o_unlock_controller(struct i2o_controller *c)
{
	atomic_dec(&c->users);
}

struct i2o_controller *i2o_find_controller(int n)
{
	struct i2o_controller *c;
	
	if(n<0 || n>=MAX_I2O_CONTROLLERS)
		return NULL;
	
	spin_lock(&i2o_configuration_lock);
	c=i2o_controllers[n];
	if(c!=NULL)
		atomic_inc(&c->users);
	spin_unlock(&i2o_configuration_lock);
	return c;
}
	

/*
 * Claim a device for use as either the primary user or just
 * as a management/secondary user
 */
int i2o_claim_device(struct i2o_device *d, struct i2o_handler *h, u32 type)
{
	/* Device already has a primary user or too many managers */
	if((type == I2O_CLAIM_PRIMARY && d->owner) ||
		(d->num_managers == I2O_MAX_MANAGERS))
	{
			return -EBUSY;
	}

	if(i2o_issue_claim(d->controller,d->lct_data->tid, h->context, 1, &reply_flag, type))
	{
		return -EBUSY;
	}

	spin_lock(&i2o_configuration_lock);
	if(d->owner)
	{
		spin_unlock(&i2o_configuration_lock);
		return -EBUSY;
	}
	atomic_inc(&d->controller->users);

	if(type == I2O_CLAIM_PRIMARY)
		d->owner=h;
	else
		i2o_add_management_user(d, h);

	spin_unlock(&i2o_configuration_lock);
	return 0;
}

int i2o_release_device(struct i2o_device *d, struct i2o_handler *h, u32 type)
{
	int err = 0;

	spin_lock(&i2o_configuration_lock);

	/* Primary user */
	if(type == I2O_CLAIM_PRIMARY)
	{
		if(d->owner != h)
			err = -ENOENT;
		else
		{
			if(i2o_issue_claim(d->controller, d->lct_data->tid, h->context, 0,
					   &reply_flag, type))
			{
				err = -ENXIO;
			}
			else
			{
				d->owner = NULL;
				atomic_dec(&d->controller->users);
			}
		}

		spin_unlock(&i2o_configuration_lock);
		return err;
	}

	/* Management or other user */
	if(i2o_remove_management_user(d, h))
		err = -ENOENT;
	else
	{
		atomic_dec(&d->controller->users);

		if(i2o_issue_claim(d->controller,d->lct_data->tid, h->context, 0, 
				   &reply_flag, type))
			err = -ENXIO;
	}

	spin_unlock(&i2o_configuration_lock);
	return err;
}

int i2o_add_management_user(struct i2o_device *d, struct i2o_handler *h)
{
	int i;

	if(d->num_managers == I2O_MAX_MANAGERS)
		return 1;

	for(i = 0; i < I2O_MAX_MANAGERS; i++)
		if(!d->managers[i])
			d->managers[i] = h;
	
	d->num_managers++;
	
	return 0;
}

int i2o_remove_management_user(struct i2o_device *d, struct i2o_handler *h)
{
	int i;

	for(i=0; i < I2O_MAX_MANAGERS; i++)
	{
		if(d->managers[i] == h)
		{
			d->managers[i] = NULL;
			return 0;
		}
	}

	return -ENOENT;
}

/*
 *	This is called by the bus specific driver layer when an interrupt
 *	or poll of this card interface is desired.
 */
 
void i2o_run_queue(struct i2o_controller *c)
{
	struct i2o_message *m;
	u32 mv;

#ifdef DEBUG_IRQ
	printk(KERN_INFO "iop%d interrupt\n", c->unit);
#endif

	while((mv=I2O_REPLY_READ32(c))!=0xFFFFFFFF)
	{
		struct i2o_handler *i;
		m=(struct i2o_message *)bus_to_virt(mv);
		/*
		 *	Temporary Debugging
		 */
		if(((m->function_addr>>24)&0xFF)==0x15)
			printk("UTFR!\n");

#ifdef DEBUG_IRQ
		i2o_dump_message((u32*)m);
#endif

		i=i2o_handlers[m->initiator_context&(MAX_I2O_MODULES-1)];
		if(i)
			i->reply(i,c,m);
		else
		{
			printk("Spurious reply to handler %d\n", 
				m->initiator_context&(MAX_I2O_MODULES-1));
			i2o_dump_message((u32*)m);
		}	
	 	i2o_flush_reply(c,mv);
		mb();
	}
}


/*
 *	Do i2o class name lookup
 */
const char *i2o_get_class_name(int class)
{
	int idx = 16;
	static char *i2o_class_name[] = {
		"Executive",
		"Device Driver Module",
		"Block Device",
		"Tape Device",
		"LAN Interface",
		"WAN Interface",
		"Fibre Channel Port",
		"Fibre Channel Device",
		"SCSI Device",
		"ATE Port",
		"ATE Device",
		"Floppy Controller",
		"Floppy Device",
		"Secondary Bus Port",
		"Peer Transport Agent",
		"Peer Transport",
		"Unknown"
	};
	
	switch(class&0xFFF)
	{
		case I2O_CLASS_EXECUTIVE:
			idx = 0; break;
		case I2O_CLASS_DDM:
			idx = 1; break;
		case I2O_CLASS_RANDOM_BLOCK_STORAGE:
			idx = 2; break;
		case I2O_CLASS_SEQUENTIAL_STORAGE:
			idx = 3; break;
		case I2O_CLASS_LAN:
			idx = 4; break;
		case I2O_CLASS_WAN:
			idx = 5; break;
		case I2O_CLASS_FIBRE_CHANNEL_PORT:
			idx = 6; break;
		case I2O_CLASS_FIBRE_CHANNEL_PERIPHERAL:
			idx = 7; break;
		case I2O_CLASS_SCSI_PERIPHERAL:
			idx = 8; break;
		case I2O_CLASS_ATE_PORT:
			idx = 9; break;
		case I2O_CLASS_ATE_PERIPHERAL:
			idx = 10; break;
		case I2O_CLASS_FLOPPY_CONTROLLER:
			idx = 11; break;
		case I2O_CLASS_FLOPPY_DEVICE:
			idx = 12; break;
		case I2O_CLASS_BUS_ADAPTER_PORT:
			idx = 13; break;
		case I2O_CLASS_PEER_TRANSPORT_AGENT:
			idx = 14; break;
		case I2O_CLASS_PEER_TRANSPORT:
			idx = 15; break;
	}

	return i2o_class_name[idx];
}


/*
 *	Wait up to 5 seconds for a message slot to be available.
 */
 
u32 i2o_wait_message(struct i2o_controller *c, char *why)
{
	long time=jiffies;
	u32 m;
	while((m=I2O_POST_READ32(c))==0xFFFFFFFF)
	{
		if((jiffies-time)>=5*HZ)
		{
#ifdef DRIVERDEBUG
			printk(KERN_ERR "%s: Timeout waiting for message frame to send %s.\n", 
				c->name, why);
#endif
			return 0xFFFFFFFF;
		}
		schedule();
		barrier();
	}
	return m;
}


/*
 *	Wait up to timeout seconds for a reply to be available.
 */
 
u32 i2o_wait_reply(struct i2o_controller *c, char *why, int timeout)
{
	u32 m;
	long time=jiffies;
	
	while((m=I2O_REPLY_READ32(c))==0xFFFFFFFF)
	{
		if(jiffies-time >= timeout*HZ )
		{
#ifdef DRIVERDEBUG
			printk(KERN_ERR "%s: timeout waiting for %s reply.\n",
				c->name, why);
#endif
			return 0xFFFFFFFF;
		}
		schedule();
	}
	return m;
}
	

static int i2o_query_scalar_polled(struct i2o_controller *c, int tid, void *buf, int buflen, 
	int group, int field)
{
	u32 m;
	u32 *msg;
	u16 op[8];
	u32 *p;
	int i;
	u32 *rbuf;

	op[0]=1;			/* One Operation */
	op[1]=0;			/* PAD */
	op[2]=1;			/* FIELD_GET */
	op[3]=group;			/* group number */
	op[4]=1;			/* 1 field */
	op[5]=field;			/* Field number */

	m=i2o_wait_message(c, "ParamsGet");
	if(m==0xFFFFFFFF)
	{	
		return -ETIMEDOUT;
	}
	
	msg=(u32 *)(c->mem_offset+m);
	
	rbuf=kmalloc(buflen+32, GFP_KERNEL);
	if(rbuf==NULL)
	{
		printk(KERN_ERR "No free memory for scalar read.\n");
		return -ENOMEM;
	}

	__raw_writel(NINE_WORD_MSG_SIZE|SGL_OFFSET_5, &msg[0]);
	__raw_writel(I2O_CMD_UTIL_PARAMS_GET<<24|HOST_TID<<12|tid, &msg[1]);
	__raw_writel(0, &msg[2]);			/* Context */
	__raw_writel(0, &msg[3]);
	__raw_writel(0, &msg[4]);
	__raw_writel(0x54000000|12, &msg[5]);
	__raw_writel(virt_to_bus(op), &msg[6]);
	__raw_writel(0xD0000000|(32+buflen), &msg[7]);
	__raw_writel(virt_to_bus(rbuf), &msg[8]);

	i2o_post_message(c,m);
	barrier();
	
	/*
	 *	Now wait for a reply
	 */
	 
	m=i2o_wait_reply(c, "ParamsGet", 5);
	
	if(m==0xFFFFFFFF)
	{
		kfree(rbuf);
		return -ETIMEDOUT;
	}

	msg = (u32 *)bus_to_virt(m);
	if(msg[4]>>24)
	{
		i2o_report_status(KERN_WARNING, "i2o_core", msg);
	}
	
	p=rbuf;

	/* Ok 'p' is the reply block - lets see what happened */
	/* p0->p2 are the header */
	
	/* FIXME: endians - turn p3 to little endian */
	
	if((p[0]&0xFFFF)!=1)	
		printk(KERN_WARNING "Suspicious field read return 0x%08X\n", p[0]);
		
	i=(p[1]&0xFFFF)<<2;		/* Message size */
	if(i<buflen)
		buflen=i;
	
	/* Do we have an error block ? */
	if(p[1]&0xFF000000)
	{
#ifdef DRIVERDEBUG
		printk(KERN_ERR "%s: error in field read.\n",
			c->name);
#endif
		kfree(rbuf);
		return -EBADR;
	}
		
	/* p[1] holds the more flag and row count - we dont care */
	
	/* Ok it worked p[2]-> hold the data */
	memcpy(buf,  p+2, buflen);
	
	kfree(rbuf);
	
	/* Finally return the message */
	I2O_REPLY_WRITE32(c,m);
	return buflen;
}

/*
 *	 Dump the information block associated with a given unit (TID)
 */
 
void i2o_report_controller_unit(struct i2o_controller *c, int unit)
{
	char buf[64];
	
	if(i2o_query_scalar(c, unit, 0xF100, 3, buf, 16)>=0)
	{
		buf[16]=0;
		printk(KERN_INFO "     Vendor: %s", buf);
	}
	if(i2o_query_scalar(c, unit, 0xF100, 4, buf, 16)>=0)
	{

		buf[16]=0;
		printk("     Device: %s", buf);
	}
#if 0
	if(i2o_query_scalar(c, unit, 0xF100, 5, buf, 16)>=0)
	{
		buf[16]=0;
		printk(KERN_INFO "Description: %s\n", buf);
	}
#endif	
	if(i2o_query_scalar(c, unit, 0xF100, 6, buf, 8)>=0)
	{
		buf[8]=0;
		printk("        Rev: %s\n", buf);
	}
}


/*
 *	Parse the hardware resource table. Right now we print it out
 *	and don't do a lot with it. We should collate these and then
 *	interact with the Linux resource allocation block.
 *
 *	Lets prove we can read it first eh ?
 *
 *	This is full of endianisms!
 */
 
static int i2o_parse_hrt(struct i2o_controller *c)
{
#ifdef DRIVERDEBUG
	u32 *rows=(u32*)c->hrt;
	u8 *p=(u8 *)c->hrt;
	u8 *d;
	int count;
	int length;
	int i;
	int state;
	
	if(p[3]!=0)
	{
		printk(KERN_ERR "i2o: HRT table for controller is too new a version.\n");
		return -1;
	}
		
	count=p[0]|(p[1]<<8);
	length = p[2];
	
	printk(KERN_INFO "iop%d: HRT has %d entries of %d bytes each.\n",
		c->unit, count, length<<2);

	rows+=2;
	
	for(i=0;i<count;i++)
	{
		printk(KERN_INFO "Adapter %08X: ", rows[0]);
		p=(u8 *)(rows+1);
		d=(u8 *)(rows+2);
		state=p[1]<<8|p[0];
		
		printk("TID %04X:[", state&0xFFF);
		state>>=12;
		if(state&(1<<0))
			printk("H");		/* Hidden */
		if(state&(1<<2))
		{
			printk("P");		/* Present */
			if(state&(1<<1))
				printk("C");	/* Controlled */
		}
		if(state>9)
			printk("*");		/* Hard */
		
		printk("]:");
		
		switch(p[3]&0xFFFF)
		{
			case 0:
				/* Adapter private bus - easy */
				printk("Local bus %d: I/O at 0x%04X Mem 0x%08X", 
					p[2], d[1]<<8|d[0], *(u32 *)(d+4));
				break;
			case 1:
				/* ISA bus */
				printk("ISA %d: CSN %d I/O at 0x%04X Mem 0x%08X",
					p[2], d[2], d[1]<<8|d[0], *(u32 *)(d+4));
				break;
					
			case 2: /* EISA bus */
				printk("EISA %d: Slot %d I/O at 0x%04X Mem 0x%08X",
					p[2], d[3], d[1]<<8|d[0], *(u32 *)(d+4));
				break;

			case 3: /* MCA bus */
				printk("MCA %d: Slot %d I/O at 0x%04X Mem 0x%08X",
					p[2], d[3], d[1]<<8|d[0], *(u32 *)(d+4));
				break;

			case 4: /* PCI bus */
				printk("PCI %d: Bus %d Device %d Function %d",
					p[2], d[2], d[1], d[0]);
				break;

			case 0x80: /* Other */
			default:
				printk("Unsupported bus type.");
				break;
		}
		printk("\n");
		rows+=length;
	}
#endif
	return 0;
}
	
/*
 *	The logical configuration table tells us what we can talk to
 *	on the board. Most of the stuff isn't interesting to us. 
 */

static int i2o_parse_lct(struct i2o_controller *c)
{
	int i;
	int max;
	int tid;
	u32 *p;
	struct i2o_device *d;
	char str[22];
	i2o_lct *lct = c->lct;

	max = lct->table_size;
	
	max -= 3;
	max /= 9;

	if(max==0)
	{
		printk(KERN_ERR "LCT is empty????\n");
		return -1;
	}
	
	printk(KERN_INFO "LCT has %d entries.\n", max);
	
	if(max > 128)
	{
		printk(KERN_INFO "LCT was truncated.\n");
		max=128;
	}
	
	if(lct->iop_flags&(1<<0))
		printk(KERN_WARNING "I2O: Configuration dialog desired by iop%d.\n", c->unit);
		
	for(i=0;i<max;i++)
	{
		d = (struct i2o_device *)kmalloc(sizeof(struct i2o_device), GFP_KERNEL);
		if(d==NULL)
		{
			printk("i2o_core: Out of memory for I2O device data.\n");
			return -ENOMEM;
		}
		
		d->controller = c;
		d->next = NULL;

		d->lct_data = &lct->lct_entry[i];

		d->flags = 0;
		tid = d->lct_data->tid;
		
		printk(KERN_INFO "Task ID %d.\n", tid);

		i2o_report_controller_unit(c, tid);
		
		i2o_install_device(c, d);
		
		printk(KERN_INFO "     Class: ");
		
		sprintf(str, "%-21s", i2o_get_class_name(d->lct_data->class_id));
		printk("%s", str);
		
		printk(" Subclass: 0x%04X                Flags: ",
			d->lct_data->sub_class);
			
		if(d->lct_data->device_flags&(1<<0))
			printk("C");		// ConfigDialog requested
		if(d->lct_data->device_flags&(1<<1))
			printk("M");		// Multi-user capable
		if(!(d->lct_data->device_flags&(1<<4)))
			printk("P");		// Peer service enabled!
		if(!(d->lct_data->device_flags&(1<<5)))
			printk("m");		// Mgmt service enabled!
		printk("\n");
		p+=9;
	}
	return 0;
}


/* Quiesce IOP */
int i2o_quiesce_controller(struct i2o_controller *c)
{
	u32 msg[4];

	if(c->status_block->iop_state != ADAPTER_STATE_OPERATIONAL)
		return -EINVAL;

	msg[0]=FOUR_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1]=I2O_CMD_SYS_QUIESCE<<24|HOST_TID<<12|ADAPTER_TID;
	msg[2]=(u32)core_context;
	msg[3]=(u32)&reply_flag;

	/* Long timeout needed for quiesce if lots of devices */
#ifdef DRIVERDEBUG
	printk(KERN_INFO "Posting quiesce message to iop%d\n", c->unit);
#endif
	if(i2o_post_wait(c, msg, sizeof(msg), 120))
		return -1;
	else
		return 0;
}

/* Enable IOP */
int i2o_enable_controller(struct i2o_controller *c)
{
	u32 msg[4];

	msg[0]=FOUR_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1]=I2O_CMD_SYS_ENABLE<<24|HOST_TID<<12|ADAPTER_TID;
	msg[2]=core_context;
	msg[3]=(u32)&reply_flag;

	/* How long of a timeout do we need? */
	return i2o_post_wait(c, msg, sizeof(msg), 240);
}

/*
 * Quiesce _all_ IOPs in OP state.  
 * Used during init/shutdown time.
 */
int i2o_quiesce_system(void)
{
	struct i2o_controller *iop;
	int ret = 0;

	for(iop=i2o_controller_chain; iop != NULL; iop=iop->next)
	{
		/* 
		 * Quiesce only needed on operational IOPs
		 */
		i2o_status_get(iop);

		if(iop->status_block->iop_state == ADAPTER_STATE_OPERATIONAL)
		{
#ifdef DRIVERDEBUG
			printk(KERN_INFO "Attempting to quiesce iop%d\n", iop->unit);
#endif
			if(i2o_quiesce_controller(iop))
			{
				printk(KERN_INFO "Unable to quiesce iop%d\n", iop->unit);
				ret = -ENXIO;
			}
#ifdef DRIVERDEBUG
			else
				printk(KERN_INFO "%s quiesced\n", iop->name);
#endif

			i2o_status_get(iop);	// Update IOP state information
		}
	}
	
	return ret;
}

/*
 * (re)Enable _all_ IOPs in READY state.
 */
int i2o_enable_system(void)
{
	struct i2o_controller *iop;
	int ret = 0;
		
	for(iop=i2o_controller_chain; iop != NULL; iop=iop->next)
	{
		/*
		 * Enable only valid for IOPs in READY state
		 */
		i2o_status_get(iop);

		if(iop->status_block->iop_state == ADAPTER_STATE_READY)
		{
			if(i2o_enable_controller(iop)<0)
				printk(KERN_INFO "Unable to (re)enable iop%d\n",
				       iop->unit);
			
			i2o_status_get(iop);	// Update IOP state information
		}
	}

	return ret;
}


/* Reset an IOP, but keep message queues alive */
int i2o_clear_controller(struct i2o_controller *c)
{
	u32 msg[4];
	int ret;

#ifdef DRIVERDEBUG
	printk(KERN_INFO "Clearing iop%d\n", c->unit);
#endif

	/* Then clear the IOP */
	msg[0]=FOUR_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1]=I2O_CMD_ADAPTER_CLEAR<<24|HOST_TID<<12|ADAPTER_TID;
	msg[2]=core_context;
	msg[3]=(u32)&reply_flag;

	if((ret=i2o_post_wait(c, msg, sizeof(msg), 30)))
		printk(KERN_INFO "ExecIopClear failed: %#10x\n", ret);
#ifdef DRIVERDEBUG
	else
		printk(KERN_INFO "ExecIopClear success!\n");
#endif

	return ret;
}


/* Reset the IOP to sane state */
static int i2o_reset_controller(struct i2o_controller *c)
{
	u32 m;
	u8 *work8;
	u32 *msg;
	long time;

#ifdef DRIVERDEBUG
	printk(KERN_INFO "Reseting iop%d\n", c->unit);
#endif

	/* Get a message */
	m=i2o_wait_message(c, "AdapterReset");
	if(m==0xFFFFFFFF)	
		return -ETIMEDOUT;
	msg=(u32 *)(c->mem_offset+m);
	
	work8=(void *)kmalloc(4, GFP_KERNEL);
	if(work8==NULL) {
		printk(KERN_ERR "IOP reset failed - no free memory.\n");
		return -ENOMEM;
	}
	
	memset(work8, 0, 4);
	
	msg[0]=EIGHT_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1]=I2O_CMD_ADAPTER_RESET<<24|HOST_TID<<12|ADAPTER_TID;
	msg[2]=core_context;
	msg[3]=(u32)&reply_flag;
	msg[4]=0;
	msg[5]=0;
	msg[6]=virt_to_phys(work8);
	msg[7]=0;	/* 64bit host FIXME */

	/* Then reset the IOP */	
	i2o_post_message(c,m);

	/* Wait for a reply */
	time=jiffies;
	
	/* DPT driver claims they need this */
	mdelay(5);

#ifdef DRIVERDEBUG
	printk(KERN_INFO "Reset posted, waiting...\n");
#endif
	while(work8[0]==0)
	{
		if((jiffies-time)>=5*HZ)
		{
			printk(KERN_ERR "IOP reset timeout.\n");
			kfree(work8);
			return -ETIMEDOUT;
		}
		schedule();
		barrier();
	}

	if (work8[0]==0x02) 
	{ 
		printk(KERN_WARNING "IOP Reset rejected\n"); 
	} 
	else 
	{
		/* 
		 * Once the reset is sent, the IOP goes into the INIT state 
		 * which is indeterminate.  We need to wait until the IOP 
		 * has rebooted before we can let the system talk to 
		 * it. We read the inbound Free_List until a message is 
		 * available.  If we can't read one in the given ammount of 
		 * time, we assume the IOP could not reboot properly.  
		 */ 
#ifdef DRIVERDEBUG 
		printk(KERN_INFO "Reset succeeded...waiting for reboot\n"); 
#endif 
		time = jiffies; 
		m = I2O_POST_READ32(c); 
		while(m == 0XFFFFFFFF) 
		{ 
			if((jiffies-time) >= 30*HZ)
			{
				printk(KERN_ERR "i2o/iop%d: Timeout waiting for IOP reset.\n", 
							c->unit); 
				return -ETIMEDOUT; 
			} 
			schedule(); 
			barrier(); 
			m = I2O_POST_READ32(c); 
		} 
#ifdef DRIVERDEBUG 
		printk(KERN_INFO "Reboot completed\n"); 
#endif 
	}

	return 0;
}


int i2o_status_get(struct i2o_controller *c)
{
	long time;
	u32 m;
	u32 *msg;
	u8 *status_block;
	int i;

#ifdef DRIVERDEBUG
	printk(KERN_INFO "Getting status block for iop%d\n", c->unit);
#endif
	if(c->status_block)
		kfree(c->status_block);

	c->status_block = 
		(i2o_status_block *)kmalloc(sizeof(i2o_status_block),GFP_KERNEL);
	if(c->status_block == NULL)
	{
#ifdef DRIVERDEBUG
		printk(KERN_ERR "No memory in status get!\n");
#endif
		return -ENOMEM;
	}

	status_block = (u8*)c->status_block;
	
	m=i2o_wait_message(c, "StatusGet");
	if(m==0xFFFFFFFF)
		return -ETIMEDOUT;
	
	memset(status_block, 0, sizeof(i2o_status_block));

	msg=(u32 *)(c->mem_offset+m);
		__raw_writel(NINE_WORD_MSG_SIZE|SGL_OFFSET_0, &msg[0]);
	__raw_writel(I2O_CMD_STATUS_GET<<24|HOST_TID<<12|ADAPTER_TID, &msg[1]);
	__raw_writel(0, &msg[2]);
 	__raw_writel(0, &msg[3]);
 	__raw_writel(0, &msg[4]);
 	__raw_writel(0, &msg[5]);
	__raw_writel(virt_to_bus(c->status_block), &msg[6]);
	__raw_writel(0, &msg[7]);	/* 64bit host FIXME */
	__raw_writel(sizeof(i2o_status_block), &msg[8]);
	
	i2o_post_message(c,m);
	
	/* DPT work around */
	mdelay(5);

	/* Wait for a reply */
	time=jiffies;
	
	while((jiffies-time)<=HZ)
	{
		if(status_block[87]!=0)
		{
			/* Ok the reply has arrived. Fill in the important stuff */
			c->inbound_size = (status_block[12]|(status_block[13]<<8))*4;
			return 0;
		}
		schedule();
		barrier();
	}
#ifdef DRIVERDEBUG
	printk(KERN_ERR "IOP get status timeout.\n");
#endif
	return -ETIMEDOUT;
}


int i2o_hrt_get(struct i2o_controller *c)
{
	u32 msg[6];

	if(c->hrt)
		kfree(c->hrt);

	c->hrt=kmalloc(2048, GFP_KERNEL);
	if(c->hrt==NULL)
	{
		printk(KERN_ERR "IOP init failed; no memory.\n");
		return -ENOMEM;
	}

	msg[0]= SIX_WORD_MSG_SIZE| SGL_OFFSET_4;
	msg[1]= I2O_CMD_HRT_GET<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2]= core_context;
	msg[3]= 0x0;				/* Transaction context */
	msg[4]= (0xD0000000 | 2048);		/* Simple transaction , 2K */
	msg[5]= virt_to_phys(c->hrt);		/* Dump it here */

	return i2o_post_wait(c, msg, sizeof(msg), 20);
}

static int i2o_systab_send(struct i2o_controller* iop)
{
	u32 msg[10];
	u32 privmem[2];
	u32 privio[2];
	int ret;

	privmem[0]=iop->priv_mem;	/* Private memory space base address */
	privmem[1]=iop->priv_mem_size;
	privio[0]=iop->priv_io;		/* Private I/O address */
	privio[1]=iop->priv_io_size;

	msg[0] = NINE_WORD_MSG_SIZE|SGL_OFFSET_6;
	msg[1] = I2O_CMD_SYS_TAB_SET<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2] = 0;	/* Context not needed */
	msg[3] = 0;
	msg[4] = (0<<16)|((iop->unit+2)<<12);	/* Host 0 IOP ID (unit + 2) */
	msg[5] = 0;				/* Segment 0 */
	
	/*
	 *	Scatter Gather List
	 */
	msg[6] = 0x54000000|sys_tbl_len;
	msg[7] = virt_to_phys(sys_tbl);
	msg[8] = 0xD4000000|48;	/* One table for now */
	msg[9] = virt_to_phys(privmem);
/*	msg[10] = virt_to_phys(privio); */

	ret=i2o_post_wait(iop, msg, sizeof(msg), 120);
	if(ret)
		return ret;

	return 0;
}

/*
 * Initialize I2O subsystem.
 */
static void __init i2o_sys_init()
{
	struct i2o_controller *iop, *niop;
	int ret;
	u32 m;

	printk(KERN_INFO "Activating I2O controllers\n");
	printk(KERN_INFO "This may take a few minutes if there are many devices\n");

	/* Get the status for each IOP */
	for(iop = i2o_controller_chain; iop; iop = niop)
	{
		niop = iop->next;
#ifdef DRIVERDEBUG
		printk(KERN_INFO "Getting initial status for iop%d\n", iop->unit);
#endif
		if(i2o_status_get(iop)<0)
		{
			printk("Unable to obtain status of IOP, attempting a reset.\n");
			i2o_reset_controller(iop);
			if(i2o_status_get(iop)<0)
			{
				printk("IOP not responding.\n");
				i2o_delete_controller(iop);
				continue;
			}
		}

		if(iop->status_block->iop_state == ADAPTER_STATE_FAULTED)
		{
			printk(KERN_CRIT "i2o: iop%d has hardware fault\n",
					iop->unit);
			i2o_delete_controller(iop);
			continue;
		}

		if(iop->status_block->iop_state == ADAPTER_STATE_HOLD ||
	   	iop->status_block->iop_state == ADAPTER_STATE_READY ||
	   	iop->status_block->iop_state == ADAPTER_STATE_OPERATIONAL ||
	   	iop->status_block->iop_state == ADAPTER_STATE_FAILED)
		{
			int msg[256];

#ifdef DRIVERDEBUG
			printk(KERN_INFO "iop%d already running...trying to reboot\n",
					iop->unit);
#endif
			i2o_init_outbound_q(iop);
			I2O_REPLY_WRITE32(iop,virt_to_phys(msg));
			i2o_quiesce_controller(iop);
			i2o_reset_controller(iop);
			if(i2o_status_get(iop) || 
				iop->status_block->iop_state != ADAPTER_STATE_RESET)
			{
				printk(KERN_CRIT "Failed to initialize iop%d\n", iop->unit);
				i2o_delete_controller(iop);
				continue;
			}
		}
	}

	/*
	 * Now init the outbound queue for each one.
	 */
	for(iop = i2o_controller_chain; iop; iop = niop)
	{
		int i;
		
		niop = iop->next;

		if((ret=i2o_init_outbound_q(iop)))
		{
			printk(KERN_ERR 
				"IOP%d initialization failed: Could not initialize outbound q\n",
				iop->unit);
			i2o_delete_controller(iop);
			continue;
		}
		iop->page_frame = kmalloc(MSG_POOL_SIZE, GFP_KERNEL);

		if(iop->page_frame==NULL)
		{
			printk(KERN_CRIT "iop%d init failed: no memory for message page.\n", 
						iop->unit);
			i2o_delete_controller(iop);
			continue;
		}
	
		m=virt_to_phys(iop->page_frame);
	
		for(i=0; i< NMBR_MSG_FRAMES; i++)
		{
			I2O_REPLY_WRITE32(iop,m);
			mb();
			m+=MSG_FRAME_SIZE;
			mb();
		}
	}

	/*
	 * OK..parse the HRT
	 */
	for(iop = i2o_controller_chain; iop; iop = niop)
	{
		niop = iop->next;
		if(i2o_hrt_get(iop))
		{
			printk(KERN_CRIT "iop%d: Could not get HRT!\n", iop->unit);
			i2o_delete_controller(iop);
			continue;
		}
		if(i2o_parse_hrt(iop))
		{
			printk(KERN_CRIT "iop%d: Could not parse HRT!\n", iop->unit);
			i2o_delete_controller(iop);
			continue;
		}
	}

	/*
	 * Build and send the system table
	 *
	 * If build_sys_table fails, we kill everything and bail
	 * as we can't init the IOPs w/o a system table
	 */	
	if(i2o_build_sys_table())
	{
		printk(KERN_CRIT "I2O: Error building system table. Aborting!\n");
		i2o_sys_shutdown();
		return;
	}

	for(iop = i2o_controller_chain; iop; iop = niop)
#ifdef DRIVERDEBUG
	{
		niop = iop->next;
		printk(KERN_INFO "Sending system table to iop%d\n", iop->unit);
#endif
		if(i2o_systab_send(iop))
		{
			printk(KERN_CRIT "iop%d: Error sending system table\n", iop->unit);
			i2o_delete_controller(iop);
			continue;
		}
#ifdef DRIVERDEBUG
	}
#endif

	/*
	 * Enable
	 */
	for(iop = i2o_controller_chain; iop; iop = niop)
	{
		niop = iop->next;
#ifdef DRIVERDEBUG
		printk(KERN_INFO "Enabling iop%d\n", iop->unit);
#endif
		if(i2o_enable_controller(iop))
		{
			printk(KERN_ERR "Could not enable iop%d\n", iop->unit);
			i2o_delete_controller(iop);
			continue;
		}
	}

	/*
	 * OK..one last thing and we're ready to go!
	 */
	for(iop = i2o_controller_chain; iop; iop = niop)
	{
		niop = iop->next;
#ifdef DRIVERDEBUG
		printk(KERN_INFO "Getting LCT for iop%d\n", iop->unit);
#endif
		if(i2o_lct_get(iop))
		{
			printk(KERN_ERR "Could not get LCT from iop%d\n", iop->unit);
			i2o_delete_controller(iop);
			continue;
		}
		else	
			i2o_parse_lct(iop);
	}
}

/*
 * Shutdown I2O system
 *
 * 1. Quiesce all controllers
 * 2. Delete all controllers
 *
 */
static void i2o_sys_shutdown(void)
{
	struct i2o_controller *iop = NULL;

	i2o_quiesce_system();
	for(iop = i2o_controller_chain; iop; iop = iop->next)
	{
		if(i2o_delete_controller(iop))
			iop->bus_disable(iop);
	}
}

/*
 *	Bring an I2O controller into HOLD state. See the 1.5
 *	spec. Basically we go
 *
 *	Wait for the message queue to initialise. 
 *	If it didnt -> controller is dead
 *	
 *	Send a get status using the message queue
 *	Poll for a reply block 88 bytes long
 *
 *	Send an initialise outbound queue
 *	Poll for a reply
 *
 *	Post our blank messages to the queue FIFO
 *
 *	Send GetHRT, Parse it
 */
int i2o_activate_controller(struct i2o_controller *c)
{
	return 0;
#ifdef I2O_HOTPLUG_SUPPORT
	u32 m;
	int i;
	int ret;

	printk(KERN_INFO "Configuring I2O controller at 0x%08X.\n",
	       (u32)c->mem_phys);

	if((ret=i2o_status_get(c)))
		return ret;

	/* not likely to be seen */
	if(c->status_block->iop_state == ADAPTER_STATE_FAULTED) 
	{
		printk(KERN_CRIT "i2o: iop%d has hardware fault\n",
		       c->unit);
		return -1;
	}

	/*
	 * If the board is running, reset it - we have no idea
	 * what kind of a mess the previous owner left it in.
	 * We need to feed the IOP a single outbound message
	 * so that it can reply back to the ExecSysQuiesce.
	 */
	if(c->status_block->iop_state == ADAPTER_STATE_HOLD ||
	   c->status_block->iop_state == ADAPTER_STATE_READY ||
	   c->status_block->iop_state == ADAPTER_STATE_OPERATIONAL ||
	   c->status_block->iop_state == ADAPTER_STATE_FAILED)
	{
		int msg[256];
		printk(KERN_INFO "i2o/iop%d already running, reseting\n", c->unit);

		if(i2o_init_outbound_q(c));
		I2O_REPLY_WRITE32(c,virt_to_phys(msg));

		if((ret=i2o_reset_controller(c)))
			return ret;

		if((ret=i2o_status_get(c)))
			return ret;
	}

	if((ret=i2o_init_outbound_q(c)))
	{
		printk(KERN_ERR 
			"IOP%d initialization failed: Could not initialize outbound queue\n",
			c->unit);
		return ret;
	}

	/* TODO: v2.0: Set Executive class group 000Bh - OS Operating Info */

	c->page_frame = kmalloc(MSG_POOL_SIZE, GFP_KERNEL);
	if(c->page_frame==NULL)
	{
		printk(KERN_ERR "IOP init failed: no memory for message page.\n");
		return -ENOMEM;
	}
	
	m=virt_to_phys(c->page_frame);
	
	for(i=0; i< NMBR_MSG_FRAMES; i++)
	{
		I2O_REPLY_WRITE32(c,m);
		mb();
		m+=MSG_FRAME_SIZE;
		mb();
	}
	
	/* 
	 *	The outbound queue is initialised and loaded, 
	 * 
	 *	Now we need the Hardware Resource Table. We must ask for 
	 *	this next we can't issue random messages yet.  
	 */ 
	ret=i2o_hrt_get(c); if(ret) return ret;

	ret=i2o_parse_hrt(c);
	if(ret)
		return ret;

	return i2o_online_controller(c);
#endif
}

/*
 * Clear and (re)initialize IOP's outbound queue
 */
int i2o_init_outbound_q(struct i2o_controller *c)
{
	u8 workspace[88];
	u32 m;
	u32 *msg;
	u32 time;

	memset(workspace, 0, 88);

//	printk(KERN_INFO "i2o/iop%d: Initializing Outbound Queue\n", c->unit);
	m=i2o_wait_message(c, "OutboundInit");
	if(m==0xFFFFFFFF)
	{	
		kfree(workspace);
		return -ETIMEDOUT;
	}

	msg=(u32 *)(c->mem_offset+m);

	msg[0]= EIGHT_WORD_MSG_SIZE| TRL_OFFSET_6;
	msg[1]= I2O_CMD_OUTBOUND_INIT<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2]= core_context;
	msg[3]= 0x0106;			/* Transaction context */
	msg[4]= 4096;			/* Host page frame size */
	msg[5]= MSG_FRAME_SIZE<<16|0x80;	/* Outbound msg frame size and Initcode */
	msg[6]= 0xD0000004;		/* Simple SG LE, EOB */
	msg[7]= virt_to_bus(workspace);
	*((u32 *)workspace)=0;

	/*
	 *	Post it
	 */
	i2o_post_message(c,m);
	
	barrier();
	
	time=jiffies;
	
	while(workspace[0]!=I2O_CMD_OUTBOUND_INIT_COMPLETE)
	{
		if((jiffies-time)>=5*HZ)
		{
			printk(KERN_ERR "i2o/iop%d: IOP outbound initialise failed.\n",
						c->unit);
			return -ETIMEDOUT;
		}
		schedule();
		barrier();
	}

	return 0;
}

/*
 * Get the IOP's Logical Configuration Table
 */
int i2o_lct_get(struct i2o_controller *c)
{
	u32 msg[8];

#ifdef DRIVERDEBUG
	printk(KERN_INFO "Getting lct for iop%d\n", c->unit);
#endif

	if(c->lct)
		kfree(c->lct);

	c->lct = kmalloc(8192, GFP_KERNEL);
	if(c->lct==NULL)
	{
		printk(KERN_ERR "i2o/iop%d: No free memory for i2o controller buffer.\n",
				c->unit);
		return -ENOMEM;
	}
	
	memset(c->lct, 0, 8192);
	
	msg[0] = EIGHT_WORD_MSG_SIZE|SGL_OFFSET_6;
	msg[1] = I2O_CMD_LCT_NOTIFY<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2] = 0;		/* Context not needed */
	msg[3] = 0;
	msg[4] = 0xFFFFFFFF;	/* All devices */
	msg[5] = 0x00000000;	/* Report now */
	msg[6] = 0xD0000000|8192;
	msg[7] = virt_to_bus(c->lct);
	
	return(i2o_post_wait(c, msg, sizeof(msg), 120));
}


/*
 *	Bring a controller online. Needs completing for multiple controllers
 */
 
int i2o_online_controller(struct i2o_controller *c)
{
	return 0;
#ifdef I2O_HOTPLUG_SUPPORT
	u32 msg[10];
	u32 privmem[2];
	u32 privio[2];
	u32 systab[32];
	int ret;

	systab[0]=1;
	systab[1]=0;
	systab[2]=0;
	systab[3]=0;
	systab[4]=0;			/* Organisation ID */
	systab[5]=2;			/* Ident 2 for now */
	systab[6]=0<<24|0<<16|I2OVERSION<<12|1; /* Memory mapped, IOPState, v1.5, segment 1 */
	systab[7]=MSG_FRAME_SIZE>>2;	/* Message size */
	systab[8]=0;			/* LastChanged */
	systab[9]=0;			/* Should be IOP capabilities */
	systab[10]=virt_to_phys(c->post_port);
	systab[11]=0;
	
	i2o_build_sys_table();
	
	privmem[0]=c->priv_mem;		/* Private memory space base address */
	privmem[1]=c->priv_mem_size;
	privio[0]=c->priv_io;		/* Private I/O address */
	privio[1]=c->priv_io_size;

	__raw_writel(NINE_WORD_MSG_SIZE|SGL_OFFSET_6, &msg[0]);
	__raw_writel(I2O_CMD_SYS_TAB_SET<<24 | HOST_TID<<12 | ADAPTER_TID, &msg[1]);
	__raw_writel(0, &msg[2]);		/* Context not needed */
	__raw_writel(0, &msg[3]);
	__raw_writel((0<<16)|(2<<12), &msg[4]);	/* Host 1 I2O 2 */
	__raw_writel(0, &msg[5]);		/* Segment 1 */
	
	/*
	 *	Scatter Gather List
	 */
	
	__raw_writel(0x54000000|sys_tbl_len, &msg[6]);	/* One table for now */
	__raw_writel(virt_to_phys(sys_tbl), &msg[[7]);
	__raw_writel(0xD4000000|48, &msg[8]);	/* One table for now */
	__raw_writel(virt_to_phys(privmem), &msg[9]);

	return(i2o_post_wait(c, msg, sizeof(msg), 120));
	
	/*
	 *	Finally we go online
	 */
	ret = i2o_enable_controller(c);
	if(ret)
		return ret;
	
	/*
	 *	Grab the LCT, see what is attached
	 */
	ret=i2o_lct_get(c);
	if(ret)
	{
		/* Maybe we should do also do something else */
		return ret;
	}

	ret=i2o_parse_lct(c);
	if(ret)
		return ret;

	return 0;
#endif
}

static int i2o_build_sys_table(void)
{
	struct i2o_controller *iop = NULL;
	int count = 0;
#ifdef DRIVERDEBUG
	u32 *table;
#endif

	sys_tbl_len = sizeof(struct i2o_sys_tbl) +	// Header + IOPs
				(i2o_num_controllers) *
					sizeof(struct i2o_sys_tbl_entry);

#ifdef DRIVERDEBUG
	printk(KERN_INFO "Building system table len = %d\n", sys_tbl_len);
#endif
	if(sys_tbl)
		kfree(sys_tbl);

	sys_tbl = kmalloc(sys_tbl_len, GFP_KERNEL);
	if(!sys_tbl)
		return -ENOMEM;
	memset((void*)sys_tbl, 0, sys_tbl_len);

	sys_tbl->num_entries = i2o_num_controllers;
	sys_tbl->version = I2OVERSION; /* TODO: Version 2.0 */
	sys_tbl->change_ind = sys_tbl_ind++;

	for(iop = i2o_controller_chain; iop; iop = iop->next)
	{
		// Get updated IOP state so we have the latest information
		i2o_status_get(iop);	

		sys_tbl->iops[count].org_id = iop->status_block->org_id;
		sys_tbl->iops[count].iop_id = iop->unit + 2;
		sys_tbl->iops[count].seg_num = 0;
		sys_tbl->iops[count].i2o_version = 
				iop->status_block->i2o_version;
		sys_tbl->iops[count].iop_state = 
				iop->status_block->iop_state;
		sys_tbl->iops[count].msg_type = 
				iop->status_block->msg_type;
		sys_tbl->iops[count].frame_size = 
				iop->status_block->inbound_frame_size;
		sys_tbl->iops[count].last_changed = sys_tbl_ind - 1; // ??
		sys_tbl->iops[count].iop_capabilities = 
				iop->status_block->iop_capabilities;
		sys_tbl->iops[count].inbound_low = 
				(u32)virt_to_phys(iop->post_port);
		sys_tbl->iops[count].inbound_high = 0;	// TODO: 64-bit support

		count++;
	}

#ifdef DRIVERDEBUG
	table = (u32*)sys_tbl;
	for(count = 0; count < (sys_tbl_len >>2); count++)
		printk(KERN_INFO "sys_tbl[%d] = %0#10x\n", count, table[count]);
#endif

	return 0;
}


/*
 *	Run time support routines
 */
 
/*
 *	Generic "post and forget" helpers. This is less efficient - we do
 *	a memcpy for example that isnt strictly needed, but for most uses
 *	this is simply not worth optimising
 */

int i2o_post_this(struct i2o_controller *c, u32 *data, int len)
{
	u32 m;
	u32 *msg;
	unsigned long t=jiffies;

	do
	{
		mb();
		m = I2O_POST_READ32(c);
	}
	while(m==0xFFFFFFFF && (jiffies-t)<HZ);
	
	
	if(m==0xFFFFFFFF)
	{
		printk(KERN_ERR "i2o/iop%d: Timeout waiting for message frame!\n",
					c->unit);
		return -ETIMEDOUT;
	}
	msg = (u32 *)(c->mem_offset + m);
 	memcpy_toio(msg, data, len);
	i2o_post_message(c,m);
	return 0;
}

/*
 *	Post a message and wait for a response flag to be set.
 */
int i2o_post_wait(struct i2o_controller *c, u32 *msg, int len, int timeout)
{
	DECLARE_WAIT_QUEUE_HEAD(wq_i2o_post);
	int status = 0;
	int flags = 0;
	int ret = 0;
	struct i2o_post_wait_data *p1, *p2;
	struct i2o_post_wait_data *wait_data =
		kmalloc(sizeof(struct i2o_post_wait_data), GFP_KERNEL);

	if(!wait_data)
		return -ETIMEDOUT;

	p1 = p2 = NULL;
	
	/* 
	 * The spin locking is needed to keep anyone from playing 
	 * with the queue pointers and id while we do the same
	 */
	spin_lock_irqsave(&post_wait_lock, flags);
	wait_data->next = post_wait_queue;
	post_wait_queue = wait_data;
	wait_data->id = ++post_wait_id;
	spin_unlock_irqrestore(&post_wait_lock, flags);

	wait_data->wq = &wq_i2o_post;
	wait_data->status = -ETIMEDOUT;

	msg[3] = (u32)wait_data->id;	
	msg[2] = 0x80000000|(u32)core_context;

	if((ret=i2o_post_this(c, msg, len)))
		return ret;
	/*
	 * Go to sleep and wait for timeout or wake_up call
	 */
	interruptible_sleep_on_timeout(&wq_i2o_post, HZ * timeout);

	/*
	 * Grab transaction status
	 */
	status = wait_data->status;

	/* 
	 * Remove the entry from the queue.
	 * Since i2o_post_wait() may have been called again by
	 * a different thread while we were waiting for this 
	 * instance to complete, we're not guaranteed that 
	 * this entry is at the head of the queue anymore, so 
	 * we need to search for it, find it, and delete it.
	 */
	spin_lock_irqsave(&post_wait_lock, flags);
	for(p1 = post_wait_queue; p1;  )
	{
		if(p1 == wait_data)
		{
			if(p2)
				p2->next = p1->next;
			else
				post_wait_queue = p1->next;

			break;
		}
		p1 = p1->next;
	}
	spin_unlock_irqrestore(&post_wait_lock, flags);
	
	kfree(wait_data);

	return status;
}

/*
 * i2o_post_wait is completed and we want to wake up the 
 * sleeping proccess. Called by core's reply handler.
 */
static void i2o_post_wait_complete(u32 context, int status)
{
	struct i2o_post_wait_data *p1 = NULL;

	/* 
	 * We need to search through the post_wait 
	 * queue to see if the given message is still
	 * outstanding.  If not, it means that the IOP 
	 * took longer to respond to the message than we 
	 * had allowed and timer has already expired.  
	 * Not much we can do about that except log
	 * it for debug purposes, increase timeout, and recompile
	 *
	 * Lock needed to keep anyone from moving queue pointers 
	 * around while we're looking through them.
	 */
	spin_lock(&post_wait_lock);
	for(p1 = post_wait_queue; p1; p1 = p1->next)
	{
		if(p1->id == context)
		{
			p1->status = status;
			wake_up_interruptible(p1->wq);
			spin_unlock(&post_wait_lock);
			return;
		}
	}
	spin_unlock(&post_wait_lock);

	printk(KERN_DEBUG "i2o_post_wait reply after timeout!");
}

/*
 *	Issue UTIL_CLAIM messages
 */
 
int i2o_issue_claim(struct i2o_controller *c, int tid, int context, int onoff, int *flag, u32 type)
{
	u32 msg[6];

	msg[0] = FIVE_WORD_MSG_SIZE | SGL_OFFSET_0;
	if(onoff)
		msg[1] = I2O_CMD_UTIL_CLAIM << 24 | HOST_TID<<12 | tid;
	else	
		msg[1] = I2O_CMD_UTIL_RELEASE << 24 | HOST_TID << 12 | tid;
	
	/* The 0x80000000 convention for flagging is assumed by this helper */
	
	msg[2] = 0x80000000|context;
	msg[3] = (u32)flag;
	msg[4] = type;
	
	return i2o_post_wait(c, msg, 20, 2);
}

/*	Issue UTIL_PARAMS_GET or UTIL_PARAMS_SET
 *
 *	This function can be used for all UtilParamsGet/Set operations.
 *	The OperationBlock is given in opblk-buffer, 
 *	and results are returned in resblk-buffer.
 *	Note that the minimum sized resblk is 8 bytes and contains
 *	ResultCount, ErrorInfoSize, BlockStatus and BlockSize.
 */
int i2o_issue_params(int cmd, struct i2o_controller *iop, int tid, 
                void *opblk, int oplen, void *resblk, int reslen)
{
	u32 msg[9]; 
	u8 *res = (u8 *)resblk;
	int res_count;
	int blk_size;
	int bytes;
	int wait_status;

	msg[0] = NINE_WORD_MSG_SIZE | SGL_OFFSET_5;
	msg[1] = cmd << 24 | HOST_TID << 12 | tid; 
	msg[4] = 0;
	msg[5] = 0x54000000 | oplen;	/* OperationBlock */
	msg[6] = virt_to_bus(opblk);
	msg[7] = 0xD0000000 | reslen;	/* ResultBlock */
	msg[8] = virt_to_bus(resblk);

	wait_status = i2o_post_wait(iop, msg, sizeof(msg), 10);
	if (wait_status)       
   	return wait_status; 	/* -DetailedStatus */

	if (res[1]&0x00FF0000) 	/* BlockStatus != SUCCESS */
	{
		printk(KERN_WARNING "%s - Error:\n  ErrorInfoSize = 0x%02x, " 
					"BlockStatus = 0x%02x, BlockSize = 0x%04x\n",
					(cmd == I2O_CMD_UTIL_PARAMS_SET) ? "PARAMS_SET"
					: "PARAMS_GET",   
					res[1]>>24, (res[1]>>16)&0xFF, res[1]&0xFFFF);
		return -((res[1] >> 16) & 0xFF); /* -BlockStatus */
	}

	res_count = res[0] & 0xFFFF; /* # of resultblocks */
	bytes = 4; 
	res  += 4;
	while (res_count--)
	{
		blk_size = (res[0] & 0xFFFF) << 2;
		bytes   += blk_size;
		res     += blk_size;
	}
  
	return bytes; /* total sizeof Result List in bytes */
}

/*
 *	 Query one scalar group value or a whole scalar group.
 */                  	
int i2o_query_scalar(struct i2o_controller *iop, int tid, 
                     int group, int field, void *buf, int buflen)
{
	u16 opblk[] = { 1, 0, I2O_PARAMS_FIELD_GET, group, 1, field };
	u8  resblk[8+buflen]; /* 8 bytes for header */
	int size;

	if (field == -1)  		/* whole group */
       		opblk[4] = -1;
              
	size = i2o_issue_params(I2O_CMD_UTIL_PARAMS_GET, iop, tid, 
		opblk, sizeof(opblk), resblk, sizeof(resblk));
		
	if (size < 0)
		return size;	

	memcpy(buf, resblk+8, buflen);  /* cut off header */
	return buflen;
}

/*
 *	Set a scalar group value or a whole group.
 */
int i2o_set_scalar(struct i2o_controller *iop, int tid, 
		   int group, int field, void *buf, int buflen)
{
	u16 *opblk;
	u8  resblk[8+buflen]; /* 8 bytes for header */
        int size;

	opblk = kmalloc(buflen+64, GFP_KERNEL);
	if (opblk == NULL)
	{
		printk(KERN_ERR "i2o: no memory for operation buffer.\n");
		return -ENOMEM;
	}

	opblk[0] = 1;                        /* operation count */
	opblk[1] = 0;                        /* pad */
	opblk[2] = I2O_PARAMS_FIELD_SET;
	opblk[3] = group;

	if(field == -1) {               /* whole group */
		opblk[4] = -1;
		memcpy(opblk+5, buf, buflen);
	}
	else                            /* single field */
	{
		opblk[4] = 1;
		opblk[5] = field;
		memcpy(opblk+6, buf, buflen);
	}   

	size = i2o_issue_params(I2O_CMD_UTIL_PARAMS_SET, iop, tid, 
				opblk, 12+buflen, resblk, sizeof(resblk));

	kfree(opblk);
	return size;
}

/* 
 * 	if oper == I2O_PARAMS_TABLE_GET: 
 *		Get all table group fields from all rows or
 *		get specific table group fields from all rows.
 *
 * 		if fieldcount == -1 we query all fields from all rows
 *			ibuf is NULL and ibuflen is 0
 * 		else we query specific fields from all rows
 *  			ibuf contains fieldindexes
 *
 * 	if oper == I2O_PARAMS_LIST_GET:
 *		Get all table group fields from specified rows or
 *		get specific table group fields from specified rows.
 *
 * 		if fieldcount == -1 we query all fields from specified rows
 *			ibuf contains rowcount, keyvalues
 * 		else we query specific fields from specified rows
 *  			ibuf contains fieldindexes, rowcount, keyvalues
 *
 *	You could also use directly function i2o_issue_params().
 */
int i2o_query_table(int oper, struct i2o_controller *iop, int tid, int group,
		int fieldcount, void *ibuf, int ibuflen,
		void *resblk, int reslen) 
{
	u16 *opblk;
	int size;

	opblk = kmalloc(10 + ibuflen, GFP_KERNEL);
	if (opblk == NULL)
	{
		printk(KERN_ERR "i2o: no memory for query buffer.\n");
		return -ENOMEM;
	}

	opblk[0] = 1;				/* operation count */
	opblk[1] = 0;				/* pad */
	opblk[2] = oper;
	opblk[3] = group;		
	opblk[4] = fieldcount;
	memcpy(opblk+5, ibuf, ibuflen);		/* other params */

	size = i2o_issue_params(I2O_CMD_UTIL_PARAMS_GET,iop, tid, 
				opblk, 10+ibuflen, resblk, reslen);

	kfree(opblk);
	return size;
}

/*
 * 	Clear table group, i.e. delete all rows.
 */

int i2o_clear_table(struct i2o_controller *iop, int tid, int group)
{
	u16 opblk[] = { 1, 0, I2O_PARAMS_TABLE_CLEAR, group };
	u8  resblk[32]; /* min 8 bytes for result header */

	return i2o_issue_params(I2O_CMD_UTIL_PARAMS_SET, iop, tid, 
				opblk, sizeof(opblk), resblk, sizeof(resblk));
}

/*
 * 	Add a new row into a table group.
 *
 * 	if fieldcount==-1 then we add whole rows
 *		buf contains rowcount, keyvalues
 * 	else just specific fields are given, rest use defaults
 *  		buf contains fieldindexes, rowcount, keyvalues
 */	

int i2o_row_add_table(struct i2o_controller *iop, int tid,
		    int group, int fieldcount, void *buf, int buflen)
{
	u16 *opblk;
	u8  resblk[32]; /* min 8 bytes for header */
	int size;

	opblk = kmalloc(buflen+64, GFP_KERNEL);
	if (opblk == NULL)
	{
		printk(KERN_ERR "i2o: no memory for operation buffer.\n");
		return -ENOMEM;
	}

	opblk[0] = 1;			/* operation count */
	opblk[1] = 0;			/* pad */
	opblk[2] = I2O_PARAMS_ROW_ADD;
	opblk[3] = group;	
	opblk[4] = fieldcount;
	memcpy(opblk+5, buf, buflen);

	size = i2o_issue_params(I2O_CMD_UTIL_PARAMS_SET, iop, tid, 
				opblk, 10+buflen, resblk, sizeof(resblk));

	kfree(opblk);
	return size;
}

/*
 *	Delete rows from a table group.
 */ 

int i2o_row_delete_table(struct i2o_controller *iop, int tid,
		    int group, int keycount, void *keys, int keyslen)
{
	u16 *opblk; 
	u8  resblk[32]; /* min 8 bytes for header */
	int size;

	opblk = kmalloc(keyslen+64, GFP_KERNEL);
	if (opblk == NULL)
	{
		printk(KERN_ERR "i2o: no memory for operation buffer.\n");
		return -ENOMEM;
	}

	opblk[0] = 1;			/* operation count */
	opblk[1] = 0;			/* pad */
	opblk[2] = I2O_PARAMS_ROW_DELETE;
	opblk[3] = group;	
	opblk[4] = keycount;
	memcpy(opblk+5, keys, keyslen);

	size = i2o_issue_params(I2O_CMD_UTIL_PARAMS_SET, iop, tid,
				opblk, 10+keyslen, resblk, sizeof(resblk));

	kfree(opblk);
	return size;
}

void i2o_report_common_status(u8 req_status)
{
	/* the following reply status strings are common to all classes */

	static char *REPLY_STATUS[] = { 
		"SUCCESS", 
		"ABORT_DIRTY", 
		"ABORT_NO_DATA_TRANSFER",
		"ABORT_PARTIAL_TRANSFER",
		"ERROR_DIRTY",
		"ERROR_NO_DATA_TRANSFER",
		"ERROR_PARTIAL_TRANSFER",
		"PROCESS_ABORT_DIRTY",
		"PROCESS_ABORT_NO_DATA_TRANSFER",
		"PROCESS_ABORT_PARTIAL_TRANSFER",
		"TRANSACTION_ERROR",
		"PROGRESS_REPORT"	
	};

	if (req_status > I2O_REPLY_STATUS_PROGRESS_REPORT)
		printk("%0#4x / ", req_status);
	else
		printk("%s / ", REPLY_STATUS[req_status]);
	
	return;
}

static void i2o_report_common_dsc(u16 detailed_status)
{
	/* The following detailed statuscodes are valid 
	   - for executive class, utility class, DDM class and
	   - for transaction error replies
	*/	

	static char *COMMON_DSC[] = { 
		"SUCCESS",
		"0x01",				// not used
		"BAD_KEY",
		"TCL_ERROR",
		"REPLY_BUFFER_FULL",
		"NO_SUCH_PAGE",
		"INSUFFICIENT_RESOURCE_SOFT",
		"INSUFFICIENT_RESOURCE_HARD",
		"0x08",				// not used
		"CHAIN_BUFFER_TOO_LARGE",
		"UNSUPPORTED_FUNCTION",
		"DEVICE_LOCKED",
		"DEVICE_RESET",
		"INAPPROPRIATE_FUNCTION",
		"INVALID_INITIATOR_ADDRESS",
		"INVALID_MESSAGE_FLAGS",
		"INVALID_OFFSET",
		"INVALID_PARAMETER",
		"INVALID_REQUEST",
		"INVALID_TARGET_ADDRESS",
		"MESSAGE_TOO_LARGE",
		"MESSAGE_TOO_SMALL",
		"MISSING_PARAMETER",
		"TIMEOUT",
		"UNKNOWN_ERROR",
		"UNKNOWN_FUNCTION",
		"UNSUPPORTED_VERSION",
		"DEVICE_BUSY",
		"DEVICE_NOT_AVAILABLE"		
	};

	if (detailed_status > I2O_DSC_DEVICE_NOT_AVAILABLE)
		printk("%0#4x.\n", detailed_status);
	else
		printk("%s.\n", COMMON_DSC[detailed_status]);

	return;
}

static void i2o_report_lan_dsc(u16 detailed_status)
{
	static char *LAN_DSC[] = {	// Lan detailed status code strings
		"SUCCESS",
		"DEVICE_FAILURE",
		"DESTINATION_NOT_FOUND",
		"TRANSMIT_ERROR",
		"TRANSMIT_ABORTED",
		"RECEIVE_ERROR",
		"RECEIVE_ABORTED",
		"DMA_ERROR",
		"BAD_PACKET_DETECTED",
		"OUT_OF_MEMORY",
		"BUCKET_OVERRUN",
		"IOP_INTERNAL_ERROR",
		"CANCELED",
		"INVALID_TRANSACTION_CONTEXT",
		"DEST_ADDRESS_DETECTED",
		"DEST_ADDRESS_OMITTED",
		"PARTIAL_PACKET_RETURNED",
		"TEMP_SUSPENDED_STATE",	// last Lan detailed status code
		"INVALID_REQUEST"	// general detailed status code
	};

	if (detailed_status > I2O_DSC_INVALID_REQUEST)
		printk("%0#4x.\n", detailed_status);
	else
		printk("%s.\n", LAN_DSC[detailed_status]);

	return;	
}

static void i2o_report_util_cmd(u8 cmd)
{
	switch (cmd) {
	case I2O_CMD_UTIL_NOP:
		printk("UTIL_NOP, ");
		break;			
	case I2O_CMD_UTIL_ABORT:
		printk("UTIL_ABORT, ");
		break;
	case I2O_CMD_UTIL_CLAIM:
		printk("UTIL_CLAIM, ");
		break;
	case I2O_CMD_UTIL_RELEASE:
		printk("UTIL_CLAIM_RELEASE, ");
		break;
	case I2O_CMD_UTIL_CONFIG_DIALOG:
		printk("UTIL_CONFIG_DIALOG, ");
		break;
	case I2O_CMD_UTIL_DEVICE_RESERVE:
		printk("UTIL_DEVICE_RESERVE, ");
		break;
	case I2O_CMD_UTIL_DEVICE_RELEASE:
		printk("UTIL_DEVICE_RELEASE, ");
		break;
	case I2O_CMD_UTIL_EVT_ACK:
		printk("UTIL_EVENT_ACKNOWLEDGE, ");
		break;
	case I2O_CMD_UTIL_EVT_REGISTER:
		printk("UTIL_EVENT_REGISTER, ");
		break;
	case I2O_CMD_UTIL_LOCK:
		printk("UTIL_LOCK, ");
		break;
	case I2O_CMD_UTIL_LOCK_RELEASE:
		printk("UTIL_LOCK_RELEASE, ");
		break;
	case I2O_CMD_UTIL_PARAMS_GET:
		printk("UTIL_PARAMS_GET, ");
		break;
	case I2O_CMD_UTIL_PARAMS_SET:
		printk("UTIL_PARAMS_SET, ");
		break;
	case I2O_CMD_UTIL_REPLY_FAULT_NOTIFY:
		printk("UTIL_REPLY_FAULT_NOTIFY, ");
		break;
	default:
		printk("%0#2x, ",cmd);	
	}

	return;	
}


static void i2o_report_exec_cmd(u8 cmd)
{
	switch (cmd) {
	case I2O_CMD_ADAPTER_ASSIGN:
		printk("EXEC_ADAPTER_ASSIGN, ");
		break;
	case I2O_CMD_ADAPTER_READ:
		printk("EXEC_ADAPTER_READ, ");
		break;
	case I2O_CMD_ADAPTER_RELEASE:
		printk("EXEC_ADAPTER_RELEASE, ");
		break;
	case I2O_CMD_BIOS_INFO_SET:
		printk("EXEC_BIOS_INFO_SET, ");
		break;
	case I2O_CMD_BOOT_DEVICE_SET:
		printk("EXEC_BOOT_DEVICE_SET, ");
		break;
	case I2O_CMD_CONFIG_VALIDATE:
		printk("EXEC_CONFIG_VALIDATE, ");
		break;
	case I2O_CMD_CONN_SETUP:
		printk("EXEC_CONN_SETUP, ");
		break;
	case I2O_CMD_DDM_DESTROY:
		printk("EXEC_DDM_DESTROY, ");
		break;
	case I2O_CMD_DDM_ENABLE:
		printk("EXEC_DDM_ENABLE, ");
		break;
	case I2O_CMD_DDM_QUIESCE:
		printk("EXEC_DDM_QUIESCE, ");
		break;
	case I2O_CMD_DDM_RESET:
		printk("EXEC_DDM_RESET, ");
		break;
	case I2O_CMD_DDM_SUSPEND:
		printk("EXEC_DDM_SUSPEND, ");
		break;
	case I2O_CMD_DEVICE_ASSIGN:
		printk("EXEC_DEVICE_ASSIGN, ");
		break;
	case I2O_CMD_DEVICE_RELEASE:
		printk("EXEC_DEVICE_RELEASE, ");
		break;
	case I2O_CMD_HRT_GET:
		printk("EXEC_HRT_GET, ");
		break;
	case I2O_CMD_ADAPTER_CLEAR:
		printk("EXEC_IOP_CLEAR, ");
		break;
	case I2O_CMD_ADAPTER_CONNECT:
		printk("EXEC_IOP_CONNECT, ");
		break;
	case I2O_CMD_ADAPTER_RESET:
		printk("EXEC_IOP_RESET, ");
		break;
	case I2O_CMD_LCT_NOTIFY:
		printk("EXEC_LCT_NOTIFY, ");
		break;
	case I2O_CMD_OUTBOUND_INIT:
		printk("EXEC_OUTBOUND_INIT, ");
		break;
	case I2O_CMD_PATH_ENABLE:
		printk("EXEC_PATH_ENABLE, ");
		break;
	case I2O_CMD_PATH_QUIESCE:
		printk("EXEC_PATH_QUIESCE, ");
		break;
	case I2O_CMD_PATH_RESET:
		printk("EXEC_PATH_RESET, ");
		break;
	case I2O_CMD_STATIC_MF_CREATE:
		printk("EXEC_STATIC_MF_CREATE, ");
		break;
	case I2O_CMD_STATIC_MF_RELEASE:
		printk("EXEC_STATIC_MF_RELEASE, ");
		break;
	case I2O_CMD_STATUS_GET:
		printk("EXEC_STATUS_GET, ");
		break;
	case I2O_CMD_SW_DOWNLOAD:
		printk("EXEC_SW_DOWNLOAD, ");
		break;
	case I2O_CMD_SW_UPLOAD:
		printk("EXEC_SW_UPLOAD, ");
		break;
	case I2O_CMD_SW_REMOVE:
		printk("EXEC_SW_REMOVE, ");
		break;
	case I2O_CMD_SYS_ENABLE:
		printk("EXEC_SYS_ENABLE, ");
		break;
	case I2O_CMD_SYS_MODIFY:
		printk("EXEC_SYS_MODIFY, ");
		break;
	case I2O_CMD_SYS_QUIESCE:
		printk("EXEC_SYS_QUIESCE, ");
		break;
	case I2O_CMD_SYS_TAB_SET:
		printk("EXEC_SYS_TAB_SET, ");
		break;
	default:
		printk("%02x, ",cmd);	
	}

	return;	
}

static void i2o_report_lan_cmd(u8 cmd)
{
	switch (cmd) {
	case LAN_PACKET_SEND:
		printk("LAN_PACKET_SEND, "); 
		break;
	case LAN_SDU_SEND:
		printk("LAN_SDU_SEND, ");
		break;
	case LAN_RECEIVE_POST:
		printk("LAN_RECEIVE_POST, ");
		break;
	case LAN_RESET:
		printk("LAN_RESET, ");
		break;
	case LAN_SUSPEND:
		printk("LAN_SUSPEND, ");
		break;
	default:
		printk("%02x, ",cmd);	
	}	

	return;
}

/* TODO: Add support for other classes */
void i2o_report_status(const char *severity, const char *module, u32 *msg)
{
	u8 cmd = (msg[1]>>24)&0xFF;
	u8 req_status = (msg[4]>>24)&0xFF;
	u16 detailed_status = msg[4]&0xFFFF;

	printk("%s%s: ", severity, module);

	if (cmd < 0x1F) { 			// Utility Class
		i2o_report_util_cmd(cmd);
		i2o_report_common_status(req_status);
		i2o_report_common_dsc(detailed_status);
		return;
	}

	if (cmd >= 0x30 && cmd <= 0x3F) {	// LAN class
		i2o_report_lan_cmd(cmd);
		i2o_report_common_status(req_status);		
		i2o_report_lan_dsc(detailed_status);
		return;
	}
	
	if (cmd >= 0xA0 && cmd <= 0xEF) {	// Executive class
		i2o_report_exec_cmd(cmd);
		i2o_report_common_status(req_status);
		i2o_report_common_dsc(detailed_status);
		return;
	}
	
	printk("%02x, %02x / %04x.\n", cmd, req_status, detailed_status);
	return;
}

/* Used to dump a message to syslog during debugging */
static void i2o_dump_message(u32 *msg)
{
#ifdef DRIVERDEBUG
	int i;

	printk(KERN_INFO "Dumping I2O message size %d @ %p\n", 
		msg[0]>>16&0xffff, msg);
	for(i = 0; i < ((msg[0]>>16)&0xffff); i++)
		printk(KERN_INFO "  msg[%d] = %0#10x\n", i, msg[i]);
#endif
}

#ifdef MODULE

EXPORT_SYMBOL(i2o_install_handler);
EXPORT_SYMBOL(i2o_remove_handler);
EXPORT_SYMBOL(i2o_install_device);
EXPORT_SYMBOL(i2o_delete_device);
EXPORT_SYMBOL(i2o_quiesce_controller);
EXPORT_SYMBOL(i2o_clear_controller);
EXPORT_SYMBOL(i2o_install_controller);
EXPORT_SYMBOL(i2o_delete_controller);
EXPORT_SYMBOL(i2o_unlock_controller);
EXPORT_SYMBOL(i2o_find_controller);
EXPORT_SYMBOL(i2o_num_controllers);
EXPORT_SYMBOL(i2o_claim_device);
EXPORT_SYMBOL(i2o_release_device);
EXPORT_SYMBOL(i2o_run_queue);
EXPORT_SYMBOL(i2o_report_controller_unit);
EXPORT_SYMBOL(i2o_activate_controller);
EXPORT_SYMBOL(i2o_online_controller);
EXPORT_SYMBOL(i2o_get_class_name);
EXPORT_SYMBOL(i2o_status_get);

EXPORT_SYMBOL(i2o_query_scalar);
EXPORT_SYMBOL(i2o_set_scalar);
EXPORT_SYMBOL(i2o_query_table);
EXPORT_SYMBOL(i2o_clear_table);
EXPORT_SYMBOL(i2o_row_add_table);
EXPORT_SYMBOL(i2o_row_delete_table);

EXPORT_SYMBOL(i2o_post_this);
EXPORT_SYMBOL(i2o_post_wait);
EXPORT_SYMBOL(i2o_issue_claim);
EXPORT_SYMBOL(i2o_issue_params);

EXPORT_SYMBOL(i2o_report_status);

MODULE_AUTHOR("Red Hat Software");
MODULE_DESCRIPTION("I2O Core");


int init_module(void)
{
	printk(KERN_INFO "I2O Core - (C) Copyright 1999 Red Hat Software\n");
	if (i2o_install_handler(&i2o_core_handler) < 0)
	{
		printk(KERN_ERR 
			"i2o_core: Unable to install core handler.\nI2O stack not loaded!");
		return 0;
	}

	core_context = i2o_core_handler.context;

	/*
	 * Attach core to I2O PCI transport (and others as they are developed)
	 */
#ifdef CONFIG_I2O_PCI_MODULE
	if(i2o_pci_core_attach(&i2o_core_functions) < 0)
		printk(KERN_INFO "No PCI I2O controllers found\n");
#endif

	if(i2o_num_controllers)
		i2o_sys_init();

	return 0;
}

void cleanup_module(void)
{
	if(i2o_num_controllers)
		i2o_sys_shutdown();

#ifdef CONFIG_I2O_PCI_MODULE
	i2o_pci_core_detach();
#endif

	i2o_remove_handler(&i2o_core_handler);
}

#else

extern int i2o_block_init(void);
extern int i2o_config_init(void);
extern int i2o_lan_init(void);
extern int i2o_pci_init(void);
extern int i2o_proc_init(void);
extern int i2o_scsi_init(void);

int __init i2o_init(void)
{
	printk(KERN_INFO "Loading I2O Core - (c) Copyright 1999 Red Hat Software\n");
	if (i2o_install_handler(&i2o_core_handler) < 0)
	{
		printk(KERN_ERR 
			"i2o_core: Unable to install core handler.\nI2O stack not loaded!");
		return 0;
	}

	core_context = i2o_core_handler.context;

#ifdef CONFIG_I2O_PCI
	i2o_pci_init();
#endif

	if(i2o_num_controllers)
		i2o_init();

	i2o_config_init();
#ifdef CONFIG_I2O_BLOCK
	i2o_block_init();
#endif
#ifdef CONFIG_I2O_SCSI
	i2o_scsi_init();
#endif
#ifdef CONFIG_I2O_LAN
	i2o_lan_init();
#endif
#ifdef CONFIG_I2O_PROC
	i2o_proc_init();
#endif
	return 0;
}

#endif
