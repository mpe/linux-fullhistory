/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

/*
 *	page_buf.c
 *
 *	The page_buf module provides an abstract buffer cache model on top of
 *	the Linux page cache.  Cached metadata blocks for a file system are
 *	hashed to the inode for the block device.  The page_buf module
 *	assembles buffer (page_buf_t) objects on demand to aggregate such
 *	cached pages for I/O.
 *
 *
 *      Written by Steve Lord, Jim Mostek, Russell Cattelan
 *		    and Rajagopal Ananthanarayanan ("ananth") at SGI.
 *
 */

#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h>
#include <linux/suspend.h>

#include <support/debug.h>
#include <support/kmem.h>

#include "page_buf_internal.h"

#define BBSHIFT		9
#define BN_ALIGN_MASK	((1 << (PAGE_CACHE_SHIFT - BBSHIFT)) - 1)

#ifndef GFP_READAHEAD
#define GFP_READAHEAD	(__GFP_NOWARN|__GFP_NORETRY)
#endif

/*
 * Debug code
 */

#ifdef PAGEBUF_TRACE
static spinlock_t		pb_trace_lock = SPIN_LOCK_UNLOCKED;
struct pagebuf_trace_buf	pb_trace;
EXPORT_SYMBOL(pb_trace);
EXPORT_SYMBOL(pb_trace_func);
#define CIRC_INC(i)	(((i) + 1) & (PB_TRACE_BUFSIZE - 1))

void
pb_trace_func(
	page_buf_t	*pb,
	int		event,
	void		*misc,
	void		*ra)
{
	int		j;
	unsigned long	flags;

	if (!pb_params.debug.val) return;

	if (ra == NULL) ra = (void *)__builtin_return_address(0);

	spin_lock_irqsave(&pb_trace_lock, flags);
	j = pb_trace.start;
	pb_trace.start = CIRC_INC(j);
	spin_unlock_irqrestore(&pb_trace_lock, flags);

	pb_trace.buf[j].pb = (unsigned long) pb;
	pb_trace.buf[j].event = event;
	pb_trace.buf[j].flags = pb->pb_flags;
	pb_trace.buf[j].hold = pb->pb_hold.counter;
	pb_trace.buf[j].lock_value = pb->pb_sema.count.counter;
	pb_trace.buf[j].task = (void *)current;
	pb_trace.buf[j].misc = misc;
	pb_trace.buf[j].ra = ra;
	pb_trace.buf[j].offset = pb->pb_file_offset;
	pb_trace.buf[j].size = pb->pb_buffer_length;
}
#endif	/* PAGEBUF_TRACE */

/*
 *	File wide globals
 */

STATIC kmem_cache_t *pagebuf_cache;
STATIC void pagebuf_daemon_wakeup(int);
STATIC void pagebuf_delwri_queue(page_buf_t *, int);
STATIC struct workqueue_struct *pagebuf_logio_workqueue;
STATIC struct workqueue_struct *pagebuf_dataio_workqueue;

/*
 * Pagebuf module configuration parameters, exported via
 * /proc/sys/vm/pagebuf
 */

pagebuf_param_t pb_params = {
			  /*	MIN	DFLT	MAX	*/
	.flush_interval	= {	HZ/2,	HZ,	30*HZ	},
	.age_buffer	= {	1*HZ,	15*HZ,	300*HZ	},
	.stats_clear	= {	0,	0,	1	},
	.debug		= {	0,	0,	1	},
};

/*
 * Pagebuf statistics variables
 */

DEFINE_PER_CPU(struct pbstats, pbstats);

/*
 * Pagebuf allocation / freeing.
 */

#define pb_to_gfp(flags) \
	(((flags) & PBF_READ_AHEAD) ? GFP_READAHEAD : \
	 ((flags) & PBF_DONT_BLOCK) ? GFP_NOFS : GFP_KERNEL)

#define pagebuf_allocate(flags) \
	kmem_cache_alloc(pagebuf_cache, pb_to_gfp(flags))
#define pagebuf_deallocate(pb) \
	kmem_cache_free(pagebuf_cache, (pb));

/*
 * Pagebuf hashing
 */

#define NBITS	8
#define NHASH	(1<<NBITS)

typedef struct {
	struct list_head	pb_hash;
	int			pb_count;
	spinlock_t		pb_hash_lock;
} pb_hash_t;

STATIC pb_hash_t	pbhash[NHASH];
#define pb_hash(pb)	&pbhash[pb->pb_hash_index]

STATIC int
_bhash(
	struct block_device *bdev,
	loff_t		base)
{
	int		bit, hval;

	base >>= 9;
	base ^= (unsigned long)bdev / L1_CACHE_BYTES;
	for (bit = hval = 0; base && bit < sizeof(base) * 8; bit += NBITS) {
		hval ^= (int)base & (NHASH-1);
		base >>= NBITS;
	}
	return hval;
}

/*
 * Mapping of multi-page buffers into contiguous virtual space
 */

STATIC void *pagebuf_mapout_locked(page_buf_t *);

typedef struct a_list {
	void		*vm_addr;
	struct a_list	*next;
} a_list_t;
STATIC a_list_t		*as_free_head;
STATIC int		as_list_len;
STATIC spinlock_t	as_lock = SPIN_LOCK_UNLOCKED;


/*
 * Try to batch vunmaps because they are costly.
 */
STATIC void
free_address(
	void		*addr)
{
	a_list_t	*aentry;

	aentry = kmalloc(sizeof(a_list_t), GFP_ATOMIC);
	if (aentry) {
		spin_lock(&as_lock);
		aentry->next = as_free_head;
		aentry->vm_addr = addr;
		as_free_head = aentry;
		as_list_len++;
		spin_unlock(&as_lock);
	} else {
		vunmap(addr);
	}
}

STATIC void
purge_addresses(void)
{
	a_list_t	*aentry, *old;

	if (as_free_head == NULL)
		return;

	spin_lock(&as_lock);
	aentry = as_free_head;
	as_free_head = NULL;
	as_list_len = 0;
	spin_unlock(&as_lock);

	while ((old = aentry) != NULL) {
		vunmap(aentry->vm_addr);
		aentry = aentry->next;
		kfree(old);
	}
}

/*
 *	Locking model:
 *
 *	Buffers associated with inodes for which buffer locking
 *	is not enabled are not protected by semaphores, and are
 *	assumed to be exclusively owned by the caller.  There is
 *	spinlock in the buffer, for use by the caller when concurrent
 *	access is possible.
 */

/*
 *	Internal pagebuf object manipulation
 */

STATIC void
_pagebuf_initialize(
	page_buf_t		*pb,
	pb_target_t		*target,
	loff_t			range_base,
	size_t			range_length,
	page_buf_flags_t	flags)
{
	/*
	 * We don't want certain flags to appear in pb->pb_flags.
	 */
	flags &= ~(PBF_LOCK|PBF_MAPPED|PBF_DONT_BLOCK|PBF_READ_AHEAD);

	memset(pb, 0, sizeof(page_buf_t));
	atomic_set(&pb->pb_hold, 1);
	init_MUTEX_LOCKED(&pb->pb_iodonesema);
	INIT_LIST_HEAD(&pb->pb_list);
	INIT_LIST_HEAD(&pb->pb_hash_list);
	init_MUTEX_LOCKED(&pb->pb_sema); /* held, no waiters */
	PB_SET_OWNER(pb);
	pb->pb_target = target;
	pb->pb_file_offset = range_base;
	/*
	 * Set buffer_length and count_desired to the same value initially.
	 * IO routines should use count_desired, which will be the same in
	 * most cases but may be reset (e.g. XFS recovery).
	 */
	pb->pb_buffer_length = pb->pb_count_desired = range_length;
	pb->pb_flags = flags | PBF_NONE;
	pb->pb_bn = PAGE_BUF_DADDR_NULL;
	atomic_set(&pb->pb_pin_count, 0);
	init_waitqueue_head(&pb->pb_waiters);

	PB_STATS_INC(pb_create);
	PB_TRACE(pb, PB_TRACE_REC(get), target);
}

