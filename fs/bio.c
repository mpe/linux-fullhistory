/*
 * Copyright (C) 2001 Jens Axboe <axboe@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of

 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public Licens
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-
 *
 */
#include <linux/mm.h>
#include <linux/bio.h>
#include <linux/blk.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mempool.h>

#define BIO_POOL_SIZE 256

static mempool_t *bio_pool;
static kmem_cache_t *bio_slab;

#define BIOVEC_NR_POOLS 6

struct biovec_pool {
	int nr_vecs;
	char *name; 
	kmem_cache_t *slab;
	mempool_t *pool;
};

/*
 * if you change this list, also change bvec_alloc or things will
 * break badly! cannot be bigger than what you can fit into an
 * unsigned short
 */

#define BV(x) { x, "biovec-" #x }
static struct biovec_pool bvec_array[BIOVEC_NR_POOLS] = {
	BV(1), BV(4), BV(16), BV(64), BV(128), BV(BIO_MAX_PAGES),
};
#undef BV

static void *slab_pool_alloc(int gfp_mask, void *data)
{
	return kmem_cache_alloc(data, gfp_mask);
}

static void slab_pool_free(void *ptr, void *data)
{
	kmem_cache_free(data, ptr);
}

static inline struct bio_vec *bvec_alloc(int gfp_mask, int nr, unsigned long *idx)
{
	struct biovec_pool *bp;
	struct bio_vec *bvl;

	/*
	 * see comment near bvec_array define!
	 */
	switch (nr) {
		case   1        : *idx = 0; break;
		case   2 ...   4: *idx = 1; break;
		case   5 ...  16: *idx = 2; break;
		case  17 ...  64: *idx = 3; break;
		case  65 ... 128: *idx = 4; break;
		case 129 ... BIO_MAX_PAGES: *idx = 5; break;
		default:
			return NULL;
	}
	/*
	 * idx now points to the pool we want to allocate from
	 */
	bp = bvec_array + *idx;

	bvl = mempool_alloc(bp->pool, gfp_mask);
	if (bvl)
		memset(bvl, 0, bp->nr_vecs * sizeof(struct bio_vec));
	return bvl;
}

/*
 * default destructor for a bio allocated with bio_alloc()
 */
void bio_destructor(struct bio *bio)
{
	const int pool_idx = BIO_POOL_IDX(bio);
	struct biovec_pool *bp = bvec_array + pool_idx;

	BIO_BUG_ON(pool_idx >= BIOVEC_NR_POOLS);

	/*
	 * cloned bio doesn't own the veclist
	 */
	if (!bio_flagged(bio, BIO_CLONED))
		mempool_free(bio->bi_io_vec, bp->pool);

	mempool_free(bio, bio_pool);
}

inline void bio_init(struct bio *bio)
{
	bio->bi_next = NULL;
	bio->bi_flags = 1 << BIO_UPTODATE;
	bio->bi_rw = 0;
	bio->bi_vcnt = 0;
	bio->bi_idx = 0;
	bio->bi_phys_segments = 0;
	bio->bi_hw_segments = 0;
	bio->bi_size = 0;
	bio->bi_max_vecs = 0;
	bio->bi_end_io = NULL;
	atomic_set(&bio->bi_cnt, 1);
	bio->bi_private = NULL;
}

/**
 * bio_alloc - allocate a bio for I/O
 * @gfp_mask:   the GFP_ mask given to the slab allocator
 * @nr_iovecs:	number of iovecs to pre-allocate
 *
 * Description:
 *   bio_alloc will first try it's on mempool to satisfy the allocation.
 *   If %__GFP_WAIT is set then we will block on the internal pool waiting
 *   for a &struct bio to become free.
 **/
struct bio *bio_alloc(int gfp_mask, int nr_iovecs)
{
	int pf_flags = current->flags;
	struct bio_vec *bvl = NULL;
	unsigned long idx;
	struct bio *bio;

	current->flags |= PF_NOWARN;
	bio = mempool_alloc(bio_pool, gfp_mask);
	if (unlikely(!bio))
		goto out;

	bio_init(bio);

	if (unlikely(!nr_iovecs))
		goto noiovec;

