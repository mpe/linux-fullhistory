/*
 *	Core I2O structure managment
 *
 *	(C) Copyright 1999   Red Hat Software
 *	
 *	Written by Alan Cox, Building Number Three Ltd
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 * 	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	A lot of the I2O message side code from this is taken from the
 *	Red Creek RCPCI45 adapter driver by Red Creek Communications
 *
 *	Fixes by Philipp Rumpf
 *		      Juha Sievänen <Juha.Sievanen@cs.Helsinki.FI>
 *	         Auvo Häkkinen <Auvo.Hakkinen@cs.Helsinki.FI>
 *				Deepak Saxena <deepak@plexity.net>
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

#include <asm/io.h>

#include "i2o_lan.h"

#define DRIVERDEBUG

/*
 *	Size of the I2O module table
 */
 
static struct i2o_handler *i2o_handlers[MAX_I2O_MODULES];
static struct i2o_controller *i2o_controllers[MAX_I2O_CONTROLLERS];
int i2o_num_controllers = 0;
static int core_context = 0;
static int reply_flag = 0;

extern int i2o_online_controller(struct i2o_controller *c);
static void i2o_core_reply(struct i2o_handler *, struct i2o_controller *,
			   struct i2o_message *);
static int i2o_add_management_user(struct i2o_device *, struct i2o_handler *);
static int i2o_remove_management_user(struct i2o_device *, struct i2o_handler *);
static void i2o_dump_message(u32 *);

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

void i2o_core_reply(struct i2o_handler *h, struct i2o_controller *c,
		    struct i2o_message *m)
{
	u32 *msg=(u32 *)m;
	u32 *flag = (u32 *)msg[3];

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

	if (msg[4] >> 24)
	{
		i2o_report_status(KERN_WARNING, "i2o_core", msg);
		*flag = -(msg[4] & 0xFFFF);
	}
	else
		*flag = I2O_POST_WAIT_OK;
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

