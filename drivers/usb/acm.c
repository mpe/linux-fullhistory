/*
 * USB Abstract Control Model based on Brad Keryan's USB busmouse driver 
 *
 * Armin Fuerst 5/8/1999 <armin.please@put.your.email.here.!!!!>
 *
 * version 0.8: Fixed endianity bug, some cleanups. I really hate to have
 * half of driver in form if (...) { info("x"); return y; }
 * 						Pavel Machek <pavel@suse.cz>
 *
 * version 0.7: Added usb flow control. Fixed bug in uhci.c (what idiot
 * wrote this code? ...Oops that was me). Fixed module cleanup. Did some
 * testing at 3Com => zmodem uload+download works, pppd had trouble but
 * seems to work now. Changed Menuconfig texts "Communications Device
 * Class (ACM)" might be a bit more intuitive. Ported to 2.3.13-1 prepatch. 
 * (2/8/99)
 *
 * version 0.6: Modularized driver, added disconnect code, improved
 * assignment of device to tty minor number.
 * (21/7/99)
 *
 * version 0.5: Driver now generates a tty instead of a simple character
 * device. Moved async bulk transfer to 2.3.10 kernel version. fixed a bug
 * in uhci_td_allocate. Commenetd out getstringtable which causes crash.
 * (13/7/99)
 *
 * version 0.4: Small fixes in the FIFO, cleanup. Updated Bulk transfer in 
 * uhci.c. Should have the correct interface now. 
 * (6/6/99)
 *
 * version 0.3: Major changes. Changed Bulk transfer to interrupt based
 * transfer. Using FIFO Buffers now. Consistent handling of open/close
 * file state and detected/nondetected device. File operations behave
 * according to this. Driver is able to send+receive now! Heureka!
 * (27/5/99)
 *
 * version 0.2: Improved Bulk transfer. TX led now flashes every time data is
 * sent. Send Encapsulated Data is not needed, nor does it do anything.
 * Why's that ?!? Thanks to Thomas Sailer for his close look at the bulk code.
 * He told me about some importand bugs. (5/21/99)
 *
 * version 0.1: Bulk transfer for uhci seems to work now, no dangling tds any
 * more. TX led of the ISDN TA flashed the first time. Does this mean it works?
 * The interrupt of the ctrl endpoint crashes the kernel => no read possible
 * (5/19/99)
 *
 * version 0.0: Driver sets up configuration, sets up data pipes, opens misc
 * device. No actual data transfer is done, since we don't have bulk transfer,
 * yet. Purely skeleton for now. (5/8/99)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#include "usb.h"

#define NR_PORTS 3
#define ACM_MAJOR 166	/* Wow, major is now officially allocated */

//#define info(message...); printk(message);
#define info(message...);

#define CTRL_STAT_DTR	1
#define CTRL_STAT_RTS	2

static int acm_refcount;

static struct tty_driver acm_tty_driver;
static struct tty_struct *acm_tty[NR_PORTS];
static struct termios *acm_termios[NR_PORTS];
static struct termios *acm_termios_locked[NR_PORTS];
static struct acm_state acm_state_table[NR_PORTS];

struct acm_state {
	struct usb_device *dev;				//the coresponding usb device
	struct tty_struct *tty;				//the coresponding tty
	char present;					//a device for this struct was detected => this tty is used
	char active;					//someone has this acm's device open 
	unsigned int ctrlstate;				//Status of the serial control lines  (handshake,...)
	unsigned int linecoding;			//Status of the line coding (Bits, Stop, Parity)
	int writesize, readsize;			//size of the usb buffers
	char *writebuffer, *readbuffer;			//the usb buffers
	void *readtransfer, *writetransfer;
	void *ctrltransfer;				//ptr to HC internal transfer struct
	char writing, reading;				//flag if transfer is running
	unsigned int readendp,writeendp,ctrlendp;	//endpoints and
	unsigned int readpipe,writepipe,ctrlpipe;	//pipes (are one of these obsolete?)
	unsigned ctrlinterval;				//interval to poll from device
};

#define ACM_READY (acm->present && acm->active)

//functions for various ACM requests

