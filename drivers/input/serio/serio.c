/*
 *  The Serio abstraction module
 *
 *  Copyright (c) 1999-2004 Vojtech Pavlik
 *  Copyright (c) 2004 Dmitry Torokhov
 *  Copyright (c) 2003 Daniele Bellucci
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/serio.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Serio abstraction core");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(serio_interrupt);
EXPORT_SYMBOL(__serio_register_port);
EXPORT_SYMBOL(serio_unregister_port);
EXPORT_SYMBOL(__serio_unregister_port_delayed);
EXPORT_SYMBOL(__serio_register_driver);
EXPORT_SYMBOL(serio_unregister_driver);
EXPORT_SYMBOL(serio_open);
EXPORT_SYMBOL(serio_close);
EXPORT_SYMBOL(serio_rescan);
EXPORT_SYMBOL(serio_reconnect);

/*
 * serio_sem protects entire serio subsystem and is taken every time
 * serio port or driver registrered or unregistered.
 */
static DECLARE_MUTEX(serio_sem);

static LIST_HEAD(serio_list);

static struct bus_type serio_bus = {
	.name =	"serio",
};

static void serio_add_port(struct serio *serio);
static void serio_destroy_port(struct serio *serio);
static void serio_reconnect_port(struct serio *serio);
static void serio_disconnect_port(struct serio *serio);

static int serio_match_port(const struct serio_device_id *ids, struct serio *serio)
{
	while (ids->type || ids->proto) {
		if ((ids->type == SERIO_ANY || ids->type == serio->id.type) &&
		    (ids->proto == SERIO_ANY || ids->proto == serio->id.proto) &&
		    (ids->extra == SERIO_ANY || ids->extra == serio->id.extra) &&
		    (ids->id == SERIO_ANY || ids->id == serio->id.id))
			return 1;
		ids++;
	}
	return 0;
}

/*
 * Basic serio -> driver core mappings
 */

static void serio_bind_driver(struct serio *serio, struct serio_driver *drv)
{
	down_write(&serio_bus.subsys.rwsem);

	if (serio_match_port(drv->id_table, serio)) {
		serio->dev.driver = &drv->driver;
		if (drv->connect(serio, drv)) {
			serio->dev.driver = NULL;
			goto out;
		}
		device_bind_driver(&serio->dev);
	}
out:
	up_write(&serio_bus.subsys.rwsem);
}

static void serio_release_driver(struct serio *serio)
{
	down_write(&serio_bus.subsys.rwsem);
	device_release_driver(&serio->dev);
	up_write(&serio_bus.subsys.rwsem);
}

static void serio_find_driver(struct serio *serio)
{
	down_write(&serio_bus.subsys.rwsem);
	device_attach(&serio->dev);
	up_write(&serio_bus.subsys.rwsem);
}


/*
 * Serio event processing.
 */

enum serio_event_type {
	SERIO_RESCAN,
	SERIO_RECONNECT,
	SERIO_REGISTER_PORT,
	SERIO_UNREGISTER_PORT,
	SERIO_REGISTER_DRIVER,
};

struct serio_event {
	enum serio_event_type type;
	void *object;
	struct module *owner;
	struct list_head node;
};

static DEFINE_SPINLOCK(serio_event_lock);	/* protects serio_event_list */
static LIST_HEAD(serio_event_list);
static DECLARE_WAIT_QUEUE_HEAD(serio_wait);
static DECLARE_COMPLETION(serio_exited);
static int serio_pid;

static void serio_queue_event(void *object, struct module *owner,
			      enum serio_event_type event_type)
{
	unsigned long flags;
	struct serio_event *event;

	spin_lock_irqsave(&serio_event_lock, flags);

	/*
 	 * Scan event list for the other events for the same serio port,
	 * starting with the most recent one. If event is the same we
	 * do not need add new one. If event is of different type we
	 * need to add this event and should not look further because
	 * we need to preseve sequence of distinct events.
 	 */
	list_for_each_entry_reverse(event, &serio_event_list, node) {
		if (event->object == object) {
			if (event->type == event_type)
				goto out;
			break;
		}
	}

	if ((event = kmalloc(sizeof(struct serio_event), GFP_ATOMIC))) {
		if (!try_module_get(owner)) {
			printk(KERN_WARNING "serio: Can't get module reference, dropping event %d\n", event_type);
			goto out;
		}

		event->type = event_type;
		event->object = object;
		event->owner = owner;

		list_add_tail(&event->node, &serio_event_list);
		wake_up(&serio_wait);
	} else {
		printk(KERN_ERR "serio: Not enough memory to queue event %d\n", event_type);
	}
out:
	spin_unlock_irqrestore(&serio_event_lock, flags);
}

