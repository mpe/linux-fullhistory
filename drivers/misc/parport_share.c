/* $Id: parport_share.c,v 1.15 1998/01/11 12:06:17 philip Exp $
 * Parallel-port resource manager code.
 * 
 * Authors: David Campbell <campbell@tirian.che.curtin.edu.au>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *          Jose Renau <renau@acm.org>
 *          Philip Blundell <philb@gnu.org>
 *	    Andrea Arcangeli
 *
 * based on work by Grant Guenther <grant@torque.net>
 *          and Philip Blundell
 */

#undef PARPORT_DEBUG_SHARING		/* undef for production */

#include <linux/config.h>

#include <linux/string.h>

#include <linux/tasks.h>

#include <linux/parport.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/sched.h>

#include <asm/spinlock.h>
#include <asm/irq.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#undef PARPORT_PARANOID

#define PARPORT_DEFAULT_TIMESLICE	(HZ/5)

unsigned long parport_default_timeslice = PARPORT_DEFAULT_TIMESLICE;
int parport_default_spintime =  DEFAULT_SPIN_TIME;

static struct parport *portlist = NULL, *portlist_tail = NULL;
spinlock_t parportlist_lock = SPIN_LOCK_UNLOCKED;

static struct parport_driver *driver_chain = NULL;
spinlock_t driverlist_lock = SPIN_LOCK_UNLOCKED;

static void call_driver_chain(int attach, struct parport *port)
{
	struct parport_driver *drv;

	for (drv = driver_chain; drv; drv = drv->next) {
		if (attach)
			drv->attach (port);
		else
			drv->detach (port);
	}
}

int parport_register_driver (struct parport_driver *drv)
{
	struct parport *port;

	spin_lock (&driverlist_lock);
	drv->next = driver_chain;
	driver_chain = drv;
	spin_unlock (&driverlist_lock);

	for (port = portlist; port; port = port->next)
		drv->attach (port);

	return 0;
}

void parport_unregister_driver (struct parport_driver *arg)
{
	struct parport_driver *drv = driver_chain, *olddrv = NULL;

	while (drv) {
		if (drv == arg) {
			spin_lock (&driverlist_lock);
			if (olddrv)
				olddrv->next = drv->next;
			else
				driver_chain = drv->next;
			spin_unlock (&driverlist_lock);
			return;
		}
		olddrv = drv;
		drv = drv->next;
	}
}

/* Return a list of all the ports we know about. */
struct parport *parport_enumerate(void)
{
#ifdef CONFIG_KMOD
	if (portlist == NULL) {
		request_module("parport_lowlevel");
	}
#endif /* CONFIG_KMOD */
	return portlist;
}

struct parport *parport_register_port(unsigned long base, int irq, int dma,
				      struct parport_operations *ops)
{
	struct parport *tmp;
	int portnum;
	int device;
	char *name;

	tmp = kmalloc(sizeof(struct parport), GFP_KERNEL);
	if (!tmp) {
		printk(KERN_WARNING "parport: memory squeeze\n");
		return NULL;
	}

	/* Search for the lowest free parport number. */
	for (portnum = 0; ; portnum++) {
		struct parport *itr = portlist;
		while (itr) {
			if (itr->number == portnum)
				/* No good, already used. */
				break;
			else
				itr = itr->next;
		}

		if (itr == NULL)
			/* Got to the end of the list. */
			break;
	}
	
	/* Init our structure */
 	memset(tmp, 0, sizeof(struct parport));
	tmp->base = base;
	tmp->irq = irq;
	tmp->dma = dma;
	tmp->muxport = tmp->daisy = tmp->muxsel = -1;
	tmp->modes = 0;
 	tmp->next = NULL;
	tmp->devices = tmp->cad = NULL;
	tmp->flags = 0;
	tmp->ops = ops;
	tmp->portnum = tmp->number = portnum;
	tmp->physport = tmp;
	memset (tmp->probe_info, 0, 5 * sizeof (struct parport_device_info));
	tmp->cad_lock = RW_LOCK_UNLOCKED;
	spin_lock_init(&tmp->waitlist_lock);
	spin_lock_init(&tmp->pardevice_lock);
	tmp->ieee1284.mode = IEEE1284_MODE_COMPAT;
	tmp->ieee1284.phase = IEEE1284_PH_FWD_IDLE;
	init_MUTEX_LOCKED (&tmp->ieee1284.irq); /* actually a semaphore at 0 */
	tmp->spintime = parport_default_spintime;

