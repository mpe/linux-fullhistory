/*
 *  pm.c - Power management interface
 *
 *  Copyright (C) 2000 Andrew Henroid
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/pm.h>

int pm_active = 0;

static spinlock_t pm_devs_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(pm_devs);

/*
 * Register a device with power management
 */
struct pm_dev *pm_register(pm_dev_t type,
			   unsigned long id,
			   pm_callback callback)
{
	struct pm_dev *dev = kmalloc(sizeof(struct pm_dev), GFP_KERNEL);
	if (dev) {
		unsigned long flags;

		memset(dev, 0, sizeof(*dev));
		dev->type = type;
		dev->id = id;
		dev->callback = callback;

		spin_lock_irqsave(&pm_devs_lock, flags);
		list_add(&dev->entry, &pm_devs);
		spin_unlock_irqrestore(&pm_devs_lock, flags);
	}
	return dev;
}

/*
 * Unregister a device with power management
 */
void pm_unregister(struct pm_dev *dev)
{
	if (dev) {
		unsigned long flags;

		spin_lock_irqsave(&pm_devs_lock, flags);
		list_del(&dev->entry);
		spin_unlock_irqrestore(&pm_devs_lock, flags);

		kfree(dev);
	}
}

/*
 * Unregister all devices with matching callback
 */
void pm_unregister_all(pm_callback callback)
{
	struct list_head *entry;

	if (!callback)
		return;

	entry = pm_devs.next;
	while (entry != &pm_devs) {
		struct pm_dev *dev = list_entry(entry, struct pm_dev, entry);
		entry = entry->next;
		if (dev->callback == callback)
			pm_unregister(dev);
	}
}

/*
 * Send request to a single device
 */
int pm_send(struct pm_dev *dev, pm_request_t rqst, void *data)
{
	int status = 0;
	int prev_state, next_state;
	switch (rqst) {
	case PM_SUSPEND:
	case PM_RESUME:
		prev_state = dev->state;
		next_state = (int) data;
		if (prev_state != next_state) {
			if (dev->callback)
				status = (*dev->callback)(dev, rqst, data);
			if (!status) {
				dev->state = next_state;
				dev->prev_state = prev_state;
			}
		}
		else {
			dev->prev_state = prev_state;
		}
		break;
	default:
		if (dev->callback)
			status = (*dev->callback)(dev, rqst, data);
		break;
	}
	return status;
}

/*
 * Undo incomplete request
 */
static void pm_undo_all(struct pm_dev *last)
{
	struct list_head *entry = last->entry.prev;
	while (entry != &pm_devs) {
		struct pm_dev *dev = list_entry(entry, struct pm_dev, entry);
		if (dev->state != dev->prev_state) {
			/* previous state was zero (running) resume or
			 * previous state was non-zero (suspended) suspend
			 */
			pm_request_t undo = (dev->prev_state
					     ? PM_SUSPEND:PM_RESUME);
			pm_send(dev, undo, (void*) dev->prev_state);
		}
		entry = entry->prev;
	}
}

/*
 * Send a request to all devices
 */
int pm_send_all(pm_request_t rqst, void *data)
{
	struct list_head *entry = pm_devs.next;
	while (entry != &pm_devs) {
		struct pm_dev *dev = list_entry(entry, struct pm_dev, entry);
		if (dev->callback) {
			int status = pm_send(dev, rqst, data);
			if (status) {
				/* return devices to previous state on
				 * failed suspend request
				 */
				if (rqst == PM_SUSPEND)
					pm_undo_all(dev);
				return status;
			}
		}
		entry = entry->next;
	}
	return 0;
}

/*
 * Find a device
 */
struct pm_dev *pm_find(pm_dev_t type, struct pm_dev *from)
{
	struct list_head *entry = from ? from->entry.next:pm_devs.next;
	while (entry != &pm_devs) {
		struct pm_dev *dev = list_entry(entry, struct pm_dev, entry);
		if (type == PM_UNKNOWN_DEV || dev->type == type)
			return dev;
		entry = entry->next;
	}
	return 0;
}

EXPORT_SYMBOL(pm_register);
EXPORT_SYMBOL(pm_unregister);
EXPORT_SYMBOL(pm_unregister_all);
EXPORT_SYMBOL(pm_send);
EXPORT_SYMBOL(pm_send_all);
EXPORT_SYMBOL(pm_find);
EXPORT_SYMBOL(pm_active);
