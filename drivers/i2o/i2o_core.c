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
#include <linux/smp_lock.h>

#include <linux/bitops.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/tqueue.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <asm/semaphore.h>

#include <asm/io.h>
#include <linux/reboot.h>

#include "i2o_lan.h"

// #define DRIVERDEBUG

#ifdef DRIVERDEBUG
#define dprintk(x) printk x
#else
#define dprintk(x)
#endif

/* OSM table */
static struct i2o_handler *i2o_handlers[MAX_I2O_MODULES] = {NULL};

/* Controller list */
static struct i2o_controller *i2o_controllers[MAX_I2O_CONTROLLERS] = {NULL};
struct i2o_controller *i2o_controller_chain = NULL;
int i2o_num_controllers = 0;

/* Initiator Context for Core message */
static int core_context = 0;

/* Initialization && shutdown functions */
static void i2o_sys_init(void);
static void i2o_sys_shutdown(void);
static int i2o_clear_controller(struct i2o_controller *);
static int i2o_reboot_event(struct notifier_block *, unsigned long , void *);
static int i2o_online_controller(struct i2o_controller *);
static int i2o_init_outbound_q(struct i2o_controller *);
static int i2o_post_outbound_messages(struct i2o_controller *);
static int i2o_issue_claim(struct i2o_controller *, int, int, int, u32);

/* Reply handler */
static void i2o_core_reply(struct i2o_handler *, struct i2o_controller *,
			   struct i2o_message *);

/* Various helper functions */
static int i2o_lct_get(struct i2o_controller *);
static int i2o_lct_notify(struct i2o_controller *);
static int i2o_hrt_get(struct i2o_controller *);

static int i2o_build_sys_table(void);
static int i2o_systab_send(struct i2o_controller *c);

/* I2O core event handler */
static int i2o_core_evt(void *);
static int evt_pid;
static int evt_running;

/* Dynamic LCT update handler */
static int i2o_dyn_lct(void *);

void i2o_report_controller_unit(struct i2o_controller *, struct i2o_device *);

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

/* OSM descriptor handler */ 
static struct i2o_handler i2o_core_handler =
{
	(void *)i2o_core_reply,
	NULL,
	NULL,
	NULL,
	"I2O core layer",
	0,
	0
};


/*
 * Used when queing a reply to be handled later
 */
struct reply_info
{
	struct i2o_controller *iop;
	u32 msg[MSG_FRAME_SIZE];
};
static struct reply_info evt_reply;
static struct reply_info events[I2O_EVT_Q_LEN];
static int evt_in = 0;
static int evt_out = 0;
static int evt_q_len = 0;
#define MODINC(x,y) (x = x++ % y)

/*
 * I2O configuration spinlock. This isnt a big deal for contention
 * so we have one only
 */
static spinlock_t i2o_configuration_lock = SPIN_LOCK_UNLOCKED;

/* 
 * Event spinlock.  Used to keep event queue sane and from
 * handling multiple events simultaneously.
 */
static spinlock_t i2o_evt_lock = SPIN_LOCK_UNLOCKED;

/*
 * Semaphore used to syncrhonize event handling thread with 
 * interrupt handler.
 */
DECLARE_MUTEX(evt_sem);
DECLARE_WAIT_QUEUE_HEAD(evt_wait);

static struct notifier_block i2o_reboot_notifier =
{
        i2o_reboot_event,
        NULL,
        0
};


/*
 * I2O Core reply handler
 */
void i2o_core_reply(struct i2o_handler *h, struct i2o_controller *c,
		    struct i2o_message *m)
{
	u32 *msg=(u32 *)m;
	u32 status;
	u32 context = msg[2];

#if 0
	i2o_report_status(KERN_INFO, "i2o_core", msg);
#endif

	if (msg[0] & (1<<13)) // Fail bit is set
	{
		printk(KERN_ERR "%s: Failed to process the msg:\n",c->name);
		printk(KERN_ERR "  Cmd = 0x%02X, InitiatorTid = %d, TargetTid =% d\n",    
			(msg[1] >> 24) & 0xFF, (msg[1] >> 12) & 0xFFF, msg[1] & 0xFFF); 
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
			i2o_report_status(KERN_INFO, "i2o_core: post_wait reply", msg);
			status = -(msg[4] & 0xFFFF);
		}
		else
			status = I2O_POST_WAIT_OK;
	
		i2o_post_wait_complete(context, status);
		return;
	}

	if(m->function == I2O_CMD_UTIL_EVT_REGISTER)
	{
		memcpy(events[evt_in].msg, msg, MSG_FRAME_SIZE);
		events[evt_in].iop = c;

		spin_lock(&i2o_evt_lock);
		MODINC(evt_in, I2O_EVT_Q_LEN);
		if(evt_q_len == I2O_EVT_Q_LEN)
			MODINC(evt_out, I2O_EVT_Q_LEN);
		else
			evt_q_len++;
		spin_unlock(&i2o_evt_lock);

		up(&evt_sem);
		wake_up_interruptible(&evt_wait);
		return;
	}

	if(m->function == I2O_CMD_LCT_NOTIFY)
	{
		up(&c->lct_sem);
		return;
	}

	/*
	 * If this happens, we want to dump the message to the syslog so
	 * it can be sent back to the card manufacturer by the end user
	 * to aid in debugging.
	 * 
	 */
	printk(KERN_WARNING "%s: Unsolicited message reply sent to core!"
			"Message dumped to syslog\n", 
			c->name);
	i2o_dump_message(msg);

	return;
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
 *	Each I2O controller has a chain of devices on it.
 * Each device has a pointer to it's LCT entry to be used
 * for fun purposes.
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

	for(i = 0; i < I2O_MAX_MANAGERS; i++)
		d->managers[i] = NULL;

	spin_unlock(&i2o_configuration_lock);
	return 0;
}

/* we need this version to call out of i2o_delete_controller */