	name = kmalloc(15, GFP_KERNEL);
	if (!name) {
		printk(KERN_ERR "parport: memory squeeze\n");
		kfree(tmp);
		return NULL;
	}
	sprintf(name, "parport%d", portnum);
	tmp->name = name;

	/*
	 * Chain the entry to our list.
	 *
	 * This function must not run from an irq handler so we don' t need
	 * to clear irq on the local CPU. -arca
	 */
	spin_lock(&parportlist_lock);
	if (portlist_tail)
		portlist_tail->next = tmp;
	portlist_tail = tmp;
	if (!portlist)
		portlist = tmp;
	spin_unlock(&parportlist_lock);

	for (device = 0; device < 5; device++)
		/* assume the worst */
		tmp->probe_info[device].class = PARPORT_CLASS_LEGACY;

	tmp->waithead = tmp->waittail = NULL;

	return tmp;
}

void parport_announce_port (struct parport *port)
{
#ifdef CONFIG_PARPORT_1284
	/* Analyse the IEEE1284.3 topology of the port. */
	parport_daisy_init (port);
#endif

	/* Let drivers know that a new port has arrived. */
	call_driver_chain (1, port);
}

void parport_unregister_port(struct parport *port)
{
	struct parport *p;
	int d;

	/* Spread the word. */
	call_driver_chain (0, port);

#ifdef CONFIG_PARPORT_1284
	/* Forget the IEEE1284.3 topology of the port. */
	parport_daisy_fini (port);
#endif

	spin_lock(&parportlist_lock);
	if (portlist == port) {
		if ((portlist = port->next) == NULL)
			portlist_tail = NULL;
	} else {
		for (p = portlist; (p != NULL) && (p->next != port); 
		     p=p->next);
		if (p) {
			if ((p->next = port->next) == NULL)
				portlist_tail = p;
		}
		else printk (KERN_WARNING
			     "%s not found in port list!\n", port->name);
	}
	spin_unlock(&parportlist_lock);

	for (d = 0; d < 5; d++) {
		if (port->probe_info[d].class_name)
			kfree (port->probe_info[d].class_name);
		if (port->probe_info[d].mfr)
			kfree (port->probe_info[d].mfr);
		if (port->probe_info[d].model)
			kfree (port->probe_info[d].model);
		if (port->probe_info[d].cmdset)
			kfree (port->probe_info[d].cmdset);
		if (port->probe_info[d].description)
			kfree (port->probe_info[d].description);
	}

	kfree(port->name);
	kfree(port);
}

struct pardevice *parport_register_device(struct parport *port, const char *name,
			  int (*pf)(void *), void (*kf)(void *),
			  void (*irq_func)(int, void *, struct pt_regs *), 
			  int flags, void *handle)
{
	struct pardevice *tmp;

	if (port->physport->flags & PARPORT_FLAG_EXCL) {
		/* An exclusive device is registered. */
		printk (KERN_DEBUG "%s: no more devices allowed\n",
			port->name);
		return NULL;
	}

	if (flags & PARPORT_DEV_LURK) {
		if (!pf || !kf) {
			printk(KERN_INFO "%s: refused to register lurking device (%s) without callbacks\n", port->name, name);
			return NULL;
		}
	}

	tmp = kmalloc(sizeof(struct pardevice), GFP_KERNEL);
	if (tmp == NULL) {
		printk(KERN_WARNING "%s: memory squeeze, couldn't register %s.\n", port->name, name);
		return NULL;
	}

	tmp->state = kmalloc(sizeof(struct parport_state), GFP_KERNEL);
	if (tmp->state == NULL) {
		printk(KERN_WARNING "%s: memory squeeze, couldn't register %s.\n", port->name, name);
		kfree(tmp);
		return NULL;
	}

	tmp->name = name;
	tmp->port = port;
	tmp->daisy = -1;
	tmp->preempt = pf;
	tmp->wakeup = kf;
	tmp->private = handle;
	tmp->flags = flags;
	tmp->irq_func = irq_func;
	tmp->waiting = 0;
	tmp->timeout = 5 * HZ;

	/* Chain this onto the list */
	tmp->prev = NULL;
	/*
	 * This function must not run from an irq handler so we don' t need
	 * to clear irq on the local CPU. -arca
	 */
	spin_lock(&port->physport->pardevice_lock);

	if (flags & PARPORT_DEV_EXCL) {
		if (port->physport->devices) {
			spin_unlock (&port->physport->pardevice_lock);
			kfree (tmp->state);
			kfree (tmp);
			printk (KERN_DEBUG
				"%s: cannot grant exclusive access for "
				"device %s\n", port->name, name);
			return NULL;
		}
		port->flags |= PARPORT_FLAG_EXCL;
	}

	tmp->next = port->physport->devices;
	if (port->physport->devices)
		port->physport->devices->prev = tmp;
	port->physport->devices = tmp;
	spin_unlock(&port->physport->pardevice_lock);

	inc_parport_count();
	port->ops->inc_use_count();

	init_waitqueue_head(&tmp->wait_q);
	tmp->timeslice = parport_default_timeslice;
	tmp->waitnext = tmp->waitprev = NULL;

	/*
	 * This has to be run as last thing since init_state may need other
	 * pardevice fields. -arca
	 */
	port->ops->init_state(tmp, tmp->state);
	parport_device_proc_register(tmp);
	return tmp;
}