static void serio_free_event(struct serio_event *event)
{
	module_put(event->owner);
	kfree(event);
}

static void serio_remove_duplicate_events(struct serio_event *event)
{
	struct list_head *node, *next;
	struct serio_event *e;
	unsigned long flags;

	spin_lock_irqsave(&serio_event_lock, flags);

	list_for_each_safe(node, next, &serio_event_list) {
		e = list_entry(node, struct serio_event, node);
		if (event->object == e->object) {
			/*
			 * If this event is of different type we should not
			 * look further - we only suppress duplicate events
			 * that were sent back-to-back.
			 */
			if (event->type != e->type)
				break;

			list_del_init(node);
			serio_free_event(e);
		}
	}

	spin_unlock_irqrestore(&serio_event_lock, flags);
}


static struct serio_event *serio_get_event(void)
{
	struct serio_event *event;
	struct list_head *node;
	unsigned long flags;

	spin_lock_irqsave(&serio_event_lock, flags);

	if (list_empty(&serio_event_list)) {
		spin_unlock_irqrestore(&serio_event_lock, flags);
		return NULL;
	}

	node = serio_event_list.next;
	event = list_entry(node, struct serio_event, node);
	list_del_init(node);

	spin_unlock_irqrestore(&serio_event_lock, flags);

	return event;
}

static void serio_handle_events(void)
{
	struct serio_event *event;
	struct serio_driver *serio_drv;

	down(&serio_sem);

	while ((event = serio_get_event())) {

		switch (event->type) {
			case SERIO_REGISTER_PORT:
				serio_add_port(event->object);
				break;

			case SERIO_UNREGISTER_PORT:
				serio_disconnect_port(event->object);
				serio_destroy_port(event->object);
				break;

			case SERIO_RECONNECT:
				serio_reconnect_port(event->object);
				break;

			case SERIO_RESCAN:
				serio_disconnect_port(event->object);
				serio_find_driver(event->object);
				break;

			case SERIO_REGISTER_DRIVER:
				serio_drv = event->object;
				driver_register(&serio_drv->driver);
				break;

			default:
				break;
		}

		serio_remove_duplicate_events(event);
		serio_free_event(event);
	}

	up(&serio_sem);
}

/*
 * Remove all events that have been submitted for a given serio port.
 */
static void serio_remove_pending_events(struct serio *serio)
{
	struct list_head *node, *next;
	struct serio_event *event;
	unsigned long flags;

	spin_lock_irqsave(&serio_event_lock, flags);

	list_for_each_safe(node, next, &serio_event_list) {
		event = list_entry(node, struct serio_event, node);
		if (event->object == serio) {
			list_del_init(node);
			serio_free_event(event);
		}
	}

	spin_unlock_irqrestore(&serio_event_lock, flags);
}

/*
 * Destroy child serio port (if any) that has not been fully registered yet.
 *
 * Note that we rely on the fact that port can have only one child and therefore
 * only one child registration request can be pending. Additionally, children
 * are registered by driver's connect() handler so there can't be a grandchild
 * pending registration together with a child.
 */
static struct serio *serio_get_pending_child(struct serio *parent)
{
	struct serio_event *event;
	struct serio *serio, *child = NULL;
	unsigned long flags;

	spin_lock_irqsave(&serio_event_lock, flags);

	list_for_each_entry(event, &serio_event_list, node) {
		if (event->type == SERIO_REGISTER_PORT) {
			serio = event->object;
			if (serio->parent == parent) {
				child = serio;
				break;
			}
		}
	}

	spin_unlock_irqrestore(&serio_event_lock, flags);
	return child;
}

static int serio_thread(void *nothing)
{
	lock_kernel();
	daemonize("kseriod");
	allow_signal(SIGTERM);

	do {
		serio_handle_events();
		wait_event_interruptible(serio_wait, !list_empty(&serio_event_list));
		try_to_freeze(PF_FREEZE);
	} while (!signal_pending(current));

	printk(KERN_DEBUG "serio: kseriod exiting\n");

	unlock_kernel();
	complete_and_exit(&serio_exited, 0);
}


/*
 * Serio port operations
 */

static ssize_t serio_show_description(struct device *dev, char *buf)
{
	struct serio *serio = to_serio_port(dev);
	return sprintf(buf, "%s\n", serio->name);
}

static ssize_t serio_show_id_type(struct device *dev, char *buf)
{
	struct serio *serio = to_serio_port(dev);
	return sprintf(buf, "%02x\n", serio->id.type);
}

