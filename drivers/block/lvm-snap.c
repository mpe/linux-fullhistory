/*
 * kernel/lvm-snap.c
 *
 * Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *
 * LVM snapshot driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * LVM driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA. 
 *
 */

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/smp_lock.h>
#include <linux/types.h>
#include <linux/iobuf.h>
#include <linux/lvm.h>


static char *lvm_snap_version = "LVM 0.8final (15/02/2000)\n";

extern const char *const lvm_name;
extern int lvm_blocksizes[];

void lvm_snapshot_release(lv_t *);

#define hashfn(dev,block,mask,chunk_size) \
	((HASHDEV(dev)^((block)/(chunk_size))) & (mask))

static inline lv_block_exception_t *
lvm_find_exception_table(kdev_t org_dev, unsigned long org_start, lv_t * lv)
{
	struct list_head * hash_table = lv->lv_snapshot_hash_table, * next;
	unsigned long mask = lv->lv_snapshot_hash_mask;
	int chunk_size = lv->lv_chunk_size;
	lv_block_exception_t * ret;
	int i = 0;

	hash_table = &hash_table[hashfn(org_dev, org_start, mask, chunk_size)];
	ret = NULL;
	for (next = hash_table->next; next != hash_table; next = next->next)
	{
		lv_block_exception_t * exception;

		exception = list_entry(next, lv_block_exception_t, hash);
		if (exception->rsector_org == org_start &&
		    exception->rdev_org == org_dev)
		{
			if (i)
			{
				/* fun, isn't it? :) */
				list_del(next);
				list_add(next, hash_table);
			}
			ret = exception;
			break;
		}
		i++;
	}
	return ret;
}

static inline void lvm_hash_link(lv_block_exception_t * exception,
				 kdev_t org_dev, unsigned long org_start,
				 lv_t * lv)
{
	struct list_head * hash_table = lv->lv_snapshot_hash_table;
	unsigned long mask = lv->lv_snapshot_hash_mask;
	int chunk_size = lv->lv_chunk_size;

	hash_table = &hash_table[hashfn(org_dev, org_start, mask, chunk_size)];
	list_add(&exception->hash, hash_table);
}

int lvm_snapshot_remap_block(kdev_t * org_dev, unsigned long * org_sector,
			     unsigned long pe_start, lv_t * lv)
{
	int ret;
	unsigned long pe_off, pe_adjustment, __org_start;
	kdev_t __org_dev;
	int chunk_size = lv->lv_chunk_size;
	lv_block_exception_t * exception;

	pe_off = pe_start % chunk_size;
	pe_adjustment = (*org_sector-pe_off) % chunk_size;
	__org_start = *org_sector - pe_adjustment;
	__org_dev = *org_dev;

	ret = 0;
	exception = lvm_find_exception_table(__org_dev, __org_start, lv);
	if (exception)
	{
		*org_dev = exception->rdev_new;
		*org_sector = exception->rsector_new + pe_adjustment;
		ret = 1;
	}
	return ret;
}

static void lvm_drop_snapshot(lv_t * lv_snap, const char * reason)
{
	kdev_t last_dev;
	int i;

	/* no exception storage space available for this snapshot
	   or error on this snapshot --> release it */
	invalidate_buffers(lv_snap->lv_dev);

	for (i = last_dev = 0; i < lv_snap->lv_remap_ptr; i++) {
		if ( lv_snap->lv_block_exception[i].rdev_new != last_dev) {
			last_dev = lv_snap->lv_block_exception[i].rdev_new;
			invalidate_buffers(last_dev);
		}
	}

	lvm_snapshot_release(lv_snap);

	printk(KERN_INFO
	       "%s -- giving up to snapshot %s on %s due %s\n",
	       lvm_name, lv_snap->lv_snapshot_org->lv_name, lv_snap->lv_name,
	       reason);
}

static inline void lvm_snapshot_prepare_blocks(unsigned long * blocks,
					       unsigned long start,
					       int nr_sectors,
					       int blocksize)
{
	int i, sectors_per_block, nr_blocks;

	sectors_per_block = blocksize >> 9;
	nr_blocks = nr_sectors / sectors_per_block;
	start /= sectors_per_block;

	for (i = 0; i < nr_blocks; i++)
		blocks[i] = start++;
}

