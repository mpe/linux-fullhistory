/*
 *  Routines for driver control interface
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#define __NO_VERSION__
#include <sound/driver.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/info.h>
#include <sound/control.h>

typedef struct _snd_kctl_ioctl {
	struct list_head list;		/* list of all ioctls */
	snd_kctl_ioctl_func_t fioctl;
} snd_kctl_ioctl_t;

#define snd_kctl_ioctl(n) list_entry(n, snd_kctl_ioctl_t, list)

static rwlock_t snd_ioctl_rwlock = RW_LOCK_UNLOCKED;
static LIST_HEAD(snd_control_ioctls);

static inline void dec_mod_count(struct module *module)
{
	if (module)
		__MOD_DEC_USE_COUNT(module);
}

static int snd_ctl_open(struct inode *inode, struct file *file)
{
	int cardnum = SNDRV_MINOR_CARD(minor(inode->i_rdev));
	unsigned long flags;
	snd_card_t *card;
	snd_ctl_file_t *ctl;
	int err;

#ifdef LINUX_2_2
	MOD_INC_USE_COUNT;
#endif
	card = snd_cards[cardnum];
	if (!card) {
		err = -ENODEV;
		goto __error1;
	}
	if (!try_inc_mod_count(card->module)) {
		err = -EFAULT;
		goto __error1;
	}
	ctl = snd_magic_kcalloc(snd_ctl_file_t, 0, GFP_KERNEL);
	if (ctl == NULL) {
		err = -ENOMEM;
		goto __error;
	}
	INIT_LIST_HEAD(&ctl->events);
	init_waitqueue_head(&ctl->change_sleep);
	spin_lock_init(&ctl->read_lock);
	ctl->card = card;
	ctl->pid = current->pid;
	file->private_data = ctl;
	write_lock_irqsave(&card->control_rwlock, flags);
	list_add_tail(&ctl->list, &card->ctl_files);
	write_unlock_irqrestore(&card->control_rwlock, flags);
	return 0;

      __error:
      	dec_mod_count(card->module);
      __error1:
#ifdef LINUX_2_2
      	MOD_DEC_USE_COUNT;
#endif
      	return err;
}

static void snd_ctl_empty_read_queue(snd_ctl_file_t * ctl)
{
	snd_kctl_event_t *cread;
	
	spin_lock(&ctl->read_lock);
	while (!list_empty(&ctl->events)) {
		cread = snd_kctl_event(ctl->events.next);
		list_del(&cread->list);
		kfree(cread);
	}
	spin_unlock(&ctl->read_lock);
}

static int snd_ctl_release(struct inode *inode, struct file *file)
{
	unsigned long flags;
	struct list_head *list;
	snd_card_t *card;
	snd_ctl_file_t *ctl;
	snd_kcontrol_t *control;

	ctl = snd_magic_cast(snd_ctl_file_t, file->private_data, return -ENXIO);
	fasync_helper(-1, file, 0, &ctl->fasync);
	file->private_data = NULL;
	card = ctl->card;
	write_lock_irqsave(&card->control_rwlock, flags);
	list_del(&ctl->list);
	write_unlock_irqrestore(&card->control_rwlock, flags);
	write_lock(&card->control_owner_lock);
	list_for_each(list, &card->controls) {
		control = snd_kcontrol(list);
		if (control->owner == ctl)
			control->owner = NULL;
	}
	write_unlock(&card->control_owner_lock);
	snd_ctl_empty_read_queue(ctl);
	snd_magic_kfree(ctl);
	dec_mod_count(card->module);
#ifdef LINUX_2_2
	MOD_DEC_USE_COUNT;
#endif
	return 0;
}