	bvl = bvec_alloc(gfp_mask, nr_iovecs, &idx);
	if (bvl) {
		bio->bi_flags |= idx << BIO_POOL_OFFSET;
		bio->bi_max_vecs = bvec_array[idx].nr_vecs;
noiovec:
		bio->bi_io_vec = bvl;
		bio->bi_destructor = bio_destructor;
out:
		current->flags = pf_flags;
		return bio;
	}

	mempool_free(bio, bio_pool);
	bio = NULL;
	goto out;
}

/**
 * bio_put - release a reference to a bio
 * @bio:   bio to release reference to
 *
 * Description:
 *   Put a reference to a &struct bio, either one you have gotten with
 *   bio_alloc or bio_get. The last put of a bio will free it.
 **/
void bio_put(struct bio *bio)
{
	BIO_BUG_ON(!atomic_read(&bio->bi_cnt));

	/*
	 * last put frees it
	 */
	if (atomic_dec_and_test(&bio->bi_cnt)) {
		bio->bi_next = NULL;
		bio->bi_destructor(bio);
	}
}

inline int bio_phys_segments(request_queue_t *q, struct bio *bio)
{
	if (unlikely(!bio_flagged(bio, BIO_SEG_VALID)))
		blk_recount_segments(q, bio);

	return bio->bi_phys_segments;
}

inline int bio_hw_segments(request_queue_t *q, struct bio *bio)
{
	if (unlikely(!bio_flagged(bio, BIO_SEG_VALID)))
		blk_recount_segments(q, bio);

	return bio->bi_hw_segments;
}

/**
 * 	__bio_clone	-	clone a bio
 * 	@bio: destination bio
 * 	@bio_src: bio to clone
 *
 *	Clone a &bio. Caller will own the returned bio, but not
 *	the actual data it points to. Reference count of returned
 * 	bio will be one.
 */
inline void __bio_clone(struct bio *bio, struct bio *bio_src)
{
	bio->bi_io_vec = bio_src->bi_io_vec;

	bio->bi_sector = bio_src->bi_sector;
	bio->bi_bdev = bio_src->bi_bdev;
	bio->bi_flags |= 1 << BIO_CLONED;
	bio->bi_rw = bio_src->bi_rw;

	/*
	 * notes -- maybe just leave bi_idx alone. assume identical mapping
	 * for the clone
	 */
	bio->bi_vcnt = bio_src->bi_vcnt;
	bio->bi_idx = bio_src->bi_idx;
	if (bio_flagged(bio, BIO_SEG_VALID)) {
		bio->bi_phys_segments = bio_src->bi_phys_segments;
		bio->bi_hw_segments = bio_src->bi_hw_segments;
		bio->bi_flags |= (1 << BIO_SEG_VALID);
	}
	bio->bi_size = bio_src->bi_size;

	/*
	 * cloned bio does not own the bio_vec, so users cannot fiddle with
	 * it. clear bi_max_vecs and clear the BIO_POOL_BITS to make this
	 * apparent
	 */
	bio->bi_max_vecs = 0;
	bio->bi_flags &= (BIO_POOL_MASK - 1);
}

/**
 *	bio_clone	-	clone a bio
 *	@bio: bio to clone
 *	@gfp_mask: allocation priority
 *
 * 	Like __bio_clone, only also allocates the returned bio
 */
struct bio *bio_clone(struct bio *bio, int gfp_mask)
{
	struct bio *b = bio_alloc(gfp_mask, 0);

	if (b)
		__bio_clone(b, bio);

	return b;
}

/**
 *	bio_copy	-	create copy of a bio
 *	@bio: bio to copy
 *	@gfp_mask: allocation priority
 *	@copy: copy data to allocated bio
 *
 *	Create a copy of a &bio. Caller will own the returned bio and
 *	the actual data it points to. Reference count of returned
 * 	bio will be one.
 */
struct bio *bio_copy(struct bio *bio, int gfp_mask, int copy)
{
	struct bio *b = bio_alloc(gfp_mask, bio->bi_vcnt);
	unsigned long flags = 0; /* gcc silly */
	struct bio_vec *bv;
	int i;

	if (unlikely(!b))
		return NULL;