int __i2o_delete_device(struct i2o_device *d)
{
	struct i2o_device **p;
	int i;

	p=&(d->controller->devices);

	/*
	 *	Hey we have a driver!
	 * Check to see if the driver wants us to notify it of 
	 * device deletion. If it doesn't we assume that it
	 * is unsafe to delete a device with an owner and 
	 * fail.
	 */
	if(d->owner)
	{
		if(d->owner->dev_del_notify)
		{
			dprintk((KERN_INFO "Device has owner, notifying\n"));
			d->owner->dev_del_notify(d->controller, d);
			if(d->owner)
			{
				printk(KERN_WARNING 
					"Driver \"%s\" did not release device!\n", d->owner->name);
				return -EBUSY;
			}
		}
		else
			return -EBUSY;
	}

	/*
	 * Tell any other users who are talking to this device
	 * that it's going away.  We assume that everything works.
	 */
	for(i=0; i < I2O_MAX_MANAGERS; i++)
	{
		if(d->managers[i] && d->managers[i]->dev_del_notify)
			d->managers[i]->dev_del_notify(d->controller, d);
	}
	 			
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

	/*
	 *	Seek, locate
	 */

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
			c->devices = NULL;
			c->next=i2o_controller_chain;
			i2o_controller_chain=c;
			c->unit = i;
			c->page_frame = NULL;
			c->hrt = NULL;
			c->lct = NULL;
			c->dlct = (i2o_lct*)kmalloc(8192, GFP_KERNEL);
			c->status_block = NULL;
			sprintf(c->name, "i2o/iop%d", i);
			i2o_num_controllers++;
			init_MUTEX_LOCKED(&c->lct_sem);
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
	char name[16];
	int stat;

	dprintk((KERN_INFO "Deleting controller iop%d\n", c->unit));

	/*
	 * Clear event registration as this can cause weird behavior
	 */
	if(c->status_block->iop_state == ADAPTER_STATE_OPERATIONAL)
		i2o_event_register(c, core_context, 0, 0, 0);

	spin_lock(&i2o_configuration_lock);
	if((users=atomic_read(&c->users)))
	{
		dprintk((KERN_INFO "I2O: %d users for controller iop%d\n", users,
					c->unit));
		spin_unlock(&i2o_configuration_lock);
		return -EBUSY;
	}
	while(c->devices)
	{
		if(__i2o_delete_device(c->devices)<0)
		{
			/* Shouldnt happen */
			c->bus_disable(c);
			spin_unlock(&i2o_configuration_lock);
			return -EBUSY;
		}
	}

	/*
	 * If this is shutdown time, the thread's already been killed
	 */
	if(c->lct_running) {
		stat = kill_proc(c->lct_pid, SIGTERM, 1);
		if(!stat) {
			int count = 10 * 100;
			while(c->lct_running && --count) {
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(1);
			}
		
			if(!count)
				printk(KERN_ERR 
					"%s: LCT thread still running!\n", 
					c->name);
		}
	}

	p=&i2o_controller_chain;

	while(*p)
	{
		if(*p==c)
		{
			/* Ask the IOP to switch to HOLD state */
			if (i2o_clear_controller(c) < 0)
				printk(KERN_ERR "Unable to clear iop%d\n", c->unit);

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
			if(c->status_block)
				kfree(c->status_block);
			if(c->dlct)
				kfree(c->dlct);

			i2o_controllers[c->unit]=NULL;
			memcpy(name, c->name, strlen(c->name)+1);
			kfree(c);
			dprintk((KERN_INFO "%s: Deleted from controller chain.\n", name));
			
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
 * Claim a device for use by an OSM
 */
int i2o_claim_device(struct i2o_device *d, struct i2o_handler *h)
{
	spin_lock(&i2o_configuration_lock);
	if(d->owner)
	{
		printk(KERN_INFO "issue claim called, but dev as owner!");
		spin_unlock(&i2o_configuration_lock);
		return -EBUSY;
	}

	if(i2o_issue_claim(d->controller,d->lct_data.tid, h->context, 1,
		 I2O_CLAIM_PRIMARY))
	{
		spin_unlock(&i2o_configuration_lock);
		return -EBUSY;
	}

	d->owner=h;
	spin_unlock(&i2o_configuration_lock);
	return 0;
}

/*
 * Release a device that the OS is using
 */
int i2o_release_device(struct i2o_device *d, struct i2o_handler *h)
{
	int err = 0;

	spin_lock(&i2o_configuration_lock);
	if(d->owner != h)
	{
		spin_unlock(&i2o_configuration_lock);
		return -ENOENT;
	}	

	if(i2o_issue_claim(d->controller, d->lct_data.tid, h->context, 0,
			I2O_CLAIM_PRIMARY))
	{
		err = -ENXIO;
	}

	d->owner = NULL;

	spin_unlock(&i2o_configuration_lock);
	return err;
}

/*
 * Called by OSMs to let the core know that they want to be
 * notified if the given device is deleted from the system.
 */
int i2o_device_notify_on(struct i2o_device *d, struct i2o_handler *h)
{
	int i;

	if(d->num_managers == I2O_MAX_MANAGERS)
		return -ENOSPC;

	for(i = 0; i < I2O_MAX_MANAGERS; i++)
	{
		if(!d->managers[i])
		{
			d->managers[i] = h;
			break;
		}
	}
	
	d->num_managers++;
	
	return 0;
}

/*
 * Called by OSMs to let the core know that they no longer
 * are interested in the fate of the given device.
 */
int i2o_device_notify_off(struct i2o_device *d, struct i2o_handler *h)
{
	int i;

	for(i=0; i < I2O_MAX_MANAGERS; i++)
	{
		if(d->managers[i] == h)
		{
			d->managers[i] = NULL;
			d->num_managers--;
			return 0;
		}
	}

	return -ENOENT;
}

/*
 * Event registration API
 */
int i2o_event_register(struct i2o_controller *c, u32 tid, 
		u32 init_context, u32 tr_context, u32 evt_mask)
{
	u32 msg[5];	// Not performance critical, so we just 
					// i2o_post_this it instead of building it
					// in IOP memory
	
	msg[0] = FIVE_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1] = I2O_CMD_UTIL_EVT_REGISTER<<24 | HOST_TID<<12 | tid;
	msg[2] = (u32)init_context;
	msg[3] = (u32)tr_context;
	msg[4] = evt_mask;

	return i2o_post_this(c, msg, sizeof(msg));
}

/*
 * Event ack API
 *
 * We just take a pointer to the original UTIL_EVENT_REGISTER reply
 * message and change the function code since that's what spec
 * describes an EventAck message looking like.
 */
int i2o_event_ack(struct i2o_controller *c, u32 *msg)
{
	struct i2o_message *m = (struct i2o_message *)msg;

	m->function = I2O_CMD_UTIL_EVT_ACK;

	return i2o_post_wait(c, msg, m->size * 4, 2);
}

/*
 * Core event handler.  Runs as a separate thread and is woken
 * up whenever there is an Executive class event.
 */
static int i2o_core_evt(void *foo)
{
	struct reply_info reply_data;
	struct reply_info *reply = &reply_data;
	u32 *msg = reply->msg;
	struct i2o_controller *c = NULL;
	int flags;

	lock_kernel();
	exit_files(current);
	daemonize();
	unlock_kernel();

	strcpy(current->comm, "i2oevtd");
	evt_running = 1;

	while(1)
	{
		down_interruptible(&evt_sem);
		if(signal_pending(current))
		{
			dprintk((KERN_INFO "I2O event thread dead\n"));
			evt_running = 0;
			return 0;	
		}

		/* 
		 * Copy the data out of the queue so that we don't have to lock
		 * around the whole function and just around the qlen update
		 */
		spin_lock_irqsave(&i2o_evt_lock, flags);
		memcpy(reply, &events[evt_out], sizeof(struct reply_info));
		MODINC(evt_out, I2O_EVT_Q_LEN);
		evt_q_len--;
		spin_unlock_irqrestore(&i2o_evt_lock, flags);
	
		c = reply->iop;
	 	dprintk((KERN_INFO "I2O IRTOS EVENT: iop%d, event %#10x\n", c->unit, msg[4]));

		/* 
		 * We do not attempt to delete/quiesce/etc. the controller if
		 * some sort of error indidication occurs.  We may want to do
		 * so in the future, but for now we just let the user deal with 
		 * it.  One reason for this is that what to do with an error
		 * or when to send what ærror is not really agreed on, so
		 * we get errors that may not be fatal but just look like they
		 * are...so let the user deal with it.
		 */
		switch(msg[4])
		{
			case I2O_EVT_IND_EXEC_RESOURCE_LIMITS:
				printk(KERN_ERR "iop%d: Out of resources\n", c->unit);
				break;

			case I2O_EVT_IND_EXEC_POWER_FAIL:
				printk(KERN_ERR "iop%d: Power failure\n", c->unit);
				break;

			case I2O_EVT_IND_EXEC_HW_FAIL:
			{
				char *fail[] = 
					{ 
						"Unknown Error",
						"Power Lost",
						"Code Violation",
						"Parity Error",
						"Code Execution Exception",
						"Watchdog Timer Expired" 
					};

				if(msg[5] <= 6)
					printk(KERN_ERR "%s: Hardware Failure: %s\n", 
						c->name, fail[msg[5]]);
				else
					printk(KERN_ERR "%s: Unknown Hardware Failure\n", c->name);

				break;
			}

			/*
		 	 * New device created
		 	 * - Create a new i2o_device entry
		 	 * - Inform all interested drivers about this device's existence
		 	 */
			case I2O_EVT_IND_EXEC_NEW_LCT_ENTRY:
			{
				struct i2o_device *d = (struct i2o_device *)
					kmalloc(sizeof(struct i2o_device), GFP_KERNEL);
				int i;

				memcpy(&d->lct_data, &msg[5], sizeof(i2o_lct_entry));
	
				d->next = NULL;
				d->controller = c;
				d->flags = 0;
	
				i2o_report_controller_unit(c, d);
				i2o_install_device(c,d);
	
				for(i = 0; i < MAX_I2O_MODULES; i++)
				{
					if(i2o_handlers[i] && 
						i2o_handlers[i]->new_dev_notify &&
						(i2o_handlers[i]->class&d->lct_data.class_id))
						i2o_handlers[i]->new_dev_notify(c,d);
				}
			
				break;
			}
	
			/*
 		 	 * LCT entry for a device has been modified, so update it
		 	 * internally.
		 	 */
			case I2O_EVT_IND_EXEC_MODIFIED_LCT:
			{
				struct i2o_device *d;
				i2o_lct_entry *new_lct = (i2o_lct_entry *)&msg[5];

				for(d = c->devices; d; d = d->next)
				{
					if(d->lct_data.tid == new_lct->tid)
					{
						memcpy(&d->lct_data, new_lct, sizeof(i2o_lct_entry));
						break;
					}
				}
				break;
			}
	
			case I2O_EVT_IND_CONFIGURATION_FLAG:
				printk(KERN_WARNING "%s requires user configuration\n", c->name);
				break;
	
			case I2O_EVT_IND_GENERAL_WARNING:
				printk(KERN_WARNING "%s: Warning notification received!"
					"Check configuration for errors!\n", c->name);
				break;
	
			default:
				printk(KERN_WARNING "%s: Unknown event...check config\n", c->name);
				break;
		}
	}

	return 0;
}

/*
 * Dynamic LCT update.  This compares the LCT with the currently
 * installed devices to check for device deletions..this needed b/c there
 * is no DELETED_LCT_ENTRY EventIndicator for the Executive class so
 * we can't just have the event handler do this...annoying
 *
 * This is a hole in the spec that will hopefully be fixed someday.
 */
static int i2o_dyn_lct(void *foo)
{
	struct i2o_controller *c = (struct i2o_controller *)foo;
	struct i2o_device *d = NULL;
	struct i2o_device *d1 = NULL;
	int i = 0;
	int found = 0;
	int entries;
	void *tmp;
	char name[16];

	lock_kernel();
	exit_files(current);
	daemonize();
	unlock_kernel();

	sprintf(name, "iop%d_lctd", c->unit);
	strcpy(current->comm, name);	
	
	c->lct_running = 1;

	while(1)
	{
		down_interruptible(&c->lct_sem);
		if(signal_pending(current))
		{
			dprintk((KERN_ERR "%s: LCT thread dead\n", c->name));
			c->lct_running = 0;
			return 0;
		}

		entries = c->dlct->table_size;
		entries -= 3;
		entries /= 9;

		dprintk((KERN_INFO "I2O: Dynamic LCT Update\n"));
		dprintk((KERN_INFO "I2O: Dynamic LCT contains %d entries\n", entries));

		if(!entries)
		{
			printk(KERN_INFO "iop%d: Empty LCT???\n", c->unit);
			continue;
		}

		/*
		 * Loop through all the devices on the IOP looking for their
		 * LCT data in the LCT.  We assume that TIDs are not repeated.
		 * as that is the only way to really tell.  It's been confirmed
		 * by the IRTOS vendor(s?) that TIDs are not reused until they 
		 * wrap arround(4096), and I doubt a system will up long enough
		 * to create/delete that many devices.
		 */
		for(d = c->devices; d; )
		{
			found = 0;
			d1 = d->next;
			
			for(i = 0; i < entries; i++) 
			{ 
				if(d->lct_data.tid == c->dlct->lct_entry[i].tid) 
				{ 
					found = 1; 
					break; 
				} 
			} 
			if(!found) 
			{
				dprintk((KERN_INFO "Deleted device!\n")); 
				i2o_delete_device(d); 
			} 
			d = d1; 
		}

		/* 
		 * Tell LCT to renotify us next time there is a change
	 	 */
		i2o_lct_notify(c);

		/*
		 * Copy new LCT into public LCT
		 *
		 * Possible race if someone is reading LCT while  we are copying 
		 * over it. If this happens, we'll fix it then. but I doubt that
		 * the LCT will get updated often enough or will get read by
		 * a user often enough to worry.
		 */
		if(c->lct->table_size < c->dlct->table_size)
		{
			tmp = c->lct;
			c->lct = kmalloc(c->dlct->table_size<<2, GFP_KERNEL);
			if(!c->lct)
			{
				printk(KERN_ERR "%s: No memory for LCT!\n", c->name);
				c->lct = tmp;
				continue;
			}
			kfree(tmp);
		}
		memcpy(c->lct, c->dlct, c->dlct->table_size<<2);
	}

	return 0;
}

/*
 *	This is called by the bus specific driver layer when an interrupt
 *	or poll of this card interface is desired.
 */
 
void i2o_run_queue(struct i2o_controller *c)
{
	struct i2o_message *m;
	u32 mv;
	u32 *msg;
	int count = 0;

	/*
	 * Old 960 steppings had a bug in the I2O unit that caused
	 * the queue to appear empty when it wasn't.
	 */
	if((mv=I2O_REPLY_READ32(c))==0xFFFFFFFF)
		mv=I2O_REPLY_READ32(c);

	while(mv!=0xFFFFFFFF)
	{
		struct i2o_handler *i;
		m=(struct i2o_message *)bus_to_virt(mv);
		msg=(u32*)m;

		count++;

		/*
		 *	Temporary Debugging
		 */
		if(m->function==0x15)
			printk(KERN_ERR "%s: UTFR!\n", c->name);

		i=i2o_handlers[m->initiator_context&(MAX_I2O_MODULES-1)];
		if(i && i->reply)
			i->reply(i,c,m);
		else
		{
			printk(KERN_WARNING "I2O: Spurious reply to handler %d\n", 
				m->initiator_context&(MAX_I2O_MODULES-1));
		}	
	 	i2o_flush_reply(c,mv);
		mb();

		/* That 960 bug again... */	
		if((mv=I2O_REPLY_READ32(c))==0xFFFFFFFF)
			mv=I2O_REPLY_READ32(c);

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
			dprintk((KERN_ERR "%s: Timeout waiting for message frame to send %s.\n", 
				c->name, why));
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
			dprintk((KERN_ERR "%s: timeout waiting for %s reply.\n",
				c->name, why));
			return 0xFFFFFFFF;
		}
		schedule();
	}
	return m;
}
	
/*
 *	 Dump the information block associated with a given unit (TID)
 */
 
void i2o_report_controller_unit(struct i2o_controller *c, struct i2o_device *d)
{
	char buf[64];
	char str[22];
	int ret;
	int unit = d->lct_data.tid;
	
	printk(KERN_INFO "Target ID %d.\n", unit);

	if((ret=i2o_query_scalar(c, unit, 0xF100, 3, buf, 16))>=0)
	{
		buf[16]=0;
		printk(KERN_INFO "     Vendor: %s\n", buf);
	}
	if((ret=i2o_query_scalar(c, unit, 0xF100, 4, buf, 16))>=0)
	{

		buf[16]=0;
		printk(KERN_INFO "     Device: %s\n", buf);
	}
#if 0
	if(i2o_query_scalar(c, unit, 0xF100, 5, buf, 16)>=0)
	{
		buf[16]=0;
		printk(KERN_INFO "     Description: %s\n", buf);
	}
#endif	
	if((ret=i2o_query_scalar(c, unit, 0xF100, 6, buf, 8))>=0)
	{
		buf[8]=0;
		printk(KERN_INFO "        Rev: %s\n", buf);
	}

	printk(KERN_INFO "    Class: ");
	sprintf(str, "%-21s", i2o_get_class_name(d->lct_data.class_id));
	printk("%s\n", str);
		
	printk(KERN_INFO "  Subclass: 0x%04X\n", d->lct_data.sub_class);
	printk(KERN_INFO "     Flags: ");
		
	if(d->lct_data.device_flags&(1<<0))
		printk("C");		// ConfigDialog requested
	if(d->lct_data.device_flags&(1<<1))
		printk("U");		// Multi-user capable
	if(!(d->lct_data.device_flags&(1<<4)))
		printk("P");		// Peer service enabled!
	if(!(d->lct_data.device_flags&(1<<5)))
		printk("M");		// Mgmt service enabled!
	printk("\n");
			
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
		printk(KERN_ERR "%s: HRT table for controller is too new a version.\n",
			c->name);
		return -1;
	}
		
	count=p[0]|(p[1]<<8);
	length = p[2];
	
	printk(KERN_INFO "%s: HRT has %d entries of %d bytes each.\n",
		c->name, count, length<<2);

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
	struct i2o_device *d;
	i2o_lct *lct = c->lct;

	if (lct == NULL) {
		printk(KERN_ERR "%s: LCT is empty???\n", c->name);
		return -1;
	}

	max = lct->table_size;
	max -= 3;
	max /= 9;
	
	printk(KERN_INFO "%s: LCT has %d entries.\n", c->name, max);
	
	if(lct->iop_flags&(1<<0))
		printk(KERN_WARNING "%s: Configuration dialog desired.\n", c->name);
		
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

		memcpy(&d->lct_data, &lct->lct_entry[i], sizeof(i2o_lct_entry));

		d->flags = 0;
		tid = d->lct_data.tid;
		
		i2o_report_controller_unit(c, d);
		
		i2o_install_device(c, d);
	}
	return 0;
}