static inline int get_blksize(kdev_t dev)
{
	int correct_size = BLOCK_SIZE, i, major;

	major = MAJOR(dev);
	if (blksize_size[major])
	{
		i = blksize_size[major][MINOR(dev)];
		if (i)
			correct_size = i;
	}
	return correct_size;
}

#ifdef DEBUG_SNAPSHOT
static inline void invalidate_snap_cache(unsigned long start, unsigned long nr,
					 kdev_t dev)
{
	struct buffer_head * bh;
	int sectors_per_block, i, blksize, minor;

	minor = MINOR(dev);
	blksize = lvm_blocksizes[minor];
	sectors_per_block = blksize >> 9;
	nr /= sectors_per_block;
	start /= sectors_per_block;

	for (i = 0; i < nr; i++)
	{
		bh = get_hash_table(dev, start++, blksize);
		if (bh)
			bforget(bh);
	}
}
#endif

/*
 * copy on write handler for one snapshot logical volume
 *
 * read the original blocks and store it/them on the new one(s).
 * if there is no exception storage space free any longer --> release snapshot.
 *
 * this routine gets called for each _first_ write to a physical chunk.
 */
int lvm_snapshot_COW(kdev_t org_phys_dev,
		     unsigned long org_phys_sector,
		     unsigned long org_pe_start,
		     unsigned long org_virt_sector,
		     lv_t * lv_snap)
{
	const char * reason;
	unsigned long org_start, snap_start, snap_phys_dev, virt_start, pe_off;
	int idx = lv_snap->lv_remap_ptr, chunk_size = lv_snap->lv_chunk_size;
	struct kiobuf * iobuf;
	unsigned long blocks[KIO_MAX_SECTORS];
	int blksize_snap, blksize_org, min_blksize, max_blksize;
	int max_sectors, nr_sectors;

	/* check if we are out of snapshot space */
	if (idx >= lv_snap->lv_remap_end)
		goto fail_out_of_space;

	/* calculate physical boundaries of source chunk */
	pe_off = org_pe_start % chunk_size;
	org_start = org_phys_sector - ((org_phys_sector-pe_off) % chunk_size);
	virt_start = org_virt_sector - (org_phys_sector - org_start);

	/* calculate physical boundaries of destination chunk */
	snap_phys_dev = lv_snap->lv_block_exception[idx].rdev_new;
	snap_start = lv_snap->lv_block_exception[idx].rsector_new;

#ifdef DEBUG_SNAPSHOT
	printk(KERN_INFO
	       "%s -- COW: "
	       "org %02d:%02d faulting %lu start %lu, "
	       "snap %02d:%02d start %lu, "
	       "size %d, pe_start %lu pe_off %lu, virt_sec %lu\n",
	       lvm_name,
	       MAJOR(org_phys_dev), MINOR(org_phys_dev), org_phys_sector,
	       org_start,
	       MAJOR(snap_phys_dev), MINOR(snap_phys_dev), snap_start,
	       chunk_size,
	       org_pe_start, pe_off,
	       org_virt_sector);
#endif

	iobuf = lv_snap->lv_iobuf;

	blksize_org = get_blksize(org_phys_dev);
	blksize_snap = get_blksize(snap_phys_dev);
	max_blksize = max(blksize_org, blksize_snap);
	min_blksize = min(blksize_org, blksize_snap);
	max_sectors = KIO_MAX_SECTORS * (min_blksize>>9);

	if (chunk_size % (max_blksize>>9))
		goto fail_blksize;

	while (chunk_size)
	{
		nr_sectors = min(chunk_size, max_sectors);
		chunk_size -= nr_sectors;

		iobuf->length = nr_sectors << 9;

		lvm_snapshot_prepare_blocks(blocks, org_start,
					    nr_sectors, blksize_org);
		if (brw_kiovec(READ, 1, &iobuf, org_phys_dev,
			       blocks, blksize_org) != (nr_sectors<<9))
			goto fail_raw_read;

		lvm_snapshot_prepare_blocks(blocks, snap_start,
					    nr_sectors, blksize_snap);
		if (brw_kiovec(WRITE, 1, &iobuf, snap_phys_dev,
			       blocks, blksize_snap) != (nr_sectors<<9))
			goto fail_raw_write;
	}

#ifdef DEBUG_SNAPSHOT
	/* invalidate the logcial snapshot buffer cache */
	invalidate_snap_cache(virt_start, lv_snap->lv_chunk_size,
			      lv_snap->lv_dev);
#endif

	/* the original chunk is now stored on the snapshot volume
	   so update the execption table */
	lv_snap->lv_block_exception[idx].rdev_org = org_phys_dev;
	lv_snap->lv_block_exception[idx].rsector_org = org_start;
	lvm_hash_link(lv_snap->lv_block_exception + idx,
		      org_phys_dev, org_start, lv_snap);
	lv_snap->lv_remap_ptr = idx + 1;
	return 0;

	/* slow path */
 out:
	lvm_drop_snapshot(lv_snap, reason);
	return 1;

 fail_out_of_space:
	reason = "out of space";
	goto out;
 fail_raw_read:
	reason = "read error";
	goto out;
 fail_raw_write:
	reason = "write error";
	goto out;
 fail_blksize:
	reason = "blocksize error";
	goto out;
}