void snd_ctl_notify(snd_card_t *card, unsigned int mask, snd_ctl_elem_id_t *id)
{
	unsigned long flags;
	struct list_head *flist;
	snd_ctl_file_t *ctl;
	snd_kctl_event_t *ev;
	
	snd_runtime_check(card != NULL && id != NULL, return);
	read_lock_irqsave(&card->control_rwlock, flags);
#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
	card->mixer_oss_change_count++;
#endif
	list_for_each(flist, &card->ctl_files) {
		struct list_head *elist;
		ctl = snd_ctl_file(flist);
		if (!ctl->subscribed)
			continue;
		spin_lock(&ctl->read_lock);
		list_for_each(elist, &ctl->events) {
			ev = snd_kctl_event(elist);
			if (ev->id.numid == id->numid) {
				ev->mask |= mask;
				goto _found;
			}
		}
		ev = snd_kcalloc(sizeof(*ev), in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
		if (ev) {
			ev->id = *id;
			ev->mask = mask;
			list_add_tail(&ev->list, &ctl->events);
		} else {
			snd_printk(KERN_ERR "No memory available to allocate event\n");
		}
	_found:
		wake_up(&ctl->change_sleep);
		kill_fasync(&ctl->fasync, SIGIO, POLL_IN);
		spin_unlock(&ctl->read_lock);
	}
	read_unlock_irqrestore(&card->control_rwlock, flags);
}

snd_kcontrol_t *snd_ctl_new(snd_kcontrol_t * control)
{
	snd_kcontrol_t *kctl;
	
	snd_runtime_check(control != NULL, return NULL);
	kctl = (snd_kcontrol_t *)snd_magic_kmalloc(snd_kcontrol_t, 0, GFP_KERNEL);
	if (kctl == NULL)
		return NULL;
	*kctl = *control;
	return kctl;
}

snd_kcontrol_t *snd_ctl_new1(snd_kcontrol_new_t * ncontrol, void *private_data)
{
	snd_kcontrol_t kctl;
	
	snd_runtime_check(ncontrol != NULL, return NULL);
	snd_assert(ncontrol->info != NULL, return NULL);
	memset(&kctl, 0, sizeof(kctl));
	kctl.id.iface = ncontrol->iface;
	kctl.id.device = ncontrol->device;
	kctl.id.subdevice = ncontrol->subdevice;
	strncpy(kctl.id.name, ncontrol->name, sizeof(kctl.id.name)-1);
	kctl.id.index = ncontrol->index;
	kctl.access = ncontrol->access == 0 ? SNDRV_CTL_ELEM_ACCESS_READWRITE :
		      (ncontrol->access & (SNDRV_CTL_ELEM_ACCESS_READWRITE|SNDRV_CTL_ELEM_ACCESS_INACTIVE|SNDRV_CTL_ELEM_ACCESS_INDIRECT));
	kctl.info = ncontrol->info;
	kctl.get = ncontrol->get;
	kctl.put = ncontrol->put;
	kctl.private_value = ncontrol->private_value;
	kctl.private_data = private_data;
	return snd_ctl_new(&kctl);
}

void snd_ctl_free_one(snd_kcontrol_t * kcontrol)
{
	if (kcontrol) {
		if (kcontrol->private_free)
			kcontrol->private_free(kcontrol);
		snd_magic_kfree(kcontrol);
	}
}

int snd_ctl_add(snd_card_t * card, snd_kcontrol_t * kcontrol)
{
	snd_runtime_check(card != NULL && kcontrol != NULL, return -EINVAL);
	snd_assert(kcontrol->info != NULL, return -EINVAL);
	snd_assert(!(kcontrol->access & SNDRV_CTL_ELEM_ACCESS_READ) || kcontrol->get != NULL, return -EINVAL);
	snd_assert(!(kcontrol->access & SNDRV_CTL_ELEM_ACCESS_WRITE) || kcontrol->put != NULL, return -EINVAL);
	write_lock(&card->control_rwlock);
	list_add_tail(&kcontrol->list, &card->controls);
	card->controls_count++;
	kcontrol->id.numid = ++card->last_numid;
	write_unlock(&card->control_rwlock);
	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_ADD, &kcontrol->id);
	return 0;
}

