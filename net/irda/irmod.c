/*********************************************************************
 *                
 * Filename:      irmod.c
 * Version:       0.8
 * Description:   IrDA module code and some other stuff
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Dec 15 13:55:39 1997
 * Modified at:   Wed Aug 11 08:53:56 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997, 1999 Dag Brattli, All Rights Reserved.
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
#include <linux/poll.h>
#include <linux/proc_fs.h>

#include <asm/segment.h>

#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irlap.h>
#ifdef CONFIG_IRDA_COMPRESSION
#include <net/irda/irlap_comp.h>
#endif /* CONFIG_IRDA_COMPRESSION */
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/irias_object.h>
#include <net/irda/irttp.h>
#include <net/irda/irda_device.h>
#include <net/irda/wrapper.h>
#include <net/irda/timer.h>
#include <net/irda/parameters.h>

extern struct proc_dir_entry *proc_irda;

struct irda_cb irda; /* One global instance */

#ifdef CONFIG_IRDA_DEBUG
__u32 irda_debug = IRDA_DEBUG_LEVEL;
#endif

extern void irda_proc_register(void);
extern void irda_proc_unregister(void);
extern int  irda_sysctl_register(void);
extern void irda_sysctl_unregister(void);

extern void irda_proto_init(struct net_proto *pro);
extern void irda_proto_cleanup(void);

extern int irda_device_init(void);
extern int irlan_init(void);
extern int irlan_client_init(void);
extern int irlan_server_init(void);
extern int ircomm_init(void);
extern int ircomm_tty_init(void);
extern int irlpt_client_init(void);
extern int irlpt_server_init(void);

#ifdef CONFIG_IRDA_COMPRESSION
#ifdef CONFIG_IRDA_DEFLATE
extern irda_deflate_init();
#endif /* CONFIG_IRDA_DEFLATE */
#endif /* CONFIG_IRDA_COMPRESSION */

static int irda_open(struct inode * inode, struct file *file);
static int irda_ioctl(struct inode *inode, struct file *filp, 
		      unsigned int cmd, unsigned long arg);
static int irda_close(struct inode *inode, struct file *file);
static ssize_t irda_read(struct file *file, char *buffer, size_t count, 
			 loff_t *noidea);
static ssize_t irda_write(struct file *file, const char *buffer,
			  size_t count, loff_t *noidea);
static u_int irda_poll(struct file *file, poll_table *wait);

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

/* IrTTP */
EXPORT_SYMBOL(irttp_open_tsap);
EXPORT_SYMBOL(irttp_close_tsap);
EXPORT_SYMBOL(irttp_connect_response);
EXPORT_SYMBOL(irttp_data_request);
EXPORT_SYMBOL(irttp_disconnect_request);
EXPORT_SYMBOL(irttp_flow_request);
EXPORT_SYMBOL(irttp_connect_request);
EXPORT_SYMBOL(irttp_udata_request);
EXPORT_SYMBOL(irttp_dup);

/* Main IrDA module */
#ifdef CONFIG_IRDA_DEBUG
EXPORT_SYMBOL(irda_debug);
#endif
EXPORT_SYMBOL(irda_notify_init);
EXPORT_SYMBOL(irmanager_notify);
EXPORT_SYMBOL(irda_lock);
EXPORT_SYMBOL(proc_irda);
EXPORT_SYMBOL(irda_param_insert);
EXPORT_SYMBOL(irda_param_extract);
EXPORT_SYMBOL(irda_param_extract_all);
EXPORT_SYMBOL(irda_param_pack);
EXPORT_SYMBOL(irda_param_unpack);

/* IrIAP/IrIAS */
EXPORT_SYMBOL(iriap_getvaluebyclass_request);
EXPORT_SYMBOL(irias_object_change_attribute);
EXPORT_SYMBOL(irias_add_integer_attrib);
EXPORT_SYMBOL(irias_add_octseq_attrib);
EXPORT_SYMBOL(irias_add_string_attrib);
EXPORT_SYMBOL(irias_insert_object);
EXPORT_SYMBOL(irias_new_object);
EXPORT_SYMBOL(irias_delete_object);
EXPORT_SYMBOL(irias_find_object);
EXPORT_SYMBOL(irias_find_attrib);
EXPORT_SYMBOL(irias_new_integer_value);
EXPORT_SYMBOL(irias_new_string_value);
EXPORT_SYMBOL(irias_new_octseq_value);

/* IrLMP */
EXPORT_SYMBOL(irlmp_discovery_request);
EXPORT_SYMBOL(irlmp_register_client);
EXPORT_SYMBOL(irlmp_unregister_client);
EXPORT_SYMBOL(irlmp_update_client);
EXPORT_SYMBOL(irlmp_register_service);
EXPORT_SYMBOL(irlmp_unregister_service);
EXPORT_SYMBOL(irlmp_service_to_hint);
EXPORT_SYMBOL(irlmp_data_request);
EXPORT_SYMBOL(irlmp_open_lsap);
EXPORT_SYMBOL(irlmp_close_lsap);
EXPORT_SYMBOL(irlmp_connect_request);
EXPORT_SYMBOL(irlmp_connect_response);
EXPORT_SYMBOL(irlmp_disconnect_request);
EXPORT_SYMBOL(irlmp_get_daddr);
EXPORT_SYMBOL(irlmp_get_saddr);
EXPORT_SYMBOL(irlmp_dup);
EXPORT_SYMBOL(lmp_reasons);