static ssize_t serio_show_id_proto(struct device *dev, char *buf)
{
	struct serio *serio = to_serio_port(dev);
	return sprintf(buf, "%02x\n", serio->id.proto);
}

static ssize_t serio_show_id_id(struct device *dev, char *buf)
{
	struct serio *serio = to_serio_port(dev);
	return sprintf(buf, "%02x\n", serio->id.id);
}

static ssize_t serio_show_id_extra(struct device *dev, char *buf)
{
	struct serio *serio = to_serio_port(dev);
	return sprintf(buf, "%02x\n", serio->id.extra);
}

static ssize_t serio_rebind_driver(struct device *dev, const char *buf, size_t count)
{
	struct serio *serio = to_serio_port(dev);
	struct device_driver *drv;
	int retval;

	retval = down_interruptible(&serio_sem);
	if (retval)
		return retval;

	retval = count;
	if (!strncmp(buf, "none", count)) {
		serio_disconnect_port(serio);
	} else if (!strncmp(buf, "reconnect", count)) {
		serio_reconnect_port(serio);
	} else if (!strncmp(buf, "rescan", count)) {
		serio_disconnect_port(serio);
		serio_find_driver(serio);
	} else if ((drv = driver_find(buf, &serio_bus)) != NULL) {
		serio_disconnect_port(serio);
		serio_bind_driver(serio, to_serio_driver(drv));
		put_driver(drv);
	} else {
		retval = -EINVAL;
	}

	up(&serio_sem);

	return retval;
}

static ssize_t serio_show_bind_mode(struct device *dev, char *buf)
{
	struct serio *serio = to_serio_port(dev);
	return sprintf(buf, "%s\n", serio->manual_bind ? "manual" : "auto");
}

static ssize_t serio_set_bind_mode(struct device *dev, const char *buf, size_t count)
{
	struct serio *serio = to_serio_port(dev);
	int retval;

	retval = count;
	if (!strncmp(buf, "manual", count)) {
		serio->manual_bind = 1;
	} else if (!strncmp(buf, "auto", count)) {
		serio->manual_bind = 0;
	} else {
		retval = -EINVAL;
	}

	return retval;
}

static struct device_attribute serio_device_attrs[] = {
	__ATTR(description, S_IRUGO, serio_show_description, NULL),
	__ATTR(id_type, S_IRUGO, serio_show_id_type, NULL),
	__ATTR(id_proto, S_IRUGO, serio_show_id_proto, NULL),
	__ATTR(id_id, S_IRUGO, serio_show_id_id, NULL),
	__ATTR(id_extra, S_IRUGO, serio_show_id_extra, NULL),
	__ATTR(drvctl, S_IWUSR, NULL, serio_rebind_driver),
	__ATTR(bind_mode, S_IWUSR | S_IRUGO, serio_show_bind_mode, serio_set_bind_mode),
	__ATTR_NULL
};


static void serio_release_port(struct device *dev)
{
	struct serio *serio = to_serio_port(dev);

	kfree(serio);
	module_put(THIS_MODULE);
}

/*
 * Prepare serio port for registration.
 */
static void serio_init_port(struct serio *serio)
{
	static atomic_t serio_no = ATOMIC_INIT(0);

	__module_get(THIS_MODULE);

	spin_lock_init(&serio->lock);
	init_MUTEX(&serio->drv_sem);
	device_initialize(&serio->dev);
	snprintf(serio->dev.bus_id, sizeof(serio->dev.bus_id),
		 "serio%ld", (long)atomic_inc_return(&serio_no) - 1);
	serio->dev.bus = &serio_bus;
	serio->dev.release = serio_release_port;
	if (serio->parent)
		serio->dev.parent = &serio->parent->dev;
}

/*
 * Complete serio port registration.
 * Driver core will attempt to find appropriate driver for the port.
 */
static void serio_add_port(struct serio *serio)
{
	if (serio->parent) {
		serio_pause_rx(serio->parent);
		serio->parent->child = serio;
		serio_continue_rx(serio->parent);
	}

	list_add_tail(&serio->node, &serio_list);
	if (serio->start)
		serio->start(serio);
	device_add(&serio->dev);
	serio->registered = 1;
}

/*
 * serio_destroy_port() completes deregistration process and removes
 * port from the system
 */
static void serio_destroy_port(struct serio *serio)
{
	struct serio *child;

	child = serio_get_pending_child(serio);
	if (child) {
		serio_remove_pending_events(child);
		put_device(&child->dev);
	}

	if (serio->stop)
		serio->stop(serio);

	if (serio->parent) {
		serio_pause_rx(serio->parent);
		serio->parent->child = NULL;
		serio_continue_rx(serio->parent);
		serio->parent = NULL;
	}

	if (serio->registered) {
		device_del(&serio->dev);
		list_del_init(&serio->node);
		serio->registered = 0;
	}

	serio_remove_pending_events(serio);
	put_device(&serio->dev);
}