int snd_ctl_remove(snd_card_t * card, snd_kcontrol_t * kcontrol)
{
	snd_runtime_check(card != NULL && kcontrol != NULL, return -EINVAL);
	write_lock(&card->control_rwlock);
	list_del(&kcontrol->list);
	card->controls_count--;
	write_unlock(&card->control_rwlock);
	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_REMOVE, &kcontrol->id);
	snd_ctl_free_one(kcontrol);
	return 0;
}

int snd_ctl_remove_id(snd_card_t * card, snd_ctl_elem_id_t *id)
{
	snd_kcontrol_t *kctl;

	kctl = snd_ctl_find_id(card, id);
	if (kctl == NULL)
		return -ENOENT;
	return snd_ctl_remove(card, kctl);
}

int snd_ctl_rename_id(snd_card_t * card, snd_ctl_elem_id_t *src_id, snd_ctl_elem_id_t *dst_id)
{
	snd_kcontrol_t *kctl;

	kctl = snd_ctl_find_id(card, src_id);
	if (kctl == NULL)
		return -ENOENT;
	write_lock(&card->control_rwlock);
	kctl->id = *dst_id;
	kctl->id.numid = ++card->last_numid;
	write_unlock(&card->control_rwlock);
	return 0;
}

snd_kcontrol_t *snd_ctl_find_numid(snd_card_t * card, unsigned int numid)
{
	struct list_head *list;
	snd_kcontrol_t *kctl;

	snd_runtime_check(card != NULL && numid != 0, return NULL);
	read_lock(&card->control_rwlock);
	list_for_each(list, &card->controls) {
		kctl = snd_kcontrol(list);
		if (kctl->id.numid == numid) {
			read_unlock(&card->control_rwlock);
			return kctl;
		}
	}
	read_unlock(&card->control_rwlock);
	return NULL;
}

snd_kcontrol_t *snd_ctl_find_id(snd_card_t * card, snd_ctl_elem_id_t *id)
{
	struct list_head *list;
	snd_kcontrol_t *kctl;

	snd_runtime_check(card != NULL && id != NULL, return NULL);
	if (id->numid != 0)
		return snd_ctl_find_numid(card, id->numid);
	read_lock(&card->control_rwlock);
	list_for_each(list, &card->controls) {
		kctl = snd_kcontrol(list);
		if (kctl->id.iface != id->iface)
			continue;
		if (kctl->id.device != id->device)
			continue;
		if (kctl->id.subdevice != id->subdevice)
			continue;
		if (strncmp(kctl->id.name, id->name, sizeof(kctl->id.name)))
			continue;
		if (kctl->id.index != id->index)
			continue;
		read_unlock(&card->control_rwlock);
		return kctl;
	}
	read_unlock(&card->control_rwlock);
	return NULL;
}

static int snd_ctl_card_info(snd_card_t * card, snd_ctl_file_t * ctl,
			     unsigned int cmd, unsigned long arg)
{
	snd_ctl_card_info_t info;

	memset(&info, 0, sizeof(info));
	read_lock(&snd_ioctl_rwlock);
	info.card = card->number;
	strncpy(info.id, card->id, sizeof(info.id) - 1);
	strncpy(info.driver, card->driver, sizeof(info.driver) - 1);
	strncpy(info.name, card->shortname, sizeof(info.name) - 1);
	strncpy(info.longname, card->longname, sizeof(info.longname) - 1);
	strncpy(info.mixername, card->mixername, sizeof(info.mixername) - 1);
	strncpy(info.components, card->components, sizeof(info.components) - 1);
	read_unlock(&snd_ioctl_rwlock);
	if (copy_to_user((void *) arg, &info, sizeof(snd_ctl_card_info_t)))
		return -EFAULT;
	return 0;
}

