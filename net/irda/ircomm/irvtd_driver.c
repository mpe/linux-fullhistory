/*********************************************************************
 *                
 * Filename:      irvtd_driver.c
 * Version:       
 * Description:   An implementation of "port emulation entity" of IrCOMM
 * Status:        Experimental.
 * Author:        Takahide Higuchi <thiguchi@pluto.dti.ne.jp>
 * Source:        serial.c by Linus Torvalds
 *                isdn_tty.c by Fritz Elfert
 *
 *     Copyright (c) 1998, Takahide Higuchi, <thiguchi@pluto.dti.ne.jp>,
 *     All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     I, Takahide Higuchi, provide no warranty for any of this software.
 *     This material is provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/termios.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

#include <net/irda/irda.h>
#include <net/irda/irttp.h>

#include <net/irda/irvtd.h>
#include <net/irda/irvtd_driver.h>

#ifndef MIN
#define MIN(a,b)        ((a) < (b) ? (a) : (b))
#endif

#define DO_RESTART
#define RELEVANT_IFLAG(iflag) (iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

static char *irvtd_ttyname = "irnine";
struct tty_driver irvtd_drv, irvtd_callout_driver;
struct tty_struct *irvtd_table[COMM_MAX_TTY];
struct termios *irvtd_termios[COMM_MAX_TTY];
struct termios *irvtd_termios_locked[COMM_MAX_TTY];
static int ircomm_vsd_refcount;
extern struct ircomm_cb **ircomm;
extern struct irvtd_cb **irvtd;

/*
 * prototypes
 */

int irvtd_open(struct tty_struct *tty, struct file *filp);
void irvtd_close(struct tty_struct * tty, struct file * filp);
int irvtd_write(struct tty_struct * tty, int from_user,
			const unsigned char *buf, int count);
void irvtd_put_char(struct tty_struct *tty, unsigned char ch);
int irvtd_write_room(struct tty_struct *tty);
int irvtd_chars_in_buffer(struct tty_struct *tty);
int irvtd_ioctl(struct tty_struct *tty, struct file * file,
	     unsigned int cmd, unsigned long arg);
void irvtd_set_termios(struct tty_struct *tty, struct termios * old);
void irvtd_throttle(struct tty_struct *tty);
void irvtd_unthrottle(struct tty_struct *tty);
void irvtd_stop(struct tty_struct *tty);
void irvtd_start(struct tty_struct *tty);
void irvtd_hangup(struct tty_struct *tty);
void irvtd_flush_buffer(struct tty_struct *tty);

static void flush_txbuff(struct irvtd_cb *info);
static void change_speed(struct irvtd_cb *driver);
static void irvtd_write_to_tty( void *instance );

static void irvtd_break(struct tty_struct *tty, int break_state);
static void irvtd_send_xchar(struct tty_struct *tty, char ch);

#if 0
static char *rcsid = "$Id: irvtd_driver.c,v 1.13 1998/12/06 10:09:07 takahide Exp $";
#endif




/*
 * Function ircomm_register_device(void) 
 *   we register "port emulation entity"(see IrCOMM specification) here
 *   as a tty device.
 *   it will be called when you insmod.
 *   ( This function derives from linux/drivers/char/serial.c )
 */

int irvtd_register_ttydriver(void){

        DEBUG( 4, "-->irvtd_register_ttydriver\n");

	/* setup virtual serial port device */

        /* Initialize the tty_driver structure ,which is defined in 
	   tty_driver.h */
        
        memset(&irvtd_drv, 0, sizeof(struct tty_driver));
	irvtd_drv.magic = IRVTD_MAGIC;
	irvtd_drv.name = irvtd_ttyname;
	irvtd_drv.major = IRCOMM_MAJOR;
	irvtd_drv.minor_start = IRVTD_MINOR;
	irvtd_drv.num = COMM_MAX_TTY;
	irvtd_drv.type = TTY_DRIVER_TYPE_SERIAL;  /* see tty_driver.h */
	irvtd_drv.subtype = IRVTD_TYPE_NORMAL;      /* private type */

	/*
	 * see drivers/char/tty_io.c and termios(3)
	 */

        irvtd_drv.init_termios = tty_std_termios;
        irvtd_drv.init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
        irvtd_drv.flags = TTY_DRIVER_REAL_RAW;   /* see tty_driver.h */
        irvtd_drv.refcount = &ircomm_vsd_refcount;

	/* pointer to the tty data structures */

        irvtd_drv.table = irvtd_table;  
        irvtd_drv.termios = irvtd_termios;
        irvtd_drv.termios_locked = irvtd_termios_locked;

        /*
         * Interface table from the kernel(tty driver) to the ircomm
         * layer
         */

        irvtd_drv.open = irvtd_open;
        irvtd_drv.close = irvtd_close;
        irvtd_drv.write = irvtd_write;
        irvtd_drv.put_char = irvtd_put_char;
	irvtd_drv.flush_chars = irvtd_flush_chars;
        irvtd_drv.write_room = irvtd_write_room;
	irvtd_drv.chars_in_buffer = irvtd_chars_in_buffer; 
        irvtd_drv.flush_buffer = irvtd_flush_buffer;
 	irvtd_drv.ioctl = irvtd_ioctl; 
	irvtd_drv.throttle = irvtd_throttle;
	irvtd_drv.unthrottle = irvtd_unthrottle;
 	irvtd_drv.set_termios = irvtd_set_termios;
	irvtd_drv.stop = NULL;          /*  irvtd_stop; */
	irvtd_drv.start = NULL;         /* irvtd_start; */
        irvtd_drv.hangup = irvtd_hangup;

        irvtd_drv.send_xchar = irvtd_send_xchar;
	irvtd_drv.break_ctl = irvtd_break;
	irvtd_drv.read_proc = NULL;
	irvtd_drv.wait_until_sent = NULL;

        /*
         * The callout device is just like normal device except for
         * minor number and the subtype.
         */

	/* What is difference between callout device and normal device? */
	/* My system dosen't have /dev/cua??, so we don't need it? :{| */
	irvtd_callout_driver = irvtd_drv;
	irvtd_callout_driver.name = "irninecua";
	irvtd_callout_driver.minor_start = IRVTD_CALLOUT_MINOR; 
	irvtd_callout_driver.subtype = IRVTD_TYPE_CALLOUT;


	if (tty_register_driver(&irvtd_drv)){
		DEBUG(0,"IrCOMM:Couldn't register tty driver\n");
		return(1);
	}
	if (tty_register_driver(&irvtd_callout_driver))
		DEBUG(0,"IrCOMM:Couldn't register callout tty driver\n");

	DEBUG( 4, "irvtd_register_ttydriver: done.\n");
	return(0);
}


/*
 * Function irvtd_unregister_device(void) 
 *   it will be called when you rmmod
 */

void irvtd_unregister_ttydriver(void){

	int err;	
        DEBUG( 4, "--> irvtd_unregister_device\n");

	/* unregister tty device   */

	err = tty_unregister_driver(&irvtd_drv);
        if (err)
                printk("IrCOMM: failed to unregister vtd driver(%d)\n",err);
	err = tty_unregister_driver(&irvtd_callout_driver);
        if (err)
                printk("IrCOMM: failed to unregister vtd_callout driver(%d)\n", err);

        DEBUG( 4, "irvtd_unregister_device -->\n");
	return;
}