	spin_lock(&i2o_configuration_lock);
	if((users=atomic_read(&c->users)))
	{
		printk("I2O: %d users for controller iop%d\n", users, c->unit);
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

	/* Send first SysQuiesce to other IOPs */
	while(*p)
	{
		if(*p!=c)
		{
			printk("Quiescing controller %p != %p\n", c, *p);
			if(i2o_quiesce_controller(*p)<0)
				printk(KERN_INFO "Unable to quiesce iop%d\n",
				       (*p)->unit);
		}
		p=&((*p)->next);
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
			if(c->page_frame)
				kfree(c->page_frame);
			if(c->hrt)
				kfree(c->hrt);
			if(c->lct)
				kfree(c->lct);
			i2o_controllers[c->unit]=NULL;
			kfree(c);
			i2o_num_controllers--;
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

	if(i2o_issue_claim(d->controller,d->id, h->context, 1, &reply_flag, type) < 0)
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
			if(i2o_issue_claim(d->controller, d->id, h->context, 0,
					   &reply_flag, type) < 0)
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

		if(i2o_issue_claim(d->controller,d->id, h->context, 0, 
				   &reply_flag, type) < 0)
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
	
	while((mv=I2O_REPLY_READ32(c))!=0xFFFFFFFF)
	{
		struct i2o_handler *i;
		m=(struct i2o_message *)bus_to_virt(mv);
		/*
		 *	Temporary Debugging
		 */
		if(((m->function_addr>>24)&0xFF)==0x15)
			printk("UTFR!\n");
//		printk("dispatching.\n");
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
			printk(KERN_ERR "%s: Timeout waiting for message frame to send %s.\n", 
				c->name, why);
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
			printk(KERN_ERR "%s: timeout waiting for %s reply.\n",
				c->name, why);
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

	msg[0]=NINE_WORD_MSG_SIZE|SGL_OFFSET_5;
	msg[1]=I2O_CMD_UTIL_PARAMS_GET<<24|HOST_TID<<12|tid;
	msg[2]=0;			/* Context */
	msg[3]=0;
	msg[4]=0;
	msg[5]=0x54000000|12;
	msg[6]=virt_to_bus(op);
	msg[7]=0xD0000000|(32+buflen);
	msg[8]=virt_to_bus(rbuf);

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
		printk(KERN_ERR "%s: error in field read.\n",
			c->name);
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
	
	if(i2o_query_scalar_polled(c, unit, buf, 16, 0xF100, 3)>=0)
	{
		buf[16]=0;
		printk(KERN_INFO "     Vendor: %s\n", buf);
	}
	if(i2o_query_scalar_polled(c, unit, buf, 16, 0xF100, 4)>=0)
	{
		buf[16]=0;
		printk(KERN_INFO "     Device: %s\n", buf);
	}
#if 0
	if(i2o_query_scalar_polled(c, unit, buf, 16, 0xF100, 5)>=0)
	{
		buf[16]=0;
		printk(KERN_INFO "Description: %s\n", buf);
	}
#endif	
	if(i2o_query_scalar_polled(c, unit, buf, 8, 0xF100, 6)>=0)
	{
		buf[8]=0;
		printk(KERN_INFO "        Rev: %s\n", buf);
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
	u32 *rows=c->hrt;
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
	
	printk(KERN_INFO "HRT has %d entries of %d bytes each.\n",
		count, length<<2);

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
	u32 *lct=(u32 *)c->lct;

	max=lct[0]&0xFFFF;
	
	max-=3;
	max/=9;

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
	
	if(lct[1]&(1<<0))
		printk(KERN_WARNING "Configuration dialog desired.\n");
		
	p=lct+3;
	
	for(i=0;i<max;i++)
	{
		d = (struct i2o_device *)kmalloc(sizeof(struct i2o_device), GFP_KERNEL);
		if(d==NULL)
		{
			printk("i2o_core: Out of memory for LCT data.\n");
			return -ENOMEM;
		}
		
		d->controller = c;
		d->next = NULL;
		
		d->id = tid = (p[0]>>16)&0xFFF;
		d->class = p[3]&0xFFF;
		d->subclass = p[4]&0xFFF;
		d->parent =  (p[5]>>12)&0xFFF;
		d->flags = 0;
		
		printk(KERN_INFO "TID %d.\n", tid);

		i2o_report_controller_unit(c, tid);
		
		i2o_install_device(c, d);
		
		printk(KERN_INFO "  Class: ");
		
		sprintf(str, "%-21s", i2o_get_class_name(d->class));
		printk("%s", str);
		
		printk("  Subclass: 0x%03X   Flags: ",
			d->subclass);
			
		if(p[2]&(1<<0))
			printk("C");		// ConfigDialog requested
		if(p[2]&(1<<1))
			printk("M");		// Multi-user capable
		if(!(p[2]&(1<<4)))
			printk("P");		// Peer service enabled!
		if(!(p[2]&(1<<5)))
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

	msg[0]=FOUR_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1]=I2O_CMD_SYS_QUIESCE<<24|HOST_TID<<12|ADAPTER_TID;
	msg[2]=core_context;
	msg[3]=(u32)&reply_flag;

	/* Long timeout needed for quiesce if lots of devices */
	return i2o_post_wait(c, ADAPTER_TID, msg, sizeof(msg), &reply_flag, 120);
}


int i2o_clear_controller(struct i2o_controller *c)
{
	u32 msg[4];

	/* First stop external operations for this IOP */
	if(i2o_quiesce_controller(c)<0)
		printk(KERN_INFO "Unable to quiesce iop%d\n", c->unit);
	else
		printk(KERN_INFO "Iop%d quiesced\n", c->unit);

	/* Then clear the IOP */
	msg[0]=FOUR_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1]=I2O_CMD_ADAPTER_CLEAR<<24|HOST_TID<<12|ADAPTER_TID;
	msg[2]=core_context;
	msg[3]=(u32)&reply_flag;

	return i2o_post_wait(c, ADAPTER_TID, msg, sizeof(msg), &reply_flag, 10);
}


/* Reset the IOP to sane state */
static int i2o_reset_controller(struct i2o_controller *c)
{
	u32 m;
	u8 *work8;
	u32 *msg;
	long time;
	struct i2o_controller *iop;

	/* First stop external operations */
	for(iop=i2o_controller_chain; iop != NULL; iop=iop->next)
	{
		/* Quiesce is rejected on hold state */
		if(iop->status != ADAPTER_STATE_HOLD)
		{
			if(i2o_quiesce_controller(iop)<0)
				printk(KERN_INFO "Unable to quiesce iop%d\n",
				       iop->unit);
			else
				printk(KERN_DEBUG "%s quiesced\n", iop->name);
		}
	}

	/* Then reset the IOP */
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

	i2o_post_message(c,m);

	/* Wait for a reply */
	time=jiffies;

	while(work8[0]==0x01)
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
		printk(KERN_WARNING "IOP Reset rejected\n");

	return 0;
}


int i2o_status_get(struct i2o_controller *c)
{
	long time;
	u32 m;
	u32 *msg;
	u8 *status_block;

	status_block=(void *)kmalloc(88, GFP_KERNEL);
	if(status_block==NULL)
	{
		printk(KERN_ERR "StatusGet failed - no free memory.\n");
		return -ENOMEM;
	}

	m=i2o_wait_message(c, "StatusGet");
	if(m==0xFFFFFFFF)
		return -ETIMEDOUT;

	msg=(u32 *)(c->mem_offset+m);

	msg[0]=NINE_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1]=I2O_CMD_STATUS_GET<<24|HOST_TID<<12|ADAPTER_TID;
	msg[2]=core_context;
	msg[3]=0;
	msg[4]=0;
	msg[5]=0;
	msg[6]=virt_to_phys(status_block);
	msg[7]=0;	/* 64bit host FIXME */
	msg[8]=88;

	i2o_post_message(c,m);

	/* Wait for a reply */
	time=jiffies;

	while(status_block[87]!=0xFF)
	{
		if((jiffies-time)>=5*HZ)
		{
			printk(KERN_ERR "IOP get status timeout.\n");
			return -ETIMEDOUT;
		}
		schedule();
		barrier();
	}
	
	/* Ok the reply has arrived. Fill in the important stuff */
	c->status = status_block[10];
	c->i2oversion = (status_block[9]>>4)&0xFF;
	c->inbound_size = (status_block[12]|(status_block[13]<<8))*4;

	return 0;
}


int i2o_hrt_get(struct i2o_controller *c)
{
	u32 m;
	u32 *msg;

	c->hrt=kmalloc(2048, GFP_KERNEL);
	if(c->hrt==NULL)
	{
		printk(KERN_ERR "IOP init failed; no memory.\n");
		return -ENOMEM;
	}

	m=i2o_wait_message(c, "HRTGet");
	if(m==0xFFFFFFFF)
		return -ETIMEDOUT;

	msg=(u32 *)(c->mem_offset+m);

	msg[0]= SIX_WORD_MSG_SIZE| SGL_OFFSET_4;
	msg[1]= I2O_CMD_HRT_GET<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2]= core_context;
	msg[3]= 0x0;				/* Transaction context */
	msg[4]= (0xD0000000 | 2048);		/* Simple transaction , 2K */
	msg[5]= virt_to_phys(c->hrt);		/* Dump it here */

	i2o_post_message(c,m);

	barrier();

	/* Now wait for a reply */
	m=i2o_wait_reply(c, "HRTGet", 5);

	if(m==0xFFFFFFFF)
		return -ETIMEDOUT;

	msg=(u32 *)bus_to_virt(m);

	if(msg[4]>>24)
		i2o_report_status(KERN_WARNING, "i2o_core", msg);

	I2O_REPLY_WRITE32(c,m);

	return 0;
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
	long time;
	u32 m;
	u8 *workspace;
	u32 *msg;
	int i;
	int ret;

	printk(KERN_INFO "Configuring I2O controller at 0x%08X.\n",
	       (u32)c->mem_phys);

	if((ret=i2o_status_get(c)))
		return ret;

	if(c->status == ADAPTER_STATE_FAULTED) /* not likely to be seen */
	{
		printk(KERN_CRIT "i2o: iop%d has hardware fault\n",
		       c->unit);
		return -1;
	}

	/*
	 *	If the board is running, reset it - we have no idea
	 *	what kind of a mess the previous owner left it in.
	 */
	if(c->status == ADAPTER_STATE_HOLD ||
	   c->status == ADAPTER_STATE_READY ||
	   c->status == ADAPTER_STATE_OPERATIONAL ||
	   c->status == ADAPTER_STATE_FAILED)
	{
		if((ret=i2o_reset_controller(c)))
			return ret;

		if((ret=i2o_status_get(c)))
			return ret;
	}

	workspace = (void *)kmalloc(88, GFP_KERNEL);
	if(workspace==NULL)
	{
		printk(KERN_ERR "IOP initialisation failed - no free memory.\n");
		return -ENOMEM;
	}

	memset(workspace, 0, 88);

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
	msg[7]= virt_to_phys(workspace);
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
			printk(KERN_ERR "IOP outbound initialise failed.\n");
			kfree(workspace);
			return -ETIMEDOUT;
		}
		schedule();
		barrier();
	}

	kfree(workspace);

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
	}
	
	/*
	 *	The outbound queue is initialised and loaded,
	 *
	 *	Now we need the Hardware Resource Table. We must ask for
	 *	this next we can't issue random messages yet.
	 */
	ret=i2o_hrt_get(c);
	if(ret)
		return ret;

	ret=i2o_parse_hrt(c);
	if(ret)
		return ret;

	return i2o_online_controller(c);