static int snd_ctl_elem_list(snd_card_t *card, snd_ctl_elem_list_t *_list)
{
	struct list_head *plist;
	snd_ctl_elem_list_t list;
	snd_kcontrol_t *kctl;
	snd_ctl_elem_id_t *dst, *id;
	int offset, space;
	
	if (copy_from_user(&list, _list, sizeof(list)))
		return -EFAULT;
	offset = list.offset;
	space = list.space;
	/* try limit maximum space */
	if (space > 16384)
		return -ENOMEM;
	if (space > 0) {
		/* allocate temporary buffer for atomic operation */
		dst = vmalloc(space * sizeof(snd_ctl_elem_id_t));
		if (dst == NULL)
			return -ENOMEM;
		read_lock(&card->control_rwlock);
		list.count = card->controls_count;
		plist = card->controls.next;
		while (offset-- > 0 && plist != &card->controls)
			plist = plist->next;
		list.used = 0;
		id = dst;
		while (space > 0 && plist != &card->controls) {
			kctl = snd_kcontrol(plist);
			memcpy(id, &kctl->id, sizeof(snd_ctl_elem_id_t));
			id++;
			plist = plist->next;
			space--;
			list.used++;
		}
		read_unlock(&card->control_rwlock);
		if (list.used > 0 && copy_to_user(list.pids, dst, list.used * sizeof(snd_ctl_elem_id_t)))
			return -EFAULT;
		vfree(dst);
	} else {
		read_lock(&card->control_rwlock);
		list.count = card->controls_count;
		read_unlock(&card->control_rwlock);
	}
	if (copy_to_user(_list, &list, sizeof(list)))
		return -EFAULT;
	return 0;
}

static int snd_ctl_elem_info(snd_ctl_file_t *ctl, snd_ctl_elem_info_t *_info)
{
	snd_card_t *card = ctl->card;
	snd_ctl_elem_info_t info;
	snd_kcontrol_t *kctl;
	int result;
	
	if (copy_from_user(&info, _info, sizeof(info)))
		return -EFAULT;
	read_lock(&card->control_rwlock);
	kctl = snd_ctl_find_id(card, &info.id);
	if (kctl == NULL) {
		read_unlock(&card->control_rwlock);
		return -ENOENT;
	}
#ifdef CONFIG_SND_DEBUG
	info.access = 0;
#endif
	result = kctl->info(kctl, &info);
	if (result >= 0) {
		snd_assert(info.access == 0, );
		info.id = kctl->id;
		info.access = kctl->access;
		if (kctl->owner) {
			info.access |= SNDRV_CTL_ELEM_ACCESS_LOCK;
			if (kctl->owner == ctl)
				info.access |= SNDRV_CTL_ELEM_ACCESS_OWNER;
			info.owner = kctl->owner_pid;
		} else {
			info.owner = -1;
		}
	}
	read_unlock(&card->control_rwlock);
	if (result >= 0)
		if (copy_to_user(_info, &info, sizeof(info)))
			return -EFAULT;
	return result;
}

static int snd_ctl_elem_read(snd_card_t *card, snd_ctl_elem_value_t *_control)
{
	snd_ctl_elem_value_t control;
	snd_kcontrol_t *kctl;
	int result, indirect;
	
	if (copy_from_user(&control, _control, sizeof(control)))
		return -EFAULT;
	read_lock(&card->control_rwlock);
	kctl = snd_ctl_find_id(card, &control.id);
	if (kctl == NULL) {
		result = -ENOENT;
	} else {
		indirect = kctl->access & SNDRV_CTL_ELEM_ACCESS_INDIRECT ? 1 : 0;
		if (control.indirect != indirect) {
			result = -EACCES;
		} else {
			if ((kctl->access & SNDRV_CTL_ELEM_ACCESS_READ) && kctl->get != NULL) {
				result = kctl->get(kctl, &control);
				if (result >= 0)
					control.id = kctl->id;
			} else
				result = -EPERM;
		}
	}
	read_unlock(&card->control_rwlock);
	if (result >= 0)
		if (copy_to_user(_control, &control, sizeof(control)))
			return -EFAULT;
	return result;
}