void Set_Control_Line_Status (unsigned int status,struct acm_state *acm)
{
	devrequest dr;

	info("Set_control_Line_Status\n");

	dr.requesttype = 0x22;
	dr.request = 0x22;
	dr.value = status;
	dr.index = 0;
	dr.length = 0;
	acm->dev->bus->op->control_msg(acm->dev, usb_sndctrlpipe(acm->dev,0), &dr, NULL, 0, HZ);

	acm->ctrlstate=status;
}

void Set_Line_Coding (unsigned int coding,struct acm_state *acm)
{
	devrequest dr;

	info("Set_Line_Coding\n");

	dr.requesttype = 0x22;
	dr.request = 0x30;
	dr.value = coding;
	dr.index = 0;
	dr.length = 0;
	acm->dev->bus->op->control_msg(acm->dev, usb_sndctrlpipe(acm->dev,0), &dr, NULL, 0, HZ);
	
	acm->linecoding=coding;
}

//Interrupt handler for various usb events
static int acm_irq(int state, void *__buffer, int count, void *dev_id)
{

	unsigned char *data;
	struct acm_state *acm = (struct acm_state *) dev_id; 
        devrequest *dr;
		
	info("ACM_USB_IRQ\n");

	if (!acm->present)
		return 0;
	if (!acm->active)
		return 1;
       
        dr=__buffer;
	data=__buffer;
	data+=sizeof(dr);
 
#if 0
        printk("reqtype: %02X\n",dr->requesttype);
        printk("request: %02X\n",dr->request);
	printk("wValue: %02X\n",dr->value);
	printk("wIndex: %02X\n",dr->index);
	printk("wLength: %02X\n",dr->length);
#endif
	
	switch(dr->request) {
	case 0x00: /* Network connection */
		printk("Network connection: ");
		if (dr->request==0) printk("disconnected\n");
		if (dr->request==1) printk("connected\n");
		break;
	    
	case 0x01: /* Response available */
		printk("Response available\n");
		break;
	
	case 0x20: /* Set serial line state */
		printk("acm.c: Set serial control line state\n");
		if ((dr->index==1)&&(dr->length==2)) {
			acm->ctrlstate= data[0] || (data[1] << 16);
			printk("Serstate: %02X\n",acm->ctrlstate);
		}
		break;
	}

	return 1; /* Continue transfer */
}

static int acm_read_irq(int state, void *__buffer, int count, void *dev_id)
{
	struct acm_state *acm = (struct acm_state *) dev_id; 
       	struct tty_struct *tty = acm->tty; 
       	unsigned char* data=__buffer;
	int i;

	info("ACM_READ_IRQ: state %d, %d bytes\n", state, count);
	if (state) {
		printk( "acm_read_irq: strange state received: %x\n", state );
		return 1;
	}
	
	if (!ACM_READY)
		return 0;	/* stop transfer */

	for (i=0;i<count;i++)
		tty_insert_flip_char(tty,data[i],0);
  	tty_flip_buffer_push(tty);

	return 1; /* continue transfer */
}

static int acm_write_irq(int state, void *__buffer, int count, void *dev_id)
{
	struct acm_state *acm = (struct acm_state *) dev_id; 
       	struct tty_struct *tty = acm->tty; 

	info("ACM_WRITE_IRQ\n");

	if (!ACM_READY)
		return 0; /* stop transfer */

	usb_terminate_bulk(acm->dev, acm->writetransfer);
	acm->writing=0;
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
	wake_up_interruptible(&tty->write_wait);
	
	return 0; /* stop tranfer */
}

/*TTY STUFF*/
static int rs_open(struct tty_struct *tty, struct file * filp) 
{
	struct acm_state *acm;
	int ret;
	
	info("USB_FILE_OPEN\n");

	tty->driver_data=acm=&acm_state_table[MINOR(tty->device)-tty->driver.minor_start];
	acm->tty=tty;
	 
	if (!acm->present)
		return -EINVAL;

	if (acm->active++)
		return 0;
 
	/* Start reading from the device */
	ret = usb_request_irq(acm->dev,acm->ctrlpipe, acm_irq, acm->ctrlinterval, acm, &acm->ctrltransfer);
	if (ret)
		printk (KERN_WARNING "usb-acm: usb_request_irq failed (0x%x)\n", ret);
	acm->reading=1;
	acm->readtransfer=usb_request_bulk(acm->dev,acm->readpipe, acm_read_irq, acm->readbuffer, acm->readsize, acm);
	
	Set_Control_Line_Status (CTRL_STAT_DTR | CTRL_STAT_RTS, acm);
				                  
	return 0;
}