/* Queue */
EXPORT_SYMBOL(hashbin_find);
EXPORT_SYMBOL(hashbin_new);
EXPORT_SYMBOL(hashbin_insert);
EXPORT_SYMBOL(hashbin_delete);
EXPORT_SYMBOL(hashbin_remove);
EXPORT_SYMBOL(hashbin_get_next);
EXPORT_SYMBOL(hashbin_get_first);

/* IrLAP */
#ifdef CONFIG_IRDA_COMPRESSION
EXPORT_SYMBOL(irda_unregister_compressor);
EXPORT_SYMBOL(irda_register_compressor);
#endif /* CONFIG_IRDA_COMPRESSION */
EXPORT_SYMBOL(irda_init_max_qos_capabilies);
EXPORT_SYMBOL(irda_qos_bits_to_value);
EXPORT_SYMBOL(irda_device_open);
EXPORT_SYMBOL(irda_device_close);
EXPORT_SYMBOL(irda_device_setup);
EXPORT_SYMBOL(irda_device_set_media_busy);
EXPORT_SYMBOL(irda_device_txqueue_empty);
EXPORT_SYMBOL(irda_device_net_open);
EXPORT_SYMBOL(irda_device_net_close);

EXPORT_SYMBOL(irda_device_init_dongle);
EXPORT_SYMBOL(irda_device_register_dongle);
EXPORT_SYMBOL(irda_device_unregister_dongle);

EXPORT_SYMBOL(async_wrap_skb);
EXPORT_SYMBOL(async_unwrap_char);
EXPORT_SYMBOL(irda_start_timer);
EXPORT_SYMBOL(setup_dma);

#ifdef CONFIG_IRTTY
EXPORT_SYMBOL(irtty_set_dtr_rts);
EXPORT_SYMBOL(irtty_register_dongle);
EXPORT_SYMBOL(irtty_unregister_dongle);
EXPORT_SYMBOL(irtty_set_packet_mode);
#endif

int __init irda_init(void)
{
	printk(KERN_INFO "IrDA (tm) Protocols for Linux-2.3 (Dag Brattli)\n");

 	irlmp_init();
	irlap_init();

#ifdef MODULE
	irda_device_init();	/* Called by init/main.c when non-modular */
#endif

	iriap_init();
 	irttp_init();
	
#ifdef CONFIG_PROC_FS
	irda_proc_register();
#endif
#ifdef CONFIG_SYSCTL
	irda_sysctl_register();
#endif
	init_waitqueue_head(&irda.wait_queue);
	irda.dev.minor = MISC_DYNAMIC_MINOR;
	irda.dev.name = "irda";
	irda.dev.fops = &irda_fops;
	
	misc_register(&irda.dev);

	irda.in_use = FALSE;
	
	init_waitqueue_head(&irda.wait_queue);

	/* 
	 * Initialize modules that got compiled into the kernel 
	 */
#ifdef CONFIG_IRLAN
	irlan_init();
#endif
#ifdef CONFIG_IRCOMM
	ircomm_init();
	ircomm_tty_init();
#endif

#ifdef CONFIG_IRDA_COMPRESSION
#ifdef CONFIG_IRDA_DEFLATE
	irda_deflate_init();
#endif /* CONFIG_IRDA_DEFLATE */
#endif /* CONFIG_IRDA_COMPRESSION */

	return 0;
}

#ifdef MODULE
void irda_cleanup(void)
{
	misc_deregister(&irda.dev);

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
#endif /* MODULE */

/*
 * Function irda_unlock (lock)
 *
 *    Unlock variable. Returns false if lock is already unlocked
 *
 */
inline int irda_unlock(int *lock) 
{
	if (!test_and_clear_bit(0, (void *) lock))  {
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
void irda_notify_init(notify_t *notify)
{
	notify->data_indication = NULL;
	notify->udata_indication = NULL;
	notify->connect_confirm = NULL;
	notify->connect_indication = NULL;
	notify->disconnect_indication = NULL;
	notify->flow_indication = NULL;
	notify->instance = NULL;
	strncpy(notify->name, "Unknown", NOTIFY_MAX_NAME);
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
	enqueue_last(&irda.todo_queue, (QUEUE *) new);

	event.event = EVENT_NEED_PROCESS_CONTEXT;

	/* Notify the user space manager */
	irmanager_notify(&event);
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
	if (!event)
		return 0;

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

void irda_mod_inc_use_count(void)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

void irda_mod_dec_use_count(void)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}

/*
 * Function irda_proc_modcount (inode, fill)
 *
 *    Use by the proc file system functions to prevent the irda module
 *    being removed while the use is standing in the net/irda directory
 */
void irda_proc_modcount(struct inode *inode, int fill)
{
#ifdef MODULE
#ifdef CONFIG_PROC_FS
	if (fill)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
#endif /* CONFIG_PROC_FS */
#endif /* MODULE */
}

#ifdef MODULE

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("The Linux IrDA Protocol Subsystem"); 
MODULE_PARM(irda_debug, "1l");

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
#endif /* MODULE */
