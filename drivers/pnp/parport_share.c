/* $Id: parport_share.c,v 1.1.2.4 1997/04/01 18:19:11 phil Exp $
 * Parallel-port resource manager code.
 * 
 * Authors: David Campbell <campbell@tirian.che.curtin.edu.au>
 *          Tim Waugh <tmw20@cam.ac.uk>
 *	    Jose Renau <renau@acm.org>
 *
 * based on work by Grant Guenther <grant@torque.net>
 *              and Philip Blundell <Philip.Blundell@pobox.com>
 */

#include <asm/io.h>
#include <asm/dma.h>

#include <linux/parport.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>

#include "parport_ll_io.h"

static struct parport *portlist = NULL, *portlist_tail = NULL;
static int portcount = 0;

/* from parport_init.c */
extern int initialize_parport(struct parport *, unsigned long base, 
			      int irq, int dma, int count);

/* Return a list of all the ports we know about. */
struct parport *parport_enumerate(void)
{
	return portlist;
}

static void parport_null_intr_func(int irq, void *dev_id, struct pt_regs *regs)
{
	/* NULL function - Does nothing */
	return;
}

struct parport *parport_register_port(unsigned long base, int irq, int dma)
{
	struct parport new, *tmp;

	/* Check for a previously registered port.
	 * NOTE: we will ignore irq and dma if we find a previously
	 * registered device.
	 */
	for (tmp = portlist; tmp; tmp = tmp->next) {
		if (tmp->base == base)
			return tmp;
	}

	/* Has someone grabbed the address yet? */
	if (check_region(base, 3))
		return NULL;

	if (!initialize_parport(&new,base,irq,dma,portcount))
		return NULL;
					   
	if (new.dma >= 0) {
		if (request_dma(new.dma, new.name)) {
			printk(KERN_INFO "%s: unable to claim DMA %d\n", 
			       new.name, new..dma);
			release_region(new.base, new.size);
			if( new.modes & PARPORT_MODE_ECR )
				release_region(new.base+0x400, 3);
			kfree(new.name);
			return NULL;
		}
	}

	tmp = kmalloc(sizeof(struct parport), GFP_KERNEL);
	if (!tmp) {
		printk(KERN_WARNING "parport: memory squeeze\n");
		release_region(new.base, new.size);
		if( new.modes & PARPORT_MODE_ECR )
			release_region(new.base+0x400, 3);
		kfree(new.name);
		return NULL;
	}
	memcpy(tmp, &new, sizeof(struct parport));

	if (new.irq != -1) {
		if (request_irq(new.irq, parport_null_intr_func,
			  SA_INTERRUPT, new.name, tmp) != 0) {
			printk(KERN_INFO "%s: unable to claim IRQ %d\n", 
			       new.name, new.irq);
			kfree(tmp);
			release_region(new.base, new.size);
			if( new.modes & PARPORT_MODE_ECR )
				release_region(new.base+0x400, 3);
			kfree(new.name);
			return NULL;
		}
	}

	/* Here we chain the entry to our list. */
	if (portlist_tail)
		portlist_tail->next = tmp;
	portlist_tail = tmp;
	if (!portlist)
		portlist = tmp;

	printk(KERN_INFO "%s at 0x%x", tmp->name, tmp->base);
	if (tmp->irq >= 0)
		printk(", irq %d", tmp->irq);
	if (tmp->dma >= 0)
		printk(", dma %d", tmp->dma);
	printk(" [");
	{
		/* Ugh! */
#define printmode(x) {if(tmp->modes&PARPORT_MODE_##x){printk("%s%s",f?",":"",#x);f++;}}
		int f = 0;
		printmode(SPP);
		printmode(PS2);
		printmode(EPP);
		printmode(ECP);
		printmode(ECPEPP);
		printmode(ECPPS2);
#undef printmode
	}
	printk("]\n");
	portcount++;

	/* Restore device back to default conditions */
	if (tmp->modes & PARPORT_MODE_ECR)
		w_ecr(tmp, tmp->ecr);
	w_ctr(tmp, tmp->ctr);

	tmp->probe_info.class = PARPORT_CLASS_LEGACY;  /* assume the worst */
	return tmp;
}

void parport_destroy(struct parport *port)
{
	/* Dangerous to try destroying a port if its friends are nearby. */
	if (port->devices) {
		printk("%s: attempt to release active port\n", port->name);
		return;		/* Devices still present */
	}

	/* No point in further destroying a port that already lies in ruins. */
	if (port->flags & PARPORT_FLAG_COMA) 
		return;

	/* Now clean out the port entry */
	if (port->irq >= 0)
		free_irq(port->irq, port);
	if (port->dma >= 0)
		free_dma(port->dma);
	release_region(port->base, port->size);
	if( port->modes & PARPORT_MODE_ECR )
		release_region(port->base+0x400, 3);
	port->flags |= PARPORT_FLAG_COMA; 
}