/*
 * Allocate a page array capable of holding a specified number
 * of pages, and point the page buf at it.
 */
STATIC int
_pagebuf_get_pages(
	page_buf_t		*pb,
	int			page_count,
	page_buf_flags_t	flags)
{
	int			gpf_mask = pb_to_gfp(flags);

	/* Make sure that we have a page list */
	if (pb->pb_pages == NULL) {
		pb->pb_offset = page_buf_poff(pb->pb_file_offset);
		pb->pb_page_count = page_count;
		if (page_count <= PB_PAGES) {
			pb->pb_pages = pb->pb_page_array;
		} else {
			pb->pb_pages = kmalloc(sizeof(struct page *) *
					page_count, gpf_mask);
			if (pb->pb_pages == NULL)
				return -ENOMEM;
		}
		memset(pb->pb_pages, 0, sizeof(struct page *) * page_count);
	}
	return 0;
}

/*
 * Walk a pagebuf releasing all the pages contained within it.
 */
STATIC inline void
_pagebuf_freepages(
	page_buf_t		*pb)
{
	int			buf_index;

	for (buf_index = 0; buf_index < pb->pb_page_count; buf_index++) {
		struct page	*page = pb->pb_pages[buf_index];

		if (page) {
			pb->pb_pages[buf_index] = NULL;
			page_cache_release(page);
		}
	}

	if (pb->pb_pages != pb->pb_page_array)
		kfree(pb->pb_pages);
}

/*
 *	_pagebuf_free_object
 *
 *	_pagebuf_free_object releases the contents specified buffer.
 *	The modification state of any associated pages is left unchanged.
 */
void
_pagebuf_free_object(
	pb_hash_t		*hash,	/* hash bucket for buffer */
	page_buf_t		*pb)	/* buffer to deallocate	*/
{
	page_buf_flags_t	pb_flags = pb->pb_flags;

	PB_TRACE(pb, PB_TRACE_REC(free_obj), 0);
	pb->pb_flags |= PBF_FREED;

	if (hash) {
		if (!list_empty(&pb->pb_hash_list)) {
			hash->pb_count--;
			list_del_init(&pb->pb_hash_list);
		}
		spin_unlock(&hash->pb_hash_lock);
	}

	if (!(pb_flags & PBF_FREED)) {
		/* release any virtual mapping */ ;
		if (pb->pb_flags & _PBF_ADDR_ALLOCATED) {
			void *vaddr = pagebuf_mapout_locked(pb);
			if (vaddr) {
				free_address(vaddr);
			}
		}

		if (pb->pb_flags & _PBF_MEM_ALLOCATED) {
			if (pb->pb_pages) {
				/* release the pages in the address list */
				if (pb->pb_pages[0] &&
				    PageSlab(pb->pb_pages[0])) {
					/*
					 * This came from the slab
					 * allocator free it as such
					 */
					kfree(pb->pb_addr);
				} else {
					_pagebuf_freepages(pb);
				}

				pb->pb_pages = NULL;
			}
			pb->pb_flags &= ~_PBF_MEM_ALLOCATED;
		}
	}

	pagebuf_deallocate(pb);
}

/*
 *	_pagebuf_lookup_pages
 *
 *	_pagebuf_lookup_pages finds all pages which match the buffer
 *	in question and the range of file offsets supplied,
 *	and builds the page list for the buffer, if the
 *	page list is not already formed or if not all of the pages are
 *	already in the list. Invalid pages (pages which have not yet been
 *	read in from disk) are assigned for any pages which are not found.
 */
STATIC int
_pagebuf_lookup_pages(
	page_buf_t		*pb,
	struct address_space	*aspace,
	page_buf_flags_t	flags)
{
	loff_t			next_buffer_offset;
	unsigned long		page_count, pi, index;
	struct page		*page;
	int			gfp_mask, retry_count = 5, rval = 0;
	int			all_mapped, good_pages, nbytes;
	unsigned int		blocksize, sectorshift;
	size_t			size, offset;


	/* For pagebufs where we want to map an address, do not use
	 * highmem pages - so that we do not need to use kmap resources
	 * to access the data.
	 *
	 * For pages where the caller has indicated there may be resource
	 * contention (e.g. called from a transaction) do not flush
	 * delalloc pages to obtain memory.
	 */

	if (flags & PBF_READ_AHEAD) {
		gfp_mask = GFP_READAHEAD;
		retry_count = 0;
	} else if (flags & PBF_DONT_BLOCK) {
		gfp_mask = GFP_NOFS;
	} else if (flags & PBF_MAPPABLE) {
		gfp_mask = GFP_KERNEL;
	} else {
		gfp_mask = GFP_HIGHUSER;
	}

	next_buffer_offset = pb->pb_file_offset + pb->pb_buffer_length;

	good_pages = page_count = (page_buf_btoc(next_buffer_offset) -
				   page_buf_btoct(pb->pb_file_offset));

	if (pb->pb_flags & _PBF_ALL_PAGES_MAPPED) {
		/* Bring pages forward in cache */
		for (pi = 0; pi < page_count; pi++) {
			mark_page_accessed(pb->pb_pages[pi]);
		}
		if ((flags & PBF_MAPPED) && !(pb->pb_flags & PBF_MAPPED)) {
			all_mapped = 1;
			goto mapit;
		}
		return 0;
	}

	/* Ensure pb_pages field has been initialised */
	rval = _pagebuf_get_pages(pb, page_count, flags);
	if (rval)
		return rval;

	rval = pi = 0;
	blocksize = pb->pb_target->pbr_bsize;
	sectorshift = pb->pb_target->pbr_sshift;
	size = pb->pb_count_desired;
	offset = pb->pb_offset;

	/* Enter the pages in the page list */
	index = (pb->pb_file_offset - pb->pb_offset) >> PAGE_CACHE_SHIFT;
	for (all_mapped = 1; pi < page_count; pi++, index++) {
		if (pb->pb_pages[pi] == 0) {
		      retry:
			page = find_or_create_page(aspace, index, gfp_mask);
			if (!page) {
				if (--retry_count > 0) {
					PB_STATS_INC(pb_page_retries);
					pagebuf_daemon_wakeup(1);
					current->state = TASK_UNINTERRUPTIBLE;
					schedule_timeout(10);
					goto retry;
				}
				rval = -ENOMEM;
				all_mapped = 0;
				continue;
			}
			PB_STATS_INC(pb_page_found);
			mark_page_accessed(page);
			pb->pb_pages[pi] = page;
		} else {
			page = pb->pb_pages[pi];
			lock_page(page);
		}

		nbytes = PAGE_CACHE_SIZE - offset;
		if (nbytes > size)
			nbytes = size;
		size -= nbytes;

		if (!PageUptodate(page)) {
			if (blocksize == PAGE_CACHE_SIZE) {
				if (flags & PBF_READ)
					pb->pb_locked = 1;
				good_pages--;
			} else if (!PagePrivate(page)) {
				unsigned long	i, range;

				/*
				 * In this case page->private holds a bitmap
				 * of uptodate sectors within the page
				 */
				ASSERT(blocksize < PAGE_CACHE_SIZE);
				range = (offset + nbytes) >> sectorshift;
				for (i = offset >> sectorshift; i < range; i++)
					if (!test_bit(i, &page->private))
						break;
				if (i != range)
					good_pages--;
			} else {
				good_pages--;
			}
		}
		offset = 0;
	}

	if (!pb->pb_locked) {
		for (pi = 0; pi < page_count; pi++) {
			if (pb->pb_pages[pi])
				unlock_page(pb->pb_pages[pi]);
		}
	}

mapit:
	pb->pb_flags |= _PBF_MEM_ALLOCATED;
	if (all_mapped) {
		pb->pb_flags |= _PBF_ALL_PAGES_MAPPED;

		/* A single page buffer is always mappable */
		if (page_count == 1) {
			pb->pb_addr = (caddr_t)
				page_address(pb->pb_pages[0]) + pb->pb_offset;
			pb->pb_flags |= PBF_MAPPED;
		} else if (flags & PBF_MAPPED) {
			if (as_list_len > 64)
				purge_addresses();
			pb->pb_addr = vmap(pb->pb_pages, page_count,
					VM_MAP, PAGE_KERNEL);
			if (pb->pb_addr == NULL)
				return -ENOMEM;
			pb->pb_addr += pb->pb_offset;
			pb->pb_flags |= PBF_MAPPED | _PBF_ADDR_ALLOCATED;
		}
	}
	/* If some pages were found with data in them
	 * we are not in PBF_NONE state.
	 */
	if (good_pages != 0) {
		pb->pb_flags &= ~(PBF_NONE);
		if (good_pages != page_count) {
			pb->pb_flags |= PBF_PARTIAL;
		}
	}

	PB_TRACE(pb, PB_TRACE_REC(look_pg), good_pages);

	return rval;
}