static int snd_ctl_elem_write(snd_ctl_file_t *file, snd_ctl_elem_value_t *_control)
{
	snd_card_t *card = file->card;
	snd_ctl_elem_value_t control;
	snd_kcontrol_t *kctl;
	int result, indirect;
	
	if (copy_from_user(&control, _control, sizeof(control)))
		return -EFAULT;
	read_lock(&card->control_rwlock);
	kctl = snd_ctl_find_id(card, &control.id);
	if (kctl == NULL) {
		result = -ENOENT;
	} else {
		indirect = kctl->access & SNDRV_CTL_ELEM_ACCESS_INDIRECT ? 1 : 0;
		if (control.indirect != indirect) {
			result = -EACCES;
		} else {
			read_lock(&card->control_owner_lock);
			if (!(kctl->access & SNDRV_CTL_ELEM_ACCESS_WRITE) ||
			    kctl->put == NULL ||
			    (kctl->owner != NULL && kctl->owner != file)) {
				result = -EPERM;
			} else {
				result = kctl->put(kctl, &control);
				if (result >= 0)
					control.id = kctl->id;
			}
			read_unlock(&card->control_owner_lock);
			if (result > 0) {
				result = 0;
				snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE, &kctl->id);
			}
		}
	}
	read_unlock(&card->control_rwlock);
	if (result >= 0)
		if (copy_to_user(_control, &control, sizeof(control)))
			return -EFAULT;
	return result;
}

static int snd_ctl_elem_lock(snd_ctl_file_t *file, snd_ctl_elem_id_t *_id)
{
	snd_card_t *card = file->card;
	snd_ctl_elem_id_t id;
	snd_kcontrol_t *kctl;
	int result;
	
	if (copy_from_user(&id, _id, sizeof(id)))
		return -EFAULT;
	read_lock(&card->control_rwlock);
	kctl = snd_ctl_find_id(card, &id);
	if (kctl == NULL) {
		result = -ENOENT;
	} else {
		write_lock(&card->control_owner_lock);
		if (kctl->owner != NULL)
			result = -EBUSY;
		else {
			kctl->owner = file;
			kctl->owner_pid = current->pid;
			result = 0;
		}
		write_unlock(&card->control_owner_lock);
	}
	read_unlock(&card->control_rwlock);
	return result;
}

static int snd_ctl_elem_unlock(snd_ctl_file_t *file, snd_ctl_elem_id_t *_id)
{
	snd_card_t *card = file->card;
	snd_ctl_elem_id_t id;
	snd_kcontrol_t *kctl;
	int result;
	
	if (copy_from_user(&id, _id, sizeof(id)))
		return -EFAULT;
	read_lock(&card->control_rwlock);
	kctl = snd_ctl_find_id(card, &id);
	if (kctl == NULL) {
		result = -ENOENT;
	} else {
		write_lock(&card->control_owner_lock);
		if (kctl->owner == NULL)
			result = -EINVAL;
		else if (kctl->owner != file)
			result = -EPERM;
		else {
			kctl->owner = NULL;
			kctl->owner_pid = 0;
			result = 0;
		}
		write_unlock(&card->control_owner_lock);
	}
	read_unlock(&card->control_rwlock);
	return result;
}

static int snd_ctl_subscribe_events(snd_ctl_file_t *file, int *ptr)
{
	int subscribe;
	if (get_user(subscribe, ptr))
		return -EFAULT;
	if (subscribe < 0) {
		subscribe = file->subscribed;
		if (put_user(subscribe, ptr))
			return -EFAULT;
		return 0;
	}
	if (subscribe) {
		file->subscribed = 1;
		return 0;
	} else if (file->subscribed) {
		snd_ctl_empty_read_queue(file);
		file->subscribed = 0;
	}
	return 0;
}