	/*
	 * iterate iovec list and alloc pages + copy data
	 */
	__bio_for_each_segment(bv, bio, i, 0) {
		struct bio_vec *bbv = &b->bi_io_vec[i];
		char *vfrom, *vto;

		bbv->bv_page = alloc_page(gfp_mask);
		if (bbv->bv_page == NULL)
			goto oom;

		bbv->bv_len = bv->bv_len;
		bbv->bv_offset = bv->bv_offset;

		/*
		 * if doing a copy for a READ request, no need
		 * to memcpy page data
		 */
		if (!copy)
			continue;

		if (gfp_mask & __GFP_WAIT) {
			vfrom = kmap(bv->bv_page);
			vto = kmap(bbv->bv_page);
		} else {
			local_irq_save(flags);
			vfrom = kmap_atomic(bv->bv_page, KM_BIO_SRC_IRQ);
			vto = kmap_atomic(bbv->bv_page, KM_BIO_DST_IRQ);
		}

		memcpy(vto + bbv->bv_offset, vfrom + bv->bv_offset, bv->bv_len);
		if (gfp_mask & __GFP_WAIT) {
			kunmap(bbv->bv_page);
			kunmap(bv->bv_page);
		} else {
			kunmap_atomic(vto, KM_BIO_DST_IRQ);
			kunmap_atomic(vfrom, KM_BIO_SRC_IRQ);
			local_irq_restore(flags);
		}
	}

	b->bi_sector = bio->bi_sector;
	b->bi_bdev = bio->bi_bdev;
	b->bi_rw = bio->bi_rw;

	b->bi_vcnt = bio->bi_vcnt;
	b->bi_size = bio->bi_size;

	return b;

oom:
	while (--i >= 0)
		__free_page(b->bi_io_vec[i].bv_page);

	mempool_free(b, bio_pool);
	return NULL;
}

/**
 *	bio_get_nr_vecs		- return approx number of vecs
 *	@bdev:  I/O target
 *
 *	Return the approximate number of pages we can send to this target.
 *	There's no guarentee that you will be able to fit this number of pages
 *	into a bio, it does not account for dynamic restrictions that vary
 *	on offset.
 */
