/*********************************************************************
 *                
 * Filename:      irmod.c
 * Version:       0.8
 * Description:   IrDA module code and some other stuff
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Dec 15 13:55:39 1997
 * Modified at:   Tue Jan 19 23:34:18 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997 Dag Brattli, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#include <linux/config.h>
#include <linux/module.h> 
#include <linux/init.h>
#include <asm/segment.h>
#include <linux/poll.h>

#include <net/irda/irda.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/irttp.h>

struct irda_cb irda; /* One global instance */

#ifdef CONFIG_IRDA_DEBUG
__u32 irda_debug = IRDA_DEBUG;
#endif

extern void irda_proc_register(void);
extern void irda_proc_unregister(void);
extern int irda_sysctl_register(void);
extern void irda_sysctl_unregister(void);

extern void irda_proto_init(struct net_proto *pro);
extern void irda_proto_cleanup(void);

extern int irda_device_init(void);
extern int irobex_init(void);
extern int irlan_init(void);
extern int irlan_client_init(void);
extern int irlan_server_init(void);
extern int ircomm_init(void);
extern int irvtd_init(void);
extern int irlpt_client_init(void);
extern int irlpt_server_init(void);

static int irda_open( struct inode * inode, struct file *file);
static int irda_ioctl( struct inode *inode, struct file *filp, 
			 unsigned int cmd, unsigned long arg);
static int irda_close( struct inode *inode, struct file *file);
static ssize_t irda_read( struct file *file, char *buffer, size_t count, 
			    loff_t *noidea);
static ssize_t irda_write( struct file *file, const char *buffer,
			     size_t count, loff_t *noidea);
static u_int irda_poll( struct file *file, poll_table *wait);

static struct file_operations irda_fops = {
	NULL,	       /* seek */
	irda_read,     /* read */
	irda_write,    /* write */
	NULL,	       /* readdir */
	irda_poll,     /* poll */
	irda_ioctl,    /* ioctl */
	NULL,	       /* mmap */
	irda_open,
	NULL,
	irda_close,
	NULL,
	NULL,          /* fasync */
};

__initfunc(int irda_init(void))
{
        printk( KERN_INFO "Linux Support for the IrDA (tm) protocols (Dag Brattli)\n");

	irda_device_init();
	irlap_init();
 	irlmp_init();
	iriap_init();
 	irttp_init();
	
#ifdef CONFIG_PROC_FS
	irda_proc_register();
#endif
#ifdef CONFIG_SYSCTL
	irda_sysctl_register();
#endif

	irda.dev.minor = MISC_DYNAMIC_MINOR;
	irda.dev.name = "irda";
	irda.dev.fops = &irda_fops;
	
	misc_register( &irda.dev);

	irda.in_use = FALSE;

	/* 
	 * Initialize modules that got compiled into the kernel 
	 */
#ifdef CONFIG_IRLAN
	irlan_init();
#endif
#ifdef CONFIG_IRLAN_CLIENT
	irlan_client_init();
#endif
#ifdef CONFIG_IRLAN_SERVER
	irlan_server_init();
#endif
#ifdef CONFIG_IROBEX
	irobex_init();
#endif
#ifdef CONFIG_IRCOMM
	ircomm_init();
	irvtd_init();
#endif

#ifdef CONFIG_IRLPT_CLIENT
	irlpt_client_init();
#endif

#ifdef CONFIG_IRLPT_SERVER
	irlpt_server_init();
#endif

	return 0;
}

void irda_cleanup(void)
{
	misc_deregister( &irda.dev);

#ifdef CONFIG_SYSCTL
	irda_sysctl_unregister();
#endif	

#ifdef CONFIG_PROC_FS
	irda_proc_unregister();
#endif

	/* Remove higher layers */
	irttp_cleanup();
	iriap_cleanup();

	/* Remove lower layers */
	irda_device_cleanup();
	irlap_cleanup(); /* Must be done before irlmp_cleanup()! DB */

	/* Remove middle layer */
	irlmp_cleanup();
}

/*
 * Function irda_lock (lock)
 *
 *    Lock variable. Returns false if the lock is already set.
 *    
 */
inline int irda_lock( int *lock) {
	if ( test_and_set_bit( 0, (void *) lock))  {
             printk("Trying to lock, already locked variable!\n");
	     return FALSE;
        }  
	return TRUE;
}

/*
 * Function irda_unlock (lock)
 *
 *    Unlock variable. Returns false if lock is already unlocked
 *
 */
inline int irda_unlock( int *lock) {
	if ( !test_and_clear_bit( 0, (void *) lock))  {
		printk("Trying to unlock already unlocked variable!\n");
		return FALSE;
        }
	return TRUE;
}

/*
 * Function irda_notify_init (notify)
 *
 *    Used for initializing the notify structure
 *
 */
void irda_notify_init( struct notify_t *notify)
{

	notify->data_indication = NULL;
	notify->udata_indication = NULL;
	notify->connect_confirm = NULL;
	notify->connect_indication = NULL;
	notify->disconnect_indication = NULL;
	notify->flow_indication = NULL;
	notify->instance = NULL;
	strncpy( notify->name, "Unknown", NOTIFY_MAX_NAME);
}