static void rs_close(struct tty_struct *tty, struct file * filp)
{
	struct acm_state *acm = (struct acm_state *) tty->driver_data; 
	info("rs_close\n");
	
	if (!acm->present)
		return;

	if (--acm->active)
		return;

	Set_Control_Line_Status (0, acm);
	
	if (acm->writing){
		usb_terminate_bulk(acm->dev, acm->writetransfer);
		acm->writing=0;
	}
	if (acm->reading){
		usb_terminate_bulk(acm->dev, acm->readtransfer);
		acm->reading=0;
	}
//	usb_release_irq(acm->dev,acm->ctrltransfer, acm->ctrlpipe);
}

static int rs_write(struct tty_struct * tty, int from_user,
		    const unsigned char *buf, int count)
{
	struct acm_state *acm = (struct acm_state *) tty->driver_data; 
	int written;
	
	info("rs_write\n");

	if (!ACM_READY)
		return -EINVAL;
	
	if (acm->writing) {
		info ("already writing\n");
		return 0;
	}

	written=(count>acm->writesize) ? acm->writesize : count;
	  
	if (from_user) copy_from_user(acm->writebuffer,buf,written);
	else	       memcpy(acm->writebuffer,buf,written);

	//start the transfer
	acm->writing=1;
	acm->writetransfer=usb_request_bulk(acm->dev,acm->writepipe, acm_write_irq, acm->writebuffer, written, acm);

	return written;
} 

static void rs_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct acm_state *acm = (struct acm_state *) tty->driver_data; 
	
	printk( "acm: rs_put_char: Who called this unsupported routine?\n" );
	BUG();
}                   

static int rs_write_room(struct tty_struct *tty) 
{
	struct acm_state *acm = (struct acm_state *) tty->driver_data; 

	info("rs_write_room\n");
	
	if (!ACM_READY)
		return -EINVAL;
	
	return acm->writing ? 0 : acm->writesize;
}

static int rs_chars_in_buffer(struct tty_struct *tty) 
{
	struct acm_state *acm = (struct acm_state *) tty->driver_data; 

//	info("rs_chars_in_buffer\n");
	
	if (!ACM_READY)
		return -EINVAL;
	
	return acm->writing ? acm->writesize :  0;
}

static void rs_throttle(struct tty_struct * tty)
{
	struct acm_state *acm = (struct acm_state *) tty->driver_data; 
	
	info("rs_throttle\n");
	
	if (!ACM_READY)
		return;
/*	
	if (I_IXOFF(tty))
		rs_send_xchar(tty, STOP_CHAR(tty));
*/    
	if (tty->termios->c_cflag & CRTSCTS)
		Set_Control_Line_Status (acm->ctrlstate & ~CTRL_STAT_RTS, acm);
}

static void rs_unthrottle(struct tty_struct * tty)
{
	struct acm_state *acm = (struct acm_state *) tty->driver_data; 
	
	info("rs_unthrottle\n");
	
	if (!ACM_READY)
		return;
/*	
	if (I_IXOFF(tty))
		rs_send_xchar(tty, STOP_CHAR(tty));
*/    
	if (tty->termios->c_cflag & CRTSCTS)
		Set_Control_Line_Status (acm->ctrlstate | CTRL_STAT_RTS, acm);
}

static int get_free_acm(void)
{
	int i;
 
	for (i=0;i<NR_PORTS;i++) {
		if (!acm_state_table[i].present)
			return i;
	}
	return -1;
}