static int snd_ctl_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	snd_ctl_file_t *ctl;
	snd_card_t *card;
	struct list_head *list;
	snd_kctl_ioctl_t *p;
	int err;

	ctl = snd_magic_cast(snd_ctl_file_t, file->private_data, return -ENXIO);
	card = ctl->card;
	snd_assert(card != NULL, return -ENXIO);
	switch (cmd) {
	case SNDRV_CTL_IOCTL_PVERSION:
		return put_user(SNDRV_CTL_VERSION, (int *)arg) ? -EFAULT : 0;
	case SNDRV_CTL_IOCTL_CARD_INFO:
		return snd_ctl_card_info(card, ctl, cmd, arg);
	case SNDRV_CTL_IOCTL_ELEM_LIST:
		return snd_ctl_elem_list(ctl->card, (snd_ctl_elem_list_t *) arg);
	case SNDRV_CTL_IOCTL_ELEM_INFO:
		return snd_ctl_elem_info(ctl, (snd_ctl_elem_info_t *) arg);
	case SNDRV_CTL_IOCTL_ELEM_READ:
		return snd_ctl_elem_read(ctl->card, (snd_ctl_elem_value_t *) arg);
	case SNDRV_CTL_IOCTL_ELEM_WRITE:
		return snd_ctl_elem_write(ctl, (snd_ctl_elem_value_t *) arg);
	case SNDRV_CTL_IOCTL_ELEM_LOCK:
		return snd_ctl_elem_lock(ctl, (snd_ctl_elem_id_t *) arg);
	case SNDRV_CTL_IOCTL_ELEM_UNLOCK:
		return snd_ctl_elem_unlock(ctl, (snd_ctl_elem_id_t *) arg);
	case SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS:
		return snd_ctl_subscribe_events(ctl, (int *) arg);
	case SNDRV_CTL_IOCTL_POWER:
		if (get_user(err, (int *)arg))
			return -EFAULT;
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
#ifdef CONFIG_PM
		if (card->set_power_state == NULL)
			return -ENOPROTOOPT;
		return card->set_power_state(card, err);
#else
		return -ENOPROTOOPT;
#endif
	case SNDRV_CTL_IOCTL_POWER_STATE:
#ifdef CONFIG_PM
		return put_user(card->power_state, (int *)arg) ? -EFAULT : 0;
#else
		return put_user(SNDRV_CTL_POWER_D0, (int *)arg) ? -EFAULT : 0;
#endif
	}
	read_lock(&snd_ioctl_rwlock);
	list_for_each(list, &snd_control_ioctls) {
		p = list_entry(list, snd_kctl_ioctl_t, list);
		err = p->fioctl(card, ctl, cmd, arg);
		if (err != -ENOIOCTLCMD) {
			read_unlock(&snd_ioctl_rwlock);
			return err;
		}
	}
	read_unlock(&snd_ioctl_rwlock);
	snd_printd("unknown ioctl = 0x%x\n", cmd);
	return -ENOTTY;
}

static ssize_t snd_ctl_read(struct file *file, char *buffer, size_t count, loff_t * offset)
{
	snd_ctl_file_t *ctl;
	int err = 0;
	ssize_t result = 0;

	ctl = snd_magic_cast(snd_ctl_file_t, file->private_data, return -ENXIO);
	snd_assert(ctl != NULL && ctl->card != NULL, return -ENXIO);
	if (!ctl->subscribed)
		return -EBADFD;
	if (count < sizeof(snd_ctl_event_t))
		return -EINVAL;
	spin_lock_irq(&ctl->read_lock);
	while (count >= sizeof(snd_ctl_event_t)) {
		snd_ctl_event_t ev;
		snd_kctl_event_t *kev;
		while (list_empty(&ctl->events)) {
			wait_queue_t wait;
			if (file->f_flags & O_NONBLOCK) {
				err = -EAGAIN;
				goto __end;
			}
			init_waitqueue_entry(&wait, current);
			add_wait_queue(&ctl->change_sleep, &wait);
			spin_unlock_irq(&ctl->read_lock);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&ctl->change_sleep, &wait);
			if (signal_pending(current))
				return result > 0 ? result : -ERESTARTSYS;
			spin_lock_irq(&ctl->read_lock);
		}
		kev = snd_kctl_event(ctl->events.next);
		ev.type = SNDRV_CTL_EVENT_ELEM;
		ev.data.elem.mask = kev->mask;
		ev.data.elem.id = kev->id;
		list_del(&kev->list);
		spin_unlock_irq(&ctl->read_lock);
		kfree(kev);
		if (copy_to_user(buffer, &ev, sizeof(snd_ctl_event_t))) {
			err = -EFAULT;
			goto __end;
		}
		spin_lock_irq(&ctl->read_lock);
		buffer += sizeof(snd_ctl_event_t);
		count -= sizeof(snd_ctl_event_t);
		result += sizeof(snd_ctl_event_t);
	}
      __end:
	spin_unlock_irq(&ctl->read_lock);
      	return result > 0 ? result : err;
}

