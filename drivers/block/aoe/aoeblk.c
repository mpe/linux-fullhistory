/* Copyright (c) 2004 Coraid, Inc.  See COPYING for GPL terms. */
/*
 * aoeblk.c
 * block device routines
 */

#include <linux/hdreg.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/genhd.h>
#include <linux/netdevice.h>
#include "aoe.h"

static kmem_cache_t *buf_pool_cache;

/* add attributes for our block devices in sysfs */
static ssize_t aoedisk_show_state(struct gendisk * disk, char *page)
{
	struct aoedev *d = disk->private_data;

	return snprintf(page, PAGE_SIZE,
			"%s%s\n",
			(d->flags & DEVFL_UP) ? "up" : "down",
			(d->flags & DEVFL_CLOSEWAIT) ? ",closewait" : "");
}
static ssize_t aoedisk_show_mac(struct gendisk * disk, char *page)
{
	struct aoedev *d = disk->private_data;

	return snprintf(page, PAGE_SIZE, "%012llx\n", mac_addr(d->addr));
}
static ssize_t aoedisk_show_netif(struct gendisk * disk, char *page)
{
	struct aoedev *d = disk->private_data;

	return snprintf(page, PAGE_SIZE, "%s\n", d->ifp->name);
}

static struct disk_attribute disk_attr_state = {
	.attr = {.name = "state", .mode = S_IRUGO },
	.show = aoedisk_show_state
};
static struct disk_attribute disk_attr_mac = {
	.attr = {.name = "mac", .mode = S_IRUGO },
	.show = aoedisk_show_mac
};
static struct disk_attribute disk_attr_netif = {
	.attr = {.name = "netif", .mode = S_IRUGO },
	.show = aoedisk_show_netif
};

static void
aoedisk_add_sysfs(struct aoedev *d)
{
	sysfs_create_file(&d->gd->kobj, &disk_attr_state.attr);
	sysfs_create_file(&d->gd->kobj, &disk_attr_mac.attr);
	sysfs_create_file(&d->gd->kobj, &disk_attr_netif.attr);
}
void
aoedisk_rm_sysfs(struct aoedev *d)
{
	sysfs_remove_link(&d->gd->kobj, "state");
	sysfs_remove_link(&d->gd->kobj, "mac");
	sysfs_remove_link(&d->gd->kobj, "netif");
}