/* 
 * Quiesce IOP. Causes IOP to make external operation quiescend. 
 * Internal operation of the IOP continues normally.
 */
int i2o_quiesce_controller(struct i2o_controller *c)
{
	u32 msg[4];
	int ret;

	i2o_status_get(c);

	/* SysQuiesce discarded if IOP not in READY or OPERATIONAL state */

	if ((c->status_block->iop_state != ADAPTER_STATE_READY) &&
		(c->status_block->iop_state != ADAPTER_STATE_OPERATIONAL))
	{
		return 0;
	}

	msg[0] = FOUR_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1] = I2O_CMD_SYS_QUIESCE<<24|HOST_TID<<12|ADAPTER_TID;
	msg[3] = 0;

	/* Long timeout needed for quiesce if lots of devices */

	if ((ret = i2o_post_wait(c, msg, sizeof(msg), 240)))
		printk(KERN_INFO "%s: Unable to quiesce (status=%#10x).\n",
			c->name, ret);
	else
		dprintk((KERN_INFO "%s: Quiesced.\n", c->name));

	i2o_status_get(c); // Reread the Status Block

   return ret;

}

/*
 * Enable IOP. Allows the IOP to resume external operations.
 */
int i2o_enable_controller(struct i2o_controller *c)
{
	u32 msg[4];
	int ret;

	i2o_status_get(c);
	
	/* Enable only allowed on READY state */	
	if(c->status_block->iop_state != ADAPTER_STATE_READY)
		return -EINVAL;

	msg[0]=FOUR_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1]=I2O_CMD_SYS_ENABLE<<24|HOST_TID<<12|ADAPTER_TID;

   /* How long of a timeout do we need? */

	if ((ret = i2o_post_wait(c, msg, sizeof(msg), 240)))
		printk(KERN_ERR "%s: Could not enable (status=%#10x).\n",
			c->name, ret);
	else
		dprintk((KERN_INFO "%s: Enabled.\n", c->name));

	i2o_status_get(c);

	return ret;
}