void parport_unregister_device(struct pardevice *dev)
{
	struct parport *port;

#ifdef PARPORT_PARANOID
	if (dev == NULL) {
		printk(KERN_ERR "parport_unregister_device: passed NULL\n");
		return;
	}
#endif

	parport_device_proc_unregister(dev);

	port = dev->port->physport;

	if (port->cad == dev) {
		printk(KERN_DEBUG "%s: %s forgot to release port\n",
		       port->name, dev->name);
		parport_release (dev);
	}

	spin_lock(&port->pardevice_lock);
	if (dev->next)
		dev->next->prev = dev->prev;
	if (dev->prev)
		dev->prev->next = dev->next;
	else
		port->devices = dev->next;

	if (dev->flags & PARPORT_DEV_EXCL)
		port->flags &= ~PARPORT_FLAG_EXCL;

	spin_unlock(&port->pardevice_lock);

	kfree(dev->state);
	kfree(dev);

	dec_parport_count();
	port->ops->dec_use_count();
}

int parport_claim(struct pardevice *dev)
{
	struct pardevice *oldcad;
	struct parport *port = dev->port->physport;
	unsigned long flags;

	if (port->cad == dev) {
		printk(KERN_INFO "%s: %s already owner\n",
		       dev->port->name,dev->name);
		return 0;
	}

try_again:
	/* Preempt any current device */
	if ((oldcad = port->cad) != NULL) {
		if (oldcad->preempt) {
			if (oldcad->preempt(oldcad->private))
				goto blocked;
			port->ops->save_state(port, dev->state);
		} else
			goto blocked;

		if (port->cad != oldcad) {
			printk(KERN_WARNING 
			       "%s: %s released port when preempted!\n",
			       port->name, oldcad->name);
			if (port->cad)
				goto blocked;
		}
	}

	/* Can't fail from now on, so mark ourselves as no longer waiting.  */
	if (dev->waiting & 1) {
		dev->waiting = 0;

		/* Take ourselves out of the wait list again.  */
		spin_lock_irqsave (&port->waitlist_lock, flags);
		if (dev->waitprev)
			dev->waitprev->waitnext = dev->waitnext;
		else
			port->waithead = dev->waitnext;
		if (dev->waitnext)
			dev->waitnext->waitprev = dev->waitprev;
		else
			port->waittail = dev->waitprev;
		spin_unlock_irqrestore (&port->waitlist_lock, flags);
		dev->waitprev = dev->waitnext = NULL;
	}

	/* Now we do the change of devices */
	write_lock_irqsave(&port->cad_lock, flags);
	port->cad = dev;
	write_unlock_irqrestore(&port->cad_lock, flags);

#ifdef CONFIG_PARPORT_1284
	/* If it's a mux port, select it. */
	if (dev->port->muxport >= 0) {
		/* FIXME */
		port->muxsel = dev->port->muxport;
	}

	/* If it's a daisy chain device, select it. */
	if (dev->daisy >= 0) {
		/* This could be lazier. */
		if (!parport_daisy_select (port, dev->daisy,
					   IEEE1284_MODE_COMPAT))
			port->daisy = dev->daisy;
	}
#endif /* IEEE1284.3 support */

	/* Restore control registers */
	port->ops->restore_state(port, dev->state);
	dev->time = jiffies;
	return 0;

blocked:
	/* If this is the first time we tried to claim the port, register an
	   interest.  This is only allowed for devices sleeping in
	   parport_claim_or_block(), or those with a wakeup function.  */
	if (dev->waiting & 2 || dev->wakeup) {
		spin_lock_irqsave (&port->waitlist_lock, flags);
		if (port->cad == NULL) {
			/* The port got released in the meantime. */
			spin_unlock_irqrestore (&port->waitlist_lock, flags);
			goto try_again;
		}
		if (test_and_set_bit(0, &dev->waiting) == 0) {
			/* First add ourselves to the end of the wait list. */
			dev->waitnext = NULL;
			dev->waitprev = port->waittail;
			if (port->waittail) {
				port->waittail->waitnext = dev;
				port->waittail = dev;
			} else
				port->waithead = port->waittail = dev;
		}
		spin_unlock_irqrestore (&port->waitlist_lock, flags);
	}
	return -EAGAIN;
}