static int
aoeblk_open(struct inode *inode, struct file *filp)
{
	struct aoedev *d;
	ulong flags;

	d = inode->i_bdev->bd_disk->private_data;

	spin_lock_irqsave(&d->lock, flags);
	if (d->flags & DEVFL_UP) {
		d->nopen++;
		spin_unlock_irqrestore(&d->lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&d->lock, flags);
	return -ENODEV;
}

static int
aoeblk_release(struct inode *inode, struct file *filp)
{
	struct aoedev *d;
	ulong flags;

	d = inode->i_bdev->bd_disk->private_data;

	spin_lock_irqsave(&d->lock, flags);

	if (--d->nopen == 0 && (d->flags & DEVFL_CLOSEWAIT)) {
		d->flags &= ~DEVFL_CLOSEWAIT;
		spin_unlock_irqrestore(&d->lock, flags);
		aoecmd_cfg(d->aoemajor, d->aoeminor);
		return 0;
	}
	spin_unlock_irqrestore(&d->lock, flags);

	return 0;
}

static int
aoeblk_make_request(request_queue_t *q, struct bio *bio)
{
	struct aoedev *d;
	struct buf *buf;
	struct sk_buff *sl;
	ulong flags;

	blk_queue_bounce(q, &bio);

	d = bio->bi_bdev->bd_disk->private_data;
	buf = mempool_alloc(d->bufpool, GFP_NOIO);
	if (buf == NULL) {
		printk(KERN_INFO "aoe: aoeblk_make_request: buf allocation "
			"failure\n");
		bio_endio(bio, bio->bi_size, -ENOMEM);
		return 0;
	}
	memset(buf, 0, sizeof(*buf));
	INIT_LIST_HEAD(&buf->bufs);
	buf->bio = bio;
	buf->resid = bio->bi_size;
	buf->sector = bio->bi_sector;
	buf->bv = buf->bio->bi_io_vec;
	buf->bv_resid = buf->bv->bv_len;
	buf->bufaddr = page_address(buf->bv->bv_page) + buf->bv->bv_offset;

	spin_lock_irqsave(&d->lock, flags);

	if ((d->flags & DEVFL_UP) == 0) {
		printk(KERN_INFO "aoe: aoeblk_make_request: device %ld.%ld is not up\n",
			d->aoemajor, d->aoeminor);
		spin_unlock_irqrestore(&d->lock, flags);
		mempool_free(buf, d->bufpool);
		bio_endio(bio, bio->bi_size, -ENXIO);
		return 0;
	}

	list_add_tail(&buf->bufs, &d->bufq);
	aoecmd_work(d);

	sl = d->skblist;
	d->skblist = NULL;

	spin_unlock_irqrestore(&d->lock, flags);

	aoenet_xmit(sl);
	return 0;
}

/* This ioctl implementation expects userland to have the device node
 * permissions set so that only priviledged users can open an aoe
 * block device directly.
 */
static int
aoeblk_ioctl(struct inode *inode, struct file *filp, uint cmd, ulong arg)
{
	struct aoedev *d;

	if (!arg)
		return -EINVAL;

	d = inode->i_bdev->bd_disk->private_data;
	if ((d->flags & DEVFL_UP) == 0) {
		printk(KERN_ERR "aoe: aoeblk_ioctl: disk not up\n");
		return -ENODEV;
	}

	if (cmd == HDIO_GETGEO) {
		d->geo.start = get_start_sect(inode->i_bdev);
		if (!copy_to_user((void __user *) arg, &d->geo, sizeof d->geo))
			return 0;
		return -EFAULT;
	}
	printk(KERN_INFO "aoe: aoeblk_ioctl: unknown ioctl %d\n", cmd);
	return -EINVAL;
}

static struct block_device_operations aoe_bdops = {
	.open = aoeblk_open,
	.release = aoeblk_release,
	.ioctl = aoeblk_ioctl,
	.owner = THIS_MODULE,
};

/* alloc_disk and add_disk can sleep */
void
aoeblk_gdalloc(void *vp)
{
	struct aoedev *d = vp;
	struct gendisk *gd;
	ulong flags;

	gd = alloc_disk(AOE_PARTITIONS);
	if (gd == NULL) {
		printk(KERN_ERR "aoe: aoeblk_gdalloc: cannot allocate disk "
			"structure for %ld.%ld\n", d->aoemajor, d->aoeminor);
		spin_lock_irqsave(&d->lock, flags);
		d->flags &= ~DEVFL_WORKON;
		spin_unlock_irqrestore(&d->lock, flags);
		return;
	}

	d->bufpool = mempool_create(MIN_BUFS,
				    mempool_alloc_slab, mempool_free_slab,
				    buf_pool_cache);
	if (d->bufpool == NULL) {
		printk(KERN_ERR "aoe: aoeblk_gdalloc: cannot allocate bufpool "
			"for %ld.%ld\n", d->aoemajor, d->aoeminor);
		put_disk(gd);
		spin_lock_irqsave(&d->lock, flags);
		d->flags &= ~DEVFL_WORKON;
		spin_unlock_irqrestore(&d->lock, flags);
		return;
	}

	spin_lock_irqsave(&d->lock, flags);
	blk_queue_make_request(&d->blkq, aoeblk_make_request);
	gd->major = AOE_MAJOR;
	gd->first_minor = d->sysminor * AOE_PARTITIONS;
	gd->fops = &aoe_bdops;
	gd->private_data = d;
	gd->capacity = d->ssize;
	snprintf(gd->disk_name, sizeof gd->disk_name, "etherd/e%ld.%ld",
		d->aoemajor, d->aoeminor);

	gd->queue = &d->blkq;
	d->gd = gd;
	d->flags &= ~DEVFL_WORKON;
	d->flags |= DEVFL_UP;

	spin_unlock_irqrestore(&d->lock, flags);

	add_disk(gd);
	aoedisk_add_sysfs(d);
	
	printk(KERN_INFO "aoe: %012llx e%lu.%lu v%04x has %llu "
		"sectors\n", mac_addr(d->addr), d->aoemajor, d->aoeminor,
		d->fw_ver, (long long)d->ssize);
}

void
aoeblk_exit(void)
{
	kmem_cache_destroy(buf_pool_cache);
}

int __init
aoeblk_init(void)
{
	buf_pool_cache = kmem_cache_create("aoe_bufs", 
					   sizeof(struct buf),
					   0, 0, NULL, NULL);
	if (buf_pool_cache == NULL)
		return -ENOMEM;

	return 0;
}