struct ppd *parport_register_device(struct parport *port, const char *name,
				   callback_func pf, callback_func kf,
				   irq_handler_func irq_func, int flags,
				   void *handle)
{
	struct ppd *tmp;

	/* We only allow one lurker device (eg PLIP) */
	if (flags & PARPORT_DEV_LURK) {
		if (port->lurker) {
			printk(KERN_INFO "%s: refused to register second lurker (%s)\n",
				   port->name, name);
			return NULL;
		}
		if (!pf || !kf) {
			printk(KERN_INFO "%s: refused to register lurking device (%s) without callbacks\n"
				   ,port->name, name);
			return NULL;
		}
	}

	/* We may need to claw back the port hardware. */
	if (port->flags & PARPORT_FLAG_COMA) {
		if (check_region(port->base, 3)) {
			return NULL;
		}
		request_region(port->base, port->size, port->name);
		if( port->modes & PARPORT_MODE_ECR )
			request_region(port->base+0x400, 3,port->name);
			
		if (port->dma >= 0) {
			if (request_dma(port->dma, port->name)) {
				release_region(port->base, port->size);
				if( port->modes & PARPORT_MODE_ECR )
					release_region(port->base+0x400, 3);
				return NULL;
			}
		}
		if (port->irq != -1) {
			if (request_irq(port->irq, 
							parport_null_intr_func,
							SA_INTERRUPT, port->name,
							port) != 0) {
				release_region(port->base, port->size);
				if( port->modes & PARPORT_MODE_ECR )
					release_region(port->base+0x400, 3);
				if (port->dma >= 0)
					free_dma(port->dma);
				return NULL;
			}
		}
		port->flags &= ~PARPORT_FLAG_COMA;
	}


	tmp = kmalloc(sizeof(struct ppd), GFP_KERNEL);
	tmp->name = (char *) name;
	tmp->port = port;
	tmp->preempt = pf;
	tmp->wakeup = kf;
	tmp->private = handle;
	tmp->irq_func = irq_func;
	tmp->ctr = port->ctr;
	tmp->ecr = port->ecr;

	/* Chain this onto the list */
	tmp->prev = NULL;
	tmp->next = port->devices;
	if (port->devices)
		port->devices->prev = tmp;
	port->devices = tmp;

	if (flags & PARPORT_DEV_LURK)
		port->lurker = tmp;

	inc_parport_count();

	return tmp;
}

void parport_unregister_device(struct ppd *dev)
{
	struct parport *port;

	if (!dev) {
		printk(KERN_ERR "parport_unregister_device: passed NULL\n");
		return;
	}

	port = dev->port;

	if (port->cad == dev) {
		printk(KERN_INFO "%s: refused to unregister currently active device %s\n",
			   port->name, dev->name);
		return;
	}

	if (port->lurker == dev)
		port->lurker = NULL;

	if (dev->next)
		dev->next->prev = dev->prev;
	if (dev->prev)
		dev->prev->next = dev->next;
	else
		port->devices = dev->next;

	kfree(dev);

	dec_parport_count();

	/* If there are no more devices, put the port to sleep. */
	if (!port->devices)
		parport_destroy(port);

	return;
}

int parport_claim(struct ppd *dev)
{
	struct ppd *pd1;

	if (dev->port->cad == dev) {
		printk(KERN_INFO "%s: %s already owner\n",
			   dev->port->name,dev->name);
		return 0;
	}

	/* Preempt any current device */
	pd1 = dev->port->cad;
	if (dev->port->cad) {
		if (dev->port->cad->preempt) {
			/* Now try to preempt */
			if (dev->port->cad->preempt(dev->port->cad->private))
				return -EAGAIN;

			/* Save control registers */
			if (dev->port->modes & PARPORT_MODE_ECR)
				dev->port->cad->ecr = dev->port->ecr = 
					r_ecr(dev->port);
			dev->port->cad->ctr = dev->port->ctr =
				r_ctr(dev->port);
		} else
			return -EAGAIN;
	}

	/* Watch out for bad things */
	if (dev->port->cad != pd1) {
		printk(KERN_WARNING "%s: death while preempting %s\n",
		       dev->port->name, dev->name);
		if (dev->port->cad)
			return -EAGAIN;
	}

	/* Now we do the change of devices */
	dev->port->cad = dev;

	if (dev->port->irq >= 0) {
		free_irq(dev->port->irq, dev->port);
		request_irq(dev->port->irq, dev->irq_func ? dev->irq_func :
			    parport_null_intr_func, SA_INTERRUPT, dev->name,
			    dev->port);
	}

	/* Restore control registers */
	if (dev->port->modes & PARPORT_MODE_ECR)
		if (dev->ecr != dev->port->ecr) w_ecr(dev->port, dev->ecr);
	if (dev->ctr != dev->port->ctr) w_ctr(dev->port, dev->ctr);

	return 0;
}