int parport_claim_or_block(struct pardevice *dev)
{
	int r;

	/* Signal to parport_claim() that we can wait even without a
	   wakeup function.  */
	dev->waiting = 2;

	/* Try to claim the port.  If this fails, we need to sleep.  */
	r = parport_claim(dev);
	if (r == -EAGAIN) {
		unsigned long flags;
#ifdef PARPORT_DEBUG_SHARING
		printk(KERN_DEBUG "%s: parport_claim() returned -EAGAIN\n", dev->name);
#endif
		save_flags (flags);
		cli();
		/* If dev->waiting is clear now, an interrupt
		   gave us the port and we would deadlock if we slept.  */
		if (dev->waiting) {
			sleep_on(&dev->wait_q);
			r = 1;
		} else {
			r = 0;
#ifdef PARPORT_DEBUG_SHARING
			printk(KERN_DEBUG "%s: didn't sleep in parport_claim_or_block()\n",
			       dev->name);
#endif
		}
		restore_flags(flags);
#ifdef PARPORT_DEBUG_SHARING
		if (dev->port->physport->cad != dev)
			printk(KERN_DEBUG "%s: exiting parport_claim_or_block "
			       "but %s owns port!\n", dev->name,
			       dev->port->physport->cad ?
			       dev->port->physport->cad->name:"nobody");
#endif
	}
	dev->waiting = 0;
	return r;
}

void parport_release(struct pardevice *dev)
{
	struct parport *port = dev->port->physport;
	struct pardevice *pd;
	unsigned long flags;

	/* Make sure that dev is the current device */
	if (port->cad != dev) {
		printk(KERN_WARNING "%s: %s tried to release parport "
		       "when not owner\n", port->name, dev->name);
		return;
	}

#ifdef CONFIG_PARPORT_1284
	/* If this is on a mux port, deselect it. */
	if (dev->port->muxport >= 0) {
		/* FIXME */
		port->muxsel = -1;
	}

	/* If this is a daisy device, deselect it. */
	if (dev->daisy >= 0) {
		parport_daisy_deselect_all (port);
		port->daisy = -1;
	}
#endif

	write_lock_irqsave(&port->cad_lock, flags);
	port->cad = NULL;
	write_unlock_irqrestore(&port->cad_lock, flags);

	/* Save control registers */
	port->ops->save_state(port, dev->state);

	/* If anybody is waiting, find out who's been there longest and
	   then wake them up. (Note: no locking required) */
	for (pd = port->waithead; pd; pd = pd->waitnext) {
		if (pd->waiting & 2) { /* sleeping in claim_or_block */
			parport_claim(pd);
			if (waitqueue_active(&pd->wait_q))
				wake_up(&pd->wait_q);
			return;
		} else if (pd->wakeup) {
			pd->wakeup(pd->private);
			if (dev->port->cad)
				return;
		} else {
			printk(KERN_ERR "%s: don't know how to wake %s\n", port->name, pd->name);
		}
	}

	/* Nobody was waiting, so walk the list to see if anyone is
	   interested in being woken up.  */
	for (pd = port->devices; (port->cad == NULL) && pd; pd = pd->next) {
		if (pd->wakeup && pd != dev)
			pd->wakeup(pd->private);
	}
}

static int parport_parse_params (int nports, const char *str[], int val[],
				 int automatic, int none)
{
	unsigned int i;
	for (i = 0; i < nports && str[i]; i++) {
		if (!strncmp(str[i], "auto", 4))
			val[i] = automatic;
		else if (!strncmp(str[i], "none", 4))
			val[i] = none;
		else {
			char *ep;
			unsigned long r = simple_strtoul(str[i], &ep, 0);
			if (ep != str[i])
				val[i] = r;
			else {
				printk("parport: bad specifier `%s'\n", str[i]);
				return -1;
			}
		}
	}

	return 0;
}

int parport_parse_irqs(int nports, const char *irqstr[], int irqval[])
{
	return parport_parse_params (nports, irqstr, irqval, PARPORT_IRQ_AUTO,
				     PARPORT_IRQ_NONE);
}

int parport_parse_dmas(int nports, const char *dmastr[], int dmaval[])
{
	return parport_parse_params (nports, dmastr, dmaval, PARPORT_DMA_AUTO,
				     PARPORT_DMA_NONE);
}