/*
 * ----------------------------------------------------------------------
 * Routines for Virtual tty driver
 *
 *   most of infomation is descrived in linux/tty_driver.h, but
 *   a function ircomm_receive() derives from receive_chars() which is
 *   in 2.0.30 kernel (driver/char/serial.c).
 *   if you want to understand them, please see related kernel source 
 *   (and my comments :).
 * ----------------------------------------------------------------------
 */

/*
 * ----------------------------------------------------------------------
 * ircomm_receive_data()
 *
 * like interrupt handler in the serial.c,we receive data when 
 * ircomm_data_indication comes
 * ----------------------------------------------------------------------
 */



/* 
 * irvtd_write_to_tty
 * send incoming/queued data to tty
 */

static void irvtd_write_to_tty( void *instance ){
	
	int status, c, flag;
	
	struct sk_buff *skb;
	struct irvtd_cb *driver = (struct irvtd_cb *)instance;
	struct tty_struct *tty = driver->tty;
	
	/* does instance still exist ? should be checked */
	ASSERT(driver->magic == IRVTD_MAGIC, return;);
	
	if(driver->rx_disable ){
		DEBUG(0,__FUNCTION__"rx_disable is true:do_nothing..\n");
		return;
	}
	
	skb = skb_dequeue(&driver->rxbuff);
	ASSERT(skb != NULL, return;); /* there's nothing */
	IS_SKB(skb, return;);
	
#ifdef IRVTD_DEBUG_RX
	printk("received data:");
	{
		int i;
		for ( i=0;i<skb->len;i++)
			printk("%02x ", skb->data[i]);
		printk("\n");
	}
#endif
	
	status = driver->comm->peer_line_status & driver->read_status_mask;
	
	/* 
	 * FIXME: we must do ircomm_parse_ctrl() here, instead of 
	 * ircomm_common.c!! 
	 */
	

	/* 
	 * if there are too many errors which make a character ignored,
	 * drop characters
	 */

	if(status & driver->ignore_status_mask){
		DEBUG(0,__FUNCTION__":some error:ignore characters.\n");
		dev_kfree_skb(skb);
		return;
	}
	
	c = MIN(skb->len, (TTY_FLIPBUF_SIZE - tty->flip.count));
	DEBUG(4, __FUNCTION__"skb_len=%d, tty->flip.count=%d \n"
	      ,(int)skb->len, tty->flip.count);
	
	if (driver->comm->peer_break_signal ) {
		driver->comm->peer_break_signal = 0;
		DEBUG(0,"handling break....\n");
		
		flag = TTY_BREAK;
		if (driver->flags & IRVTD_ASYNC_SAK)
			/*
			 * do_SAK() seems to be an implementation of the 
			 * idea called "Secure Attention Key",
			 * which seems to be discribed in "Orange book".
			 * (which is published by U.S.military!!?? ,
			 * see source of do_SAK())
			 *
			 * but what kind of security do we need 
			 * when we use infrared communication??? :p)
			 */
			do_SAK(tty);
	}else if (status & LSR_PE)
		flag = TTY_PARITY;
	else if (status & LSR_FE)
		flag = TTY_FRAME;
	else if (status & LSR_OE)
		flag = TTY_OVERRUN;
	else 
		flag = TTY_NORMAL;
	
	if(c){
		DEBUG(0,"writing %d chars to tty\n",c);
		memset(tty->flip.flag_buf_ptr, flag, c);
		memcpy(tty->flip.char_buf_ptr, skb->data, c);
		tty->flip.flag_buf_ptr += c;
		tty->flip.char_buf_ptr += c;
		tty->flip.count += c;
		skb_pull(skb,c);
	}

	if(skb->len == 0)
		dev_kfree_skb(skb);
	else
	{
		/* queue rest of data again */
		DEBUG(0,__FUNCTION__":retrying frame!\n");
		skb_queue_head( &driver->rxbuff, skb );
	}
	
	/*
	 * in order to optimize this routine, these two tasks should be
	 * queued in following order
	 * ( see run_task_queue() and queue_task() in tqueue.h
	 */
	if(skb_queue_len(&driver->rxbuff))
		/* let me try again! */
		queue_task(&driver->rx_tqueue, &tq_timer);
	if(c)
		/* read your buffer! */
		queue_task(&tty->flip.tqueue, &tq_timer);
	

	if(skb_queue_len(&driver->rxbuff)< IRVTD_RX_QUEUE_LOW
	   &&  driver->ttp_stoprx){
		irttp_flow_request(driver->comm->tsap, FLOW_START);
		driver->ttp_stoprx = 0;
	}
}

void irvtd_receive_data(void *instance, void *sap, struct sk_buff *skb){
	
	struct irvtd_cb *driver = (struct irvtd_cb *)instance;

	ASSERT(driver != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);

	/* queue incoming data and make bottom half handler ready */

	skb_queue_tail( &driver->rxbuff, skb );
	if(skb_queue_len(&driver->rxbuff) == 1)
		irvtd_write_to_tty(driver);
	if(skb_queue_len(&driver->rxbuff) > IRVTD_RX_QUEUE_HIGH){
		irttp_flow_request(driver->comm->tsap, FLOW_STOP);
		driver->ttp_stoprx = 1;
	}
	return;
}

#if 0
void irvtd_receive_data(void *instance, void *sap, struct sk_buff *skb){
	
	int flag,status;
	__u8 c;
	struct tty_struct *tty;
	struct irvtd_cb *driver = (struct irvtd_cb *)instance;

	ASSERT(driver != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);

	if(driver->rx_disable ){
		DEBUG(0,__FUNCTION__"rx_disable is true:do nothing\n");
		return;
	}

	tty = driver->tty;
	status = driver->comm->peer_line_status & driver->read_status_mask;

	c = MIN(skb->len, (TTY_FLIPBUF_SIZE - tty->flip.count));
	DEBUG(0, __FUNCTION__"skb_len=%d, tty->flip.count=%d \n"
	      ,(int)skb->len, tty->flip.count);

#ifdef IRVTD_DEBUG_RX
	printk("received data:");
	{
		int i;
		for ( i=0;i<skb->len;i++)
			printk("%02x ", skb->data[i]);
		printk("\n");
	}
#endif

	/* 
	 * if there are too many errors which make a character ignored,
	 * drop characters
	 */

	if(status & driver->ignore_status_mask){
		DEBUG(0,__FUNCTION__"I/O error:ignore characters.\n");
		dev_kfree_skb(skb, FREE_READ);
		return;
	} 
	
	if (driver->comm->peer_break_signal ) {
		driver->comm->peer_break_signal = 0;
		DEBUG(0,"handling break....\n");
		
		flag = TTY_BREAK;
		if (driver->flags & IRVTD_ASYNC_SAK)
			/*
			 * do_SAK() seems to be an implementation of the 
			 * idea called "Secure Attention Key",
			 * which seems to be discribed in "Orange book".
			 * (which is published by U.S.military!!?? )
			 * see source of do_SAK() but what is "Orange book"!?
			 */
			do_SAK(tty);
	}else if (status & LSR_PE)
		flag = TTY_PARITY;
	else if (status & LSR_FE)
		flag = TTY_FRAME;
	else if (status & LSR_OE)
		flag = TTY_OVERRUN;
	else 
		flag = TTY_NORMAL;

	if(c){
		DEBUG(0,"writing %d chars to tty\n",c);
		memset(tty->flip.flag_buf_ptr, flag, c);
		memcpy(tty->flip.char_buf_ptr, skb->data, c);
		tty->flip.flag_buf_ptr += c;
		tty->flip.char_buf_ptr += c;
		tty->flip.count += c;
		skb_pull(skb,c);
		queue_task_irq_off(&tty->flip.tqueue, &tq_timer);
	}
	if(skb->len >0)
		DEBUG(0,__FUNCTION__":dropping frame!\n");
	dev_kfree_skb(skb, FREE_READ);
	DEBUG(4,__FUNCTION__":done\n");
}
#endif