/*
 * Function irda_execute_as_process (self, callback, param)
 *
 *    If a layer needs to have a function executed with a process context,
 *    then it can register the function here, and the function will then 
 *    be executed as fast as possible.
 *
 */
void irda_execute_as_process( void *self, TODO_CALLBACK callback, __u32 param)
{
	struct irda_todo *new;
	struct irmanager_event event;

	/* Make sure irmanager is running */
	if ( !irda.in_use) {
		printk( KERN_ERR "irmanager is not running!\n");
		return;
	}

	/* Make new todo event */
	new = (struct irda_todo *) kmalloc( sizeof(struct irda_todo),
					    GFP_ATOMIC);
	if ( new == NULL) {
		return;
	}
	memset( new, 0, sizeof( struct irda_todo));

	new->self = self;
	new->callback = callback;
	new->param = param;
	
	/* Queue todo */
	enqueue_last( &irda.todo_queue, (QUEUE *) new);

	event.event = EVENT_NEED_PROCESS_CONTEXT;

	/* Notify the user space manager */
	irmanager_notify( &event);
}

/*
 * Function irmanger_notify (event)
 *
 *    Send an event to the user space manager
 *
 */
void irmanager_notify( struct irmanager_event *event)
{
	struct irda_event *new;
	
	DEBUG( 4, __FUNCTION__ "()\n");

	/* Make sure irmanager is running */
	if ( !irda.in_use) {
		printk( KERN_ERR "irmanager is not running!\n");
		return;
	}

	/* Make new IrDA Event */
	new = (struct irda_event *) kmalloc( sizeof(struct irda_event),
					     GFP_ATOMIC);
	if ( new == NULL) {
		return;	
	}
	memset( new, 0, sizeof( struct irda_event));
	new->event = *event;
	
	/* Queue event */
	enqueue_last( &irda.event_queue, (QUEUE *) new);
	
	/* Wake up irmanager sleeping on read */
	wake_up_interruptible( &irda.wait_queue);
}

static int irda_open( struct inode * inode, struct file *file)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	if ( irda.in_use) {
		DEBUG( 0, __FUNCTION__ "(), irmanager is already running!\n");
		return -1;
	}
	irda.in_use = TRUE;
		
	MOD_INC_USE_COUNT;

	return 0;
}

/*
 * Function irda_ioctl (inode, filp, cmd, arg)
 *
 *    Ioctl, used by irmanager to ...
 *
 */
static int irda_ioctl( struct inode *inode, struct file *filp, 
		       unsigned int cmd, unsigned long arg)
{
	struct irda_todo *todo;
	int err = 0;
	int size = _IOC_SIZE(cmd);

	DEBUG( 4, __FUNCTION__ "()\n");

	if ( _IOC_DIR(cmd) & _IOC_READ)
		err = verify_area( VERIFY_WRITE, (void *) arg, size);
	else if ( _IOC_DIR(cmd) & _IOC_WRITE)
		err = verify_area( VERIFY_READ, (void *) arg, size);
	if ( err)
		return err;
	
	switch( cmd) {
	case IRMGR_IOCTNPC:
		/* Got process context! */
		DEBUG( 4, __FUNCTION__ "(), got process context!\n");

		while (( todo = (struct irda_todo *) dequeue_first( 
			&irda.todo_queue)) != NULL)
		{
			todo->callback( todo->self, todo->param);

			kfree( todo);
		}
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static int irda_close( struct inode *inode, struct file *file)
{
	DEBUG( 4, __FUNCTION__ "()\n");
	
	MOD_DEC_USE_COUNT;

	irda.in_use = FALSE;

	return 0;
}

static ssize_t irda_read( struct file *file, char *buffer, size_t count, 
			  loff_t *noidea)
{
	struct irda_event *event;
	unsigned long flags;
	int len;

	DEBUG( 4, __FUNCTION__ "()\n");

	/* * Go to sleep and wait for event if there is no event to be read! */
	save_flags( flags);
	cli();
	if ( !irda.event_queue)
		interruptible_sleep_on( &irda.wait_queue);
	restore_flags(flags);
	
	/*
	 *  Ensure proper reaction to signals, and screen out 
	 *  blocked signals (page 112. linux device drivers)
	 */
	if ( signal_pending( current))
		return -ERESTARTSYS;

	event = (struct irda_event *) dequeue_first( &irda.event_queue);

	len = sizeof(struct irmanager_event);
	copy_to_user( buffer, &event->event, len);

	/* Finished with event */
	kfree( event);

	return len;
}

static ssize_t irda_write( struct file *file, const char *buffer,
			   size_t count, loff_t *noidea)
{
	DEBUG( 0, __FUNCTION__ "()\n");
	
	return 0;
}

static u_int irda_poll( struct file *file, poll_table *wait)
{
	DEBUG( 0, __FUNCTION__ "(), Sorry not implemented yet!\n");

	return 0;
}

#ifdef MODULE

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("The Linux IrDA protocol subsystem"); 

/*
 * Function init_module (void)
 *
 *    Initialize the irda module
 *
 */
int init_module(void) 
{
	irda_proto_init(NULL);

	return 0;
}

/*
 * Function cleanup_module (void)
 *
 *    Cleanup the irda module
 *
 */
void cleanup_module(void) 
{
	irda_proto_cleanup();
}

#endif