/*
 * Clear an IOP to HOLD state, ie. terminate external operations, clear all
 * input queues and prepare for a system restart. IOP's internal operation
 * continues normally and the outbound queue is alive.
 * IOP is not expected to rebuild its LCT.
 */
int i2o_clear_controller(struct i2o_controller *c)
{
	struct i2o_controller *iop;
	u32 msg[4];
	int ret;

	/* Quiesce all IOPs first */

	for (iop = i2o_controller_chain; iop; iop = iop->next)
		i2o_quiesce_controller(iop);

	msg[0]=FOUR_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1]=I2O_CMD_ADAPTER_CLEAR<<24|HOST_TID<<12|ADAPTER_TID;
	msg[3]=0;

	if ((ret=i2o_post_wait(c, msg, sizeof(msg), 30)))
		printk(KERN_INFO "%s: Unable to clear (status=%#10x).\n",
			c->name, ret);
	else
		dprintk((KERN_INFO "%s: Cleared.\n",c->name));

	i2o_status_get(c);

	/* Enable other IOPs */

	for (iop = i2o_controller_chain; iop; iop = iop->next)
		if (iop != c)
			i2o_enable_controller(iop);

	return ret;
}


/*
 * Reset the IOP into INIT state and wait until IOP gets into RESET state.
 * Terminate all external operations, clear IOP's inbound and outbound
 * queues, terminate all DDMs, and reload the IOP's operating environment
 * and all local DDMs. IOP rebuilds its LCT.
 */
static int i2o_reset_controller(struct i2o_controller *c)
{
	struct i2o_controller *iop;
	u32 m;
	u8 *status;
	u32 *msg;
	long time;

	/* Quiesce all IOPs first */

	for (iop = i2o_controller_chain; iop; iop = iop->next)
		i2o_quiesce_controller(iop);

	/* Get a message */
	m=i2o_wait_message(c, "AdapterReset");
	if(m==0xFFFFFFFF)	
		return -ETIMEDOUT;
	msg=(u32 *)(c->mem_offset+m);
	
	status=(void *)kmalloc(4, GFP_KERNEL);
	if(status==NULL) {
		printk(KERN_ERR "IOP reset failed - no free memory.\n");
		return -ENOMEM;
	}
	memset(status, 0, 4);
	
	msg[0]=EIGHT_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1]=I2O_CMD_ADAPTER_RESET<<24|HOST_TID<<12|ADAPTER_TID;
	msg[2]=core_context;
	msg[3]=0;
	msg[4]=0;
	msg[5]=0;
	msg[6]=virt_to_phys(status);
	msg[7]=0;	/* 64bit host FIXME */

	i2o_post_message(c,m);

	/* Wait for a reply */
	time=jiffies;
	while(status[0]==0)
	{
		if((jiffies-time)>=20*HZ)
		{
			printk(KERN_ERR "IOP reset timeout.\n");
			kfree(status);
			return -ETIMEDOUT;
		}
		schedule();
		barrier();
	}

	if (status[0]==0x01)
	{ 
		/* 
		 * Once the reset is sent, the IOP goes into the INIT state 
		 * which is indeterminate.  We need to wait until the IOP 
		 * has rebooted before we can let the system talk to 
		 * it. We read the inbound Free_List until a message is 
		 * available.  If we can't read one in the given ammount of 
		 * time, we assume the IOP could not reboot properly.  
		 */ 

		dprintk((KERN_INFO "Reset succeeded...waiting for reboot\n")); 

		time = jiffies; 
		m = I2O_POST_READ32(c); 
		while(m == 0XFFFFFFFF) 
		{ 
			if((jiffies-time) >= 30*HZ)
			{
				printk(KERN_ERR "%s: Timeout waiting for IOP reset.\n", 
						c->name); 
				return -ETIMEDOUT; 
			} 
			schedule(); 
			barrier(); 
			m = I2O_POST_READ32(c); 
		} 

		i2o_flush_reply(c,m);

		dprintk((KERN_INFO "%s: Reset completed.\n", c->name));
	}

	/* If IopReset was rejected or didn't perform reset, try IopClear */

	i2o_status_get(c);
	if (status[0] == 0x02 || c->status_block->iop_state != ADAPTER_STATE_RESET)
	{
		printk(KERN_WARNING "%s: Reset rejected, trying to clear\n",c->name);
		i2o_clear_controller(c);
	}

	/* Enable other IOPs */

	for (iop = i2o_controller_chain; iop; iop = iop->next)
		if (iop != c)
			i2o_enable_controller(iop);

	kfree(status);
	return 0;
}


/*
 * Get the status block for the IOP
 */
int i2o_status_get(struct i2o_controller *c)
{
	long time;
	u32 m;
	u32 *msg;
	u8 *status_block;

	if (c->status_block == NULL) 
	{
		c->status_block = (i2o_status_block *)
			kmalloc(sizeof(i2o_status_block),GFP_KERNEL);
		if (c->status_block == NULL)
		{
			printk(KERN_CRIT "%s: Get Status Block failed; Out of memory.\n",
				c->name);
			return -ENOMEM;
		}
	}

	status_block = (u8*)c->status_block;
	memset(c->status_block,0,sizeof(i2o_status_block));
	
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
	msg[6]=virt_to_phys(c->status_block);
	msg[7]=0;   /* 64bit host FIXME */
	msg[8]=sizeof(i2o_status_block); /* always 88 bytes */

	i2o_post_message(c,m);

	/* Wait for a reply */

	time=jiffies;
	while(status_block[87]!=0xFF)
	{
		if((jiffies-time)>=5*HZ)
		{
			printk(KERN_ERR "%s: Get status timeout.\n",c->name);
			return -ETIMEDOUT;
		}
		schedule();
		barrier();
	}

	/* Ok the reply has arrived. Fill in the important stuff */
	c->inbound_size = (status_block[12]|(status_block[13]<<8))*4;

#ifdef DRIVERDEBUG
	printk(KERN_INFO "%s: State = ", c->name);
	switch (c->status_block->iop_state) {
		case 0x01:  
			printk("INIT\n");
			break;
		case 0x02:
			printk("RESET\n");
			break;
		case 0x04:
			printk("HOLD\n");
			break;
		case 0x05:
			printk("READY\n");
			break;
		case 0x08:
			printk("OPERATIONAL\n");
			break;
		case 0x10:
			printk("FAILED\n");
			break;
		case 0x11:
			printk("FAULTED\n");
			break;
		default: 
			printk("%x (unknown !!)\n",c->status_block->iop_state);
}     
#endif   

	return 0;
}

/*
 * Get the Hardware Resource Table for the device.
 * The HRT contains information about possible hidden devices
 * but is mostly useless to us 
 */
int i2o_hrt_get(struct i2o_controller *c)
{
	u32 msg[6];
	int ret, size = sizeof(i2o_hrt);

	/* Read first just the header to figure out the real size */

	do  {
		if (c->hrt == NULL) {
			c->hrt=kmalloc(size, GFP_KERNEL);
			if (c->hrt == NULL) {
				printk(KERN_CRIT "%s: Hrt Get failed; Out of memory.\n", c->name);
				return -ENOMEM;
			}
		}

		msg[0]= SIX_WORD_MSG_SIZE| SGL_OFFSET_4;
		msg[1]= I2O_CMD_HRT_GET<<24 | HOST_TID<<12 | ADAPTER_TID;
		msg[3]= 0;
		msg[4]= (0xD0000000 | size);	/* Simple transaction */
		msg[5]= virt_to_phys(c->hrt);	/* Dump it here */

		if ((ret = i2o_post_wait(c, msg, sizeof(msg), 20))) {
			printk(KERN_ERR "%s: Unable to get HRT (status=%#10x)\n",
				c->name, ret);	
			return ret;
		}

		if (c->hrt->num_entries * c->hrt->entry_len << 2 > size) {
			size = c->hrt->num_entries * c->hrt->entry_len << 2;
			kfree(c->hrt);
			c->hrt = NULL;
		}
	} while (c->hrt == NULL);

	i2o_parse_hrt(c); // just for debugging

	return 0;
}