/*
 *	Finding and Reading Buffers
 */

/*
 *	_pagebuf_find
 *
 *	Looks up, and creates if absent, a lockable buffer for
 *	a given range of an inode.  The buffer is returned
 *	locked.	 If other overlapping buffers exist, they are
 *	released before the new buffer is created and locked,
 *	which may imply that this call will block until those buffers
 *	are unlocked.  No I/O is implied by this call.
 */
STATIC page_buf_t *
_pagebuf_find(				/* find buffer for block	*/
	pb_target_t		*target,/* target for block		*/
	loff_t			ioff,	/* starting offset of range	*/
	size_t			isize,	/* length of range		*/
	page_buf_flags_t	flags,	/* PBF_TRYLOCK			*/
	page_buf_t		*new_pb)/* newly allocated buffer	*/
{
	loff_t			range_base;
	size_t			range_length;
	int			hval;
	pb_hash_t		*h;
	struct list_head	*p;
	page_buf_t		*pb;
	int			not_locked;

	range_base = (ioff << BBSHIFT);
	range_length = (isize << BBSHIFT);

	/* Ensure we never do IOs smaller than the sector size */
	BUG_ON(range_length < (1 << target->pbr_sshift));

	/* Ensure we never do IOs that are not sector aligned */
	BUG_ON(range_base & (loff_t)target->pbr_smask);

	hval = _bhash(target->pbr_bdev, range_base);
	h = &pbhash[hval];

	spin_lock(&h->pb_hash_lock);
	list_for_each(p, &h->pb_hash) {
		pb = list_entry(p, page_buf_t, pb_hash_list);

		if ((target == pb->pb_target) &&
		    (pb->pb_file_offset == range_base) &&
		    (pb->pb_buffer_length == range_length)) {
			if (pb->pb_flags & PBF_FREED)
				break;
			/* If we look at something bring it to the
			 * front of the list for next time
			 */
			list_del(&pb->pb_hash_list);
			list_add(&pb->pb_hash_list, &h->pb_hash);
			goto found;
		}
	}

	/* No match found */
	if (new_pb) {
		_pagebuf_initialize(new_pb, target, range_base,
				range_length, flags | _PBF_LOCKABLE);
		new_pb->pb_hash_index = hval;
		h->pb_count++;
		list_add(&new_pb->pb_hash_list, &h->pb_hash);
	} else {
		PB_STATS_INC(pb_miss_locked);
	}

	spin_unlock(&h->pb_hash_lock);
	return (new_pb);

found:
	atomic_inc(&pb->pb_hold);
	spin_unlock(&h->pb_hash_lock);

	/* Attempt to get the semaphore without sleeping,
	 * if this does not work then we need to drop the
	 * spinlock and do a hard attempt on the semaphore.
	 */
	not_locked = down_trylock(&pb->pb_sema);
	if (not_locked) {
		if (!(flags & PBF_TRYLOCK)) {
			/* wait for buffer ownership */
			PB_TRACE(pb, PB_TRACE_REC(get_lk), 0);
			pagebuf_lock(pb);
			PB_STATS_INC(pb_get_locked_waited);
		} else {
			/* We asked for a trylock and failed, no need
			 * to look at file offset and length here, we
			 * know that this pagebuf at least overlaps our
			 * pagebuf and is locked, therefore our buffer
			 * either does not exist, or is this buffer
			 */

			pagebuf_rele(pb);
			PB_STATS_INC(pb_busy_locked);
			return (NULL);
		}
	} else {
		/* trylock worked */
		PB_SET_OWNER(pb);
	}

	if (pb->pb_flags & PBF_STALE)
		pb->pb_flags &= PBF_MAPPABLE | \
				PBF_MAPPED | \
				_PBF_LOCKABLE | \
				_PBF_ALL_PAGES_MAPPED | \
				_PBF_ADDR_ALLOCATED | \
				_PBF_MEM_ALLOCATED;
	PB_TRACE(pb, PB_TRACE_REC(got_lk), 0);
	PB_STATS_INC(pb_get_locked);
	return (pb);
}


/*
 *	pagebuf_find
 *
 *	pagebuf_find returns a buffer matching the specified range of
 *	data for the specified target, if any of the relevant blocks
 *	are in memory.  The buffer may have unallocated holes, if
 *	some, but not all, of the blocks are in memory.  Even where
 *	pages are present in the buffer, not all of every page may be
 *	valid.
 */
page_buf_t *
pagebuf_find(				/* find buffer for block	*/
					/* if the block is in memory	*/
	pb_target_t		*target,/* target for block		*/
	loff_t			ioff,	/* starting offset of range	*/
	size_t			isize,	/* length of range		*/
	page_buf_flags_t	flags)	/* PBF_TRYLOCK			*/
{
	return _pagebuf_find(target, ioff, isize, flags, NULL);
}

/*
 *	pagebuf_get
 *
 *	pagebuf_get assembles a buffer covering the specified range.
 *	Some or all of the blocks in the range may be valid.  Storage
 *	in memory for all portions of the buffer will be allocated,
 *	although backing storage may not be.  If PBF_READ is set in
 *	flags, pagebuf_iostart is called also.
 */
