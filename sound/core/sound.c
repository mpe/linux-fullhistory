/*
 *  Advanced Linux Sound Architecture
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

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/info.h>
#include <sound/version.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <linux/kmod.h>
#ifdef CONFIG_DEVFS_FS
#include <linux/devfs_fs_kernel.h>
#endif

#define SNDRV_OS_MINORS 256

int snd_major = CONFIG_SND_MAJOR;
static int snd_cards_limit = SNDRV_CARDS;
int snd_device_mode = S_IFCHR | S_IRUGO | S_IWUGO;
int snd_device_gid = 0;
int snd_device_uid = 0;

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Advanced Linux Sound Architecture driver for soundcards.");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_SUPPORTED_DEVICE("sound");
MODULE_PARM(snd_major, "i");
MODULE_PARM_DESC(snd_major, "Major # for sound driver.");
MODULE_PARM_SYNTAX(snd_major, "default:116,skill:devel");
MODULE_PARM(snd_cards_limit, "i");
MODULE_PARM_DESC(snd_cards_limit, "Count of soundcards installed in the system.");
MODULE_PARM_SYNTAX(snd_cards_limit, "default:8,skill:advanced");
MODULE_PARM(snd_device_mode, "i");
MODULE_PARM_DESC(snd_device_mode, "Device file permission mask for sound dynamic device filesystem.");
MODULE_PARM_SYNTAX(snd_device_mode, "default:0666,base:8");
MODULE_PARM(snd_device_gid, "i");
MODULE_PARM_DESC(snd_device_gid, "Device file GID for sound dynamic device filesystem.");
MODULE_PARM_SYNTAX(snd_device_gid, "default:0");
MODULE_PARM(snd_device_uid, "i");
MODULE_PARM_DESC(snd_device_uid, "Device file UID for sound dynamic device filesystem.");
MODULE_PARM_SYNTAX(snd_device_uid, "default:0");

int snd_ecards_limit;

static struct list_head snd_minors_hash[SNDRV_CARDS];

static DECLARE_MUTEX(sound_mutex);

#ifdef CONFIG_DEVFS_FS
static devfs_handle_t devfs_handle = NULL;
#endif

#ifdef CONFIG_KMOD

void snd_request_card(int card)
{
	char str[32];

	if (snd_cards[card] != NULL)
		return;
	if (card < 0 || card >= snd_ecards_limit)
		return;
	sprintf(str, "snd-card-%i", card);
	request_module(str);
}

static void snd_request_other(int minor)
{
	char *str;

	switch (minor) {
	case SNDRV_MINOR_SEQUENCER:	str = "snd-seq";	break;
	case SNDRV_MINOR_TIMER:		str = "snd-timer";	break;
	default:			return;
	}
	request_module(str);
}

#endif				/* request_module support */

static snd_minor_t *snd_minor_search(int minor)
{
	struct list_head *list;
	snd_minor_t *mptr;

	list_for_each(list, &snd_minors_hash[SNDRV_MINOR_CARD(minor)]) {
		mptr = list_entry(list, snd_minor_t, list);
		if (mptr->number == minor)
			return mptr;
	}
	return NULL;
}

static int snd_open(struct inode *inode, struct file *file)
{
	int minor = minor(inode->i_rdev);
	int card = SNDRV_MINOR_CARD(minor);
	int dev = SNDRV_MINOR_DEVICE(minor);
	snd_minor_t *mptr = NULL;
	struct file_operations *old_fops;
	int err = 0;

	if (dev != SNDRV_MINOR_SEQUENCER && dev != SNDRV_MINOR_TIMER) {
		if (snd_cards[card] == NULL) {
#ifdef CONFIG_KMOD
			snd_request_card(card);
			if (snd_cards[card] == NULL)
#endif
				return -ENODEV;
		}
	} else {
#ifdef CONFIG_KMOD
		if ((mptr = snd_minor_search(minor)) == NULL)
			snd_request_other(minor);
#endif
	}
	if (mptr == NULL && (mptr = snd_minor_search(minor)) == NULL)
		return -ENODEV;
	old_fops = file->f_op;
	file->f_op = fops_get(mptr->f_ops);
	if (file->f_op->open)
		err = file->f_op->open(inode, file);
	if (err) {
		fops_put(file->f_op);
		file->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);
	return err;
}

struct file_operations snd_fops =
{
#ifndef LINUX_2_2
	.owner =	THIS_MODULE,
#endif
	.open =		snd_open
};