int bio_get_nr_vecs(struct block_device *bdev)
{
	request_queue_t *q = bdev_get_queue(bdev);
	int nr_pages;

	nr_pages = ((q->max_sectors << 9) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (nr_pages > q->max_phys_segments)
		nr_pages = q->max_phys_segments;
	if (nr_pages > q->max_hw_segments)
		nr_pages = q->max_hw_segments;

	return nr_pages;
}

/**
 *	bio_add_page	-	attempt to add page to bio
 *	@bio: destination bio
 *	@page: page to add
 *	@len: vec entry length
 *	@offset: vec entry offset
 *
 *	Attempt to add a page to the bio_vec maplist. This can fail for a
 *	number of reasons, such as the bio being full or target block
 *	device limitations.
 */
int bio_add_page(struct bio *bio, struct page *page, unsigned int len,
		 unsigned int offset)
{
	request_queue_t *q = bdev_get_queue(bio->bi_bdev);
	int fail_segments = 0, retried_segments = 0;
	struct bio_vec *bvec;

	/*
	 * cloned bio must not modify vec list
	 */
	if (unlikely(bio_flagged(bio, BIO_CLONED)))
		return 0;

	if (bio->bi_vcnt >= bio->bi_max_vecs)
		return 0;

	if (((bio->bi_size + len) >> 9) > q->max_sectors)
		return 0;

	/*
	 * we might loose a segment or two here, but rather that than
	 * make this too complex.
	 */
retry_segments:
	if (bio_phys_segments(q, bio) >= q->max_phys_segments
	    || bio_hw_segments(q, bio) >= q->max_hw_segments)
		fail_segments = 1;

	if (fail_segments) {
		if (retried_segments)
			return 0;

		bio->bi_flags &= ~(1 << BIO_SEG_VALID);
		retried_segments = 1;
		goto retry_segments;
	}

	/*
	 * setup the new entry, we might clear it again later if we
	 * cannot add the page
	 */
	bvec = &bio->bi_io_vec[bio->bi_vcnt];
	bvec->bv_page = page;
	bvec->bv_len = len;
	bvec->bv_offset = offset;

	/*
	 * if queue has other restrictions (eg varying max sector size
	 * depending on offset), it can specify a merge_bvec_fn in the
	 * queue to get further control
	 */
	if (q->merge_bvec_fn) {
		/*
		 * merge_bvec_fn() returns number of bytes it can accept
		 * at this offset
		 */
		if (q->merge_bvec_fn(q, bio, bvec) < len) {
			bvec->bv_page = NULL;
			bvec->bv_len = 0;
			bvec->bv_offset = 0;
			return 0;
		}
	}

	bio->bi_vcnt++;
	bio->bi_phys_segments++;
	bio->bi_hw_segments++;
	bio->bi_size += len;
	return len;
}

/**
 *	bio_map_user	-	map user address into bio
 *	@bdev: destination block device
 *	@uaddr: start of user address
 *	@len: length in bytes
 *	@write_to_vm: bool indicating writing to pages or not
 *
 *	Map the user space address into a bio suitable for io to a block
 *	device. Caller should check the size of the returned bio, we might
 *	not have mapped the entire range specified.
 */
struct bio *bio_map_user(struct block_device *bdev, unsigned long uaddr,
			 unsigned int len, int write_to_vm)
{
	unsigned long end = (uaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long start = uaddr >> PAGE_SHIFT;
	const int nr_pages = end - start;
	request_queue_t *q = bdev_get_queue(bdev);
	int ret, offset, i;
	struct page **pages;
	struct bio *bio;

	/*
	 * transfer and buffer must be aligned to at least hardsector
	 * size for now, in the future we can relax this restriction
	 */
	if ((uaddr & queue_dma_alignment(q)) || (len & queue_dma_alignment(q)))
		return NULL;

	bio = bio_alloc(GFP_KERNEL, nr_pages);
	if (!bio)
		return NULL;

	pages = kmalloc(nr_pages * sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		goto out;

	down_read(&current->mm->mmap_sem);
	ret = get_user_pages(current, current->mm, uaddr, nr_pages,
						write_to_vm, 0, pages, NULL);
	up_read(&current->mm->mmap_sem);

	if (ret < nr_pages)
		goto out;

	bio->bi_bdev = bdev;

	offset = uaddr & ~PAGE_MASK;
	for (i = 0; i < nr_pages; i++) {
		unsigned int bytes = PAGE_SIZE - offset;

		if (len <= 0)
			break;

		if (bytes > len)
			bytes = len;

		/*
		 * sorry...
		 */
		if (bio_add_page(bio, pages[i], bytes, offset) < bytes)
			break;

		len -= bytes;
		offset = 0;
	}

	/*
	 * release the pages we didn't map into the bio, if any
	 */
	while (i < nr_pages)
		page_cache_release(pages[i++]);

	kfree(pages);

	/*
	 * check if the mapped pages need bouncing for an isa host.
	 */
	blk_queue_bounce(q, &bio);
	return bio;
out:
	kfree(pages);
	bio_put(bio);
	return NULL;
}

/**
 *	bio_unmap_user	-	unmap a bio
 *	@bio:		the bio being unmapped
 *	@write_to_vm:	bool indicating whether pages were written to
 *
 *	Unmap a bio previously mapped by bio_map_user(). The @write_to_vm
 *	must be the same as passed into bio_map_user(). Must be called with
 *	a process context.
 */
void bio_unmap_user(struct bio *bio, int write_to_vm)
{
	struct bio_vec *bvec;
	int i;

	/*
	 * find original bio if it was bounced
	 */
	if (bio->bi_private) {
		/*
		 * someone stole our bio, must not happen
		 */
		BUG_ON(!bio_flagged(bio, BIO_BOUNCED));
	
		bio = bio->bi_private;
	}

	/*
	 * make sure we dirty pages we wrote to
	 */
	__bio_for_each_segment(bvec, bio, i, 0) {
		if (write_to_vm)
			set_page_dirty(bvec->bv_page);

		page_cache_release(bvec->bv_page);
	}

	bio_put(bio);
 }

/**
 * bio_endio - end I/O on a bio
 * @bio:	bio
 * @bytes_done:	number of bytes completed
 * @error:	error, if any
 *
 * Description:
 *   bio_endio() will end I/O on @bytes_done number of bytes. This may be
 *   just a partial part of the bio, or it may be the whole bio. bio_endio()
 *   is the preferred way to end I/O on a bio, it takes care of decrementing
 *   bi_size and clearing BIO_UPTODATE on error. @error is 0 on success, and
 *   and one of the established -Exxxx (-EIO, for instance) error values in
 *   case something went wrong. Noone should call bi_end_io() directly on
 *   a bio unless they own it and thus know that it has an end_io function.
 **/
void bio_endio(struct bio *bio, unsigned int bytes_done, int error)
{
	if (error)
		clear_bit(BIO_UPTODATE, &bio->bi_flags);

	if (unlikely(bytes_done > bio->bi_size)) {
		printk("%s: want %u bytes done, only %u left\n", __FUNCTION__,
						bytes_done, bio->bi_size);
		bytes_done = bio->bi_size;
	}

	bio->bi_size -= bytes_done;

	if (bio->bi_end_io)
		bio->bi_end_io(bio, bytes_done, error);
}

static void __init biovec_init_pools(void)
{
	int i, size, megabytes, pool_entries = BIO_POOL_SIZE;
	int scale = BIOVEC_NR_POOLS;

	megabytes = nr_free_pages() >> (20 - PAGE_SHIFT);

	/*
	 * find out where to start scaling
	 */
	if (megabytes <= 16)
		scale = 0;
	else if (megabytes <= 32)
		scale = 1;
	else if (megabytes <= 64)
		scale = 2;
	else if (megabytes <= 96)
		scale = 3;
	else if (megabytes <= 128)
		scale = 4;

	/*
	 * scale number of entries
	 */
	pool_entries = megabytes * 2;
	if (pool_entries > 256)
		pool_entries = 256;

	for (i = 0; i < BIOVEC_NR_POOLS; i++) {
		struct biovec_pool *bp = bvec_array + i;

		size = bp->nr_vecs * sizeof(struct bio_vec);

		bp->slab = kmem_cache_create(bp->name, size, 0,
						SLAB_HWCACHE_ALIGN, NULL, NULL);
		if (!bp->slab)
			panic("biovec: can't init slab cache\n");

		if (i >= scale)
			pool_entries >>= 1;

		bp->pool = mempool_create(pool_entries, slab_pool_alloc,
					slab_pool_free, bp->slab);
		if (!bp->pool)
			panic("biovec: can't init mempool\n");

		printk("biovec pool[%d]: %3d bvecs: %3d entries (%d bytes)\n",
						i, bp->nr_vecs, pool_entries,
						size);
	}
}

static int __init init_bio(void)
{
	bio_slab = kmem_cache_create("bio", sizeof(struct bio), 0,
					SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!bio_slab)
		panic("bio: can't create slab cache\n");
	bio_pool = mempool_create(BIO_POOL_SIZE, slab_pool_alloc, slab_pool_free, bio_slab);
	if (!bio_pool)
		panic("bio: can't create mempool\n");

	printk("BIO: pool of %d setup, %ZuKb (%Zd bytes/bio)\n", BIO_POOL_SIZE, BIO_POOL_SIZE * sizeof(struct bio) >> 10, sizeof(struct bio));

	biovec_init_pools();

	return 0;
}

subsys_initcall(init_bio);

EXPORT_SYMBOL(bio_alloc);
EXPORT_SYMBOL(bio_put);
EXPORT_SYMBOL(bio_endio);
EXPORT_SYMBOL(bio_init);
EXPORT_SYMBOL(bio_copy);
EXPORT_SYMBOL(__bio_clone);
EXPORT_SYMBOL(bio_clone);
EXPORT_SYMBOL(bio_phys_segments);
EXPORT_SYMBOL(bio_hw_segments);
EXPORT_SYMBOL(bio_add_page);
EXPORT_SYMBOL(bio_get_nr_vecs);
EXPORT_SYMBOL(bio_map_user);
EXPORT_SYMBOL(bio_unmap_user);