/*
 * Reconnect serio port and all its children (re-initialize attached devices)
 */
static void serio_reconnect_port(struct serio *serio)
{
	do {
		if (!serio->drv || !serio->drv->reconnect || serio->drv->reconnect(serio)) {
			serio_disconnect_port(serio);
			serio_find_driver(serio);
			/* Ok, old children are now gone, we are done */
			break;
		}
		serio = serio->child;
	} while (serio);
}

/*
 * serio_disconnect_port() unbinds a port from its driver. As a side effect
 * all child ports are unbound and destroyed.
 */
static void serio_disconnect_port(struct serio *serio)
{
	struct serio *s, *parent;

	if (serio->child) {
		/*
		 * Children ports should be disconnected and destroyed
		 * first, staring with the leaf one, since we don't want
		 * to do recursion
		 */
		for (s = serio; s->child; s = s->child)
			/* empty */;

		do {
			parent = s->parent;

			serio_release_driver(s);
			serio_destroy_port(s);
		} while ((s = parent) != serio);
	}

	/*
	 * Ok, no children left, now disconnect this port
	 */
	serio_release_driver(serio);
}

void serio_rescan(struct serio *serio)
{
	serio_queue_event(serio, NULL, SERIO_RESCAN);
}

void serio_reconnect(struct serio *serio)
{
	serio_queue_event(serio, NULL, SERIO_RECONNECT);
}

/*
 * Submits register request to kseriod for subsequent execution.
 * Note that port registration is always asynchronous.
 */
void __serio_register_port(struct serio *serio, struct module *owner)
{
	serio_init_port(serio);
	serio_queue_event(serio, owner, SERIO_REGISTER_PORT);
}

/*
 * Synchronously unregisters serio port.
 */
void serio_unregister_port(struct serio *serio)
{
	down(&serio_sem);
	serio_disconnect_port(serio);
	serio_destroy_port(serio);
	up(&serio_sem);
}

/*
 * Submits register request to kseriod for subsequent execution.
 * Can be used when it is not obvious whether the serio_sem is
 * taken or not and when delayed execution is feasible.
 */
void __serio_unregister_port_delayed(struct serio *serio, struct module *owner)
{
	serio_queue_event(serio, owner, SERIO_UNREGISTER_PORT);
}


/*
 * Serio driver operations
 */

static ssize_t serio_driver_show_description(struct device_driver *drv, char *buf)
{
	struct serio_driver *driver = to_serio_driver(drv);
	return sprintf(buf, "%s\n", driver->description ? driver->description : "(none)");
}

static ssize_t serio_driver_show_bind_mode(struct device_driver *drv, char *buf)
{
	struct serio_driver *serio_drv = to_serio_driver(drv);
	return sprintf(buf, "%s\n", serio_drv->manual_bind ? "manual" : "auto");
}

static ssize_t serio_driver_set_bind_mode(struct device_driver *drv, const char *buf, size_t count)
{
	struct serio_driver *serio_drv = to_serio_driver(drv);
	int retval;

	retval = count;
	if (!strncmp(buf, "manual", count)) {
		serio_drv->manual_bind = 1;
	} else if (!strncmp(buf, "auto", count)) {
		serio_drv->manual_bind = 0;
	} else {
		retval = -EINVAL;
	}

	return retval;
}


static struct driver_attribute serio_driver_attrs[] = {
	__ATTR(description, S_IRUGO, serio_driver_show_description, NULL),
	__ATTR(bind_mode, S_IWUSR | S_IRUGO,
		serio_driver_show_bind_mode, serio_driver_set_bind_mode),
	__ATTR_NULL
};

static int serio_driver_probe(struct device *dev)
{
	struct serio *serio = to_serio_port(dev);
	struct serio_driver *drv = to_serio_driver(dev->driver);

	return drv->connect(serio, drv);
}

static int serio_driver_remove(struct device *dev)
{
	struct serio *serio = to_serio_port(dev);
	struct serio_driver *drv = to_serio_driver(dev->driver);

	drv->disconnect(serio);
	return 0;
}

void __serio_register_driver(struct serio_driver *drv, struct module *owner)
{
	drv->driver.bus = &serio_bus;
	drv->driver.probe = serio_driver_probe;
	drv->driver.remove = serio_driver_remove;

	serio_queue_event(drv, owner, SERIO_REGISTER_DRIVER);
}