static int snd_kernel_minor(int type, snd_card_t * card, int dev)
{
	int minor;

	switch (type) {
	case SNDRV_DEVICE_TYPE_SEQUENCER:
	case SNDRV_DEVICE_TYPE_TIMER:
		minor = type;
		break;
	case SNDRV_DEVICE_TYPE_CONTROL:
		snd_assert(card != NULL, return -EINVAL);
		minor = SNDRV_MINOR(card->number, type);
		break;
	case SNDRV_DEVICE_TYPE_HWDEP:
	case SNDRV_DEVICE_TYPE_RAWMIDI:
	case SNDRV_DEVICE_TYPE_PCM_PLAYBACK:
	case SNDRV_DEVICE_TYPE_PCM_CAPTURE:
		snd_assert(card != NULL, return -EINVAL);
		minor = SNDRV_MINOR(card->number, type + dev);
		break;
	default:
		return -EINVAL;
	}
	snd_assert(minor >= 0 && minor < SNDRV_OS_MINORS, return -EINVAL);
	return minor;
}

int snd_register_device(int type, snd_card_t * card, int dev, snd_minor_t * reg, const char *name)
{
	int minor = snd_kernel_minor(type, card, dev);
	snd_minor_t *preg;

	if (minor < 0)
		return minor;
	preg = (snd_minor_t *)kmalloc(sizeof(snd_minor_t), GFP_KERNEL);
	if (preg == NULL)
		return -ENOMEM;
	*preg = *reg;
	preg->number = minor;
	preg->device = dev;
	preg->dev = NULL;
	down(&sound_mutex);
	if (snd_minor_search(minor)) {
		up(&sound_mutex);
		kfree(preg);
		return -EBUSY;
	}
	if (name)
		preg->dev = snd_info_create_device(name, minor, 0);
	list_add_tail(&preg->list, &snd_minors_hash[SNDRV_MINOR_CARD(minor)]);
	up(&sound_mutex);
	return 0;
}

int snd_unregister_device(int type, snd_card_t * card, int dev)
{
	int minor = snd_kernel_minor(type, card, dev);
	snd_minor_t *mptr;

	if (minor < 0)
		return minor;
	down(&sound_mutex);
	if ((mptr = snd_minor_search(minor)) == NULL) {
		up(&sound_mutex);
		return -EINVAL;
	}
	if (mptr->dev)
		snd_info_free_device(mptr->dev);
	list_del(&mptr->list);
	up(&sound_mutex);
	kfree(mptr);
	return 0;
}

/*
 *  INFO PART
 */

static snd_info_entry_t *snd_minor_info_entry = NULL;

static void snd_minor_info_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	int card, device;
	struct list_head *list;
	snd_minor_t *mptr;

	down(&sound_mutex);
	for (card = 0; card < SNDRV_CARDS; card++) {
		list_for_each(list, &snd_minors_hash[card]) {
			mptr = list_entry(list, snd_minor_t, list);
			if (SNDRV_MINOR_DEVICE(mptr->number) != SNDRV_MINOR_SEQUENCER) {
				if ((device = mptr->device) >= 0)
					snd_iprintf(buffer, "%3i: [%i-%2i]: %s\n", mptr->number, card, device, mptr->comment);
				else
					snd_iprintf(buffer, "%3i: [%i]   : %s\n", mptr->number, card, mptr->comment);
			} else {
				snd_iprintf(buffer, "%3i:       : %s\n", mptr->number, mptr->comment);
			}
		}
	}
	up(&sound_mutex);
}

int __init snd_minor_info_init(void)
{
	snd_info_entry_t *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, "devices", NULL);
	if (entry) {
		entry->content = SNDRV_INFO_CONTENT_TEXT;
		entry->c.text.read_size = PAGE_SIZE;
		entry->c.text.read = snd_minor_info_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	snd_minor_info_entry = entry;
	return 0;
}

int __exit snd_minor_info_done(void)
{
	if (snd_minor_info_entry)
		snd_info_unregister(snd_minor_info_entry);
	return 0;
}

/*
 *  INIT PART
 */

