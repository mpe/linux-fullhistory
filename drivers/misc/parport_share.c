/* $Id: parport_share.c,v 1.8 1997/11/08 18:55:29 philip Exp $
 * Parallel-port resource manager code.
 * 
 * Authors: David Campbell <campbell@tirian.che.curtin.edu.au>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *	    Jose Renau <renau@acm.org>
 *
 * based on work by Grant Guenther <grant@torque.net>
 *              and Philip Blundell <Philip.Blundell@pobox.com>
 */

#include <linux/tasks.h>

#include <linux/parport.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>

#include <linux/config.h>

#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif

#undef PARPORT_PARANOID

static struct parport *portlist = NULL, *portlist_tail = NULL;
static int portcount = 0;

void (*parport_probe_hook)(struct parport *port) = NULL;

/* Return a list of all the ports we know about. */
struct parport *parport_enumerate(void)
{
#ifdef CONFIG_KERNELD
	if (portlist == NULL) {
		request_module("parport_lowlevel");
#ifdef CONFIG_PNP_PARPORT_MODULE
		request_module("parport_probe");
#endif /* CONFIG_PNP_PARPORT_MODULE */
	}
#endif /* CONFIG_KERNELD */
	return portlist;
}

void parport_null_intr_func(int irq, void *dev_id, struct pt_regs *regs)
{
	/* NULL function - Does nothing */
	return;
}

struct parport *parport_register_port(unsigned long base, int irq, int dma,
				      struct parport_operations *ops)
{
	struct parport *tmp;

	/* Check for a previously registered port.
	 * NOTE: we will ignore irq and dma if we find a previously
	 * registered device.
	 */
	for (tmp = portlist; tmp; tmp = tmp->next) {
		if (tmp->base == base)
			return tmp;
	}

	tmp = kmalloc(sizeof(struct parport), GFP_KERNEL);
	if (!tmp) {
		printk(KERN_WARNING "parport: memory squeeze\n");
		return NULL;
	}

	/* Init our structure */
 	memset(tmp, 0, sizeof(struct parport));
	tmp->base = base;
	tmp->irq = irq;
	tmp->dma = dma;
	tmp->modes = 0;
 	tmp->next = NULL;
	tmp->devices = tmp->cad = tmp->lurker = NULL;
	tmp->flags = 0;
	tmp->ops = ops; 

	tmp->name = kmalloc(15, GFP_KERNEL);
	if (!tmp->name) {
		printk(KERN_ERR "parport: memory squeeze\n");
		kfree(tmp);
		return NULL;
	}
	sprintf(tmp->name, "parport%d", portcount);

	/* Here we chain the entry to our list. */
	if (portlist_tail)
		portlist_tail->next = tmp;
	portlist_tail = tmp;
	if (!portlist)
		portlist = tmp;

	portcount++;

	tmp->probe_info.class = PARPORT_CLASS_LEGACY;  /* assume the worst */
	return tmp;
}

void parport_unregister_port(struct parport *port)
{
	struct parport *p;
	kfree(port->name);
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
	}
	kfree(port);
}

void parport_quiesce(struct parport *port)
{
	if (port->devices) {
		printk(KERN_WARNING "%s: attempt to quiesce active port.\n", port->name);
		return;
	}

	if (port->flags & PARPORT_FLAG_COMA) {
		printk(KERN_WARNING "%s: attempt to quiesce comatose port.\n", port->name);
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

	/* We only allow one lurking device. */
	if (flags & PARPORT_DEV_LURK) {
		if (port->lurker) {
			printk(KERN_INFO "%s: refused to register second lurker (%s)\n",
				   port->name, name);
			return NULL;
		}
		if (!pf || !kf) {
			printk(KERN_INFO "%s: refused to register lurking device (%s) without callbacks\n", port->name, name);
			return NULL;
		}
	}

	/* We may need to claw back the port hardware. */
	if (port->flags & PARPORT_FLAG_COMA) {
		if (port->ops->claim_resources(port)) {
			printk(KERN_WARNING "%s: unable to get hardware to register %s.\n", port->name, name);
			return NULL;
		}
		port->flags &= ~PARPORT_FLAG_COMA;
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

	tmp->name = (char *) name;
	tmp->port = port;
	tmp->preempt = pf;
	tmp->wakeup = kf;
	tmp->private = handle;
	tmp->flags = flags;
	tmp->irq_func = irq_func;
	port->ops->save_state(port, tmp->state);

	/* Chain this onto the list */
	tmp->prev = NULL;
	tmp->next = port->devices;
	if (port->devices)
		port->devices->prev = tmp;
	port->devices = tmp;

	if (flags & PARPORT_DEV_LURK)
		port->lurker = tmp;

	inc_parport_count();
	port->ops->inc_use_count();

	return tmp;
}

void parport_unregister_device(struct pardevice *dev)
{
	struct parport *port;

	if (dev == NULL) {
		printk(KERN_ERR "parport_unregister_device: passed NULL\n");
		return;
	}

	port = dev->port;

	if (port->cad == dev) {
		printk(KERN_WARNING "%s: refused to unregister currently active device %s.\n", port->name, dev->name);
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
	struct pardevice *pd1;

	if (dev->port->cad == dev) {
		printk(KERN_INFO "%s: %s already owner\n",
			   dev->port->name,dev->name);
		return 0;
	}

	/* Preempt any current device */
	pd1 = dev->port->cad;
	if (dev->port->cad) {
		if (dev->port->cad->preempt) {
			if (dev->port->cad->preempt(dev->port->cad->private))
				return -EAGAIN;
			dev->port->ops->save_state(dev->port, dev->state);
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

	/* Swap the IRQ handlers. */
	if (dev->port->irq != PARPORT_IRQ_NONE) {
		free_irq(dev->port->irq, pd1?pd1->private:NULL);
		request_irq(dev->port->irq, dev->irq_func ? dev->irq_func :
			    parport_null_intr_func, SA_INTERRUPT, dev->name,
			    dev->private);
	}

	/* Restore control registers */
	dev->port->ops->restore_state(dev->port, dev->state);

	return 0;
}

void parport_release(struct pardevice *dev)
{
	struct pardevice *pd1;

	/* Make sure that dev is the current device */
	if (dev->port->cad != dev) {
		printk(KERN_WARNING "%s: %s tried to release parport when not owner\n", dev->port->name, dev->name);
		return;
	}
	dev->port->cad = NULL;

	/* Save control registers */
	dev->port->ops->save_state(dev->port, dev->state);

	/* Point IRQs somewhere harmless. */
	if (dev->port->irq != PARPORT_IRQ_NONE) {
		free_irq(dev->port->irq, dev->private);
		request_irq(dev->port->irq, parport_null_intr_func,
			    SA_INTERRUPT, dev->port->name, NULL);
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
	 * There must be a wakeup callback because we checked for it
	 * at registration.
	 */
	if (dev->port->lurker && (dev->port->lurker != dev)) {
		dev->port->lurker->wakeup(dev->port->lurker->private);
	}
}