/*
 * Send the I2O System Table to the specified IOP
 *
 * The system table contains information about all the IOPs in the
 * system.  It is build and then sent to each IOP so that IOPs can
 * establish connections between each other.
 *
 */
static int i2o_systab_send(struct i2o_controller *iop)
{
	u32 msg[12];
	u32 privmem[2];
	u32 privio[2];
	int ret;

	privmem[0] = iop->status_block->current_mem_base;
	privmem[1] = iop->status_block->current_mem_size;
	privio[0] = iop->status_block->current_io_base;
	privio[1] = iop->status_block->current_io_size;

	msg[0] = I2O_MESSAGE_SIZE(12) | SGL_OFFSET_6;
	msg[1] = I2O_CMD_SYS_TAB_SET<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[3] = 0;
	msg[4] = (0<<16) | ((iop->unit+2) << 12); /* Host 0 IOP ID (unit + 2) */
	msg[5] = 0;                               /* Segment 0 */

	/* 
 	 * Provide three SGL-elements:
 	 * System table (SysTab), Private memory space declaration and 
 	 * Private i/o space declaration  
 	 */
	msg[6] = 0x54000000 | sys_tbl_len;
	msg[7] = virt_to_phys(sys_tbl);
	msg[8] = 0x54000000 | 0;
	msg[9] = virt_to_phys(privmem);
	msg[10] = 0xD4000000 | 0;
	msg[11] = virt_to_phys(privio);

	if ((ret=i2o_post_wait(iop, msg, sizeof(msg), 120)))
		printk(KERN_INFO "%s: Unable to set SysTab (status=%#10x).\n", 
			iop->name, ret);
	else
		dprintk((KERN_INFO "%s: SysTab set.\n", iop->name));

	return ret;	

 }

/*
 * Initialize I2O subsystem.
 */
static void __init i2o_sys_init()
{
	struct i2o_controller *iop, *niop = NULL;

	printk(KERN_INFO "Activating I2O controllers\n");
	printk(KERN_INFO "This may take a few minutes if there are many devices\n");
	
	/* In INIT state, Activate IOPs */
	for (iop = i2o_controller_chain; iop; iop = niop) {
		dprintk((KERN_INFO "Calling i2o_activate_controller for %s\n", 
			iop->name));
		niop = iop->next;
		i2o_activate_controller(iop);
	}

	/* Active IOPs in HOLD state */

rebuild_sys_tab:
	if (i2o_controller_chain == NULL)
		return;

	/*
	 * If build_sys_table fails, we kill everything and bail
	 * as we can't init the IOPs w/o a system table
	 */	
	dprintk((KERN_INFO "calling i2o_build_sys_table\n"));
	if (i2o_build_sys_table() < 0) {
		i2o_sys_shutdown();
		return;
	}

	/* If IOP don't get online, we need to rebuild the System table */
	for (iop = i2o_controller_chain; iop; iop = niop) {
		niop = iop->next;
		dprintk((KERN_INFO "Calling i2o_online_controller for %s\n", iop->name));
		if (i2o_online_controller(iop) < 0)
			goto rebuild_sys_tab;
	}
	
	/* Active IOPs now in OPERATIONAL state */

	/*
	 * Register for status updates from all IOPs
	 */
	for(iop = i2o_controller_chain; iop; iop=iop->next) {

		/* Create a kernel thread to deal with dynamic LCT updates */
		iop->lct_pid = kernel_thread(i2o_dyn_lct, iop, CLONE_SIGHAND);
	
		/* Update change ind on DLCT */
		iop->dlct->change_ind = iop->lct->change_ind;

		/* Start dynamic LCT updates */
		i2o_lct_notify(iop);

		/* Register for all events from IRTOS */
		i2o_event_register(iop, core_context, 0, 0, 0xFFFFFFFF);
	}
}

/*
 * Shutdown I2O system
 */
static void i2o_sys_shutdown(void)
{
	struct i2o_controller *iop, *niop;

	/* Delete all IOPs from the controller chain */
	/* that will reset all IOPs too */

	for (iop = i2o_controller_chain; iop; iop = niop) {
		niop = iop->next;
		i2o_delete_controller(iop);
	}
}

/*
 *	Bring an I2O controller into HOLD state. See the spec.
 */
int i2o_activate_controller(struct i2o_controller *iop)
{
	/* In INIT state, Wait Inbound Q to initilaize (in i2o_status_get) */
	/* In READY state, Get status */

	if (i2o_status_get(iop) < 0) {
		printk(KERN_INFO "Unable to obtain status of IOP, attempting a reset.\n");
		i2o_reset_controller(iop);
		if (i2o_status_get(iop) < 0) {
			printk(KERN_ERR "%s: IOP not responding.\n", iop->name);
			i2o_delete_controller(iop);
			return -1;
		}
	}

	if(iop->status_block->iop_state == ADAPTER_STATE_FAULTED) {
		printk(KERN_CRIT "%s: hardware fault\n", iop->name);
		i2o_delete_controller(iop);
		return -1;
	}

	if (iop->status_block->iop_state == ADAPTER_STATE_READY ||
	    iop->status_block->iop_state == ADAPTER_STATE_OPERATIONAL ||
	    iop->status_block->iop_state == ADAPTER_STATE_HOLD ||
	    iop->status_block->iop_state == ADAPTER_STATE_FAILED)
	{
		u32 m[MSG_FRAME_SIZE];
		dprintk((KERN_INFO "%s: already running...trying to reset\n",
				iop->name));

		i2o_init_outbound_q(iop);
		I2O_REPLY_WRITE32(iop,virt_to_phys(m));

		i2o_reset_controller(iop);			

		if (i2o_status_get(iop) < 0 || 
			iop->status_block->iop_state != ADAPTER_STATE_RESET)
		{
			printk(KERN_CRIT "%s: Failed to initialize.\n", iop->name);
			i2o_delete_controller(iop);
			return -1;
		}
	}

	if (i2o_init_outbound_q(iop) < 0) {
		i2o_delete_controller(iop);
		return -1;
	}

	if (i2o_post_outbound_messages(iop)) 
		return -1;

	/* In HOLD state */
	
	if (i2o_hrt_get(iop) < 0) {
		i2o_delete_controller(iop);
		return -1;
	}

	return 0;
}


/*
 * Clear and (re)initialize IOP's outbound queue
 */
int i2o_init_outbound_q(struct i2o_controller *c)
{
	u8 *status;
	u32 m;
	u32 *msg;
	u32 time;

	dprintk((KERN_INFO "%s: Initializing Outbound Queue\n", c->name));
	m=i2o_wait_message(c, "OutboundInit");
	if(m==0xFFFFFFFF)
		return -ETIMEDOUT;
	msg=(u32 *)(c->mem_offset+m);

	status = kmalloc(4,GFP_KERNEL);
	if (status==NULL) {
		printk(KERN_ERR "%s: IOP reset failed - no free memory.\n",
			c->name);
		return -ENOMEM;
	}
	memset(status, 0, 4);


	msg[0]= EIGHT_WORD_MSG_SIZE| TRL_OFFSET_6;
	msg[1]= I2O_CMD_OUTBOUND_INIT<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2]= core_context;
	msg[3]= 0x0106;						/* Transaction context */
	msg[4]= 4096;							/* Host page frame size */
	/* Frame size is in words. Pick 128, its what everyone elses uses and
		other sizes break some adapters. */
	msg[5]= MSG_FRAME_SIZE<<16|0x80;	/* Outbound msg frame size and Initcode */
	msg[6]= 0xD0000004;					/* Simple SG LE, EOB */
	msg[7]= virt_to_bus(status);

	i2o_post_message(c,m);
	
	barrier();
	time=jiffies;
	while(status[0]<0x02)
	{
		if((jiffies-time)>=30*HZ)
		{
			if(status[0]==0x00)
				printk(KERN_ERR "%s: Ignored queue initialize request.\n",
					c->name);
			else  
				printk(KERN_ERR "%s: Outbound queue initialize timeout.\n",
					c->name);
			kfree(status);
			return -ETIMEDOUT;
		}  
		schedule();
		barrier();
	}  

	if(status[0] != I2O_CMD_OUTBOUND_INIT_COMPLETE)
	{
		printk(KERN_ERR "%s: IOP outbound initialise failed.\n", c->name);
		kfree(status);
		return -ETIMEDOUT;
	}

	return 0;
}