page_buf_t *
pagebuf_get(				/* allocate a buffer		*/
	pb_target_t		*target,/* target for buffer		*/
	loff_t			ioff,	/* starting offset of range	*/
	size_t			isize,	/* length of range		*/
	page_buf_flags_t	flags)	/* PBF_TRYLOCK			*/
{
	page_buf_t		*pb, *new_pb;
	int			error;

	new_pb = pagebuf_allocate(flags);
	if (unlikely(!new_pb))
		return (NULL);

	pb = _pagebuf_find(target, ioff, isize, flags, new_pb);
	if (pb != new_pb) {
		pagebuf_deallocate(new_pb);
		if (unlikely(!pb))
			return (NULL);
	}

	PB_STATS_INC(pb_get);

	/* fill in any missing pages */
	error = _pagebuf_lookup_pages(pb, pb->pb_target->pbr_mapping, flags);
	if (unlikely(error)) {
		pagebuf_free(pb);
		return (NULL);
	}

	/*
	 * Always fill in the block number now, the mapped cases can do
	 * their own overlay of this later.
	 */
	pb->pb_bn = ioff;
	pb->pb_count_desired = pb->pb_buffer_length;

	if (flags & PBF_READ) {
		if (PBF_NOT_DONE(pb)) {
			PB_TRACE(pb, PB_TRACE_REC(get_read), flags);
			PB_STATS_INC(pb_get_read);
			pagebuf_iostart(pb, flags);
		} else if (flags & PBF_ASYNC) {
			/*
			 * Read ahead call which is already satisfied,
			 * drop the buffer
			 */
			if (flags & (PBF_LOCK | PBF_TRYLOCK))
				pagebuf_unlock(pb);
			pagebuf_rele(pb);
			return NULL;
		} else {
			/* We do not want read in the flags */
			pb->pb_flags &= ~PBF_READ;
		}
	}

	PB_TRACE(pb, PB_TRACE_REC(get_obj), flags);
	return (pb);
}

/*
 * Create a skeletal pagebuf (no pages associated with it).
 */
page_buf_t *
pagebuf_lookup(
	struct pb_target	*target,
	loff_t			ioff,
	size_t			isize,
	page_buf_flags_t	flags)
{
	page_buf_t		*pb;

	pb = pagebuf_allocate(flags);
	if (pb) {
		_pagebuf_initialize(pb, target, ioff, isize, flags);
	}
	return pb;
}

/*
 * If we are not low on memory then do the readahead in a deadlock
 * safe manner.
 */
void
pagebuf_readahead(
	pb_target_t		*target,
	loff_t			ioff,
	size_t			isize,
	page_buf_flags_t	flags)
{
	struct backing_dev_info *bdi;

	bdi = target->pbr_mapping->backing_dev_info;
	if (bdi_read_congested(bdi))
		return;
	if (bdi_write_congested(bdi))
		return;

	flags |= (PBF_TRYLOCK|PBF_READ|PBF_ASYNC|PBF_MAPPABLE|PBF_READ_AHEAD);
	pagebuf_get(target, ioff, isize, flags);
}

page_buf_t *
pagebuf_get_empty(
	size_t			len,
	pb_target_t		*target)
{
	page_buf_t		*pb;

	pb = pagebuf_allocate(_PBF_LOCKABLE);
	if (pb)
		_pagebuf_initialize(pb, target, 0, len, _PBF_LOCKABLE);
	return pb;
}

static inline struct page *
mem_to_page(
	void			*addr)
{
	if (((unsigned long)addr < VMALLOC_START) ||
	    ((unsigned long)addr >= VMALLOC_END)) {
		return virt_to_page(addr);
	} else {
		return vmalloc_to_page(addr);
	}
}

int
pagebuf_associate_memory(
	page_buf_t		*pb,
	void			*mem,
	size_t			len)
{
	int			rval;
	int			i = 0;
	size_t			ptr;
	size_t			end, end_cur;
	off_t			offset;
	int			page_count;

	page_count = PAGE_CACHE_ALIGN(len) >> PAGE_CACHE_SHIFT;
	offset = (off_t) mem - ((off_t)mem & PAGE_CACHE_MASK);
	if (offset && (len > PAGE_CACHE_SIZE))
		page_count++;

	/* Free any previous set of page pointers */
	if (pb->pb_pages && (pb->pb_pages != pb->pb_page_array)) {
		kfree(pb->pb_pages);
	}
	pb->pb_pages = NULL;
	pb->pb_addr = mem;

	rval = _pagebuf_get_pages(pb, page_count, 0);
	if (rval)
		return rval;

	pb->pb_offset = offset;
	ptr = (size_t) mem & PAGE_CACHE_MASK;
	end = PAGE_CACHE_ALIGN((size_t) mem + len);
	end_cur = end;
	/* set up first page */
	pb->pb_pages[0] = mem_to_page(mem);

	ptr += PAGE_CACHE_SIZE;
	pb->pb_page_count = ++i;
	while (ptr < end) {
		pb->pb_pages[i] = mem_to_page((void *)ptr);
		pb->pb_page_count = ++i;
		ptr += PAGE_CACHE_SIZE;
	}
	pb->pb_locked = 0;

	pb->pb_count_desired = pb->pb_buffer_length = len;
	pb->pb_flags |= PBF_MAPPED;

	return 0;
}

page_buf_t *
pagebuf_get_no_daddr(
	size_t			len,
	pb_target_t		*target)
{
	int			rval;
	void			*rmem = NULL;
	page_buf_flags_t	flags = _PBF_LOCKABLE | PBF_FORCEIO;
	page_buf_t		*pb;
	size_t			tlen = 0;

	if (len > 0x20000)
		return(NULL);

	pb = pagebuf_allocate(flags);
	if (!pb)
		return NULL;

	_pagebuf_initialize(pb, target, 0, len, flags);

	do {
		if (tlen == 0) {
			tlen = len; /* first time */
		} else {
			kfree(rmem); /* free the mem from the previous try */
			tlen <<= 1; /* double the size and try again */
		}
		if ((rmem = kmalloc(tlen, GFP_KERNEL)) == 0) {
			pagebuf_free(pb);
			return NULL;
		}
	} while ((size_t)rmem != ((size_t)rmem & ~target->pbr_smask));

	if ((rval = pagebuf_associate_memory(pb, rmem, len)) != 0) {
		kfree(rmem);
		pagebuf_free(pb);
		return NULL;
	}
	/* otherwise pagebuf_free just ignores it */
	pb->pb_flags |= _PBF_MEM_ALLOCATED;
	PB_CLEAR_OWNER(pb);
	up(&pb->pb_sema);	/* Return unlocked pagebuf */

	PB_TRACE(pb, PB_TRACE_REC(no_daddr), rmem);

	return pb;
}


/*
 *	pagebuf_hold
 *
 *	Increment reference count on buffer, to hold the buffer concurrently
 *	with another thread which may release (free) the buffer asynchronously.
 *
 *	Must hold the buffer already to call this function.
 */
void
pagebuf_hold(
	page_buf_t		*pb)
{
	atomic_inc(&pb->pb_hold);
	PB_TRACE(pb, PB_TRACE_REC(hold), 0);
}

/*
 *	pagebuf_free
 *
 *	pagebuf_free releases the specified buffer.  The modification
 *	state of any associated pages is left unchanged.
 */
void
pagebuf_free(
	page_buf_t		*pb)
{
	if (pb->pb_flags & _PBF_LOCKABLE) {
		pb_hash_t	*h = pb_hash(pb);

		spin_lock(&h->pb_hash_lock);
		_pagebuf_free_object(h, pb);
	} else {
		_pagebuf_free_object(NULL, pb);
	}
}

/*
 *	pagebuf_rele
 *
 *	pagebuf_rele releases a hold on the specified buffer.  If the
 *	the hold count is 1, pagebuf_rele calls pagebuf_free.
 */
void
pagebuf_rele(
	page_buf_t		*pb)
{
	pb_hash_t		*h;

	PB_TRACE(pb, PB_TRACE_REC(rele), pb->pb_relse);
	if (pb->pb_flags & _PBF_LOCKABLE) {
		h = pb_hash(pb);
		spin_lock(&h->pb_hash_lock);
	} else {
		h = NULL;
	}

	if (atomic_dec_and_test(&pb->pb_hold)) {
		int		do_free = 1;

		if (pb->pb_relse) {
			atomic_inc(&pb->pb_hold);
			if (h)
				spin_unlock(&h->pb_hash_lock);
			(*(pb->pb_relse)) (pb);
			do_free = 0;
		}
		if (pb->pb_flags & PBF_DELWRI) {
			pb->pb_flags |= PBF_ASYNC;
			atomic_inc(&pb->pb_hold);
			if (h && do_free)
				spin_unlock(&h->pb_hash_lock);
			pagebuf_delwri_queue(pb, 0);
			do_free = 0;
		} else if (pb->pb_flags & PBF_FS_MANAGED) {
			if (h)
				spin_unlock(&h->pb_hash_lock);
			do_free = 0;
		}

		if (do_free) {
			_pagebuf_free_object(h, pb);
		}
	} else if (h) {
		spin_unlock(&h->pb_hash_lock);
	}
}