//	i2o_report_controller_unit(c, ADAPTER_TID);
}


int i2o_lct_get(struct i2o_controller *c)
{
	u32 m;
	u32 *msg;

	m=i2o_wait_message(c, "LCTNotify");

	if(m==0xFFFFFFFF)
		return -ETIMEDOUT;

	msg=(u32 *)(c->mem_offset+m);

	c->lct = kmalloc(8192, GFP_KERNEL);
	if(c->lct==NULL)
	{
		msg[0]=FOUR_WORD_MSG_SIZE|SGL_OFFSET_0;
		msg[1]= HOST_TID<<12|ADAPTER_TID;	/* NOP */
		i2o_post_message(c,m);
		printk(KERN_ERR "No free memory for i2o controller buffer.\n");
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

	i2o_post_message(c,m);

	barrier();

	/* Now wait for a reply */
	m=i2o_wait_reply(c, "LCTNotify", 5);

	if(m==0xFFFFFFFF)
		return -ETIMEDOUT;

	msg=(u32 *)bus_to_virt(m);

	/* TODO: Check TableSize for big LCTs and send new ExecLctNotify 
	 * with bigger workspace */

	if(msg[4]>>24)
		i2o_report_status(KERN_ERR, "i2o_core", msg);

	return 0;
}


/*
 *	Bring a controller online. Needs completing for multiple controllers
 */
 
int i2o_online_controller(struct i2o_controller *c)
{
	u32 m;
	u32 *msg;
	u32 systab[32];
	u32 privmem[2];
	u32 privio[2];
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
	
	privmem[0]=c->priv_mem;		/* Private memory space base address */
	privmem[1]=c->priv_mem_size;
	privio[0]=c->priv_io;		/* Private I/O address */
	privio[1]=c->priv_io_size;
	
	m=i2o_wait_message(c, "SysTabSet");
	if(m==0xFFFFFFFF)
		return -ETIMEDOUT;
	
	/* Now we build the systab */
	msg=(u32 *)(c->mem_offset+m);
	
	msg[0] = NINE_WORD_MSG_SIZE|SGL_OFFSET_6;
	msg[1] = I2O_CMD_SYS_TAB_SET<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2] = 0;	/* Context not needed */
	msg[3] = 0;
	msg[4] = (1<<16)|(2<<12);	/* Host 1 I2O 2 */
	msg[5] = 1;			/* Segment 1 */
	
	/*
	 *	Scatter Gather List
	 */

	msg[6] = 0x54000000|48;	/* One table for now */
	msg[7] = virt_to_phys(systab);
	msg[8] = 0xD4000000|48;	/* One table for now */
	msg[9] = virt_to_phys(privmem);
/* 	msg[10] = virt_to_phys(privio); */
	
	i2o_post_message(c,m);

	barrier();
	
	/*
	 *	Now wait for a reply
	 */
	 
	 
	m=i2o_wait_reply(c, "SysTabSet", 5);
		
	if(m==0xFFFFFFFF)
		return -ETIMEDOUT;
	
	msg=(u32 *)bus_to_virt(m);
	
	if(msg[4]>>24)
	{
		i2o_report_status(KERN_ERR, "i2o_core", msg);
	}
	I2O_REPLY_WRITE32(c,m);
	
	/*
	 *	Finally we go online
	 */
	 
	m=i2o_wait_message(c, "SysEnable");
	
	if(m==0xFFFFFFFF)
		return -ETIMEDOUT;
	
	msg=(u32 *)(c->mem_offset+m);
	
	msg[0] = FOUR_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1] = I2O_CMD_SYS_ENABLE<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2] = 0;		/* Context not needed */
	msg[3] = 0;

	i2o_post_message(c,m);
	
	barrier();
	
	/*
	 *	Now wait for a reply
	 */
	 
	
	m=i2o_wait_reply(c, "SysEnable", 240);

	if(m==0xFFFFFFFF)
		return -ETIMEDOUT;
	
	msg=(u32 *)bus_to_virt(m);
	
	if(msg[4]>>24)
	{
		i2o_report_status(KERN_ERR, "i2o_core", msg);
	}
	I2O_REPLY_WRITE32(c,m);
	
	/*
	 *	Grab the LCT, see what is attached
	 */

	ret=i2o_lct_get(c);
	if(ret)
	{
		/* Maybe we should do also sthg else */
		return ret;
	}

	ret=i2o_parse_lct(c);
	if(ret)
		return ret;

	I2O_REPLY_WRITE32(c,m);
	
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