/*
 * ----------------------------------------------------------------------
 * indication/confirmation handlers:
 * they will be registerd in irvtd_startup() to know that we
 * discovered (or we are discovered by) remote device.
 * ----------------------------------------------------------------------
 */

/* this function is called whed ircomm_attach_cable succeed */

void irvtd_attached(struct ircomm_cb *comm){

	ASSERT(comm != NULL, return;);
	ASSERT(comm->magic == IRCOMM_MAGIC, return;);

	DEBUG(0,"irvtd_attached:sending connect_request"
	      " for servicetype(%d)..\n",comm->servicetype);
	ircomm_connect_request(comm, SAR_DISABLE );
}


/*
 * irvtd_connect_confirm()
 *  ircomm_connect_request which we have send have succeed!
 */

void irvtd_connect_confirm(void *instance, void *sap, struct qos_info *qos,
			   int max_sdu_size, struct sk_buff *skb){

	struct irvtd_cb *driver = (struct irvtd_cb *)instance;
	ASSERT(driver != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);

	/*
	 * sending initial control parameters here
	 *
	 * TODO: it must be done in ircomm_connect_request()
	 */
#if 1
	if(driver->comm->servicetype ==	THREE_WIRE_RAW)
		return;                /* do nothing */

	ircomm_append_ctrl(driver->comm, SERVICETYPE);
	/* ircomm_append_ctrl(self, DATA_RATE); */
	ircomm_append_ctrl(driver->comm, DATA_FORMAT);
	ircomm_append_ctrl(driver->comm, FLOW_CONTROL);
	ircomm_append_ctrl(driver->comm, XON_XOFF_CHAR);
	/* ircomm_append_ctrl(driver->comm, ENQ_ACK_CHAR); */

	switch(driver->comm->servicetype){
	case CENTRONICS:
		break;

	case NINE_WIRE:
		ircomm_append_ctrl(driver->comm, DTELINE_STATE);
		break;
	default:
	}
	ircomm_control_request(driver->comm);
#endif
	

	wake_up_interruptible(&driver->open_wait);
}

/*
 * irvtd_connect_indication()
 *  we are discovered and being requested to connect by remote device !
 */

void irvtd_connect_indication(void *instance, void *sap, struct qos_info *qos,
			      int max_sdu_size, struct sk_buff *skb)
{

	struct irvtd_cb *driver = (struct irvtd_cb *)instance;
	struct ircomm_cb *comm = (struct ircomm_cb *)sap;
	ASSERT(driver != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);
	ASSERT(comm != NULL, return;);
	ASSERT(comm->magic == IRCOMM_MAGIC, return;);

	DEBUG(4,"irvtd_connect_indication:sending connect_response...\n");

	/*TODO: connect_response should send initialcontrolparameters! TH*/

	ircomm_connect_response(comm, NULL, SAR_DISABLE );

	wake_up_interruptible(&driver->open_wait);
}



void irvtd_disconnect_indication(void *instance, void *sap , LM_REASON reason,
				 struct sk_buff *skb){

	struct irvtd_cb *driver = (struct irvtd_cb *)instance;
	ASSERT(driver != NULL, return;);
	ASSERT(driver->tty != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);

	DEBUG(4,"irvtd_disconnect_indication:\n");
	tty_hangup(driver->tty);
}

/*
 * irvtd_control_indication
 *
 */