static int lvm_snapshot_alloc_iobuf_pages(struct kiobuf * iobuf, int sectors)
{
	int bytes, nr_pages, err, i;

	bytes = sectors << 9;
	nr_pages = (bytes + ~PAGE_MASK) >> PAGE_SHIFT;
	err = expand_kiobuf(iobuf, nr_pages);
	if (err)
		goto out;

	err = -ENOMEM;
	iobuf->locked = 1;
	iobuf->nr_pages = 0;
	for (i = 0; i < nr_pages; i++)
	{
		struct page * page;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,27)
		page = alloc_page(GFP_KERNEL);
		if (!page)
			goto out;
#else
		{
			unsigned long addr = __get_free_page(GFP_USER);
			if (!addr)
				goto out;
			iobuf->pagelist[i] = addr;
			page = mem_map + MAP_NR(addr);
		}
#endif

		iobuf->maplist[i] = page;
		/* the only point to lock the page here is to be allowed
		   to share unmap_kiobuf() in the fail-path */
#ifndef LockPage
#define LockPage(map) set_bit(PG_locked, &(map)->flags)
#endif
		LockPage(page);
		iobuf->nr_pages++;
	}
	iobuf->offset = 0;

	err = 0;
 out:
	return err;
}

static int calc_max_buckets(void)
{
	unsigned long mem;

	mem = num_physpages << PAGE_SHIFT;
	mem /= 100;
	mem *= 2;
	mem /= sizeof(struct list_head);

	return mem;
}

static int lvm_snapshot_alloc_hash_table(lv_t * lv)
{
	int err;
	unsigned long buckets, max_buckets, size;
	struct list_head * hash;

	buckets = lv->lv_remap_end;
	max_buckets = calc_max_buckets();
	buckets = min(buckets, max_buckets);
	while (buckets & (buckets-1))
		buckets &= (buckets-1);

	size = buckets * sizeof(struct list_head);

	err = -ENOMEM;
	hash = vmalloc(size);
	lv->lv_snapshot_hash_table = hash;

	if (!hash)
		goto out;

	lv->lv_snapshot_hash_mask = buckets-1;
	while (buckets--)
		INIT_LIST_HEAD(hash+buckets);
	err = 0;
 out:
	return err;
}

int lvm_snapshot_alloc(lv_t * lv_snap)
{
	int err, blocksize, max_sectors;

	err = alloc_kiovec(1, &lv_snap->lv_iobuf);
	if (err)
		goto out;

	blocksize = lvm_blocksizes[MINOR(lv_snap->lv_dev)];
	max_sectors = KIO_MAX_SECTORS << (PAGE_SHIFT-9);

	err = lvm_snapshot_alloc_iobuf_pages(lv_snap->lv_iobuf, max_sectors);
	if (err)
		goto out_free_kiovec;

	err = lvm_snapshot_alloc_hash_table(lv_snap);
	if (err)
		goto out_free_kiovec;
 out:
	return err;

 out_free_kiovec:
	unmap_kiobuf(lv_snap->lv_iobuf);
	free_kiovec(1, &lv_snap->lv_iobuf);
	goto out;
}

void lvm_snapshot_release(lv_t * lv)
{
	if (lv->lv_block_exception)
	{
		vfree(lv->lv_block_exception);
		lv->lv_block_exception = NULL;
	}
	if (lv->lv_snapshot_hash_table)
	{
		vfree(lv->lv_snapshot_hash_table);
		lv->lv_snapshot_hash_table = NULL;
	}
	if (lv->lv_iobuf)
	{
		free_kiovec(1, &lv->lv_iobuf);
		lv->lv_iobuf = NULL;
	}
}