int i2o_post_this(struct i2o_controller *c, int tid, u32 *data, int len)
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
		printk(KERN_ERR "i2o: controller not responding.\n");
		return -1;
	}
	msg = bus_to_virt(c->mem_offset + m);
 	memcpy(msg, data, len);
	i2o_post_message(c,m);
	return 0;
}

/*
 *	Post a message and wait for a response flag to be set. This API will
 *	change to use wait_queue's one day
 */
 
int i2o_post_wait(struct i2o_controller *c, int tid, u32 *data, int len, int *flag, int timeout)
{
	unsigned long t=jiffies;
	
	*flag = 0;
		
	if(i2o_post_this(c, tid, data, len))
		return I2O_POST_WAIT_TIMEOUT;
		
	while(!*flag && (jiffies-t)<timeout*HZ)
	{
		schedule();
		mb();
	}

	if (*flag < 0)
		return *flag; /* DetailedStatus */

	if (*flag == 0)
		return I2O_POST_WAIT_TIMEOUT;
	
	return I2O_POST_WAIT_OK;
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
	
	return i2o_post_wait(c, tid, msg, 20, flag,2);
}

/*	Issue UTIL_PARAMS_GET or UTIL_PARAMS_SET
 *
 *	This function can be used for all UtilParamsGet/Set operations.
 *	The OperationBlock is given in opblk-buffer, 
 *	and results are returned in resblk-buffer.
 *	Note that the minimum sized resblk is 8 bytes and contains
 *	ResultCount, ErrorInfoSize, BlockStatus and BlockSize.
 */