static int __init alsa_sound_init(void)
{
#ifdef CONFIG_DEVFS_FS
	short controlnum;
	char controlname[24];
#endif
#ifdef CONFIG_SND_OSSEMUL
	int err;
#endif
	int card;

	snd_ecards_limit = snd_cards_limit;
	for (card = 0; card < SNDRV_CARDS; card++)
		INIT_LIST_HEAD(&snd_minors_hash[card]);
#ifdef CONFIG_SND_OSSEMUL
	if ((err = snd_oss_init_module()) < 0)
		return err;
#endif
#ifdef CONFIG_DEVFS_FS
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
	devfs_handle = devfs_mk_dir(NULL, "snd", 3, NULL);
#else
	devfs_handle = devfs_mk_dir(NULL, "snd", NULL);
#endif
#endif
	if (register_chrdev(snd_major, "alsa", &snd_fops)) {
		snd_printk(KERN_ERR "unable to register native major device number %d\n", snd_major);
#ifdef CONFIG_SND_OSSEMUL
		snd_oss_cleanup_module();
#endif
		return -EIO;
	}
#ifdef CONFIG_SND_DEBUG_MEMORY
	snd_memory_init();
#endif
	if (snd_info_init() < 0) {
#ifdef CONFIG_SND_DEBUG_MEMORY
		snd_memory_done();
#endif
#ifdef CONFIG_SND_OSSEMUL
		snd_oss_cleanup_module();
#endif
		return -ENOMEM;
	}
#ifdef CONFIG_SND_OSSEMUL
	snd_info_minor_register();
#endif
#ifdef CONFIG_DEVFS_FS
	for (controlnum = 0; controlnum < snd_cards_limit; controlnum++) {
		sprintf(controlname, "snd/controlC%d", controlnum);
		devfs_register(NULL, controlname, DEVFS_FL_DEFAULT,
				snd_major, controlnum<<5, snd_device_mode | S_IFCHR,
				&snd_fops, NULL);
	}
#endif
#ifndef MODULE
	printk(KERN_INFO "Advanced Linux Sound Architecture Driver Version " CONFIG_SND_VERSION CONFIG_SND_DATE ".\n");
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0) && defined(CONFIG_APM)
	pm_init();
#endif
	return 0;
}

static void __exit alsa_sound_exit(void)
{
#ifdef CONFIG_DEVFS_FS
	char controlname[24];
	short controlnum;

	for (controlnum = 0; controlnum < snd_cards_limit; controlnum++) {
		sprintf(controlname, "snd/controlC%d", controlnum);
		devfs_find_and_unregister(NULL, controlname, 0, 0, DEVFS_SPECIAL_CHR, 0);
	}
#endif
	
#ifdef CONFIG_SND_OSSEMUL
	snd_info_minor_unregister();
	snd_oss_cleanup_module();
#endif
	snd_info_done();
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0) && defined(CONFIG_APM)
	pm_done();
#endif
#ifdef CONFIG_SND_DEBUG_MEMORY
	snd_memory_done();
#endif
	if (unregister_chrdev(snd_major, "alsa") != 0)
		snd_printk(KERN_ERR "unable to unregister major device number %d\n", snd_major);
#ifdef CONFIG_DEVFS_FS
	devfs_unregister(devfs_handle);
#endif
}

module_init(alsa_sound_init)
module_exit(alsa_sound_exit)

  /* sound.c */
EXPORT_SYMBOL(snd_ecards_limit);
#if defined(CONFIG_KMOD)
EXPORT_SYMBOL(snd_request_card);
#endif
EXPORT_SYMBOL(snd_register_device);
EXPORT_SYMBOL(snd_unregister_device);
#if defined(CONFIG_SND_OSSEMUL)
EXPORT_SYMBOL(snd_register_oss_device);
EXPORT_SYMBOL(snd_unregister_oss_device);
#endif
  /* memory.c */
#ifdef CONFIG_SND_DEBUG_MEMORY
EXPORT_SYMBOL(snd_hidden_kmalloc);
EXPORT_SYMBOL(snd_hidden_kfree);
EXPORT_SYMBOL(snd_hidden_vmalloc);
EXPORT_SYMBOL(snd_hidden_vfree);
EXPORT_SYMBOL(_snd_magic_kmalloc);
EXPORT_SYMBOL(_snd_magic_kcalloc);
EXPORT_SYMBOL(snd_magic_kfree);
#endif
EXPORT_SYMBOL(snd_kcalloc);
EXPORT_SYMBOL(snd_kmalloc_strdup);
EXPORT_SYMBOL(snd_malloc_pages);
EXPORT_SYMBOL(snd_malloc_pages_fallback);
EXPORT_SYMBOL(snd_free_pages);
#if defined(CONFIG_ISA) && ! defined(CONFIG_PCI)
EXPORT_SYMBOL(snd_malloc_isa_pages);
EXPORT_SYMBOL(snd_malloc_isa_pages_fallback);
#endif
#ifdef CONFIG_PCI
EXPORT_SYMBOL(snd_malloc_pci_pages);
EXPORT_SYMBOL(snd_malloc_pci_pages_fallback);
EXPORT_SYMBOL(snd_free_pci_pages);
#endif
#ifdef CONFIG_SBUS
EXPORT_SYMBOL(snd_malloc_sbus_pages);
EXPORT_SYMBOL(snd_malloc_sbus_pages_fallback);
EXPORT_SYMBOL(snd_free_sbus_pages);
#endif
EXPORT_SYMBOL(copy_to_user_fromio);
EXPORT_SYMBOL(copy_from_user_toio);
  /* init.c */