static int acm_probe(struct usb_device *dev)
{
	struct acm_state *acm;
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	int cfgnum,acmno;
	int swapped=0;
	
	info("acm_probe\n");
	
	if (0>(acmno=get_free_acm())) {
		info("Too many acm devices connected\n");
		return -1;
	}
	acm = &acm_state_table[acmno];

	/* Only use CDC */
	if (dev->descriptor.bDeviceClass != 2 ||
	    dev->descriptor.bDeviceSubClass != 0 ||
            dev->descriptor.bDeviceProtocol != 0)
		return -1;

#define IFCLASS(if) ((if->bInterfaceClass << 24) | (if->bInterfaceSubClass << 16) | (if->bInterfaceProtocol << 8) | (if->bNumEndpoints))

	/* Now scan all configs for a ACM configuration*/
	for (cfgnum=0;cfgnum<dev->descriptor.bNumConfigurations;cfgnum++) {
		/* The first one should be Communications interface? */
		interface = &dev->config[cfgnum].interface[0].altsetting[0];
		if (IFCLASS(interface) != 0x02020101)
			continue;

		/*Which uses an interrupt input */
		endpoint = &interface->endpoint[0];
		if ((endpoint->bEndpointAddress & 0x80) != 0x80 ||
		    (endpoint->bmAttributes & 3) != 3)
			continue;
			
		/* The second one should be a Data interface? */
		interface = &dev->config[cfgnum].interface[1].altsetting[0];
		if (interface->bInterfaceClass != 10 ||
		    interface->bNumEndpoints != 2)
			continue;

		/* if ((endpoint->bEndpointAddress & 0x80) == 0x80) */
		if ((endpoint->bEndpointAddress & 0x80) != 0x80)
			swapped = 1;

		/*With a bulk input */
		endpoint = &interface->endpoint[0^swapped];
		if ((endpoint->bEndpointAddress & 0x80) != 0x80 ||
		    (endpoint->bmAttributes & 3) != 2)
			continue;
			
		/*And a bulk output */
		endpoint = &interface->endpoint[1^swapped];
		if ((endpoint->bEndpointAddress & 0x80) == 0x80 ||
		    (endpoint->bmAttributes & 3) != 2)
			continue;

		printk("USB ACM %d found\n",acmno);
		usb_set_configuration(dev, dev->config[cfgnum].bConfigurationValue);

		acm->dev=dev;
		dev->private=acm;

		acm->readendp=dev->config[cfgnum].interface[1].altsetting[0].endpoint[0^swapped].bEndpointAddress;
		acm->readpipe=usb_rcvbulkpipe(dev,acm->readendp);
		acm->readbuffer=kmalloc(acm->readsize=dev->config[cfgnum].interface[1].altsetting[0].endpoint[0^swapped].wMaxPacketSize,GFP_KERNEL);
		acm->reading=0;
		if (!acm->readbuffer) {
			printk("ACM: Couldn't allocate readbuffer\n");
			return -1;
		}

		acm->writeendp=dev->config[cfgnum].interface[1].altsetting[0].endpoint[1^swapped].bEndpointAddress;
		acm->writepipe=usb_sndbulkpipe(dev,acm->writeendp);
		acm->writebuffer=kmalloc(acm->writesize=dev->config[cfgnum].interface[1].altsetting[0].endpoint[1^swapped].wMaxPacketSize, GFP_KERNEL);
		acm->writing=0;
		if (!acm->writebuffer) {
			printk("ACM: Couldn't allocate writebuffer\n");
			kfree(acm->readbuffer);
			return -1;
		}

		acm->ctrlendp=dev->config[cfgnum].interface[0].altsetting[0].endpoint[0].bEndpointAddress;
		acm->ctrlpipe=usb_rcvctrlpipe(acm->dev,acm->ctrlendp);
		acm->ctrlinterval=dev->config[cfgnum].interface[0].altsetting[0].endpoint[0].bInterval;

		acm->present=1;				
		MOD_INC_USE_COUNT;
		return 0;
	}
	return -1;
}