int i2o_issue_params(int cmd, 
                struct i2o_controller *iop, int tid, int context, 
                void *opblk, int oplen, void *resblk, int reslen, 
                int *flag)
{
        u32 msg[9]; 
	u8 *res = (u8 *)resblk;
	int res_count;
	int blk_size;
	int bytes;
	int wait_status;

        msg[0] = NINE_WORD_MSG_SIZE | SGL_OFFSET_5;
        msg[1] = cmd << 24 | HOST_TID << 12 | tid; 
        msg[2] = context | 0x80000000;
        msg[3] = (u32)flag;
        msg[4] = 0;
        msg[5] = 0x54000000 | oplen;	/* OperationBlock */
        msg[6] = virt_to_bus(opblk);
        msg[7] = 0xD0000000 | reslen;	/* ResultBlock */
        msg[8] = virt_to_bus(resblk);

        wait_status = i2o_post_wait(iop, tid, msg, sizeof(msg), flag, 10);
	if (wait_status < 0)       
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
int i2o_query_scalar(struct i2o_controller *iop, int tid, int context, 
                     int group, int field, void *buf, int buflen, int *flag)
{
	u16 opblk[] = { 1, 0, I2O_PARAMS_FIELD_GET, group, 1, field };
	u8  resblk[8+buflen]; /* 8 bytes for header */
	int size;

	if (field == -1)  		/* whole group */
       		opblk[4] = -1;
              
        size = i2o_issue_params(I2O_CMD_UTIL_PARAMS_GET, iop, tid, context, 
			opblk, sizeof(opblk), resblk, sizeof(resblk), flag);
			
	if (size < 0)
		return size;	

	memcpy(buf, resblk+8, buflen);  /* cut off header */
	return buflen;
}

/*
 *	Set a scalar group value or a whole group.
 */
int i2o_set_scalar(struct i2o_controller *iop, int tid, int context, 
		   int group, int field, void *buf, int buflen, int *flag)
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

        size = i2o_issue_params(I2O_CMD_UTIL_PARAMS_SET, iop, tid, context, 
			opblk, 12+buflen, resblk, sizeof(resblk), flag);

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
int i2o_query_table(int oper,
		struct i2o_controller *iop, int tid, int context, int group,
		int fieldcount, void *ibuf, int ibuflen,
		void *resblk, int reslen, int *flag) 
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

        size = i2o_issue_params(I2O_CMD_UTIL_PARAMS_GET,iop, tid, context, 
			opblk, 10+ibuflen, resblk, reslen, flag);

	kfree(opblk);
	return size;
}