EXPORT_SYMBOL(snd_cards_count);
EXPORT_SYMBOL(snd_cards);
#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
EXPORT_SYMBOL(snd_mixer_oss_notify_callback);
#endif
EXPORT_SYMBOL(snd_card_new);
EXPORT_SYMBOL(snd_card_free);
EXPORT_SYMBOL(snd_card_register);
EXPORT_SYMBOL(snd_component_add);
#ifdef CONFIG_PM
EXPORT_SYMBOL(snd_power_wait);
#endif
  /* device.c */
EXPORT_SYMBOL(snd_device_new);
EXPORT_SYMBOL(snd_device_register);
EXPORT_SYMBOL(snd_device_free);
EXPORT_SYMBOL(snd_device_free_all);
  /* isadma.c */
#ifdef CONFIG_ISA
EXPORT_SYMBOL(snd_dma_program);
EXPORT_SYMBOL(snd_dma_disable);
EXPORT_SYMBOL(snd_dma_residue);
#endif
  /* info.c */
EXPORT_SYMBOL(snd_seq_root);
EXPORT_SYMBOL(snd_create_proc_entry);
EXPORT_SYMBOL(snd_remove_proc_entry);
EXPORT_SYMBOL(snd_iprintf);
EXPORT_SYMBOL(snd_info_get_line);
EXPORT_SYMBOL(snd_info_get_str);
EXPORT_SYMBOL(snd_info_create_module_entry);
EXPORT_SYMBOL(snd_info_create_card_entry);
EXPORT_SYMBOL(snd_info_free_entry);
EXPORT_SYMBOL(snd_info_create_device);
EXPORT_SYMBOL(snd_info_free_device);
EXPORT_SYMBOL(snd_info_register);
EXPORT_SYMBOL(snd_info_unregister);
  /* info_oss.c */
#ifdef CONFIG_SND_OSSEMUL
EXPORT_SYMBOL(snd_oss_info_register);
#endif
  /* control.c */
EXPORT_SYMBOL(snd_ctl_new);
EXPORT_SYMBOL(snd_ctl_new1);
EXPORT_SYMBOL(snd_ctl_free_one);
EXPORT_SYMBOL(snd_ctl_add);
EXPORT_SYMBOL(snd_ctl_remove);
EXPORT_SYMBOL(snd_ctl_remove_id);
EXPORT_SYMBOL(snd_ctl_rename_id);
EXPORT_SYMBOL(snd_ctl_find_numid);
EXPORT_SYMBOL(snd_ctl_find_id);
EXPORT_SYMBOL(snd_ctl_notify);
EXPORT_SYMBOL(snd_ctl_register_ioctl);
EXPORT_SYMBOL(snd_ctl_unregister_ioctl);
  /* misc.c */
EXPORT_SYMBOL(snd_task_name);
#ifdef CONFIG_SND_VERBOSE_PRINTK
EXPORT_SYMBOL(snd_verbose_printk);
#endif
#if defined(CONFIG_SND_DEBUG) && defined(CONFIG_SND_VERBOSE_PRINTK)
EXPORT_SYMBOL(snd_verbose_printd);
#endif
#if defined(CONFIG_SND_DEBUG) && !defined(CONFIG_SND_VERBOSE_PRINTK)
EXPORT_SYMBOL(snd_printd);
#endif
  /* wrappers */
#ifdef CONFIG_SND_DEBUG_MEMORY
EXPORT_SYMBOL(snd_wrapper_kmalloc);
EXPORT_SYMBOL(snd_wrapper_kfree);
EXPORT_SYMBOL(snd_wrapper_vmalloc);
EXPORT_SYMBOL(snd_wrapper_vfree);
#endif
#ifdef HACK_PCI_ALLOC_CONSISTENT
EXPORT_SYMBOL(snd_pci_hack_alloc_consistent);
#endif 
