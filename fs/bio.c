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
#include <linux/blk.h>
#include <linux/slab.h>
#include <linux/iobuf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mempool.h>

#define BIO_POOL_SIZE 256

static mempool_t *bio_pool;
static kmem_cache_t *bio_slab;

#define BIOVEC_NR_POOLS 6

struct biovec_pool {
	int size;
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
	BV(1), BV(4), BV(16), BV(64), BV(128), BV(256)
}; 
#undef BV

#define BIO_MAX_PAGES	(bvec_array[BIOVEC_NR_POOLS - 1].size)

static void *slab_pool_alloc(int gfp_mask, void *data)
{
	return kmem_cache_alloc(data, gfp_mask);
}

static void slab_pool_free(void *ptr, void *data)
{
	kmem_cache_free(data, ptr);
}

static inline struct bio_vec *bvec_alloc(int gfp_mask, int nr, int *idx)
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
		case 129 ... 256: *idx = 5; break;
		default:
			return NULL;
	}
	/*
	 * idx now points to the pool we want to allocate from
	 */
	bp = bvec_array + *idx;

	bvl = mempool_alloc(bp->pool, gfp_mask);
	if (bvl)
		memset(bvl, 0, bp->size);
	return bvl;
}

/*
 * default destructor for a bio allocated with bio_alloc()
 */
void bio_destructor(struct bio *bio)
{
	struct biovec_pool *bp = bvec_array + bio->bi_max;

	BIO_BUG_ON(bio->bi_max >= BIOVEC_NR_POOLS);
	/*
	 * cloned bio doesn't own the veclist
	 */
	if (!(bio->bi_flags & (1 << BIO_CLONED)))
		mempool_free(bio->bi_io_vec, bp->pool);

	mempool_free(bio, bio_pool);
}

inline void bio_init(struct bio *bio)
{
	bio->bi_next = NULL;
	bio->bi_flags = 0;
	bio->bi_rw = 0;
	bio->bi_vcnt = 0;
	bio->bi_idx = 0;
	bio->bi_phys_segments = 0;
	bio->bi_hw_segments = 0;
	bio->bi_size = 0;
	bio->bi_end_io = NULL;
	atomic_set(&bio->bi_cnt, 1);
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
	struct bio *bio = mempool_alloc(bio_pool, gfp_mask);
	struct bio_vec *bvl = NULL;

	if (unlikely(!bio))
		return NULL;

	if (!nr_iovecs || (bvl = bvec_alloc(gfp_mask,nr_iovecs,&bio->bi_max))) {
		bio_init(bio);
		bio->bi_destructor = bio_destructor;
		bio->bi_io_vec = bvl;
		return bio;
	}

	mempool_free(bio, bio_pool);
	return NULL;
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
	if (unlikely(!(bio->bi_flags & (1 << BIO_SEG_VALID))))
		blk_recount_segments(q, bio);

	return bio->bi_phys_segments;
}

inline int bio_hw_segments(request_queue_t *q, struct bio *bio)
{
	if (unlikely(!(bio->bi_flags & (1 << BIO_SEG_VALID))))
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
	bio->bi_dev = bio_src->bi_dev;
	bio->bi_flags |= 1 << BIO_CLONED;
	bio->bi_rw = bio_src->bi_rw;

	/*
	 * notes -- maybe just leave bi_idx alone. bi_max has no use
	 * on a cloned bio. assume identical mapping for the clone
	 */
	bio->bi_vcnt = bio_src->bi_vcnt;
	bio->bi_idx = bio_src->bi_idx;
	if (bio_src->bi_flags & (1 << BIO_SEG_VALID)) {
		bio->bi_phys_segments = bio_src->bi_phys_segments;
		bio->bi_hw_segments = bio_src->bi_hw_segments;
		bio->bi_flags |= (1 << BIO_SEG_VALID);
	}
	bio->bi_size = bio_src->bi_size;
	bio->bi_max = bio_src->bi_max;
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
			vfrom = kmap_atomic(bv->bv_page, KM_BIO_IRQ);
			vto = kmap_atomic(bbv->bv_page, KM_BIO_IRQ);
		}

		memcpy(vto + bbv->bv_offset, vfrom + bv->bv_offset, bv->bv_len);
		if (gfp_mask & __GFP_WAIT) {
			kunmap(bbv->bv_page);
			kunmap(bv->bv_page);
		} else {
			kunmap_atomic(vto, KM_BIO_IRQ);
			kunmap_atomic(vfrom, KM_BIO_IRQ);
			local_irq_restore(flags);
		}
	}

	b->bi_sector = bio->bi_sector;
	b->bi_dev = bio->bi_dev;
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

static int bio_end_io_kio(struct bio *bio, int nr_sectors)
{
	struct kiobuf *kio = (struct kiobuf *) bio->bi_private;

	end_kio_request(kio, test_bit(BIO_UPTODATE, &bio->bi_flags));
	bio_put(bio);
	return 0;
}