static void acm_disconnect(struct usb_device *dev)
{
	struct acm_state *acm = (struct acm_state *) dev->private;

	info("acm_disconnect\n");
	
	if (!acm->present)
		return;

	acm->active=0;
	acm->present=0;
	if (acm->writing){
		usb_terminate_bulk(acm->dev, acm->writetransfer);
		acm->writing=0;
	}
	if (acm->reading){
		usb_terminate_bulk(acm->dev, acm->readtransfer);
		acm->reading=0;
	}
	usb_release_irq(acm->dev,acm->ctrltransfer, acm->ctrlpipe);
	//BUG: What to do if a device is open?? Notify process or not allow cleanup?
	kfree(acm->writebuffer);
	kfree(acm->readbuffer);

	MOD_DEC_USE_COUNT;
}

/*USB DRIVER STUFF*/
static struct usb_driver acm_driver = {
	"acm",
	acm_probe,
	acm_disconnect,
	{ NULL, NULL }
};

int usb_acm_init(void)
{
	int cnt;
	
	info("usb_acm_init\n");
		
	//INITIALIZE GLOBAL DATA STRUCTURES
	for (cnt=0;cnt<NR_PORTS;cnt++) {
		memset(&acm_state_table[cnt], 0, sizeof(struct acm_state));
	}

	//REGISTER TTY DRIVER
	memset(&acm_tty_driver, 0, sizeof(struct tty_driver));
	acm_tty_driver.magic = TTY_DRIVER_MAGIC;
	acm_tty_driver.driver_name = "usb";
	acm_tty_driver.name = "ttyACM";
	acm_tty_driver.major = ACM_MAJOR;
	acm_tty_driver.minor_start = 0;
	acm_tty_driver.num = NR_PORTS;
	acm_tty_driver.type = TTY_DRIVER_TYPE_SERIAL;
	acm_tty_driver.subtype = SERIAL_TYPE_NORMAL;
	acm_tty_driver.init_termios = tty_std_termios;
	acm_tty_driver.init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	acm_tty_driver.flags = TTY_DRIVER_REAL_RAW;
	acm_tty_driver.refcount = &acm_refcount;
	acm_tty_driver.table = acm_tty;
	acm_tty_driver.termios = acm_termios;
	acm_tty_driver.termios_locked = acm_termios_locked;
	
	acm_tty_driver.open = rs_open;
	acm_tty_driver.close = rs_close;
	acm_tty_driver.write = rs_write;
	acm_tty_driver.put_char = rs_put_char;
	acm_tty_driver.flush_chars = NULL; //rs_flush_chars;
	acm_tty_driver.write_room = rs_write_room;
	acm_tty_driver.ioctl = NULL; //rs_ioctl;
	acm_tty_driver.set_termios = NULL; //rs_set_termios;
	acm_tty_driver.set_ldisc = NULL; 
	acm_tty_driver.throttle = rs_throttle;
	acm_tty_driver.unthrottle = rs_unthrottle;
	acm_tty_driver.stop = NULL; //rs_stop;
	acm_tty_driver.start = NULL; //rs_start;
	acm_tty_driver.hangup = NULL; //rs_hangup;
	acm_tty_driver.break_ctl = NULL; //rs_break;
	acm_tty_driver.wait_until_sent = NULL; //rs_wait_until_sent;
	acm_tty_driver.send_xchar = NULL; //rs_send_xchar;
	acm_tty_driver.read_proc = NULL; //rs_read_proc;
	acm_tty_driver.chars_in_buffer = rs_chars_in_buffer;
	acm_tty_driver.flush_buffer = NULL; //rs_flush_buffer;
	if (tty_register_driver(&acm_tty_driver)) {
		printk( "acm: failed to register tty driver\n" );
		return -EPERM;
	}
	
	//REGISTER USB DRIVER
	usb_register(&acm_driver);

	printk(KERN_INFO "USB ACM registered.\n");
	return 0;
}

void usb_acm_cleanup(void)
{
	int i;
	struct acm_state *acm;

	info("usb_acm_cleanup\n");
		
	for (i=0;i<NR_PORTS;i++) {
		acm=&acm_state_table[i];
		if (acm->present) {
			printk("disconnecting %d\n",i);
			acm_disconnect(acm->dev);
		}  
	}
	tty_unregister_driver(&acm_tty_driver);
	
	usb_deregister(&acm_driver);
	
}

#ifdef MODULE
int init_module(void)
{
	return usb_acm_init();
}

void cleanup_module(void)
{
	usb_acm_cleanup();
}
#endif