void serio_unregister_driver(struct serio_driver *drv)
{
	struct serio *serio;

	down(&serio_sem);
	drv->manual_bind = 1;	/* so serio_find_driver ignores it */

start_over:
	list_for_each_entry(serio, &serio_list, node) {
		if (serio->drv == drv) {
			serio_disconnect_port(serio);
			serio_find_driver(serio);
			/* we could've deleted some ports, restart */
			goto start_over;
		}
	}

	driver_unregister(&drv->driver);
	up(&serio_sem);
}

static void serio_set_drv(struct serio *serio, struct serio_driver *drv)
{
	down(&serio->drv_sem);
	serio_pause_rx(serio);
	serio->drv = drv;
	serio_continue_rx(serio);
	up(&serio->drv_sem);
}

static int serio_bus_match(struct device *dev, struct device_driver *drv)
{
	struct serio *serio = to_serio_port(dev);
	struct serio_driver *serio_drv = to_serio_driver(drv);

	if (serio->manual_bind || serio_drv->manual_bind)
		return 0;

	return serio_match_port(serio_drv->id_table, serio);
}

#ifdef CONFIG_HOTPLUG

#define PUT_ENVP(fmt, val) 						\
do {									\
	envp[i++] = buffer;						\
	length += snprintf(buffer, buffer_size - length, fmt, val);	\
	if (buffer_size - length <= 0 || i >= num_envp)			\
		return -ENOMEM;						\
	length++;							\
	buffer += length;						\
} while (0)
static int serio_hotplug(struct device *dev, char **envp, int num_envp, char *buffer, int buffer_size)
{
	struct serio *serio;
	int i = 0;
	int length = 0;

	if (!dev)
		return -ENODEV;

	serio = to_serio_port(dev);

	PUT_ENVP("SERIO_TYPE=%02x", serio->id.type);
	PUT_ENVP("SERIO_PROTO=%02x", serio->id.proto);
	PUT_ENVP("SERIO_ID=%02x", serio->id.id);
	PUT_ENVP("SERIO_EXTRA=%02x", serio->id.extra);

	envp[i] = NULL;

	return 0;
}
#undef PUT_ENVP

#else

static int serio_hotplug(struct device *dev, char **envp, int num_envp, char *buffer, int buffer_size)
{
	return -ENODEV;
}

#endif /* CONFIG_HOTPLUG */

static int serio_resume(struct device *dev)
{
	struct serio *serio = to_serio_port(dev);

	if (!serio->drv || !serio->drv->reconnect || serio->drv->reconnect(serio)) {
		serio_disconnect_port(serio);
		/*
		 * Driver re-probing can take a while, so better let kseriod
		 * deal with it.
		 */
		serio_rescan(serio);
	}

	return 0;
}

/* called from serio_driver->connect/disconnect methods under serio_sem */
int serio_open(struct serio *serio, struct serio_driver *drv)
{
	serio_set_drv(serio, drv);

	if (serio->open && serio->open(serio)) {
		serio_set_drv(serio, NULL);
		return -1;
	}
	return 0;
}

/* called from serio_driver->connect/disconnect methods under serio_sem */
void serio_close(struct serio *serio)
{
	if (serio->close)
		serio->close(serio);

	serio_set_drv(serio, NULL);
}

irqreturn_t serio_interrupt(struct serio *serio,
		unsigned char data, unsigned int dfl, struct pt_regs *regs)
{
	unsigned long flags;
	irqreturn_t ret = IRQ_NONE;

	spin_lock_irqsave(&serio->lock, flags);

        if (likely(serio->drv)) {
                ret = serio->drv->interrupt(serio, data, dfl, regs);
	} else if (!dfl && serio->registered) {
		serio_rescan(serio);
		ret = IRQ_HANDLED;
	}

	spin_unlock_irqrestore(&serio->lock, flags);

	return ret;
}

static int __init serio_init(void)
{
	if (!(serio_pid = kernel_thread(serio_thread, NULL, CLONE_KERNEL))) {
		printk(KERN_ERR "serio: Failed to start kseriod\n");
		return -1;
	}

	serio_bus.dev_attrs = serio_device_attrs;
	serio_bus.drv_attrs = serio_driver_attrs;
	serio_bus.match = serio_bus_match;
	serio_bus.hotplug = serio_hotplug;
	serio_bus.resume = serio_resume;
	bus_register(&serio_bus);

	return 0;
}

static void __exit serio_exit(void)
{
	bus_unregister(&serio_bus);
	kill_proc(serio_pid, SIGTERM, 1);
	wait_for_completion(&serio_exited);
}

module_init(serio_init);
module_exit(serio_exit);