/*
 *	Pinning Buffer Storage in Memory
 */

/*
 *	pagebuf_pin
 *
 *	pagebuf_pin locks all of the memory represented by a buffer in
 *	memory.  Multiple calls to pagebuf_pin and pagebuf_unpin, for
 *	the same or different buffers affecting a given page, will
 *	properly count the number of outstanding "pin" requests.  The
 *	buffer may be released after the pagebuf_pin and a different
 *	buffer used when calling pagebuf_unpin, if desired.
 *	pagebuf_pin should be used by the file system when it wants be
 *	assured that no attempt will be made to force the affected
 *	memory to disk.	 It does not assure that a given logical page
 *	will not be moved to a different physical page.
 */
void
pagebuf_pin(
	page_buf_t		*pb)
{
	atomic_inc(&pb->pb_pin_count);
	PB_TRACE(pb, PB_TRACE_REC(pin), pb->pb_pin_count.counter);
}

/*
 *	pagebuf_unpin
 *
 *	pagebuf_unpin reverses the locking of memory performed by
 *	pagebuf_pin.  Note that both functions affected the logical
 *	pages associated with the buffer, not the buffer itself.
 */
void
pagebuf_unpin(
	page_buf_t		*pb)
{
	if (atomic_dec_and_test(&pb->pb_pin_count)) {
		wake_up_all(&pb->pb_waiters);
	}
	PB_TRACE(pb, PB_TRACE_REC(unpin), pb->pb_pin_count.counter);
}

int
pagebuf_ispin(
	page_buf_t		*pb)
{
	return atomic_read(&pb->pb_pin_count);
}

/*
 *	pagebuf_wait_unpin
 *
 *	pagebuf_wait_unpin waits until all of the memory associated
 *	with the buffer is not longer locked in memory.  It returns
 *	immediately if none of the affected pages are locked.
 */
static inline void
_pagebuf_wait_unpin(
	page_buf_t		*pb)
{
	DECLARE_WAITQUEUE	(wait, current);

	if (atomic_read(&pb->pb_pin_count) == 0)
		return;

	add_wait_queue(&pb->pb_waiters, &wait);
	for (;;) {
		current->state = TASK_UNINTERRUPTIBLE;
		if (atomic_read(&pb->pb_pin_count) == 0)
			break;
		if (atomic_read(&pb->pb_io_remaining))
			blk_run_queues();
		schedule();
	}
	remove_wait_queue(&pb->pb_waiters, &wait);
	current->state = TASK_RUNNING;
}

/*
 *	Buffer Utility Routines
 */

/*
 *	pagebuf_iodone
 *
 *	pagebuf_iodone marks a buffer for which I/O is in progress
 *	done with respect to that I/O.	The pb_iodone routine, if
 *	present, will be called as a side-effect.
 */
void
pagebuf_iodone_work(
	void			*v)
{
	page_buf_t		*pb = (page_buf_t *)v;

	if (pb->pb_iodone) {
		(*(pb->pb_iodone)) (pb);
		return;
	}

	if (pb->pb_flags & PBF_ASYNC) {
		if ((pb->pb_flags & _PBF_LOCKABLE) && !pb->pb_relse)
			pagebuf_unlock(pb);
		pagebuf_rele(pb);
	}
}

void
pagebuf_iodone(
	page_buf_t		*pb,
	int			dataio,
	int			schedule)
{
	pb->pb_flags &= ~(PBF_READ | PBF_WRITE);
	if (pb->pb_error == 0) {
		pb->pb_flags &= ~(PBF_PARTIAL | PBF_NONE);
	}

	PB_TRACE(pb, PB_TRACE_REC(done), pb->pb_iodone);

	if ((pb->pb_iodone) || (pb->pb_flags & PBF_ASYNC)) {
		if (schedule) {
			INIT_WORK(&pb->pb_iodone_work, pagebuf_iodone_work, pb);
			queue_work(dataio ? pagebuf_dataio_workqueue :
				pagebuf_logio_workqueue, &pb->pb_iodone_work);
		} else {
			pagebuf_iodone_work(pb);
		}
	} else {
		up(&pb->pb_iodonesema);
	}
}

/*
 *	pagebuf_ioerror
 *
 *	pagebuf_ioerror sets the error code for a buffer.
 */
void
pagebuf_ioerror(			/* mark/clear buffer error flag */
	page_buf_t		*pb,	/* buffer to mark		*/
	unsigned int		error)	/* error to store (0 if none)	*/
{
	pb->pb_error = error;
	PB_TRACE(pb, PB_TRACE_REC(ioerror), error);
}

/*
 *	pagebuf_iostart
 *
 *	pagebuf_iostart initiates I/O on a buffer, based on the flags supplied.
 *	If necessary, it will arrange for any disk space allocation required,
 *	and it will break up the request if the block mappings require it.
 *	The pb_iodone routine in the buffer supplied will only be called
 *	when all of the subsidiary I/O requests, if any, have been completed.
 *	pagebuf_iostart calls the pagebuf_ioinitiate routine or
 *	pagebuf_iorequest, if the former routine is not defined, to start
 *	the I/O on a given low-level request.
 */
int
pagebuf_iostart(			/* start I/O on a buffer	  */
	page_buf_t		*pb,	/* buffer to start		  */
	page_buf_flags_t	flags)	/* PBF_LOCK, PBF_ASYNC, PBF_READ, */
					/* PBF_WRITE, PBF_DELWRI,	  */
					/* PBF_SYNC, PBF_DONT_BLOCK	  */
{
	int			status = 0;

	PB_TRACE(pb, PB_TRACE_REC(iostart), flags);

	if (flags & PBF_DELWRI) {
		pb->pb_flags &= ~(PBF_READ | PBF_WRITE | PBF_ASYNC);
		pb->pb_flags |= flags &
				(PBF_DELWRI | PBF_ASYNC | PBF_SYNC);
		pagebuf_delwri_queue(pb, 1);
		return status;
	}

	pb->pb_flags &= ~(PBF_READ | PBF_WRITE | PBF_ASYNC | \
			PBF_DELWRI | PBF_READ_AHEAD | PBF_RUN_QUEUES);
	pb->pb_flags |= flags & (PBF_READ | PBF_WRITE | PBF_ASYNC | \
			PBF_SYNC | PBF_READ_AHEAD | PBF_RUN_QUEUES);

	BUG_ON(pb->pb_bn == PAGE_BUF_DADDR_NULL);

	/* For writes allow an alternate strategy routine to precede
	 * the actual I/O request (which may not be issued at all in
	 * a shutdown situation, for example).
	 */
	status = (flags & PBF_WRITE) ?
		pagebuf_iostrategy(pb) : pagebuf_iorequest(pb);

	/* Wait for I/O if we are not an async request.
	 * Note: async I/O request completion will release the buffer,
	 * and that can already be done by this point.  So using the
	 * buffer pointer from here on, after async I/O, is invalid.
	 */
	if (!status && !(flags & PBF_ASYNC))
		status = pagebuf_iowait(pb);

	return status;
}

