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

static struct parport *portlist = NULL, *portlist_tail = NULL;
spinlock_t parportlist_lock = SPIN_LOCK_UNLOCKED;

void (*parport_probe_hook)(struct parport *port) = NULL;

/* Return a list of all the ports we know about. */
struct parport *parport_enumerate(void)
{
#ifdef CONFIG_KMOD
	if (portlist == NULL) {
		request_module("parport_lowlevel");
#ifdef CONFIG_PNP_PARPORT_MODULE
		request_module("parport_probe");
#endif /* CONFIG_PNP_PARPORT_MODULE */
	}
#endif /* CONFIG_KMOD */
	return portlist;
}

struct parport *parport_register_port(unsigned long base, int irq, int dma,
				      struct parport_operations *ops)
{
	struct parport *tmp;
	int portnum;
	char *name;

	/* Check for a previously registered port.
	   NOTE: we will ignore irq and dma if we find a previously
	   registered device.  */
	for (tmp = portlist; tmp; tmp = tmp->next) {
		if (tmp->base == base)
			return tmp;
	}

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
	tmp->modes = 0;
 	tmp->next = NULL;
	tmp->devices = tmp->cad = NULL;
	tmp->flags = 0;
	tmp->ops = ops;
	tmp->number = portnum;
	memset (&tmp->probe_info, 0, sizeof (struct parport_device_info));
	tmp->cad_lock = RW_LOCK_UNLOCKED;
	spin_lock_init(&tmp->waitlist_lock);
	spin_lock_init(&tmp->pardevice_lock);

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

	tmp->probe_info.class = PARPORT_CLASS_LEGACY;  /* assume the worst */
	tmp->waithead = tmp->waittail = NULL;

	return tmp;
}

void parport_unregister_port(struct parport *port)
{
	struct parport *p;

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
	if (port->probe_info.class_name)
		kfree (port->probe_info.class_name);
	if (port->probe_info.mfr)
		kfree (port->probe_info.mfr);
	if (port->probe_info.model)
		kfree (port->probe_info.model);
	if (port->probe_info.cmdset)
		kfree (port->probe_info.cmdset);
	if (port->probe_info.description)
		kfree (port->probe_info.description);
	kfree(port->name);
	kfree(port);
}

void parport_quiesce(struct parport *port)
{
	if (port->devices) {
		printk(KERN_WARNING "%s: attempt to quiesce active port.\n",
		       port->name);
		return;
	}

	if (port->flags & PARPORT_FLAG_COMA) {
		printk(KERN_WARNING "%s: attempt to quiesce comatose port.\n",
		       port->name);
		return;
	}

	port->ops->release_resources(port);

	port->flags |= PARPORT_FLAG_COMA; 
}

struct pardevice *parport_register_device(struct parport *port, const char *name,
			  int (*pf)(void *), void (*kf)(void *),
			  void (*irq_func)(int, void *, struct pt_regs *), 
			  int flags, void *handle)
{
	struct pardevice *tmp;

	if (port->flags & PARPORT_FLAG_EXCL) {
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

	/* We may need to claw back the port hardware. */
	if (port->flags & PARPORT_FLAG_COMA) {
		if (port->ops->claim_resources(port)) {
			printk(KERN_WARNING
			       "%s: unable to get hardware to register %s.\n",
			       port->name, name);
			kfree (tmp->state);
			kfree (tmp);
			return NULL;
		}
		port->flags &= ~PARPORT_FLAG_COMA;
	}

	tmp->name = name;
	tmp->port = port;
	tmp->preempt = pf;
	tmp->wakeup = kf;
	tmp->private = handle;
	tmp->flags = flags;
	tmp->irq_func = irq_func;
	port->ops->init_state(tmp->state);
	tmp->waiting = 0;

	/* Chain this onto the list */
	tmp->prev = NULL;
	/*
	 * This function must not run from an irq handler so we don' t need
	 * to clear irq on the local CPU. -arca
	 */
	spin_lock(&port->pardevice_lock);

	if (flags & PARPORT_DEV_EXCL) {
		if (port->devices) {
			spin_unlock (&port->pardevice_lock);
			kfree (tmp->state);
			kfree (tmp);
			printk (KERN_DEBUG
				"%s: cannot grant exclusive access for "
				"device %s\n", port->name, name);
			return NULL;
		}
		port->flags |= PARPORT_FLAG_EXCL;
	}

	tmp->next = port->devices;
	if (port->devices)
		port->devices->prev = tmp;
	port->devices = tmp;
	spin_unlock(&port->pardevice_lock);

	inc_parport_count();
	port->ops->inc_use_count();

	init_waitqueue(&tmp->wait_q);
	tmp->timeslice = PARPORT_DEFAULT_TIMESLICE;
	tmp->waitnext = tmp->waitprev = NULL;

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

	port = dev->port;

	if (port->cad == dev) {
		printk(KERN_WARNING "%s: refused to unregister "
		       "currently active device %s.\n", port->name, dev->name);
		return;
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

	/* If there are no more devices, put the port to sleep. */
	if (!port->devices)
		parport_quiesce(port);

	return;
}

int parport_claim(struct pardevice *dev)
{
	struct pardevice *oldcad;
	struct parport *port = dev->port;
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

	if (oldcad && port->irq != PARPORT_IRQ_NONE && !oldcad->irq_func)
		/*
		 * If there was an irq pending it should hopefully happen
		 * before return from enable_irq(). -arca
		 */
		enable_irq(port->irq);

	/*
	 * Avoid running irq handlers if the pardevice doesn' t use it. -arca
	 */
	if (port->irq != PARPORT_IRQ_NONE && !dev->irq_func)
		disable_irq(port->irq);

	/* Now we do the change of devices */
	write_lock_irqsave(&port->cad_lock, flags);
	port->cad = dev;
	write_unlock_irqrestore(&port->cad_lock, flags);

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
		if (dev->port->cad != dev)
			printk(KERN_DEBUG "%s: exiting parport_claim_or_block but %s owns port!\n", dev->name, dev->port->cad?dev->port->cad->name:"nobody");
#endif
	}
	dev->waiting = 0;
	return r;
}

void parport_release(struct pardevice *dev)
{
	struct parport *port = dev->port;
	struct pardevice *pd;
	unsigned long flags;

	/* Make sure that dev is the current device */
	if (port->cad != dev) {
		printk(KERN_WARNING "%s: %s tried to release parport "
		       "when not owner\n", port->name, dev->name);
		return;
	}
	write_lock_irqsave(&port->cad_lock, flags);
	port->cad = NULL;
	write_unlock_irqrestore(&port->cad_lock, flags);

	/*
	 * Reenable irq and so discard the eventually pending irq while
	 * cad is NULL. -arca
	 */
	if (port->irq != PARPORT_IRQ_NONE && !dev->irq_func)
		enable_irq(port->irq);

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

void parport_parse_irqs(int nports, const char *irqstr[], int irqval[])
{
	unsigned int i;
	for (i = 0; i < nports && irqstr[i]; i++) {
		if (!strncmp(irqstr[i], "auto", 4))
			irqval[i] = PARPORT_IRQ_AUTO;
		else if (!strncmp(irqstr[i], "none", 4))
			irqval[i] = PARPORT_IRQ_NONE;
		else {
			char *ep;
			unsigned long r = simple_strtoul(irqstr[i], &ep, 0);
			if (ep != irqstr[i])
				irqval[i] = r;
			else {
				printk("parport: bad irq specifier `%s'\n", irqstr[i]);
				return;
			}
		}
	}
}