/**
 * ll_rw_kio - submit a &struct kiobuf for I/O
 * @rw:   %READ or %WRITE
 * @kio:   the kiobuf to do I/O on
 * @dev:   target device
 * @sector:   start location on disk
 *
 * Description:
 *   ll_rw_kio will map the page list inside the &struct kiobuf to
 *   &struct bio and queue them for I/O. The kiobuf given must describe
 *   a continous range of data, and must be fully prepared for I/O.
 **/
void ll_rw_kio(int rw, struct kiobuf *kio, kdev_t dev, sector_t sector)
{
	int i, offset, size, err, map_i, total_nr_pages, nr_pages;
	struct bio_vec *bvec;
	struct bio *bio;

	err = 0;
	if ((rw & WRITE) && is_read_only(dev)) {
		printk("ll_rw_bio: WRITE to ro device %s\n", kdevname(dev));
		err = -EPERM;
		goto out;
	}

	if (!kio->nr_pages) {
		err = -EINVAL;
		goto out;
	}

	/*
	 * maybe kio is bigger than the max we can easily map into a bio.
	 * if so, split it up in appropriately sized chunks.
	 */
	total_nr_pages = kio->nr_pages;
	offset = kio->offset & ~PAGE_MASK;
	size = kio->length;

	atomic_set(&kio->io_count, 1);

	map_i = 0;

next_chunk:
	nr_pages = BIO_MAX_SECTORS >> (PAGE_SHIFT - 9);
	if (nr_pages > total_nr_pages)
		nr_pages = total_nr_pages;

	atomic_inc(&kio->io_count);

	/*
	 * allocate bio and do initial setup
	 */
	if ((bio = bio_alloc(GFP_NOIO, nr_pages)) == NULL) {
		err = -ENOMEM;
		goto out;
	}

	bio->bi_sector = sector;
	bio->bi_dev = dev;
	bio->bi_idx = 0;
	bio->bi_end_io = bio_end_io_kio;
	bio->bi_private = kio;

	bvec = bio->bi_io_vec;
	for (i = 0; i < nr_pages; i++, bvec++, map_i++) {
		int nbytes = PAGE_SIZE - offset;

		if (nbytes > size)
			nbytes = size;

		BUG_ON(kio->maplist[map_i] == NULL);

		if (bio->bi_size + nbytes > (BIO_MAX_SECTORS << 9))
			goto queue_io;

		bio->bi_vcnt++;
		bio->bi_size += nbytes;

		bvec->bv_page = kio->maplist[map_i];
		bvec->bv_len = nbytes;
		bvec->bv_offset = offset;

		/*
		 * kiobuf only has an offset into the first page
		 */
		offset = 0;

		sector += nbytes >> 9;
		size -= nbytes;
		total_nr_pages--;
		kio->offset += nbytes;
	}

queue_io:
	submit_bio(rw, bio);

	if (total_nr_pages)
		goto next_chunk;

	if (size) {
		printk("ll_rw_kio: size %d left (kio %d)\n", size, kio->length);
		BUG();
	}

out:
	if (err)
		kio->errno = err;

	/*
	 * final atomic_dec of io_count to match our initial setting of 1.
	 * I/O may or may not have completed at this point, final completion
	 * handler is only run on last decrement.
	 */
	end_kio_request(kio, !err);
}

int bio_endio(struct bio *bio, int uptodate, int nr_sectors)
{
	if (uptodate)
		set_bit(BIO_UPTODATE, &bio->bi_flags);
	else
		clear_bit(BIO_UPTODATE, &bio->bi_flags);

	if (bio->bi_end_io)
		return bio->bi_end_io(bio, nr_sectors);

	return 0;
}

static void __init biovec_init_pool(void)
{
	int i, size;

	for (i = 0; i < BIOVEC_NR_POOLS; i++) {
		struct biovec_pool *bp = bvec_array + i;

		size = bp->size * sizeof(struct bio_vec);

		printk("biovec: init pool %d, %d entries, %d bytes\n", i,
						bp->size, size);

		bp->slab = kmem_cache_create(bp->name, size, 0,
						SLAB_HWCACHE_ALIGN, NULL, NULL);
		if (!bp->slab)
			panic("biovec: can't init slab cache\n");
		bp->pool = mempool_create(BIO_POOL_SIZE, slab_pool_alloc,
					slab_pool_free, bp->slab);
		if (!bp->pool)
			panic("biovec: can't init mempool\n");
		bp->size = size;	
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

	biovec_init_pool();

	return 0;
}

module_init(init_bio);

EXPORT_SYMBOL(bio_alloc);
EXPORT_SYMBOL(bio_put);
EXPORT_SYMBOL(ll_rw_kio);
EXPORT_SYMBOL(bio_endio);
EXPORT_SYMBOL(bio_init);
EXPORT_SYMBOL(bio_copy);
EXPORT_SYMBOL(__bio_clone);
EXPORT_SYMBOL(bio_clone);
EXPORT_SYMBOL(bio_phys_segments);
EXPORT_SYMBOL(bio_hw_segments);