/*
 * 	Clear table group, i.e. delete all rows.
 */

int i2o_clear_table(struct i2o_controller *iop, int tid, int context,
		    int group, int *flag)
{
	u16 opblk[] = { 1, 0, I2O_PARAMS_TABLE_CLEAR, group };
	u8  resblk[32]; /* min 8 bytes for result header */

        return i2o_issue_params(I2O_CMD_UTIL_PARAMS_SET, iop, tid, context, 
			opblk, sizeof(opblk), resblk, sizeof(resblk), flag);
}

/*
 * 	Add a new row into a table group.
 *
 * 	if fieldcount==-1 then we add whole rows
 *		buf contains rowcount, keyvalues
 * 	else just specific fields are given, rest use defaults
 *  		buf contains fieldindexes, rowcount, keyvalues
 */	

int i2o_row_add_table(struct i2o_controller *iop, int tid, int context,
		    int group, int fieldcount, void *buf, int buflen,
		    int *flag)
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

        size = i2o_issue_params(I2O_CMD_UTIL_PARAMS_SET, iop, tid, context, 
			opblk, 10+buflen, resblk, sizeof(resblk), flag);

	kfree(opblk);
	return size;
}

/*
 *	Delete rows from a table group.
 */ 

int i2o_row_delete_table(struct i2o_controller *iop, int tid, int context,
		    int group, int keycount, void *keys, int keyslen,
		    int *flag)
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

        size = i2o_issue_params(I2O_CMD_UTIL_PARAMS_SET, iop, tid, context, 
			opblk, 10+keyslen, resblk, sizeof(resblk), flag);

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
	case I2O_CMD_UTIL_ACK:
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

	printk(KERN_INFO "Dumping I2O message @ %p\n", msg);
	for(i = 0; i < ((msg[0]>>16)&0xffff); i++)
		printk(KERN_INFO "\tmsg[%d] = %#10x\n", i, msg[i]);
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
	if (i2o_install_handler(&i2o_core_handler) < 0)
	{
		printk(KERN_ERR "i2o_core: Unable to install core handler.\n");
		return 0;
	}

	core_context = i2o_core_handler.context;

	/*
	 * Attach core to I2O PCI subsystem
	 */
#ifdef CONFIG_I2O_PCI_MODULE
	if(i2o_pci_core_attach(&i2o_core_functions) < 0)
		printk(KERN_INFO "No PCI I2O controllers found\n");
#endif

	return 0;
}

void cleanup_module(void)
{
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
        if (i2o_install_handler(&i2o_core_handler) < 0)
        {
                printk(KERN_ERR "i2o_core: Unable to install core handler.\n");
                return 0;
        }

        core_context = i2o_core_handler.context;
#ifdef CONFIG_I2O_PCI
	i2o_pci_init();
#endif
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