int i2o_post_outbound_messages(struct i2o_controller *c)
{
	int i;
	u32 m;
	/* Alloc space for IOP's outbound queue message frames */

	c->page_frame = kmalloc(MSG_POOL_SIZE, GFP_KERNEL);
	if(c->page_frame==NULL) {
		printk(KERN_CRIT "%s: Outbound Q initialize failed; out of memory.\n",
			c->name);
		return -ENOMEM;
	}
	m=virt_to_phys(c->page_frame);

	/* Post frames */

	for(i=0; i< NMBR_MSG_FRAMES; i++) {
		I2O_REPLY_WRITE32(c,m);
		mb();
		m += MSG_FRAME_SIZE;
	}

	return 0;
}

/*
 * Get the IOP's Logical Configuration Table
 */
int i2o_lct_get(struct i2o_controller *c)
{
	u32 msg[8];
	int ret, size = c->status_block->expected_lct_size;

	do {
		if (c->lct == NULL) {
			c->lct = kmalloc(size, GFP_KERNEL);
			if(c->lct == NULL) {
				printk(KERN_CRIT "%s: Lct Get failed. Out of memory.\n",
					c->name);
				return -ENOMEM;
			}
		}
		memset(c->lct, 0, size);

		msg[0] = EIGHT_WORD_MSG_SIZE|SGL_OFFSET_6;
		msg[1] = I2O_CMD_LCT_NOTIFY<<24 | HOST_TID<<12 | ADAPTER_TID;
		/* msg[2] filled in i2o_post_wait */
		msg[3] = 0;
		msg[4] = 0xFFFFFFFF;	/* All devices */
		msg[5] = 0x00000000;	/* Report now */
		msg[6] = 0xD0000000|size;
		msg[7] = virt_to_bus(c->lct);

		if ((ret=i2o_post_wait(c, msg, sizeof(msg), 120))) {
			printk(KERN_ERR "%s: LCT Get failed (status=%#10x.\n", 
				c->name, ret);	
			return ret;
		}

		if (c->lct->table_size << 2 > size) {
			size = c->lct->table_size << 2;
			kfree(c->lct);
			c->lct = NULL;
		}
	} while (c->lct == NULL);

        if ((ret=i2o_parse_lct(c)) < 0)
                return ret;

	return 0;
}

/*
 * Like above, but used for async notification.  The main
 * difference is that we keep track of the CurrentChangeIndiicator
 * so that we only get updates when it actually changes.
 *
 */
int i2o_lct_notify(struct i2o_controller *c)
{
	u32 msg[8];

	msg[0] = EIGHT_WORD_MSG_SIZE|SGL_OFFSET_6;
	msg[1] = I2O_CMD_LCT_NOTIFY<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2] = core_context;
	msg[3] = 0xDEADBEEF;	
	msg[4] = 0xFFFFFFFF;	/* All devices */
	msg[5] = c->dlct->change_ind+1;	/* Next change */
	msg[6] = 0xD0000000|8192;
	msg[7] = virt_to_bus(c->dlct);

	return i2o_post_this(c, msg, sizeof(msg));
}
		
/*
 *	Bring a controller online into OPERATIONAL state. 
 */
int i2o_online_controller(struct i2o_controller *iop)
{
	if (i2o_systab_send(iop) < 0) {
		i2o_delete_controller(iop);
		return -1;
	}

	/* In READY state */

	dprintk((KERN_INFO "Attempting to enable iop%d\n", iop->unit));
	if (i2o_enable_controller(iop) < 0) {
		i2o_delete_controller(iop);
		return -1;
	}

	/* In OPERATIONAL state  */

	dprintk((KERN_INFO "Attempting to get/parse lct iop%d\n", iop->unit));
	if (i2o_lct_get(iop) < 0){
		i2o_delete_controller(iop);
		return -1;
	}

	return 0;
}

/*
 * Build system table
 *
 * The system table contains information about all the IOPs in the
 * system (duh) and is used by the Executives on the IOPs to establish
 * peer2peer connections.  We're not supporting peer2peer at the moment,
 * but this will be needed down the road for things like lan2lan forwarding.
 */