static unsigned int snd_ctl_poll(struct file *file, poll_table * wait)
{
	unsigned int mask;
	snd_ctl_file_t *ctl;

	ctl = snd_magic_cast(snd_ctl_file_t, file->private_data, return 0);
	if (!ctl->subscribed)
		return 0;
	poll_wait(file, &ctl->change_sleep, wait);

	mask = 0;
	if (!list_empty(&ctl->events))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

int snd_ctl_register_ioctl(snd_kctl_ioctl_func_t fcn)
{
	snd_kctl_ioctl_t *pn;

	pn = (snd_kctl_ioctl_t *)
		snd_kcalloc(sizeof(snd_kctl_ioctl_t), GFP_KERNEL);
	if (pn == NULL)
		return -ENOMEM;
	pn->fioctl = fcn;
	write_lock(&snd_ioctl_rwlock);
	list_add_tail(&pn->list, &snd_control_ioctls);
	write_unlock(&snd_ioctl_rwlock);
	return 0;
}

int snd_ctl_unregister_ioctl(snd_kctl_ioctl_func_t fcn)
{
	struct list_head *list;
	snd_kctl_ioctl_t *p;

	snd_runtime_check(fcn != NULL, return -EINVAL);
	write_lock(&snd_ioctl_rwlock);
	list_for_each(list, &snd_control_ioctls) {
		p = list_entry(list, snd_kctl_ioctl_t, list);
		if (p->fioctl == fcn) {
			list_del(&p->list);
			write_unlock(&snd_ioctl_rwlock);
			kfree(p);
			return 0;
		}
	}
	write_unlock(&snd_ioctl_rwlock);
	snd_BUG();
	return -EINVAL;
}

static int snd_ctl_fasync(int fd, struct file * file, int on)
{
	snd_ctl_file_t *ctl;
	int err;
	ctl = snd_magic_cast(snd_ctl_file_t, file->private_data, return -ENXIO);
	err = fasync_helper(fd, file, on, &ctl->fasync);
	if (err < 0)
		return err;
	return 0;
}

/*
 *  INIT PART
 */

static struct file_operations snd_ctl_f_ops =
{
#ifndef LINUX_2_2
	owner:		THIS_MODULE,
#endif
	read:		snd_ctl_read,
	open:		snd_ctl_open,
	release:	snd_ctl_release,
	poll:		snd_ctl_poll,
	ioctl:		snd_ctl_ioctl,
	fasync:		snd_ctl_fasync,
};

static snd_minor_t snd_ctl_reg =
{
	comment:	"ctl",
	f_ops:		&snd_ctl_f_ops,
};

int snd_ctl_register(snd_card_t *card)
{
	int err, cardnum;
	char name[16];

	snd_assert(card != NULL, return -ENXIO);
	cardnum = card->number;
	snd_assert(cardnum >= 0 && cardnum < SNDRV_CARDS, return -ENXIO);
	sprintf(name, "controlC%i", cardnum);
	if ((err = snd_register_device(SNDRV_DEVICE_TYPE_CONTROL,
					card, 0, &snd_ctl_reg, name)) < 0)
		return err;
	return 0;
}

int snd_ctl_unregister(snd_card_t *card)
{
	int err, cardnum;
	snd_kcontrol_t *control;

	snd_assert(card != NULL, return -ENXIO);
	cardnum = card->number;
	snd_assert(cardnum >= 0 && cardnum < SNDRV_CARDS, return -ENXIO);
	if ((err = snd_unregister_device(SNDRV_DEVICE_TYPE_CONTROL, card, 0)) < 0)
		return err;
	while (!list_empty(&card->controls)) {
		control = snd_kcontrol(card->controls.next);
		snd_ctl_remove(card, control);
	}
	return 0;
}