void parport_release(struct ppd *dev)
{
	struct ppd *pd1;

	/* Make sure that dev is the current device */
	if (dev->port->cad != dev) {
		printk(KERN_WARNING "%s: %s tried to release parport when not owner\n",
			   dev->port->name, dev->name);
		return;
	}
	dev->port->cad = NULL;

	/* Save control registers */
	if (dev->port->modes & PARPORT_MODE_ECR)
		dev->ecr = dev->port->ecr = r_ecr(dev->port);
	dev->ctr = dev->port->ctr = r_ctr(dev->port);
	
	if (dev->port->irq >= 0) {
		free_irq(dev->port->irq, dev->port);
		request_irq(dev->port->irq, parport_null_intr_func,
					SA_INTERRUPT, dev->port->name, dev->port);
 	}

	/* Walk the list, offering a wakeup callback to everybody other
	 * than the lurker and the device that called us.
	 */
	for (pd1 = dev->next; pd1; pd1 = pd1->next) {
		if (!(pd1->flags & PARPORT_DEV_LURK)) {
			if (pd1->wakeup) {
				pd1->wakeup(pd1->private);
				if (dev->port->cad)
					return;
			}
		}
	}

	for (pd1 = dev->port->devices; pd1 && pd1 != dev; pd1 = pd1->next) {
		if (!(pd1->flags & PARPORT_DEV_LURK)) {
			if (pd1->wakeup) {
				pd1->wakeup(pd1->private);
				if (dev->port->cad)
					return;
			}
		}
	}

	/* Now give the lurker a chance.
	 * There should be a wakeup callback because we checked for it
	 * at registration.
	 */
	if (dev->port->lurker && (dev->port->lurker != dev)) {
		if (dev->port->lurker->wakeup) {
			dev->port->lurker->wakeup(dev->port->lurker->private);
			return;
		}
		printk(KERN_DEBUG
		       "%s (%s): lurker's wakeup callback went away!\n",
		       dev->port->name, dev->name);
	}
}

/* The following read funktions are an implementation of a status readback
 * and device id request confirming to IEEE1284-1994.
 */

/* Wait for Status line(s) to change in 35 ms - see IEEE1284-1994 page 24 to
 * 25 for this. After this time we can create a timeout because the
 * peripheral doesn't conform to IEEE1284. We want to save CPU time: we are
 * waiting a maximum time of 500 us busy (this is for speed). If there is
 * not the right answer in this time, we call schedule and other processes
 * are able "to eat" the time up to 30ms.  So the maximum load avarage can't
 * get above 5% for a read even if the peripheral is really slow. (but your
 * read gets very slow then - only about 10 characters per second. This
 * should be tuneable). Thanks to Andreas who pointed me to this and ordered
 * the documentation.
 */ 

int parport_wait_peripheral(struct parport *port, unsigned char mask, 
	unsigned char result)
{
	int counter=0;
	unsigned char status; 
	
	do {
		status = parport_r_status(port);
		udelay(25);
		counter++;
		if (need_resched)
			schedule();
	} while ( ((status & mask) != result) && (counter < 20) );
	if ( (counter == 20) && ((status & mask) != result) ) { 
		current->state=TASK_INTERRUPTIBLE;
		current->timeout=jiffies+4;
		schedule(); /* wait for 4 scheduler runs (40ms) */
		status = parport_r_status(port);
		if ((status & mask) != result) return 1; /* timeout */
	}
	return 0; /* okay right response from device */
}		

/* Test if nibble mode for status readback is okay. Returns the value false
 * if the printer doesn't support readback at all. If it supports readbacks
 * and printer data is available the function returns 1, otherwise 2. The
 * only valid values for "mode" are 0 and 4. 0 requests normal nibble mode,
 * 4 is for "request device id using nibble mode". The request for the
 * device id is best done in an ioctl (or at bootup time).  There is no
 * check for an invalid value, the only function using this call at the
 * moment is lp_read and the ioctl LPGETDEVICEID both fixed calls from
 * trusted kernel.
 */
int parport_ieee1284_nibble_mode_ok(struct parport *port, unsigned char mode) 
{
	parport_w_data(port, mode);
	udelay(5);		
	parport_w_ctrl(port, parport_r_ctrl(port) & ~8);  /* SelectIN low */
	parport_w_ctrl(port, parport_r_ctrl(port) | 2); /* AutoFeed high */
	if (parport_wait_peripheral(port, 0x78, 0x38)) { /* timeout? */
		parport_w_ctrl(port, (parport_r_ctrl(port) & ~2) | 8);
		return 0; /* first stage of negotiation failed, 
                           * no IEEE1284 compliant device on this port 
                           */ 
	}
	parport_w_ctrl(port, parport_r_ctrl(port) | 1);      /* Strobe high */
	udelay(5);				     /* Strobe wait */
	parport_w_ctrl(port, parport_r_ctrl(port) & ~1);     /* Strobe low */
	udelay(5);
	parport_w_ctrl(port, parport_r_ctrl(port) & ~2);     /* AutoFeed low */
	return (parport_wait_peripheral(port, 0x20, 0))?2:1;
}