/*
 * Helper routine for pagebuf_iorequest
 */

STATIC __inline__ int
_pagebuf_iolocked(
	page_buf_t		*pb)
{
	ASSERT(pb->pb_flags & (PBF_READ|PBF_WRITE));
	if (pb->pb_flags & PBF_READ)
		return pb->pb_locked;
	return ((pb->pb_flags & _PBF_LOCKABLE) == 0);
}

STATIC __inline__ void
_pagebuf_iodone(
	page_buf_t		*pb,
	int			schedule)
{
	if (atomic_dec_and_test(&pb->pb_io_remaining) == 1) {
		pb->pb_locked = 0;
		pagebuf_iodone(pb, (pb->pb_flags & PBF_FS_DATAIOD), schedule);
	}
}

STATIC int
bio_end_io_pagebuf(
	struct bio		*bio,
	unsigned int		bytes_done,
	int			error)
{
	page_buf_t		*pb = (page_buf_t *)bio->bi_private;
	unsigned int		i, blocksize = pb->pb_target->pbr_bsize;
	unsigned int		sectorshift = pb->pb_target->pbr_sshift;
	struct bio_vec		*bvec = bio->bi_io_vec;

	if (bio->bi_size)
		return 1;

	if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
		pb->pb_error = EIO;

	for (i = 0; i < bio->bi_vcnt; i++, bvec++) {
		struct page	*page = bvec->bv_page;

		if (pb->pb_error) {
			SetPageError(page);
		} else if (blocksize == PAGE_CACHE_SIZE) {
			SetPageUptodate(page);
		} else if (!PagePrivate(page)) {
			unsigned int	j, range;

			ASSERT(blocksize < PAGE_CACHE_SIZE);
			range = (bvec->bv_offset + bvec->bv_len) >> sectorshift;
			for (j = bvec->bv_offset >> sectorshift; j < range; j++)
				set_bit(j, &page->private);
			if (page->private == (unsigned long)(PAGE_CACHE_SIZE-1))
				SetPageUptodate(page);
		}

		if (_pagebuf_iolocked(pb)) {
			unlock_page(page);
		}
	}

	_pagebuf_iodone(pb, 1);
	bio_put(bio);
	return 0;
}

void
_pagebuf_ioapply(
	page_buf_t		*pb)
{
	int			i, map_i, total_nr_pages, nr_pages;
	struct bio		*bio;
	int			offset = pb->pb_offset;
	int			size = pb->pb_count_desired;
	sector_t		sector = pb->pb_bn;
	unsigned int		blocksize = pb->pb_target->pbr_bsize;
	int			locking = _pagebuf_iolocked(pb);

	total_nr_pages = pb->pb_page_count;
	map_i = 0;

	/* Special code path for reading a sub page size pagebuf in --
	 * we populate up the whole page, and hence the other metadata
	 * in the same page.  This optimization is only valid when the
	 * filesystem block size and the page size are equal.
	 */
	if ((pb->pb_buffer_length < PAGE_CACHE_SIZE) &&
	    (pb->pb_flags & PBF_READ) && locking &&
	    (blocksize == PAGE_CACHE_SIZE)) {
		bio = bio_alloc(GFP_NOIO, 1);

		bio->bi_bdev = pb->pb_target->pbr_bdev;
		bio->bi_sector = sector - (offset >> BBSHIFT);
		bio->bi_end_io = bio_end_io_pagebuf;
		bio->bi_private = pb;

		bio_add_page(bio, pb->pb_pages[0], PAGE_CACHE_SIZE, 0);
		size = 0;

		atomic_inc(&pb->pb_io_remaining);

		goto submit_io;
	}

	/* Lock down the pages which we need to for the request */
	if (locking && (pb->pb_flags & PBF_WRITE) && (pb->pb_locked == 0)) {
		for (i = 0; size; i++) {
			int		nbytes = PAGE_CACHE_SIZE - offset;
			struct page	*page = pb->pb_pages[i];

			if (nbytes > size)
				nbytes = size;

			lock_page(page);

			size -= nbytes;
			offset = 0;
		}
		offset = pb->pb_offset;
		size = pb->pb_count_desired;
	}

next_chunk:
	atomic_inc(&pb->pb_io_remaining);
	nr_pages = BIO_MAX_SECTORS >> (PAGE_SHIFT - BBSHIFT);
	if (nr_pages > total_nr_pages)
		nr_pages = total_nr_pages;

	bio = bio_alloc(GFP_NOIO, nr_pages);
	bio->bi_bdev = pb->pb_target->pbr_bdev;
	bio->bi_sector = sector;
	bio->bi_end_io = bio_end_io_pagebuf;
	bio->bi_private = pb;

	for (; size && nr_pages; nr_pages--, map_i++) {
		int	nbytes = PAGE_CACHE_SIZE - offset;

		if (nbytes > size)
			nbytes = size;

		if (bio_add_page(bio, pb->pb_pages[map_i],
					nbytes, offset) < nbytes)
			break;

		offset = 0;
		sector += nbytes >> BBSHIFT;
		size -= nbytes;
		total_nr_pages--;
	}

submit_io:
	if (likely(bio->bi_size)) {
		submit_bio((pb->pb_flags & PBF_READ) ? READ : WRITE, bio);
		if (size)
			goto next_chunk;
	} else {
		bio_put(bio);
		pagebuf_ioerror(pb, EIO);
	}

	if (pb->pb_flags & PBF_RUN_QUEUES) {
		pb->pb_flags &= ~PBF_RUN_QUEUES;
		if (atomic_read(&pb->pb_io_remaining) > 1)
			blk_run_queues();
	}
}

/*
 *	pagebuf_iorequest
 *
 *	pagebuf_iorequest is the core I/O request routine.
 *	It assumes that the buffer is well-formed and
 *	mapped and ready for physical I/O, unlike
 *	pagebuf_iostart() and pagebuf_iophysio().  Those
 *	routines call the pagebuf_ioinitiate routine to start I/O,
 *	if it is present, or else call pagebuf_iorequest()
 *	directly if the pagebuf_ioinitiate routine is not present.
 *
 *	This function will be responsible for ensuring access to the
 *	pages is restricted whilst I/O is in progress - for locking
 *	pagebufs the pagebuf lock is the mediator, for non-locking
 *	pagebufs the pages will be locked. In the locking case we
 *	need to use the pagebuf lock as multiple meta-data buffers
 *	will reference the same page.
 */
int
pagebuf_iorequest(			/* start real I/O		*/
	page_buf_t		*pb)	/* buffer to convey to device	*/
{
	PB_TRACE(pb, PB_TRACE_REC(ioreq), 0);

	if (pb->pb_flags & PBF_DELWRI) {
		pagebuf_delwri_queue(pb, 1);
		return 0;
	}

	if (pb->pb_flags & PBF_WRITE) {
		_pagebuf_wait_unpin(pb);
	}

	pagebuf_hold(pb);

	/* Set the count to 1 initially, this will stop an I/O
	 * completion callout which happens before we have started
	 * all the I/O from calling pagebuf_iodone too early.
	 */
	atomic_set(&pb->pb_io_remaining, 1);
	_pagebuf_ioapply(pb);
	_pagebuf_iodone(pb, 0);

	pagebuf_rele(pb);
	return 0;
}

/*
 *	pagebuf_iowait
 *
 *	pagebuf_iowait waits for I/O to complete on the buffer supplied.
 *	It returns immediately if no I/O is pending.  In any case, it returns
 *	the error code, if any, or 0 if there is no error.
 */