static int i2o_build_sys_table(void)
{
	struct i2o_controller *iop = NULL;
	struct i2o_controller *niop = NULL;
	int count = 0;

	sys_tbl_len = sizeof(struct i2o_sys_tbl) +	// Header + IOPs
				(i2o_num_controllers) *
					sizeof(struct i2o_sys_tbl_entry);

	if(sys_tbl)
		kfree(sys_tbl);

	sys_tbl = kmalloc(sys_tbl_len, GFP_KERNEL);
	if(!sys_tbl) {
		printk(KERN_CRIT "SysTab Set failed. Out of memory.\n");
		return -ENOMEM;
	}
	memset((void*)sys_tbl, 0, sys_tbl_len);

	sys_tbl->num_entries = i2o_num_controllers;
	sys_tbl->version = I2OVERSION; /* TODO: Version 2.0 */
	sys_tbl->change_ind = sys_tbl_ind++;

	for(iop = i2o_controller_chain; iop; iop = niop)
	{
		niop = iop->next;

		/* 
		 * Get updated IOP state so we have the latest information
		 *
		 * We should delete the controller at this point if it
		 * doesn't respond since  if it's not on the system table 
		 * it is techninically not part of the I2O subsyßtem...
		 */
		if(i2o_status_get(iop)) {
			printk(KERN_ERR "%s: Deleting b/c could not get status while"
				"attempting to build system table", iop->name);
			i2o_delete_controller(iop);		
			sys_tbl->num_entries--;
			continue; // try the next one
		}

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
{
	u32 *table;
	table = (u32*)sys_tbl;
	for(count = 0; count < (sys_tbl_len >>2); count++)
		printk(KERN_INFO "sys_tbl[%d] = %0#10x\n", count, table[count]);
}
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
 * This core API allows an OSM to post a message and then be told whether
 * or not the system received a successful reply.  It is useful when
 * the OSM does not want to know the exact 3
 */
int i2o_post_wait(struct i2o_controller *c, u32 *msg, int len, int timeout)
{
	DECLARE_WAIT_QUEUE_HEAD(wq_i2o_post);
	int status = 0;
	int flags = 0;
	struct i2o_post_wait_data *p1, *p2;
	struct i2o_post_wait_data *wait_data =
		kmalloc(sizeof(struct i2o_post_wait_data), GFP_KERNEL);

	if(!wait_data)
		return -ENOMEM;

	/* 
	 * The spin locking is needed to keep anyone from playing 
	 * with the queue pointers and id while we do the same
	 */
	spin_lock_irqsave(&post_wait_lock, flags);
	wait_data->next = post_wait_queue;
	post_wait_queue = wait_data;
	wait_data->id = (++post_wait_id) &  0x7fff;
	spin_unlock_irqrestore(&post_wait_lock, flags);

	wait_data->wq = &wq_i2o_post;
	wait_data->status = -EAGAIN;

	msg[2] = 0x80000000|(u32)core_context|((u32)wait_data->id<<16);
	
	if ((status = i2o_post_this(c, msg, len))==0) {
		interruptible_sleep_on_timeout(&wq_i2o_post, HZ * timeout);
		status = wait_data->status;
	}  

#ifdef DRIVERDEBUG
	if(status == -EAGAIN)
		printk(KERN_INFO "POST WAIT TIMEOUT\n");
#endif

	/* 
	 * Remove the entry from the queue.
	 * Since i2o_post_wait() may have been called again by
	 * a different thread while we were waiting for this 
	 * instance to complete, we're not guaranteed that 
	 * this entry is at the head of the queue anymore, so 
	 * we need to search for it, find it, and delete it.
	 */
	p2 = NULL;
	spin_lock_irqsave(&post_wait_lock, flags);
	for(p1 = post_wait_queue; p1; p2 = p1, p1 = p1->next) {
		if(p1 == wait_data) {
			if(p2)
				p2->next = p1->next;
			else
				post_wait_queue = p1->next;

			break;
		}
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
	for(p1 = post_wait_queue; p1; p1 = p1->next) {
		if(p1->id == ((context >> 16) & 0x7fff)) {
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
 *	Issue UTIL_CLAIM or UTIL_RELEASE messages
 */
static int i2o_issue_claim(struct i2o_controller *c, int tid, int context, 
				int onoff, u32 type)
{
	u32 msg[5];

	msg[0] = FIVE_WORD_MSG_SIZE | SGL_OFFSET_0;
	if(onoff)
		msg[1] = I2O_CMD_UTIL_CLAIM << 24 | HOST_TID<<12 | tid;
	else	
		msg[1] = I2O_CMD_UTIL_RELEASE << 24 | HOST_TID << 12 | tid;

	msg[3] = 0;
	msg[4] = type;
	
	return i2o_post_wait(c, msg, sizeof(msg), 30);
}

/*	Issue UTIL_PARAMS_GET or UTIL_PARAMS_SET
 *
 *	This function can be used for all UtilParamsGet/Set operations.
 *	The OperationList is given in oplist-buffer, 
 *	and results are returned in reslist-buffer.
 *	Note that the minimum sized reslist is 8 bytes and contains
 *	ResultCount, ErrorInfoSize, BlockStatus and BlockSize.
 */
int i2o_issue_params(int cmd, struct i2o_controller *iop, int tid, 
                void *oplist, int oplen, void *reslist, int reslen)
{
	u32 msg[9]; 
	u8 *res = (u8 *)reslist;
	u32 *res32 = (u32*)reslist;
	u32 *restmp = (u32*)reslist;
	int len = 0;
	int i = 0;
	int wait_status;

	msg[0] = NINE_WORD_MSG_SIZE | SGL_OFFSET_5;
	msg[1] = cmd << 24 | HOST_TID << 12 | tid; 
	msg[3] = 0;
	msg[4] = 0;
	msg[5] = 0x54000000 | oplen;	/* OperationList */
	msg[6] = virt_to_bus(oplist);
	msg[7] = 0xD0000000 | reslen;	/* ResultList */
	msg[8] = virt_to_bus(reslist);

	if((wait_status = i2o_post_wait(iop, msg, sizeof(msg), 10)))
   	return wait_status; 	/* -DetailedStatus */

	/*
	 * Calculate number of bytes of Result LIST
	 * We need to loop through each Result BLOCK and grab the length
	 */
	restmp = res32 + 1;
	len = 1;
	for(i = 0; i < (res32[0]&0X0000FFFF); i++)
	{
		if(restmp[0]&0x00FF0000)	/* BlockStatus != SUCCESS */
		{
			printk(KERN_WARNING "%s - Error:\n  ErrorInfoSize = 0x%02x, " 
					"BlockStatus = 0x%02x, BlockSize = 0x%04x\n",
					(cmd == I2O_CMD_UTIL_PARAMS_SET) ? "PARAMS_SET"
					: "PARAMS_GET",   
					res32[1]>>24, (res32[1]>>16)&0xFF, res32[1]&0xFFFF);
	
			/*
			 *	If this is the only request,than we return an error
			 */
			if((res32[0]&0x0000FFFF) == 1)
				return -((res[1] >> 16) & 0xFF); /* -BlockStatus */
		}

		len += restmp[0] & 0x0000FFFF;	/* Length of res BLOCK */
		restmp += restmp[0] & 0x0000FFFF;	/* Skip to next BLOCK */
	}

	return (len << 2);	

	// return 4 + ((res[1] & 0x0000FFFF) << 2); /* bytes used in resblk */
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

/*
 * Used for error reporting/debugging purposes
 */
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

/*
 * Used for error reporting/debugging purposes
 */
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

/*
 * Used for error reporting/debugging purposes
 */
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

/*
 * Used for error reporting/debugging purposes
 */
static void i2o_report_util_cmd(u8 cmd)
{
	switch (cmd) {
	case I2O_CMD_UTIL_NOP:
		printk(KERN_INFO "UTIL_NOP, ");
		break;			
	case I2O_CMD_UTIL_ABORT:
		printk(KERN_INFO "UTIL_ABORT, ");
		break;
	case I2O_CMD_UTIL_CLAIM:
		printk(KERN_INFO "UTIL_CLAIM, ");
		break;
	case I2O_CMD_UTIL_RELEASE:
		printk(KERN_INFO "UTIL_CLAIM_RELEASE, ");
		break;
	case I2O_CMD_UTIL_CONFIG_DIALOG:
		printk(KERN_INFO "UTIL_CONFIG_DIALOG, ");
		break;
	case I2O_CMD_UTIL_DEVICE_RESERVE:
		printk(KERN_INFO "UTIL_DEVICE_RESERVE, ");
		break;
	case I2O_CMD_UTIL_DEVICE_RELEASE:
		printk(KERN_INFO "UTIL_DEVICE_RELEASE, ");
		break;
	case I2O_CMD_UTIL_EVT_ACK:
		printk(KERN_INFO "UTIL_EVENT_ACKNOWLEDGE, ");
		break;
	case I2O_CMD_UTIL_EVT_REGISTER:
		printk(KERN_INFO "UTIL_EVENT_REGISTER, ");
		break;
	case I2O_CMD_UTIL_LOCK:
		printk(KERN_INFO "UTIL_LOCK, ");
		break;
	case I2O_CMD_UTIL_LOCK_RELEASE:
		printk(KERN_INFO "UTIL_LOCK_RELEASE, ");
		break;
	case I2O_CMD_UTIL_PARAMS_GET:
		printk(KERN_INFO "UTIL_PARAMS_GET, ");
		break;
	case I2O_CMD_UTIL_PARAMS_SET:
		printk(KERN_INFO "UTIL_PARAMS_SET, ");
		break;
	case I2O_CMD_UTIL_REPLY_FAULT_NOTIFY:
		printk(KERN_INFO "UTIL_REPLY_FAULT_NOTIFY, ");
		break;
	default:
		printk(KERN_INFO "%0#2x, ",cmd);	
	}

	return;	
}

/*
 * Used for error reporting/debugging purposes
 */
static void i2o_report_exec_cmd(u8 cmd)
{
	switch (cmd) {
	case I2O_CMD_ADAPTER_ASSIGN:
		printk(KERN_INFO "EXEC_ADAPTER_ASSIGN, ");
		break;
	case I2O_CMD_ADAPTER_READ:
		printk(KERN_INFO "EXEC_ADAPTER_READ, ");
		break;
	case I2O_CMD_ADAPTER_RELEASE:
		printk(KERN_INFO "EXEC_ADAPTER_RELEASE, ");
		break;
	case I2O_CMD_BIOS_INFO_SET:
		printk(KERN_INFO "EXEC_BIOS_INFO_SET, ");
		break;
	case I2O_CMD_BOOT_DEVICE_SET:
		printk(KERN_INFO "EXEC_BOOT_DEVICE_SET, ");
		break;
	case I2O_CMD_CONFIG_VALIDATE:
		printk(KERN_INFO "EXEC_CONFIG_VALIDATE, ");
		break;
	case I2O_CMD_CONN_SETUP:
		printk(KERN_INFO "EXEC_CONN_SETUP, ");
		break;
	case I2O_CMD_DDM_DESTROY:
		printk(KERN_INFO "EXEC_DDM_DESTROY, ");
		break;
	case I2O_CMD_DDM_ENABLE:
		printk(KERN_INFO "EXEC_DDM_ENABLE, ");
		break;
	case I2O_CMD_DDM_QUIESCE:
		printk(KERN_INFO "EXEC_DDM_QUIESCE, ");
		break;
	case I2O_CMD_DDM_RESET:
		printk(KERN_INFO "EXEC_DDM_RESET, ");
		break;
	case I2O_CMD_DDM_SUSPEND:
		printk(KERN_INFO "EXEC_DDM_SUSPEND, ");
		break;
	case I2O_CMD_DEVICE_ASSIGN:
		printk(KERN_INFO "EXEC_DEVICE_ASSIGN, ");
		break;
	case I2O_CMD_DEVICE_RELEASE:
		printk(KERN_INFO "EXEC_DEVICE_RELEASE, ");
		break;
	case I2O_CMD_HRT_GET:
		printk(KERN_INFO "EXEC_HRT_GET, ");
		break;
	case I2O_CMD_ADAPTER_CLEAR:
		printk(KERN_INFO "EXEC_IOP_CLEAR, ");
		break;
	case I2O_CMD_ADAPTER_CONNECT:
		printk(KERN_INFO "EXEC_IOP_CONNECT, ");
		break;
	case I2O_CMD_ADAPTER_RESET:
		printk(KERN_INFO "EXEC_IOP_RESET, ");
		break;
	case I2O_CMD_LCT_NOTIFY:
		printk(KERN_INFO "EXEC_LCT_NOTIFY, ");
		break;
	case I2O_CMD_OUTBOUND_INIT:
		printk(KERN_INFO "EXEC_OUTBOUND_INIT, ");
		break;
	case I2O_CMD_PATH_ENABLE:
		printk(KERN_INFO "EXEC_PATH_ENABLE, ");
		break;
	case I2O_CMD_PATH_QUIESCE:
		printk(KERN_INFO "EXEC_PATH_QUIESCE, ");
		break;
	case I2O_CMD_PATH_RESET:
		printk(KERN_INFO "EXEC_PATH_RESET, ");
		break;
	case I2O_CMD_STATIC_MF_CREATE:
		printk(KERN_INFO "EXEC_STATIC_MF_CREATE, ");
		break;
	case I2O_CMD_STATIC_MF_RELEASE:
		printk(KERN_INFO "EXEC_STATIC_MF_RELEASE, ");
		break;
	case I2O_CMD_STATUS_GET:
		printk(KERN_INFO "EXEC_STATUS_GET, ");
		break;
	case I2O_CMD_SW_DOWNLOAD:
		printk(KERN_INFO "EXEC_SW_DOWNLOAD, ");
		break;
	case I2O_CMD_SW_UPLOAD:
		printk(KERN_INFO "EXEC_SW_UPLOAD, ");
		break;
	case I2O_CMD_SW_REMOVE:
		printk(KERN_INFO "EXEC_SW_REMOVE, ");
		break;
	case I2O_CMD_SYS_ENABLE:
		printk(KERN_INFO "EXEC_SYS_ENABLE, ");
		break;
	case I2O_CMD_SYS_MODIFY:
		printk(KERN_INFO "EXEC_SYS_MODIFY, ");
		break;
	case I2O_CMD_SYS_QUIESCE:
		printk(KERN_INFO "EXEC_SYS_QUIESCE, ");
		break;
	case I2O_CMD_SYS_TAB_SET:
		printk(KERN_INFO "EXEC_SYS_TAB_SET, ");
		break;
	default:
		printk(KERN_INFO "%02x, ",cmd);	
	}

	return;	
}

/*
 * Used for error reporting/debugging purposes
 */
static void i2o_report_lan_cmd(u8 cmd)
{
	switch (cmd) {
	case LAN_PACKET_SEND:
		printk(KERN_INFO "LAN_PACKET_SEND, "); 
		break;
	case LAN_SDU_SEND:
		printk(KERN_INFO "LAN_SDU_SEND, ");
		break;
	case LAN_RECEIVE_POST:
		printk(KERN_INFO "LAN_RECEIVE_POST, ");
		break;
	case LAN_RESET:
		printk(KERN_INFO "LAN_RESET, ");
		break;
	case LAN_SUSPEND:
		printk(KERN_INFO "LAN_SUSPEND, ");
		break;
	default:
		printk(KERN_INFO "%02x, ",cmd);	
	}	

	return;
}

/*
 * Used for error reporting/debugging purposes
 *
 * This will have to be rewritten someday.  The code currently
 * assumes that a certain range of commands is reserved for
 * given class.  This is not completely true. Exec and Util
 * message have their numbers reserved, but the rest are
 * available _for each device class to use as it wishes_
 *
 * For example 0x37 is BsaCacheFlush for a block class device and 
 * LanSuspend for a LAN class device. 
 *
 * The ideal way to do this would be to look at the TID and then
 * find the LCT entry to determine what the class of the device is.
 *
 */
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
	
	printk(KERN_INFO "%02x, %02x / %04x.\n", cmd, req_status, detailed_status);
	return;
}

/* Used to dump a message to syslog during debugging */
void i2o_dump_message(u32 *msg)
{
#ifdef DRIVERDEBUG
	int i;
	printk(KERN_INFO "Dumping I2O message size %d @ %p\n", 
		msg[0]>>16&0xffff, msg);
	for(i = 0; i < ((msg[0]>>16)&0xffff); i++)
		printk(KERN_INFO "  msg[%d] = %0#10x\n", i, msg[i]);
#endif
}

/*
 * I2O reboot/shutdown notification.
 *
 * - Call each OSM's reboot notifier (if one exists)
 * - Quiesce each IOP in the system
 *
 * Each IOP has to be quiesced before we can ensure that the system
 * can be properly shutdown as a transaction that has already been
 * acknowledged still needs to be placed in permanent store on the IOP.
 * The SysQuiesce causes the IOP to force all HDMs to complete their
 * transactions before returning, so only at that point is it safe
 * 
 */
static int i2o_reboot_event(struct notifier_block *n, unsigned long code, void
*p)
{
	int i = 0;
	struct i2o_controller *c = NULL;

	if(code != SYS_RESTART && code != SYS_HALT && code != SYS_POWER_OFF)
		return NOTIFY_DONE;

	printk(KERN_INFO "Shutting down I2O system.\n");
	printk(KERN_INFO 
		"   This could take a few minutes if there are many devices attached\n");

	for(i = 0; i < MAX_I2O_MODULES; i++)
	{
		if(i2o_handlers[i] && i2o_handlers[i]->reboot_notify)
			i2o_handlers[i]->reboot_notify();
	}

	for(c = i2o_controller_chain; c; c = c->next)
	{
		if(i2o_quiesce_controller(c))
		{
			printk(KERN_WARNING "i2o: Could not quiesce %s."  "
				Verify setup on next system power up.\n", c->name);
		}
	}

	return NOTIFY_DONE;
}


#ifdef MODULE

EXPORT_SYMBOL(i2o_controller_chain);
EXPORT_SYMBOL(i2o_num_controllers);
EXPORT_SYMBOL(i2o_find_controller);
EXPORT_SYMBOL(i2o_unlock_controller);
EXPORT_SYMBOL(i2o_status_get);

EXPORT_SYMBOL(i2o_install_handler);
EXPORT_SYMBOL(i2o_remove_handler);

EXPORT_SYMBOL(i2o_claim_device);
EXPORT_SYMBOL(i2o_release_device);
EXPORT_SYMBOL(i2o_device_notify_on);
EXPORT_SYMBOL(i2o_device_notify_off);

EXPORT_SYMBOL(i2o_post_this);
EXPORT_SYMBOL(i2o_post_wait);

EXPORT_SYMBOL(i2o_query_scalar);
EXPORT_SYMBOL(i2o_set_scalar);
EXPORT_SYMBOL(i2o_query_table);
EXPORT_SYMBOL(i2o_clear_table);
EXPORT_SYMBOL(i2o_row_add_table);
EXPORT_SYMBOL(i2o_row_delete_table);
EXPORT_SYMBOL(i2o_issue_params);

EXPORT_SYMBOL(i2o_event_register);
EXPORT_SYMBOL(i2o_event_ack);

EXPORT_SYMBOL(i2o_report_status);
EXPORT_SYMBOL(i2o_dump_message);
EXPORT_SYMBOL(i2o_get_class_name);

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
		printk(KERN_INFO "i2o: No PCI I2O controllers found\n");
#endif

	/*
	 * Initialize event handling thread
	 */	
	init_MUTEX_LOCKED(&evt_sem);
	evt_pid = kernel_thread(i2o_core_evt, &evt_reply, CLONE_SIGHAND);
	if(evt_pid < 0)
	{
		printk(KERN_ERR "I2O: Could not create event handler kernel thread\n");
		i2o_remove_handler(&i2o_core_handler);
		return 0;
	}
	else(KERN_INFO "event thread created as pid %d\n", evt_pid);

	if(i2o_num_controllers)
		i2o_sys_init();

	register_reboot_notifier(&i2o_reboot_notifier);

	return 0;
}

void cleanup_module(void)
{
	int stat;

	unregister_reboot_notifier(&i2o_reboot_notifier);

	if(i2o_num_controllers)
		i2o_sys_shutdown();

	/*
	 * If this is shutdown time, the thread has already been killed
	 */
	if(evt_running) {
		stat = kill_proc(evt_pid, SIGTERM, 1);
		if(!stat) {
			int count = 10 * 100;
			while(evt_running && count) {
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(1);
			}
	
			if(!count)
				printk(KERN_ERR "i2o: Event thread still running!\n");
		}
	}

#ifdef CONFIG_I2O_PCI_MODULE
	i2o_pci_core_detach();
#endif

	i2o_remove_handler(&i2o_core_handler);

	unregister_reboot_notifier(&i2o_reboot_notifier);
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

	/*
	 * Initialize event handling thread
	 * We may not find any controllers, but still want this as 
	 * down the road we may have hot pluggable controllers that
	 * need to be dealt with.
	 */	
	init_MUTEX_LOCKED(&evt_sem);
	if((evt_pid = kernel_thread(i2o_core_evt, &evt_reply, CLONE_SIGHAND)) < 0)
	{
		printk(KERN_ERR "I2O: Could not create event handler kernel thread\n");
		i2o_remove_handler(&i2o_core_handler);
		return 0;
	}


#ifdef CONFIG_I2O_PCI
	i2o_pci_init();
#endif

	if(i2o_num_controllers)
		i2o_sys_init();

	register_reboot_notifier(&i2o_reboot_notifier);

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