void irvtd_control_indication(void *instance, void *sap, LOCAL_FLOW flow){

	struct irvtd_cb *driver = (struct irvtd_cb *)instance;
	__u8 pi; /* instruction of control channel */

	ASSERT(driver != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);

	DEBUG(0,"irvtd_control_indication:\n");

	pi = driver->comm->pi;

	switch(pi){

	case DCELINE_STATE:
	driver->msr = driver->comm->peer_dce;

	if(driver->msr & (DELTA_CTS|DELTA_DSR|DELTA_RI|DELTA_DCD)){
		if(driver->msr & DELTA_CTS)
			driver->icount.cts++;
		if(driver->msr & DELTA_DSR)
			driver->icount.dsr++;
		if(driver->msr & DELTA_RI)
			driver->icount.rng++;
		if(driver->msr & DELTA_DCD)
			driver->icount.dcd++;
		wake_up_interruptible(&driver->delta_msr_wait);
	}

	if ((driver->flags & IRVTD_ASYNC_CHECK_CD) && (driver->msr & DELTA_DCD)) {

		DEBUG(0,"CD now %s...\n",
		      (driver->msr & MSR_DCD) ? "on" : "off");

		if (driver->msr & DELTA_DCD)
			wake_up_interruptible(&driver->open_wait);
		else if (!((driver->flags & IRVTD_ASYNC_CALLOUT_ACTIVE) &&
			   (driver->flags & IRVTD_ASYNC_CALLOUT_NOHUP))) {

                        DEBUG(0,"irvtd_control_indication:hangup..\n");
			tty_hangup(driver->tty);
		}
	}

	if (driver->comm->flow_ctrl & USE_CTS) {
		if (driver->tty->hw_stopped) {
			if (driver->msr & MSR_CTS) {
				DEBUG(0,"CTS tx start...\n");

				driver->cts_stoptx = 0;
				driver->tty->hw_stopped = driver->ttp_stoptx;
				/*  
				 * replacement of 
				 * rs_sched_event(info, RS_EVENT_WRITE_WAKEUP)
				 * in serial.c
				 */

				if ((driver->tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
				    driver->tty->ldisc.write_wakeup)
					(driver->tty->ldisc.write_wakeup)(driver->tty);

				wake_up_interruptible(&driver->tty->write_wait);
				return;
			}
		} else {
			if (!(driver->msr & MSR_CTS)) {
				DEBUG(0,"CTS tx stop...");

				driver->cts_stoptx = 1;
				driver->tty->hw_stopped = 1;
/* 				driver->IER &= ~UART_IER_THRI; */
/* 				serial_out(info, UART_IER, info->IER); */
                        }
                }
        }


	break;

	case TX_READY:
		driver->ttp_stoptx = 0;
		driver->tty->hw_stopped = driver->cts_stoptx;

		/* 
		 * driver->tty->write_wait will keep asleep if
		 * our txbuff is not empty.
		 * so if we can really send a packet now,
		 * send it and then wake it up.
		 */

		if(driver->cts_stoptx)
			break;

		flush_txbuff(driver);
		if ((driver->tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && 
		     driver->tty->ldisc.write_wakeup)
			(driver->tty->ldisc.write_wakeup)(driver->tty);
		break;

	case TX_BUSY:
		driver->ttp_stoptx = driver->tty->hw_stopped = 1;
		break;
	default:
		DEBUG(0,"irvtd:unknown control..\n");
	
	}
}


/*
 * ----------------------------------------------------------------------
 * irvtd_open() and friends
 *
 * 
 * ----------------------------------------------------------------------
 */


static int irvtd_block_til_ready(struct tty_struct *tty, struct file * filp,
				 struct irvtd_cb *driver)
{

 	struct wait_queue wait = { current, NULL };
	int		retval;
	int		do_clocal = 0;

	/*
	 * If the device is in the middle of being closed, then block
	 * (sleep) until it's done, and (when being woke up)then try again.
	 */

	if (tty_hung_up_p(filp) ||
	    (driver->flags & IRVTD_ASYNC_CLOSING)) {
		if (driver->flags & IRVTD_ASYNC_CLOSING)
			interruptible_sleep_on(&driver->close_wait);
#ifdef DO_RESTART
		if (driver->flags & IRVTD_ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
#else
		return -EAGAIN;
#endif
	}

	/*
	 * If this is a callout device, then just make sure the normal
	 * device isn't being used.
	 */

	if (tty->driver.subtype == IRVTD_TYPE_CALLOUT) {
		if (driver->flags & IRVTD_ASYNC_NORMAL_ACTIVE)
			return -EBUSY;
		if ((driver->flags & IRVTD_ASYNC_CALLOUT_ACTIVE) &&
		    (driver->flags & IRVTD_ASYNC_SESSION_LOCKOUT) &&
		    (driver->session != current->session))
			return -EBUSY;
		if ((driver->flags & IRVTD_ASYNC_CALLOUT_ACTIVE) &&
		    (driver->flags & IRVTD_ASYNC_PGRP_LOCKOUT) &&
		    (driver->pgrp != current->pgrp))
			return -EBUSY;

		driver->flags |= IRVTD_ASYNC_CALLOUT_ACTIVE;
		return 0;
	}
	
	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */

	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		if (driver->flags & IRVTD_ASYNC_CALLOUT_ACTIVE)
			return -EBUSY;

		driver->flags |= IRVTD_ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	if (driver->flags & IRVTD_ASYNC_CALLOUT_ACTIVE) {
		if (driver->normal_termios.c_cflag & CLOCAL)
			do_clocal = 1;
	} else {
		if (tty->termios->c_cflag & CLOCAL)
			do_clocal = 1;
	}
	
	/*
	 * We wait until ircomm_connect_request() succeed or
	 *   ircomm_connect_indication comes
	 *
	 * This is what is written in serial.c:
	 * "Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, driver->count is dropped by one, so that
	 * rs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal."
	 */

	retval = 0;
	add_wait_queue(&driver->open_wait, &wait);

	DEBUG(0,"block_til_ready before block: line%d, count = %d\n",
	       driver->line, driver->count);

	cli();
	if (!tty_hung_up_p(filp)) 
		driver->count--;
	sti();
	driver->blocked_open++;


	while (1) {
		current->state = TASK_INTERRUPTIBLE;

	if (!(driver->flags & IRVTD_ASYNC_CALLOUT_ACTIVE) &&
	    (driver->comm->state == COMM_CONN)){
		/* 
		 * signal DTR and RTS
		 */
		driver->comm->dte = driver->mcr |= (MCR_DTR | MCR_RTS |DELTA_DTR|DELTA_RTS);

		ircomm_append_ctrl(driver->comm, DTELINE_STATE);
		ircomm_control_request(driver->comm);
	}

		if (tty_hung_up_p(filp) ||
		    !(driver->flags & IRVTD_ASYNC_INITIALIZED)) {
#ifdef DO_RESTART
			if (driver->flags & IRVTD_ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;	
#else
			retval = -EAGAIN;
#endif
			break;
		}

		/*
		 * if clocal == 0 or received DCD or state become CONN,then break
		 */

		if (!(driver->flags & IRVTD_ASYNC_CALLOUT_ACTIVE) &&
		    !(driver->flags & IRVTD_ASYNC_CLOSING) &&
		    (driver->comm->state == COMM_CONN) && 
		    ( do_clocal || (driver->msr & MSR_DCD) )
		    )
			break;

		if(signal_pending(current)){
			retval = -ERESTARTSYS;
			break;
		}

#ifdef IRVTD_DEBUG_OPEN
		printk(KERN_INFO"block_til_ready blocking:"
		       " ttys%d, count = %d\n", driver->line, driver->count);
#endif
		schedule();
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&driver->open_wait, &wait);

	if (!tty_hung_up_p(filp))
		driver->count++;
	driver->blocked_open--;
#ifdef IRVTD_DEBUG_OPEN
	printk("block_til_ready after blocking: ttys%d, count = %d\n",
	       driver->line, driver->count);
#endif
	if (retval)
		return retval;
	driver->flags |= IRVTD_ASYNC_NORMAL_ACTIVE;
	return 0;
}	

static void change_speed(struct irvtd_cb *driver){

	unsigned cflag,cval;

	if (!driver->tty || !driver->tty->termios || !driver->comm)
		return;
	cflag = driver->tty->termios->c_cflag;



	/*
	 * change baud rate here. but not implemented now
	 */




	/* 
	 * byte size and parity
	 */
	switch (cflag & CSIZE) {
	      case CS5: cval = 0x00; break;
	      case CS6: cval = 0x01; break;
	      case CS7: cval = 0x02; break;
	      case CS8: cval = 0x03; break;
	      default:  cval = 0x00; break;	/* too keep GCC shut... */
	}
	if (cflag & CSTOPB) {      /* use 2 stop bit mode */
		cval |= 0x04;
	}
	if (cflag & PARENB)
		cval |= 0x08;
	if (!(cflag & PARODD))
		cval |= 0x10;
	
	/* CTS flow control flag and modem status interrupts */

	if (cflag & CRTSCTS)
		driver->comm->flow_ctrl |= USE_CTS;
	else
		driver->comm->flow_ctrl |= ~USE_CTS;
	
	if (cflag & CLOCAL)
		driver->flags &= ~IRVTD_ASYNC_CHECK_CD;
	else
		driver->flags |= IRVTD_ASYNC_CHECK_CD;
	
	/*
	 * Set up parity check flag
	 */

	driver->read_status_mask = LSR_OE ;
	if (I_INPCK(driver->tty))
		driver->read_status_mask |= LSR_FE | LSR_PE;
	if (I_BRKINT(driver->tty) || I_PARMRK(driver->tty))
		driver->read_status_mask |= LSR_BI;
	
	driver->ignore_status_mask = 0;

	if (I_IGNBRK(driver->tty)) {
		driver->ignore_status_mask |= LSR_BI;
		driver->read_status_mask |= LSR_BI;
		/*
		 * If we're ignore parity and break indicators, ignore 
		 * overruns too.  (For real raw support).
		 */
		if (I_IGNPAR(driver->tty)) {
			driver->ignore_status_mask |= LSR_OE | \
				LSR_PE | LSR_FE;
			driver->read_status_mask |= LSR_OE | \
				LSR_PE | LSR_FE;
		}
	}
	driver->comm->data_format = cval;
	ircomm_append_ctrl(driver->comm, DATA_FORMAT);
 	ircomm_append_ctrl(driver->comm, FLOW_CONTROL);
	ircomm_control_request(driver->comm);

	/* output to IrCOMM here*/
}




static int irvtd_startup(struct irvtd_cb *driver){

	struct notify_t irvtd_notify;


	DEBUG(4,"irvtd_startup:\n" );

	/*
	 * initialize our tx/rx buffer
	 */

	if(driver->flags & IRVTD_ASYNC_INITIALIZED)
		return(0);

	skb_queue_head_init(&driver->rxbuff);
	driver->rx_tqueue.data = driver;
	driver->rx_tqueue.routine = irvtd_write_to_tty;

	if(!driver->txbuff){
		driver->txbuff = dev_alloc_skb(COMM_DEFAULT_DATA_SIZE); 
		if (!driver->txbuff){
			DEBUG(0,"irvtd_open():alloc_skb failed!\n");
			return -ENOMEM;
		}

		skb_reserve(driver->txbuff, COMM_HEADER_SIZE);
	}

	irda_notify_init(&irvtd_notify);
	irvtd_notify.data_indication = irvtd_receive_data;
	irvtd_notify.connect_confirm = irvtd_connect_confirm;
	irvtd_notify.connect_indication = irvtd_connect_indication;
	irvtd_notify.disconnect_indication = irvtd_disconnect_indication;
	irvtd_notify.flow_indication = irvtd_control_indication;
	irvtd_notify.instance = driver;
	strncpy( irvtd_notify.name, "irvtd", NOTIFY_MAX_NAME);

	/*
	 * register ourself as a service user of IrCOMM
	 *	   TODO: other servicetype(i.e. 3wire,3wireraw) 
	 */

	driver->comm = ircomm_attach_cable(NINE_WIRE, irvtd_notify,
					   irvtd_attached);
	if(driver->comm == NULL)
		return -ENODEV;
	
	/*
	 * TODO:we have to initialize control-channel here!
	 *   i.e.set something into RTS,CTS and so on....
	 */

	if (driver->tty)
		clear_bit(TTY_IO_ERROR, &driver->tty->flags);

	change_speed(driver);

	driver->flags |= IRVTD_ASYNC_INITIALIZED;
	return 0;
}


int irvtd_open(struct tty_struct * tty, struct file * filp){
	
	struct irvtd_cb *driver;
	int retval;
	int line;

	DEBUG(4, "irvtd_open():\n");

	line = MINOR(tty->device) - tty->driver.minor_start;
	if ((line <0) || (line >= COMM_MAX_TTY))
		return -ENODEV;
	driver = irvtd[line];
	driver->line = line;
	driver->count++;

	DEBUG(0, "irvtd_open : %s%d count %d\n", tty->driver.name, line, 
	      driver->count);
	
	tty->driver_data = driver;
	driver->tty = tty;

	
	/* 
	 * start up discovering process and ircomm_layer 
	 */
	
	retval = irvtd_startup(driver);
	if (retval)
		return retval;
	MOD_INC_USE_COUNT;

	retval = irvtd_block_til_ready(tty, filp, driver);
	if (retval){
		DEBUG(0,"irvtd_open returning after block_til_ready with %d\n",
		      retval);
                return retval;
	}

	if ((driver->count == 1) && driver->flags & IRVTD_ASYNC_SPLIT_TERMIOS){
		if(tty->driver.subtype == IRVTD_TYPE_NORMAL)
			*tty->termios = driver->normal_termios;
		else
			*tty->termios = driver->callout_termios;

 		change_speed(driver);
	}
	
	driver->session = current->session;
	driver->pgrp = current->pgrp;
	driver->rx_disable = 0;
	return (0);
}





/*
 * ----------------------------------------------------------------------
 * irvtd_close() and friends
 *
 * most of this function is stolen from serial.c
 * ----------------------------------------------------------------------
 */


static void irvtd_shutdown(struct irvtd_cb * driver)
{
	unsigned long	flags;

	if (!(driver->flags & IRVTD_ASYNC_INITIALIZED))
		return;

	DEBUG(4,"irvtd_shutdown:\n");

	/*
	 * This comment is written in serial.c:
	 *
	 * clear delta_msr_wait queue to avoid mem leaks: we may free the irq
	 * here so the queue might never be waken up
	 */
 	wake_up_interruptible(&driver->delta_msr_wait);
	
	/* clear DTR and RTS */
	if (!driver->tty || (driver->tty->termios->c_cflag & HUPCL))
 		driver->mcr &= ~(MCR_DTR|MCR_RTS);

	driver->comm->dte = driver->mcr;
	ircomm_append_ctrl(driver->comm, DTELINE_STATE );
	ircomm_control_request(driver->comm);


	save_flags(flags); cli(); /* Disable interrupts */

	if (driver->tty)
		set_bit(TTY_IO_ERROR, &driver->tty->flags);
	
	ircomm_detach_cable(driver->comm);

	/*
	 * Free the transmit buffer here 
	 */
	if(driver->txbuff){
		dev_kfree_skb(driver->txbuff);     /* is it OK?*/
		driver->txbuff = NULL;
	}

	driver->flags &= ~IRVTD_ASYNC_INITIALIZED;
	restore_flags(flags);
}



void irvtd_close(struct tty_struct * tty, struct file * filp){

	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;
	int line;
	unsigned long flags;

	DEBUG(0, "irvtd_close:refc(%d)\n",ircomm_vsd_refcount);

	ASSERT(driver != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);

	save_flags(flags);cli();

	/* 
	 * tty_hung_up_p() is defined as 
	 *   " return(filp->f_op == &hung_up_tty_fops); "
	 *	 see driver/char/tty_io.c
	 */

	if(tty_hung_up_p(filp)){
		MOD_DEC_USE_COUNT;
		restore_flags(flags);
		return;
	}
	

	line = MINOR(tty->device) - tty->driver.minor_start;
	DEBUG(0, "irvtd_close : %s%d count %d\n", tty->driver.name, line, 
	      driver->count);

	if ((tty->count == 1) && (driver->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Driver->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * ircomm service layer won't be shutdown.
		 */
		printk(KERN_ERR"irvtd_close: bad serial port count;" 
		       "tty->count is 1, but driver->count is %d\n", driver->count);
		driver->count = 1;
	}
	if (--driver->count < 0) {
		printk("irvtd_close: bad count for line%d: %d\n",
		       line, driver->count);
		driver->count = 0;
	}

	if (driver->count) { 	/* do nothing */
		MOD_DEC_USE_COUNT;
		restore_flags(flags);
		return; 
	}

	driver->flags |= IRVTD_ASYNC_CLOSING;
	
	/*
	 * Save the termios structure, since this port may have
	 * separate termios for callout and dialin.
	 */

	if (driver->flags & IRVTD_ASYNC_NORMAL_ACTIVE)
		driver->normal_termios = *tty->termios;
	if (driver->flags & IRVTD_ASYNC_CALLOUT_ACTIVE)
		driver->callout_termios = *tty->termios;
	
	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (driver->closing_wait != IRVTD_ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, driver->closing_wait);
	
	/* 
	 * Now we stop accepting input.
	 */

	driver->rx_disable = TRUE;

	/* 
	 * Now we flush our buffer.., and shutdown ircomm service layer
	 */

	/* drop our tx/rx buffer */
 	if (tty->driver.flush_buffer) 
 		tty->driver.flush_buffer(tty);  

	while(skb_queue_len(&driver->rxbuff)){
		struct sk_buff *skb;
		skb = skb_dequeue( &driver->rxbuff);
		dev_kfree_skb(skb);
	}

	/* drop users buffer? */
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);



	tty->closing = 0;
	driver->tty = NULL;

	/*
	 * ad-hoc coding:
	 * we wait 2 sec before ircomm_detach_cable so that 
	 * irttp will send all contents of its queue
	 */

#if 0
	if (driver->blocked_open) {
		if (driver->close_delay) {
#endif

			/* kill time */
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(driver->close_delay + 2*HZ);
#if 0
		}
		wake_up_interruptible(&driver->open_wait);
	}
#endif
	
	driver->flags &= ~(IRVTD_ASYNC_NORMAL_ACTIVE|
			   IRVTD_ASYNC_CALLOUT_ACTIVE|
			   IRVTD_ASYNC_CLOSING);
        wake_up_interruptible(&driver->close_wait); 

	irvtd_shutdown(driver);
	MOD_DEC_USE_COUNT;
        restore_flags(flags);
	DEBUG(4,"irvtd_close:done:refc(%d)\n",ircomm_vsd_refcount);
}



/*
 * ----------------------------------------------------------------------
 * irvtd_write() and friends
 * This routine will be called when something data are passed from
 * kernel or user.
 *
 * NOTE:I have stolen copy_from_user() from 2.0.30 kernel(linux/isdnif.h)
 * to access user space of memory carefully. Thanks a lot!:)
 * ----------------------------------------------------------------------
 */

int irvtd_write(struct tty_struct * tty, int from_user,
			const unsigned char *buf, int count){

	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;
	int c = 0;
	int wrote = 0;
	struct sk_buff *skb = NULL;
	__u8 *frame;

	DEBUG(4, "irvtd_write():\n");

	if (!tty || !driver->txbuff)
                return 0;


	
	while(1){
		skb = driver->txbuff;
		
		c = MIN(count, (skb_tailroom(skb) - COMM_HEADER_SIZE));
		if (c <= 0) 
			break;

		/* write to the frame */


		frame = skb_put(skb,c);
		if(from_user){
			copy_from_user(frame,buf,c);
		} else
			memcpy(frame, buf, c);

		/* flush the frame */
		irvtd_flush_chars(tty);
		wrote += c;
		count -= c;
	}
	return (wrote);
}

/*
 * ----------------------------------------------------------------------
 * irvtd_put_char()
 * This routine is called by the kernel to pass a single character.
 * If we exausted our buffer,we can ignore the character!
 * ----------------------------------------------------------------------
 */
void irvtd_put_char(struct tty_struct *tty, unsigned char ch){

	__u8 *frame ;
	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;
	struct sk_buff *skb = driver->txbuff;

	ASSERT(tty->driver_data != NULL, return;);

	DEBUG(4, "irvtd_put_char:\n");
	if(!driver->txbuff)
		return;

	DEBUG(4, "irvtd_put_char(0x%02x) skb_len(%d) MAX(%d):\n",
	      (int)ch ,(int)skb->len,
	      driver->comm->maxsdusize - COMM_HEADER_SIZE);

	/* append a character  */

	frame = skb_put(skb,1);
	frame[0] = ch;
	return;
}

/*
 * ----------------------------------------------------------------------
 * irvtd_flush_chars() and friend
 * This routine will be called after a series of characters was written using 
 * irvtd_put_char().We have to send them down to IrCOMM.
 * ----------------------------------------------------------------------
 */

static void flush_txbuff(struct irvtd_cb *driver){
	
	struct sk_buff *skb = driver->txbuff;
	struct tty_struct *tty = driver->tty;
	ASSERT(tty != NULL, return;);

#ifdef IRVTD_DEBUG_TX
	printk("flush_txbuff:");
	{
		int i;
		for ( i=0;i<skb->len;i++)
			printk("%02x", skb->data[i]);
		printk("\n");
	}
#else
	DEBUG(4, "flush_txbuff:count(%d)\n",(int)skb->len);
#endif

	/* add "clen" field */
	skb_push(skb,1);
	skb->data[0]=0;          /* without control channel */

	ircomm_data_request(driver->comm, driver->txbuff);

	/* allocate new frame */
	skb = driver->txbuff = dev_alloc_skb(driver->comm->max_txbuff_size);
	if (skb == NULL){
		printk(KERN_ERR"flush_txbuff():alloc_skb failed!\n");
	} else {
		skb_reserve(skb, COMM_HEADER_SIZE);
	}
	wake_up_interruptible(&driver->tty->write_wait);
}

void irvtd_flush_chars(struct tty_struct *tty){

	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;
	if(!driver || driver->magic != IRVTD_MAGIC || !driver->txbuff){
		DEBUG(0,"irvtd_flush_chars:null structure:ignore\n");
		return;
	}
	DEBUG(4, "irvtd_flush_chars():\n");

	while(tty->hw_stopped){
		DEBUG(4,"irvtd_flush_chars:hw_stopped:sleep..\n");
		tty_wait_until_sent(tty,0);
		DEBUG(4,"irvtd_flush_chars:waken up!\n");
		if(!driver->txbuff->len)
			return;
	}

	flush_txbuff(driver);
}




/*
 * ----------------------------------------------------------------------
 * irvtd_write_room()
 * This routine returns the room that our buffer has now.
 *
 * NOTE: 
 * driver/char/n_tty.c drops a character(s) when this routine returns 0,
 * and then linux will be frozen after a few minutes :(    why? bug?
 * ( I found this on linux-2.0.33 )
 * So this routine flushes a buffer if there is few room,     TH
 * ----------------------------------------------------------------------
 */

int irvtd_write_room(struct tty_struct *tty){

	int ret;
	struct sk_buff *skb = (struct sk_buff *)((struct irvtd_cb *) tty->driver_data)->txbuff;

	if(!skb){
		DEBUG(0,"irvtd_write_room:NULL skb\n");
		return(0);
	}

	ret = skb_tailroom(skb) - COMM_HEADER_SIZE;

	if(ret < 0){
		DEBUG(0,"irvtd_write_room:error:room is %d!",ret);
		ret = 0;
	}
	DEBUG(4, "irvtd_write_room:\n");
	DEBUG(4, "retval(%d)\n",ret);


	/* flush buffer automatically to avoid kernel freeze :< */
	if(ret < 8)    /* why 8? there's no reason :) */
		irvtd_flush_chars(tty);

	return(ret);
}

/*
 * ----------------------------------------------------------------------
 * irvtd_chars_in_buffer()
 * This function returns how many characters which have not been sent yet 
 * are still in buffer.
 * ----------------------------------------------------------------------
 */

int irvtd_chars_in_buffer(struct tty_struct *tty){

	struct sk_buff *skb = 
		(struct sk_buff *) ((struct irvtd_cb *)tty->driver_data) ->txbuff;
	DEBUG(4, "irvtd_chars_in_buffer()\n");

	if(!skb){
		printk(KERN_ERR"irvtd_chars_in_buffer:NULL skb\n");
		return(0);
	}
	return (skb->len );
}

/*
 * ----------------------------------------------------------------------
 * irvtd_break()
 * routine which turns the break handling on or off
 * ----------------------------------------------------------------------
 */

static void irvtd_break(struct tty_struct *tty, int break_state){
	
 	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;
	unsigned long flags;

	DEBUG(0, __FUNCTION__"()\n");
	ASSERT(tty->driver_data != NULL, return;);
	ASSERT(driver->magic == IRVTD_MAGIC, return;);
	
	save_flags(flags);cli();
	if (break_state == -1)
	{
		driver->comm->break_signal = 0x01;
		ircomm_append_ctrl(driver->comm, BREAK_SIGNAL);
		ircomm_control_request(driver->comm);
	}
	else
	{
		driver->comm->break_signal = 0x00;
		ircomm_append_ctrl(driver->comm, BREAK_SIGNAL);
		ircomm_control_request(driver->comm);
	}

	restore_flags(flags);
}

/*
 * ----------------------------------------------------------------------
 * irvtd_ioctl() and friends
 * This routine allows us to implement device-specific ioctl's.
 * If passed ioctl number (i.e.cmd) is unknown one, we should return 
 * ENOIOCTLCMD.
 *
 * TODO: we can't use setserial on IrCOMM because some ioctls are not implemented.
 * we should add some ioctls and make some tool which is resemble to setserial.
 * ----------------------------------------------------------------------
 */

static int get_modem_info(struct irvtd_cb * driver, unsigned int *value)
{
	unsigned int result;
	result =  ((driver->mcr & MCR_RTS) ? TIOCM_RTS : 0)
		| ((driver->mcr & MCR_DTR) ? TIOCM_DTR : 0)
		| ((driver->msr & DELTA_DCD) ? TIOCM_CAR : 0)
		| ((driver->msr & DELTA_RI) ? TIOCM_RNG : 0)
		| ((driver->msr & DELTA_DSR) ? TIOCM_DSR : 0)
		| ((driver->msr & DELTA_CTS) ? TIOCM_CTS : 0);
	put_user(result,value);
	return 0;
}

static int set_modem_info(struct irvtd_cb * driver, unsigned int cmd,
			  unsigned int *value)
{ 
	int error;
	unsigned int arg;

	error = get_user(arg, value);
	if(error)
		return error;

	switch (cmd) {
	case TIOCMBIS: 
		if (arg & TIOCM_RTS) 
			driver->mcr |= MCR_RTS;
		if (arg & TIOCM_DTR)
			driver->mcr |= MCR_DTR;
		break;
		
	case TIOCMBIC:
		if (arg & TIOCM_RTS)
			driver->mcr &= ~MCR_RTS;
		if (arg & TIOCM_DTR)
 			driver->mcr &= ~MCR_DTR;
 		break;
		
	case TIOCMSET:
 		driver->mcr = ((driver->mcr & ~(MCR_RTS | MCR_DTR))
			       | ((arg & TIOCM_RTS) ? MCR_RTS : 0)
			       | ((arg & TIOCM_DTR) ? MCR_DTR : 0));
		break;
		
	default:
		return -EINVAL;
	}
	
	driver->comm->dte = driver->mcr;
	ircomm_append_ctrl(driver->comm, DTELINE_STATE );
	ircomm_control_request(driver->comm);
	return 0;
}

int irvtd_ioctl(struct tty_struct *tty, struct file * file,
		unsigned int cmd, unsigned long arg){

	int error;
 	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;

	struct icounter_struct cnow;
	struct icounter_struct *p_cuser;	/* user space */


	DEBUG(4,"irvtd_ioctl:requested ioctl(0x%08x)\n",cmd);

#ifdef IRVTD_DEBUG_IOCTL
	{
		/* kill time so that debug messages will come slowly  */
		unsigned long flags;
		save_flags(flags);cli();
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + HZ/4; /*0.25sec*/
		schedule();
		restore_flags(flags);
	}
#endif



	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT) &&
	    (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
		if (tty->flags & (1 << TTY_IO_ERROR)){
			DEBUG(0,"irvtd_ioctl:I/O error...\n");
			return -EIO;
		}
	}
	
	switch (cmd) {

	case TIOCMGET:
		error = verify_area(VERIFY_WRITE, (void *) arg,
				    sizeof(unsigned int));
		if (error)
			return error;
		return get_modem_info(driver, (unsigned int *) arg);

	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		return set_modem_info(driver, cmd, (unsigned int *) arg);
#if 0
	/*
	 * we wouldn't implement them since we don't use serial_struct
	 */
	case TIOCGSERIAL:
		error = verify_area(VERIFY_WRITE, (void *) arg,
				    sizeof(struct serial_struct));
		if (error)
			return error;
		return irvtd_get_serial_info(driver,
				       (struct serial_struct *) arg);
	case TIOCSSERIAL:
		error = verify_area(VERIFY_READ, (void *) arg,
				    sizeof(struct serial_struct));
		if (error)
			return error;
		return irvtd_set_serial_info(driver,
				       (struct serial_struct *) arg);


	case TIOCSERGETLSR: /* Get line status register */
		error = verify_area(VERIFY_WRITE, (void *) arg,
				    sizeof(unsigned int));
		if (error)
			return error;
		else
			return get_lsr_info(driver, (unsigned int *) arg);
#endif		

/*
 *  I think we don't need them
 */
/* 	case TIOCSERCONFIG: */
		

/*
 * They cannot be implemented because we don't use async_struct 
 * which is defined in serial.h
 */

/* 	case TIOCSERGSTRUCT: */
/* 		error = verify_area(VERIFY_WRITE, (void *) arg, */
/* 				    sizeof(struct async_struct)); */
/* 		if (error) */
/* 			return error; */
/* 		memcpy_tofs((struct async_struct *) arg, */
/* 			    driver, sizeof(struct async_struct)); */
/* 		return 0; */
		
/* 	case TIOCSERGETMULTI: */
/* 		error = verify_area(VERIFY_WRITE, (void *) arg, */
/* 				    sizeof(struct serial_multiport_struct)); */
/* 		if (error) */
/* 			return error; */
/* 		return get_multiport_struct(driver, */
/* 					    (struct serial_multiport_struct *) arg); */
/* 	case TIOCSERSETMULTI: */
/* 		error = verify_area(VERIFY_READ, (void *) arg, */
/* 				    sizeof(struct serial_multiport_struct)); */
/* 		if (error) */
/* 			return error; */
/* 		return set_multiport_struct(driver, */
/* 					    (struct serial_multiport_struct *) arg); */
		/*
		 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS)
		 * to change
		 * - mask passed in arg for lines of interest
 		 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
		 * Caller should use TIOCGICOUNT to see which one it was
		 */

	case TIOCMIWAIT:
		while (1) {
			interruptible_sleep_on(&driver->delta_msr_wait);
			/* see if a signal did it */
/* 			if (current->signal & ~current->blocked) */
/* 				return -ERESTARTSYS; */

			if ( ((arg & TIOCM_RNG) && (driver->msr & DELTA_RI))  ||
			     ((arg & TIOCM_DSR) && (driver->msr & DELTA_DSR)) ||
			     ((arg & TIOCM_CD)  && (driver->msr & DELTA_DCD)) ||
			     ((arg & TIOCM_CTS) && (driver->msr & DELTA_CTS))) {
				return 0;
			}
		}
		/* NOTREACHED */


	case TIOCGICOUNT:
		/* 
		 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
		 * Return: write counters to the user passed counter struct
		 * NB: both 1->0 and 0->1 transitions are counted except for
		 *     RI where only 0->1 is counted.
		 */
		error = verify_area(VERIFY_WRITE, (void *) arg,
				    sizeof(struct icounter_struct));
		if (error)
			return error;
		cli();
		cnow = driver->icount;
		sti();
		p_cuser = (struct icounter_struct *) arg;
		put_user(cnow.cts, &p_cuser->cts);
		put_user(cnow.dsr, &p_cuser->dsr);
		put_user(cnow.rng, &p_cuser->rng);
		put_user(cnow.dcd, &p_cuser->dcd);
		return 0;


	case TIOCGSERIAL:
	case TIOCSSERIAL:
	case TIOCSERGETLSR:
 	case TIOCSERCONFIG:
	case TIOCSERGWILD:
	case TIOCSERSWILD:
	case TIOCSERGSTRUCT:
	case TIOCSERGETMULTI:
	case TIOCSERSETMULTI:
		DEBUG(0,"irvtd_ioctl:sorry, ioctl(0x%08x)is not implemented\n",cmd);
		return -ENOIOCTLCMD;  /* ioctls which are imcompatible with serial.c */

	case TCSETS:
	case TCGETS:
	case TCFLSH:
	default:
		return -ENOIOCTLCMD;  /* ioctls which we must not touch */
	}
	return 0;
}



/*
 * ----------------------------------------------------------------------
 * irvtd_set_termios()
 * This is called when termios is changed.
 * If things that changed is significant for us,(i.e. changing baud rate etc.)
 * send something to peer device.
 * ----------------------------------------------------------------------
 */

void irvtd_set_termios(struct tty_struct *tty, struct termios * old_termios){

	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;

	ASSERT(driver != NULL,return;);
	ASSERT(driver->magic == IRVTD_MAGIC ,return;);

	DEBUG(0, "irvtd_set_termios:\n");
	return;

	if((tty->termios->c_cflag == old_termios->c_cflag) &&
	   (RELEVANT_IFLAG(tty->termios->c_iflag) 
	    == RELEVANT_IFLAG(old_termios->c_iflag)))
		return;

	change_speed(driver);

	if((old_termios->c_cflag & CRTSCTS) &&
	   !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = driver->ttp_stoptx;
		/* irvtd_start(tty); */       /* we don't need this */
	}
}

/*
 * ----------------------------------------------------------------------
 * irvtd_throttle,irvtd_unthrottle
 *   These routines will be called when we have to pause sending up data to tty.
 *   We use RTS virtual signal when servicetype is NINE_WIRE
 * ----------------------------------------------------------------------
 */

static void irvtd_send_xchar(struct tty_struct *tty, char ch){

	DEBUG(0, __FUNCTION__"():\n");
	irvtd_put_char(tty, ch);
}

void irvtd_throttle(struct tty_struct *tty){
	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;

	DEBUG(0, "irvtd_throttle:\n");

	if (I_IXOFF(tty))
		irvtd_put_char(tty, STOP_CHAR(tty));

	driver->mcr &= ~MCR_RTS; 
	driver->mcr |= DELTA_RTS; 
	driver->comm->dte = driver->mcr;
	ircomm_append_ctrl(driver->comm, DTELINE_STATE );
	ircomm_control_request(driver->comm);
        irttp_flow_request(driver->comm->tsap, FLOW_STOP);
}

void irvtd_unthrottle(struct tty_struct *tty){
	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;
	DEBUG(0, "irvtd_unthrottle:\n");

	if (I_IXOFF(tty))
		irvtd_put_char(tty, START_CHAR(tty));

	driver->mcr |= (MCR_RTS|DELTA_RTS);
	driver->comm->dte = driver->mcr;
	ircomm_append_ctrl(driver->comm, DTELINE_STATE );
	ircomm_control_request(driver->comm);
        irttp_flow_request(driver->comm->tsap, FLOW_START);
}


/*
 * ------------------------------------------------------------
 * irvtd_stop() and irvtd_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable an interrupt which means "transmitter-is-ready"
 * in serial.c, but  I think these routine are not necessary for us. 
 * ------------------------------------------------------------
 */

#if 0
irvtd_stop(struct tty_struct *tty){
 	DEBUG(0, "irvtd_stop()\n");

	struct irvtd_cb *info = (struct irvtd_cb *)tty->driver_data; 
	DEBUG(0, "irvtd_start():not implemented!\n");
}
irvtd_start(struct tty_struct *tty){

	struct irvtd_cb *info = (struct irvtd_cb *)tty->driver_data;
	DEBUG(0, "irvtd_start():not_implemented!\n");
}
#endif

/*
 * ------------------------------------------------------------
 * irvtd_hangup()
 * This routine notifies that tty layer have got HUP signal
 * Is this routine right ? :{|
 * ------------------------------------------------------------
 */

void irvtd_hangup(struct tty_struct *tty){

	struct irvtd_cb *info = (struct irvtd_cb *)tty->driver_data;
	DEBUG(0, "irvtd_hangup()\n");

	irvtd_flush_buffer(tty);
	irvtd_shutdown(info);
	info->count = 0;
	info->flags &= ~(IRVTD_ASYNC_NORMAL_ACTIVE|IRVTD_ASYNC_CALLOUT_ACTIVE);
	info->tty = NULL;
	wake_up_interruptible(&info->open_wait);
}

void irvtd_flush_buffer(struct tty_struct *tty){

	struct irvtd_cb *driver = (struct irvtd_cb *)tty->driver_data;
	struct sk_buff *skb;

	skb = (struct sk_buff *)driver->txbuff;

	DEBUG(4, "irvtd_flush_buffer:%d chars are gone..\n",(int)skb->len);
	skb_trim(skb,0); 
	
	/* write_wait is a wait queue of tty_wait_until_sent().
	 * see tty_io.c of kernel 
	 */
 	wake_up_interruptible(&tty->write_wait); 

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}