int
pagebuf_iowait(
	page_buf_t		*pb)
{
	PB_TRACE(pb, PB_TRACE_REC(iowait), 0);
	if (atomic_read(&pb->pb_io_remaining))
		blk_run_queues();
	down(&pb->pb_iodonesema);
	PB_TRACE(pb, PB_TRACE_REC(iowaited), (int)pb->pb_error);
	return pb->pb_error;
}

STATIC void *
pagebuf_mapout_locked(
	page_buf_t		*pb)
{
	void			*old_addr = NULL;

	if (pb->pb_flags & PBF_MAPPED) {
		if (pb->pb_flags & _PBF_ADDR_ALLOCATED)
			old_addr = pb->pb_addr - pb->pb_offset;
		pb->pb_addr = NULL;
		pb->pb_flags &= ~(PBF_MAPPED | _PBF_ADDR_ALLOCATED);
	}

	return old_addr;	/* Caller must free the address space,
				 * we are under a spin lock, probably
				 * not safe to do vfree here
				 */
}

caddr_t
pagebuf_offset(
	page_buf_t		*pb,
	size_t			offset)
{
	struct page		*page;

	offset += pb->pb_offset;

	page = pb->pb_pages[offset >> PAGE_CACHE_SHIFT];
	return (caddr_t) page_address(page) + (offset & (PAGE_CACHE_SIZE - 1));
}

/*
 *	pagebuf_iomove
 *
 *	Move data into or out of a buffer.
 */
void
pagebuf_iomove(
	page_buf_t		*pb,	/* buffer to process		*/
	size_t			boff,	/* starting buffer offset	*/
	size_t			bsize,	/* length to copy		*/
	caddr_t			data,	/* data address			*/
	page_buf_rw_t		mode)	/* read/write flag		*/
{
	size_t			bend, cpoff, csize;
	struct page		*page;

	bend = boff + bsize;
	while (boff < bend) {
		page = pb->pb_pages[page_buf_btoct(boff + pb->pb_offset)];
		cpoff = page_buf_poff(boff + pb->pb_offset);
		csize = min_t(size_t,
			      PAGE_CACHE_SIZE-cpoff, pb->pb_count_desired-boff);

		ASSERT(((csize + cpoff) <= PAGE_CACHE_SIZE));

		switch (mode) {
		case PBRW_ZERO:
			memset(page_address(page) + cpoff, 0, csize);
			break;
		case PBRW_READ:
			memcpy(data, page_address(page) + cpoff, csize);
			break;
		case PBRW_WRITE:
			memcpy(page_address(page) + cpoff, data, csize);
		}

		boff += csize;
		data += csize;
	}
}


/*
 * Pagebuf delayed write buffer handling
 */

STATIC int pbd_active = 1;
STATIC LIST_HEAD(pbd_delwrite_queue);
STATIC spinlock_t pbd_delwrite_lock = SPIN_LOCK_UNLOCKED;

STATIC void
pagebuf_delwri_queue(
	page_buf_t		*pb,
	int			unlock)
{
	PB_TRACE(pb, PB_TRACE_REC(delwri_q), unlock);
	spin_lock(&pbd_delwrite_lock);
	/* If already in the queue, dequeue and place at tail */
	if (!list_empty(&pb->pb_list)) {
		if (unlock) {
			atomic_dec(&pb->pb_hold);
		}
		list_del(&pb->pb_list);
	}

	list_add_tail(&pb->pb_list, &pbd_delwrite_queue);
	pb->pb_flushtime = jiffies + pb_params.age_buffer.val;
	spin_unlock(&pbd_delwrite_lock);

	if (unlock && (pb->pb_flags & _PBF_LOCKABLE)) {
		pagebuf_unlock(pb);
	}
}

void
pagebuf_delwri_dequeue(
	page_buf_t		*pb)
{
	PB_TRACE(pb, PB_TRACE_REC(delwri_uq), 0);
	spin_lock(&pbd_delwrite_lock);
	list_del_init(&pb->pb_list);
	pb->pb_flags &= ~PBF_DELWRI;
	spin_unlock(&pbd_delwrite_lock);
}

STATIC void
pagebuf_runall_queues(
	struct workqueue_struct	*queue)
{
	flush_workqueue(queue);
}

/* Defines for pagebuf daemon */
DECLARE_WAIT_QUEUE_HEAD(pbd_waitq);
STATIC int force_flush;

STATIC void
pagebuf_daemon_wakeup(
	int			flag)
{
	force_flush = flag;
	if (waitqueue_active(&pbd_waitq)) {
		wake_up_interruptible(&pbd_waitq);
	}
}

typedef void (*timeout_fn)(unsigned long);

STATIC int
pagebuf_daemon(
	void			*data)
{
	int			count;
	page_buf_t		*pb;
	struct list_head	*curr, *next, tmp;
	struct timer_list	pb_daemon_timer =
		TIMER_INITIALIZER((timeout_fn)pagebuf_daemon_wakeup, 0, 0);

	/*  Set up the thread  */
	daemonize("pagebufd");

	current->flags |= PF_MEMALLOC;

	INIT_LIST_HEAD(&tmp);
	do {
		/* swsusp */
		if (current->flags & PF_FREEZE)
			refrigerator(PF_IOTHREAD);

		if (pbd_active == 1) {
			mod_timer(&pb_daemon_timer,
				  jiffies + pb_params.flush_interval.val);
			interruptible_sleep_on(&pbd_waitq);
		}

		if (pbd_active == 0) {
			del_timer_sync(&pb_daemon_timer);
		}

		spin_lock(&pbd_delwrite_lock);

		count = 0;
		list_for_each_safe(curr, next, &pbd_delwrite_queue) {
			pb = list_entry(curr, page_buf_t, pb_list);

			PB_TRACE(pb, PB_TRACE_REC(walkq1), pagebuf_ispin(pb));

			if ((pb->pb_flags & PBF_DELWRI) && !pagebuf_ispin(pb) &&
			    (((pb->pb_flags & _PBF_LOCKABLE) == 0) ||
			     !pagebuf_cond_lock(pb))) {

				if (!force_flush &&
				    time_before(jiffies, pb->pb_flushtime)) {
					pagebuf_unlock(pb);
					break;
				}

				pb->pb_flags &= ~PBF_DELWRI;
				pb->pb_flags |= PBF_WRITE;

				list_del(&pb->pb_list);
				list_add(&pb->pb_list, &tmp);

				count++;
			}
		}

		spin_unlock(&pbd_delwrite_lock);
		while (!list_empty(&tmp)) {
			pb = list_entry(tmp.next, page_buf_t, pb_list);
			list_del_init(&pb->pb_list);

			pagebuf_iostrategy(pb);
		}

		if (as_list_len > 0)
			purge_addresses();
		if (count)
			blk_run_queues();

		force_flush = 0;
	} while (pbd_active == 1);

	pbd_active = -1;
	wake_up_interruptible(&pbd_waitq);

	return 0;
}

void
pagebuf_delwri_flush(
	pb_target_t		*target,
	u_long			flags,
	int			*pinptr)
{
	page_buf_t		*pb;
	struct list_head	*curr, *next, tmp;
	int			pincount = 0;
	int			flush_cnt = 0;

	pagebuf_runall_queues(pagebuf_dataio_workqueue);
	pagebuf_runall_queues(pagebuf_logio_workqueue);

	spin_lock(&pbd_delwrite_lock);
	INIT_LIST_HEAD(&tmp);

	list_for_each_safe(curr, next, &pbd_delwrite_queue) {
		pb = list_entry(curr, page_buf_t, pb_list);

		/*
		 * Skip other targets, markers and in progress buffers
		 */

		if ((pb->pb_flags == 0) || (pb->pb_target != target) ||
		    !(pb->pb_flags & PBF_DELWRI)) {
			continue;
		}

		PB_TRACE(pb, PB_TRACE_REC(walkq2), pagebuf_ispin(pb));
		if (pagebuf_ispin(pb)) {
			pincount++;
			continue;
		}

		pb->pb_flags &= ~PBF_DELWRI;
		pb->pb_flags |= PBF_WRITE;
		list_move(&pb->pb_list, &tmp);
	}
	/* ok found all the items that can be worked on 
	 * drop the lock and process the private list */
	spin_unlock(&pbd_delwrite_lock);

	list_for_each_safe(curr, next, &tmp) {
		pb = list_entry(curr, page_buf_t, pb_list);

		if (flags & PBDF_WAIT)
			pb->pb_flags &= ~PBF_ASYNC;
		else
			list_del_init(curr);

		pagebuf_lock(pb);
		pagebuf_iostrategy(pb);
		if (++flush_cnt > 32) {
			blk_run_queues();
			flush_cnt = 0;
		}
	}

	blk_run_queues();

	while (!list_empty(&tmp)) {
		pb = list_entry(tmp.next, page_buf_t, pb_list);

		list_del_init(&pb->pb_list);
		pagebuf_iowait(pb);
		if (!pb->pb_relse)
			pagebuf_unlock(pb);
		pagebuf_rele(pb);
	}

	if (pinptr)
		*pinptr = pincount;
}

STATIC int
pagebuf_daemon_start(void)
{
	int		rval;

	pagebuf_logio_workqueue = create_workqueue("xfslogd");
	if (!pagebuf_logio_workqueue)
		return -ENOMEM;

	pagebuf_dataio_workqueue = create_workqueue("xfsdatad");
	if (!pagebuf_dataio_workqueue) {
		destroy_workqueue(pagebuf_logio_workqueue);
		return -ENOMEM;
	}

	rval = kernel_thread(pagebuf_daemon, NULL, CLONE_FS|CLONE_FILES);
	if (rval < 0) {
		destroy_workqueue(pagebuf_logio_workqueue);
		destroy_workqueue(pagebuf_dataio_workqueue);
	}

	return rval;
}

/*
 * pagebuf_daemon_stop
 *
 * Note: do not mark as __exit, it is called from pagebuf_terminate.
 */
STATIC void
pagebuf_daemon_stop(void)
{
	pbd_active = 0;
	wake_up_interruptible(&pbd_waitq);
	wait_event_interruptible(pbd_waitq, pbd_active);
	destroy_workqueue(pagebuf_logio_workqueue);
	destroy_workqueue(pagebuf_dataio_workqueue);
}


/*
 * Pagebuf sysctl interface
 */

STATIC int
pb_stats_clear_handler(
	ctl_table		*ctl,
	int			write,
	struct file		*filp,
	void			*buffer,
	size_t			*lenp)
{
	int			c, ret;
	int			*valp = ctl->data;

	ret = proc_doulongvec_minmax(ctl, write, filp, buffer, lenp);

	if (!ret && write && *valp) {
		printk("XFS Clearing pbstats\n");
		for (c = 0; c < NR_CPUS; c++) {
			if (!cpu_possible(c)) continue;
				memset(&per_cpu(pbstats, c), 0,
				       sizeof(struct pbstats));
		}
		pb_params.stats_clear.val = 0;
	}

	return ret;
}

STATIC struct ctl_table_header *pagebuf_table_header;

STATIC ctl_table pagebuf_table[] = {
	{PB_FLUSH_INT, "flush_int", &pb_params.flush_interval.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL,
	&pb_params.flush_interval.min, &pb_params.flush_interval.max},

	{PB_FLUSH_AGE, "flush_age", &pb_params.age_buffer.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL, 
	&pb_params.age_buffer.min, &pb_params.age_buffer.max},

	{PB_STATS_CLEAR, "stats_clear", &pb_params.stats_clear.val,
	sizeof(int), 0644, NULL, &pb_stats_clear_handler,
	&sysctl_intvec, NULL, 
	&pb_params.stats_clear.min, &pb_params.stats_clear.max},

#ifdef PAGEBUF_TRACE
	{PB_DEBUG, "debug", &pb_params.debug.val,
	sizeof(int), 0644, NULL, &proc_dointvec_minmax,
	&sysctl_intvec, NULL, 
	&pb_params.debug.min, &pb_params.debug.max},
#endif
	{0}
};

STATIC ctl_table pagebuf_dir_table[] = {
	{VM_PAGEBUF, "pagebuf", NULL, 0, 0555, pagebuf_table},
	{0}
};

STATIC ctl_table pagebuf_root_table[] = {
	{CTL_VM, "vm",  NULL, 0, 0555, pagebuf_dir_table},
	{0}
};

#ifdef CONFIG_PROC_FS
STATIC int
pagebuf_readstats(
	char			*buffer,
	char			**start,
	off_t			offset,
	int			count,
	int			*eof,
	void			*data)
{
	int			c, i, len, val;

	len = 0;
	len += sprintf(buffer + len, "pagebuf");
	for (i = 0; i < sizeof(struct pbstats) / sizeof(u_int32_t); i++) {
		val = 0;
		for (c = 0 ; c < NR_CPUS; c++) {
			if (!cpu_possible(c)) continue;
			val += *(((u_int32_t*)&per_cpu(pbstats, c) + i));
		}
		len += sprintf(buffer + len, " %u", val);
	}
	buffer[len++] = '\n';

	if (offset >= len) {
		*start = buffer;
		*eof = 1;
		return 0;
	}
	*start = buffer + offset;
	if ((len -= offset) > count)
		return count;
	*eof = 1;

	return len;
}
#endif  /* CONFIG_PROC_FS */

/*
 *	Initialization and Termination
 */

int __init
pagebuf_init(void)
{
	int			i;

	pagebuf_table_header = register_sysctl_table(pagebuf_root_table, 1);

#ifdef CONFIG_PROC_FS
	if (proc_mkdir("fs/pagebuf", 0))
		create_proc_read_entry(
			"fs/pagebuf/stat", 0, 0, pagebuf_readstats, NULL);
#endif

	pagebuf_cache = kmem_cache_create("page_buf_t", sizeof(page_buf_t), 0,
			SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (pagebuf_cache == NULL) {
		printk("pagebuf: couldn't init pagebuf cache\n");
		pagebuf_terminate();
		return -ENOMEM;
	}

	for (i = 0; i < NHASH; i++) {
		spin_lock_init(&pbhash[i].pb_hash_lock);
		INIT_LIST_HEAD(&pbhash[i].pb_hash);
	}

#ifdef PAGEBUF_TRACE
	pb_trace.buf = (pagebuf_trace_t *)kmalloc(
			PB_TRACE_BUFSIZE * sizeof(pagebuf_trace_t), GFP_KERNEL);
	memset(pb_trace.buf, 0, PB_TRACE_BUFSIZE * sizeof(pagebuf_trace_t));
	pb_trace.start = 0;
	pb_trace.end = PB_TRACE_BUFSIZE - 1;
#endif

	pagebuf_daemon_start();
	return 0;
}


/*
 *	pagebuf_terminate.
 *
 *	Note: do not mark as __exit, this is also called from the __init code.
 */
void
pagebuf_terminate(void)
{
	pagebuf_daemon_stop();

	kmem_cache_destroy(pagebuf_cache);

	unregister_sysctl_table(pagebuf_table_header);
#ifdef  CONFIG_PROC_FS
	remove_proc_entry("fs/pagebuf/stat", NULL);
	remove_proc_entry("fs/pagebuf", NULL);
#endif
}


/*
 *	Module management (for kernel debugger module)
 */
EXPORT_SYMBOL(pagebuf_offset);
