/*
 * linux/mm/slab.c
 * Written by Mark Hemment, 1996/97.
 * (markhe@nextd.demon.co.uk)
 *
 * 11 April '97.  Started multi-threading - markhe
 *	The global cache-chain is protected by the semaphore 'cache_chain_sem'.
 *	The sem is only needed when accessing/extending the cache-chain, which
 *	can never happen inside an interrupt (kmem_cache_create(),
 *	kmem_cache_shrink() and kmem_cache_reap()).
 *	This is a medium-term exclusion lock.
 *
 *	Each cache has its own lock; 'c_spinlock'.  This lock is needed only
 *	when accessing non-constant members of a cache-struct.
 *	Note: 'constant members' are assigned a value in kmem_cache_create() before
 *	the cache is linked into the cache-chain.  The values never change, so not
 *	even a multi-reader lock is needed for these members.
 *	The c_spinlock is only ever held for a few cycles.
 *
 *	To prevent kmem_cache_shrink() trying to shrink a 'growing' cache (which
 *	maybe be sleeping and therefore not holding the semaphore/lock), the
 *	c_growing field is used.  This also prevents reaping from a cache.
 *
 *	Note, caches can _never_ be destroyed.  When a sub-system (eg module) has
 *	finished with a cache, it can only be shrunk.  This leaves the cache empty,
 *	but already enabled for re-use, eg. during a module re-load.
 *
 *	Notes:
 *		o Constructors/deconstructors are called while the cache-lock
 *		  is _not_ held.  Therefore they _must_ be threaded.
 *		o Constructors must not attempt to allocate memory from the
 *		  same cache that they are a constructor for - infinite loop!
 *		  (There is no easy way to trap this.)
 *		o The per-cache locks must be obtained with local-interrupts disabled.
 *		o When compiled with debug support, and an object-verify (upon release)
 *		  is request for a cache, the verify-function is called with the cache
 *		  lock held.  This helps debugging.
 *		o The functions called from try_to_free_page() must not attempt
 *		  to allocate memory from a cache which is being grown.
 *		  The buffer sub-system might try to allocate memory, via buffer_cachep.
 *		  As this pri is passed to the SLAB, and then (if necessary) onto the
 *		  gfp() funcs (which avoid calling try_to_free_page()), no deadlock
 *		  should happen.
 *
 *	The positioning of the per-cache lock is tricky.  If the lock is
 *	placed on the same h/w cache line as commonly accessed members
 *	the number of L1 cache-line faults is reduced.  However, this can
 *	lead to the cache-line ping-ponging between processors when the
 *	lock is in contention (and the common members are being accessed).
 *	Decided to keep it away from common members.
 *
 *	More fine-graining is possible, with per-slab locks...but this might be
 *	taking fine graining too far, but would have the advantage;
 *		During most allocs/frees no writes occur to the cache-struct.
 *		Therefore a multi-reader/one writer lock could be used (the writer
 *		needed when the slab chain is being link/unlinked).
 *		As we would not have an exclusion lock for the cache-structure, one
 *		would be needed per-slab (for updating s_free ptr, and/or the contents
 *		of s_index).
 *	The above locking would allow parallel operations to different slabs within
 *	the same cache with reduced spinning.
 *
 *	Per-engine slab caches, backed by a global cache (as in Mach's Zone allocator),
 *	would allow most allocations from the same cache to execute in parallel.
 *
 *	At present, each engine can be growing a cache.  This should be blocked.
 *
 *	It is not currently 100% safe to examine the page_struct outside of a kernel
 *	or global cli lock.  The risk is v. small, and non-fatal.
 *
 *	Calls to printk() are not 100% safe (the function is not threaded).  However,
 *	printk() is only used under an error condition, and the risk is v. small (not
 *	sure if the console write functions 'enjoy' executing multiple contexts in
 *	parallel.  I guess they don't...).
 *	Note, for most calls to printk() any held cache-lock is dropped.  This is not
 *	always done for text size reasons - having *_unlock() everywhere is bloat.
 */

/*
 * An implementation of the Slab Allocator as described in outline in;
 *	UNIX Internals: The New Frontiers by Uresh Vahalia
 *	Pub: Prentice Hall	ISBN 0-13-101908-2
 * or with a little more detail in;
 *	The Slab Allocator: An Object-Caching Kernel Memory Allocator
 *	Jeff Bonwick (Sun Microsystems).
 *	Presented at: USENIX Summer 1994 Technical Conference
 */

/*
 * This implementation deviates from Bonwick's paper as it
 * does not use a hash-table for large objects, but rather a per slab
 * index to hold the bufctls.  This allows the bufctl structure to
 * be small (one word), but limits the number of objects a slab (not
 * a cache) can contain when off-slab bufctls are used.  The limit is the
 * size of the largest general cache that does not use off-slab bufctls,
 * divided by the size of a bufctl.  For 32bit archs, is this 256/4 = 64.
 * This is not serious, as it is only for large objects, when it is unwise
 * to have too many per slab.
 * Note: This limit can be raised by introducing a general cache whose size
 * is less than 512 (PAGE_SIZE<<3), but greater than 256.
 */

#include	<linux/config.h>
#include	<linux/slab.h>
#include	<linux/interrupt.h>
#include	<linux/init.h>

/* If there is a different PAGE_SIZE around, and it works with this allocator,
 * then change the following.
 */
#if	(PAGE_SIZE != 8192 && PAGE_SIZE != 4096)
#error	Your page size is probably not correctly supported - please check
#endif

/* SLAB_MGMT_CHECKS	- 1 to enable extra checks in kmem_cache_create().
 *			  0 if you wish to reduce memory usage.
 *
 * SLAB_DEBUG_SUPPORT	- 1 for kmem_cache_create() to honour; SLAB_DEBUG_FREE,
 *			  SLAB_DEBUG_INITIAL, SLAB_RED_ZONE & SLAB_POISON.
 *			  0 for faster, smaller, code (especially in the critical paths).
 *
 * SLAB_STATS		- 1 to collect stats for /proc/slabinfo.
 *			  0 for faster, smaller, code (especially in the critical paths).
 *
 * SLAB_SELFTEST	- 1 to perform a few tests, mainly for development.
 */
#define		SLAB_MGMT_CHECKS	1
#define		SLAB_DEBUG_SUPPORT	0
#define		SLAB_STATS		0
#define		SLAB_SELFTEST		0

/* Shouldn't this be in a header file somewhere? */
#define	BYTES_PER_WORD		sizeof(void *)

/* Legal flag mask for kmem_cache_create(). */
#if	SLAB_DEBUG_SUPPORT
#if	0
#define	SLAB_C_MASK		(SLAB_DEBUG_FREE|SLAB_DEBUG_INITIAL|SLAB_RED_ZONE| \
				 SLAB_POISON|SLAB_HWCACHE_ALIGN|SLAB_NO_REAP| \
				 SLAB_HIGH_PACK)
#endif
#define	SLAB_C_MASK		(SLAB_DEBUG_FREE|SLAB_DEBUG_INITIAL|SLAB_RED_ZONE| \
				 SLAB_POISON|SLAB_HWCACHE_ALIGN|SLAB_NO_REAP)
#else
#if	0
#define	SLAB_C_MASK		(SLAB_HWCACHE_ALIGN|SLAB_NO_REAP|SLAB_HIGH_PACK)
#endif
#define	SLAB_C_MASK		(SLAB_HWCACHE_ALIGN|SLAB_NO_REAP)
#endif	/* SLAB_DEBUG_SUPPORT */

/* Slab management struct.
 * Manages the objs in a slab.  Placed either at the end of mem allocated
 * for a slab, or from an internal obj cache (cache_slabp).
 * Slabs are chained into a partially ordered list; fully used first, partial
 * next, and then fully free slabs.
 * The first 4 members are referenced during an alloc/free operation, and
 * should always appear on the same cache line.
 * Note: The offset between some members _must_ match offsets within
 * the kmem_cache_t - see kmem_cache_init() for the checks. */

#define	SLAB_OFFSET_BITS	16	/* could make this larger for 64bit archs */

typedef struct kmem_slab_s {
	struct kmem_bufctl_s	*s_freep;  /* ptr to first inactive obj in slab */
	struct kmem_bufctl_s	*s_index;
	unsigned long		 s_magic;
	unsigned long		 s_inuse;  /* num of objs active in slab */

	struct kmem_slab_s	*s_nextp;
	struct kmem_slab_s	*s_prevp;
	void			*s_mem;	   /* addr of first obj in slab */
	unsigned long		 s_offset:SLAB_OFFSET_BITS,
				 s_dma:1;
} kmem_slab_t;

/* When the slab management is on-slab, this gives the size to use. */
#define	slab_align_size		(L1_CACHE_ALIGN(sizeof(kmem_slab_t)))

/* Test for end of slab chain. */
#define	kmem_slab_end(x)	((kmem_slab_t*)&((x)->c_offset))

/* s_magic */
#define	SLAB_MAGIC_ALLOC	0xA5C32F2BUL	/* slab is alive */
#define	SLAB_MAGIC_DESTROYED	0xB2F23C5AUL	/* slab has been destroyed */

/* Bufctl's are used for linking objs within a slab, identifying what slab an obj
 * is in, and the address of the associated obj (for sanity checking with off-slab
 * bufctls).  What a bufctl contains depends upon the state of the obj and
 * the organisation of the cache.
 */
typedef struct kmem_bufctl_s {
	union {
		struct kmem_bufctl_s	*buf_nextp;
		kmem_slab_t		*buf_slabp;	/* slab for obj */
		void *			 buf_objp;
	} u;
} kmem_bufctl_t;

/* ...shorthand... */
#define	buf_nextp	u.buf_nextp
#define	buf_slabp	u.buf_slabp
#define	buf_objp	u.buf_objp

#if	SLAB_DEBUG_SUPPORT
/* Magic nums for obj red zoning.
 * Placed in the first word before and the first word after an obj.
 */
#define	SLAB_RED_MAGIC1		0x5A2CF071UL	/* when obj is active */
#define	SLAB_RED_MAGIC2		0x170FC2A5UL	/* when obj is inactive */

/* ...and for poisoning */
#define	SLAB_POISON_BYTE	0x5a		/* byte value for poisoning */
#define	SLAB_POISON_END	0xa5		/* end-byte of poisoning */

#endif	/* SLAB_DEBUG_SUPPORT */

/* Cache struct - manages a cache.
 * First four members are commonly referenced during an alloc/free operation.
 */
struct kmem_cache_s {
	kmem_slab_t		 *c_freep;	/* first slab w. free objs */
	unsigned long	 	  c_flags;	/* constant flags */
	unsigned long		  c_offset;
	unsigned long		  c_num;	/* # of objs per slab */

	unsigned long		  c_magic;
	unsigned long		  c_inuse;	/* kept at zero */
	kmem_slab_t		 *c_firstp;	/* first slab in chain */
	kmem_slab_t		 *c_lastp;	/* last slab in chain */

	spinlock_t		  c_spinlock;
	unsigned long		  c_growing;
	unsigned long		  c_dflags;	/* dynamic flags */
	size_t 			  c_org_size;
	unsigned long		  c_gfporder;	/* order of pgs per slab (2^n) */
	void (*c_ctor)(void *, kmem_cache_t *, unsigned long); /* constructor func */
	void (*c_dtor)(void *, kmem_cache_t *, unsigned long); /* de-constructor func */
	unsigned long		  c_align;	/* alignment of objs */
	size_t			  c_colour;	/* cache colouring range */
	size_t			  c_colour_next;/* cache colouring */
	unsigned long		  c_failures;
	const char		 *c_name;
	struct kmem_cache_s	 *c_nextp;
	kmem_cache_t		 *c_index_cachep;
#if	SLAB_STATS
	unsigned long		  c_num_active;
	unsigned long		  c_num_allocations;
	unsigned long		  c_high_mark;
	unsigned long		  c_grown;
	unsigned long		  c_reaped;
	atomic_t 		  c_errors;
#endif	/* SLAB_STATS */
};

/* internal c_flags */
#define	SLAB_CFLGS_OFF_SLAB	0x010000UL	/* slab management in own cache */
#define	SLAB_CFLGS_BUFCTL	0x020000UL	/* bufctls in own cache */
#define	SLAB_CFLGS_GENERAL	0x080000UL	/* a general cache */

/* c_dflags (dynamic flags).  Need to hold the spinlock to access this member */
#define	SLAB_CFLGS_GROWN	0x000002UL	/* don't reap a recently grown */

#define	SLAB_OFF_SLAB(x)	((x) & SLAB_CFLGS_OFF_SLAB)
#define	SLAB_BUFCTL(x)		((x) & SLAB_CFLGS_BUFCTL)
#define	SLAB_GROWN(x)		((x) & SLAB_CFLGS_GROWN)

#if	SLAB_STATS
#define	SLAB_STATS_INC_ACTIVE(x)	((x)->c_num_active++)
#define	SLAB_STATS_DEC_ACTIVE(x)	((x)->c_num_active--)
#define	SLAB_STATS_INC_ALLOCED(x)	((x)->c_num_allocations++)
#define	SLAB_STATS_INC_GROWN(x)		((x)->c_grown++)
#define	SLAB_STATS_INC_REAPED(x)	((x)->c_reaped++)
#define	SLAB_STATS_SET_HIGH(x)		do { if ((x)->c_num_active > (x)->c_high_mark) \
						(x)->c_high_mark = (x)->c_num_active; \
					} while (0)
#define	SLAB_STATS_INC_ERR(x)		(atomic_inc(&(x)->c_errors))
#else
#define	SLAB_STATS_INC_ACTIVE(x)
#define	SLAB_STATS_DEC_ACTIVE(x)
#define	SLAB_STATS_INC_ALLOCED(x)
#define	SLAB_STATS_INC_GROWN(x)
#define	SLAB_STATS_INC_REAPED(x)
#define	SLAB_STATS_SET_HIGH(x)
#define	SLAB_STATS_INC_ERR(x)
#endif	/* SLAB_STATS */

#if	SLAB_SELFTEST
#if	!SLAB_DEBUG_SUPPORT
#error	Debug support needed for self-test
#endif
static void kmem_self_test(void);
#endif	/* SLAB_SELFTEST */

/* c_magic - used to detect 'out of slabs' in __kmem_cache_alloc() */
#define	SLAB_C_MAGIC		0x4F17A36DUL

/* maximum size of an obj (in 2^order pages) */
#define	SLAB_OBJ_MAX_ORDER	5	/* 32 pages */

/* maximum num of pages for a slab (prevents large requests to the VM layer) */
#define	SLAB_MAX_GFP_ORDER	5	/* 32 pages */

/* the 'preferred' minimum num of objs per slab - maybe less for large objs */
#define	SLAB_MIN_OBJS_PER_SLAB	4

/* If the num of objs per slab is <= SLAB_MIN_OBJS_PER_SLAB,
 * then the page order must be less than this before trying the next order.
 */
#define	SLAB_BREAK_GFP_ORDER_HI	2
#define	SLAB_BREAK_GFP_ORDER_LO	1
static int slab_break_gfp_order = SLAB_BREAK_GFP_ORDER_LO;

/* Macros for storing/retrieving the cachep and or slab from the
 * global 'mem_map'.  With off-slab bufctls, these are used to find the
 * slab an obj belongs to.  With kmalloc(), and kfree(), these are used
 * to find the cache which an obj belongs to.
 */
#define	SLAB_SET_PAGE_CACHE(pg, x)	((pg)->next = (struct page *)(x))
#define	SLAB_GET_PAGE_CACHE(pg)		((kmem_cache_t *)(pg)->next)
#define	SLAB_SET_PAGE_SLAB(pg, x)	((pg)->prev = (struct page *)(x))
#define	SLAB_GET_PAGE_SLAB(pg)		((kmem_slab_t *)(pg)->prev)

/* Size description struct for general caches. */
typedef struct cache_sizes {
	size_t		 cs_size;
	kmem_cache_t	*cs_cachep;
} cache_sizes_t;

static cache_sizes_t cache_sizes[] = {
#if	PAGE_SIZE == 4096
	{  32,		NULL},
#endif
	{  64,		NULL},
	{ 128,		NULL},
	{ 256,		NULL},
	{ 512,		NULL},
	{1024,		NULL},
	{2048,		NULL},
	{4096,		NULL},
	{8192,		NULL},
	{16384,		NULL},
	{32768,		NULL},
	{65536,		NULL},
	{131072,	NULL},
	{0,		NULL}
};

/* Names for the general caches.  Not placed into the sizes struct for
 * a good reason; the string ptr is not needed while searching in kmalloc(),
 * and would 'get-in-the-way' in the h/w cache.
 */
static char *cache_sizes_name[] = {
#if	PAGE_SIZE == 4096
	"size-32",
#endif
	"size-64",
	"size-128",
	"size-256",
	"size-512",
	"size-1024",
	"size-2048",
	"size-4096",
	"size-8192",
	"size-16384",
	"size-32768",
	"size-65536",
	"size-131072"
};

/* internal cache of cache description objs */
static	kmem_cache_t	cache_cache = {
/* freep, flags */		kmem_slab_end(&cache_cache), SLAB_NO_REAP,
/* offset, num */		sizeof(kmem_cache_t),	0,
/* c_magic, c_inuse */		SLAB_C_MAGIC, 0,
/* firstp, lastp */		kmem_slab_end(&cache_cache), kmem_slab_end(&cache_cache),
/* spinlock */			SPIN_LOCK_UNLOCKED,
/* growing */			0,
/* dflags */			0,
/* org_size, gfp */		0, 0,
/* ctor, dtor, align */		NULL, NULL, L1_CACHE_BYTES,
/* colour, colour_next */	0, 0,
/* failures */			0,
/* name */			"kmem_cache",
/* nextp */			&cache_cache,
/* index */			NULL,
};

/* Guard access to the cache-chain. */
static struct semaphore	cache_chain_sem;

/* Place maintainer for reaping. */
static	kmem_cache_t	*clock_searchp = &cache_cache;

/* Internal slab management cache, for when slab management is off-slab. */
static kmem_cache_t	*cache_slabp = NULL;

/* Max number of objs-per-slab for caches which use bufctl's.
 * Needed to avoid a possible looping condition in kmem_cache_grow().
 */
static unsigned long bufctl_limit = 0;

/* Initialisation - setup the `cache' cache. */
long __init kmem_cache_init(long start, long end)
{
	size_t size, i;

#define	kmem_slab_offset(x)  ((unsigned long)&((kmem_slab_t *)0)->x)
#define kmem_slab_diff(a,b)  (kmem_slab_offset(a) - kmem_slab_offset(b))
#define	kmem_cache_offset(x) ((unsigned long)&((kmem_cache_t *)0)->x)
#define kmem_cache_diff(a,b) (kmem_cache_offset(a) - kmem_cache_offset(b))

	/* Sanity checks... */
	if (kmem_cache_diff(c_firstp, c_magic) != kmem_slab_diff(s_nextp, s_magic) ||
	    kmem_cache_diff(c_firstp, c_inuse) != kmem_slab_diff(s_nextp, s_inuse) ||
	    ((kmem_cache_offset(c_lastp) -
	      ((unsigned long) kmem_slab_end((kmem_cache_t*)NULL))) !=
	     kmem_slab_offset(s_prevp)) ||
	    kmem_cache_diff(c_lastp, c_firstp) != kmem_slab_diff(s_prevp, s_nextp)) {
		/* Offsets to the magic are incorrect, either the structures have
		 * been incorrectly changed, or adjustments are needed for your
		 * architecture.
		 */
		panic("kmem_cache_init(): Offsets are wrong - I've been messed with!");
		/* NOTREACHED */
	}
#undef	kmem_cache_offset
#undef	kmem_cache_diff
#undef	kmem_slab_offset
#undef	kmem_slab_diff

	cache_chain_sem = MUTEX;

	size = cache_cache.c_offset + sizeof(kmem_bufctl_t);
	size += (L1_CACHE_BYTES-1);
	size &= ~(L1_CACHE_BYTES-1);
	cache_cache.c_offset = size-sizeof(kmem_bufctl_t);
	
	i = (PAGE_SIZE<<cache_cache.c_gfporder)-slab_align_size;
	cache_cache.c_num = i / size;	/* num of objs per slab */

	/* Cache colouring. */
	cache_cache.c_colour = (i-(cache_cache.c_num*size))/L1_CACHE_BYTES;
	cache_cache.c_colour_next = cache_cache.c_colour;

	/*
	 * Fragmentation resistance on low memory - only use bigger
	 * page orders on machines with more than 32MB of memory.
	 */
	if (num_physpages > (32 << 20) >> PAGE_SHIFT)
		slab_break_gfp_order = SLAB_BREAK_GFP_ORDER_HI;
	return start;
}

/* Initialisation - setup remaining internal and general caches.
 * Called after the gfp() functions have been enabled, and before smp_init().
 */
void __init kmem_cache_sizes_init(void)
{
	unsigned int	found = 0;

	cache_slabp = kmem_cache_create("slab_cache", sizeof(kmem_slab_t),
					0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (cache_slabp) {
		char **names = cache_sizes_name;
		cache_sizes_t *sizes = cache_sizes;
		do {
			/* For performance, all the general caches are L1 aligned.
			 * This should be particularly beneficial on SMP boxes, as it
			 * eliminates "false sharing".
			 * Note for systems short on memory removing the alignment will
			 * allow tighter packing of the smaller caches. */
			if (!(sizes->cs_cachep =
			      kmem_cache_create(*names++, sizes->cs_size,
						0, SLAB_HWCACHE_ALIGN, NULL, NULL)))
				goto panic_time;
			if (!found) {
				/* Inc off-slab bufctl limit until the ceiling is hit. */
				if (SLAB_BUFCTL(sizes->cs_cachep->c_flags))
					found++;
				else
					bufctl_limit =
						(sizes->cs_size/sizeof(kmem_bufctl_t));
			}
			sizes->cs_cachep->c_flags |= SLAB_CFLGS_GENERAL;
			sizes++;
		} while (sizes->cs_size);
#if	SLAB_SELFTEST
		kmem_self_test();
#endif	/* SLAB_SELFTEST */
		return;
	}
panic_time:
	panic("kmem_cache_sizes_init: Error creating caches");
	/* NOTREACHED */
}

/* Interface to system's page allocator.  Dma pts to non-zero if all
 * of memory is DMAable. No need to hold the cache-lock.
 */
static inline void *
kmem_getpages(kmem_cache_t *cachep, unsigned long flags, unsigned int *dma)
{
	void	*addr;

	*dma = flags & SLAB_DMA;
	addr = (void*) __get_free_pages(flags, cachep->c_gfporder);
	/* Assume that now we have the pages no one else can legally
	 * messes with the 'struct page's.
	 * However vm_scan() might try to test the structure to see if
	 * it is a named-page or buffer-page.  The members it tests are
	 * of no interest here.....
	 */
	if (!*dma && addr) {
		/* Need to check if can dma. */
		struct page *page = mem_map + MAP_NR(addr);
		*dma = 1<<cachep->c_gfporder;
		while ((*dma)--) {
			if (!PageDMA(page)) {
				*dma = 0;
				break;
			}
			page++;
		}
	}
	return addr;
}

/* Interface to system's page release. */
static inline void
kmem_freepages(kmem_cache_t *cachep, void *addr)
{
	unsigned long i = (1<<cachep->c_gfporder);
	struct page *page = &mem_map[MAP_NR(addr)];

	/* free_pages() does not clear the type bit - we do that.
	 * The pages have been unlinked from their cache-slab,
	 * but their 'struct page's might be accessed in
	 * vm_scan(). Shouldn't be a worry.
	 */
	while (i--) {
		PageClearSlab(page);
		page++;
	}
	free_pages((unsigned long)addr, cachep->c_gfporder); 
}

#if	SLAB_DEBUG_SUPPORT
static inline void
kmem_poison_obj(kmem_cache_t *cachep, void *addr)
{
	memset(addr, SLAB_POISON_BYTE, cachep->c_org_size);
	*(unsigned char *)(addr+cachep->c_org_size-1) = SLAB_POISON_END;
}

static inline int
kmem_check_poison_obj(kmem_cache_t *cachep, void *addr)
{
	void *end;
	end = memchr(addr, SLAB_POISON_END, cachep->c_org_size);
	if (end != (addr+cachep->c_org_size-1))
		return 1;
	return 0;
}
#endif	/* SLAB_DEBUG_SUPPORT */

/* Three slab chain funcs - all called with ints disabled and the appropriate
 * cache-lock held.
 */
static inline void
kmem_slab_unlink(kmem_slab_t *slabp)
{
	kmem_slab_t	*prevp = slabp->s_prevp;
	kmem_slab_t	*nextp = slabp->s_nextp;
	prevp->s_nextp = nextp;
	nextp->s_prevp = prevp;
}

static inline void 
kmem_slab_link_end(kmem_cache_t *cachep, kmem_slab_t *slabp)
{
	kmem_slab_t	*lastp = cachep->c_lastp;
	slabp->s_nextp = kmem_slab_end(cachep);
	slabp->s_prevp = lastp;
	cachep->c_lastp = slabp;
	lastp->s_nextp = slabp;
}

static inline void
kmem_slab_link_free(kmem_cache_t *cachep, kmem_slab_t *slabp)
{
	kmem_slab_t	*nextp = cachep->c_freep;
	kmem_slab_t	*prevp = nextp->s_prevp;
	slabp->s_nextp = nextp;
	slabp->s_prevp = prevp;
	nextp->s_prevp = slabp;
	slabp->s_prevp->s_nextp = slabp;
}

/* Destroy all the objs in a slab, and release the mem back to the system.
 * Before calling the slab must have been unlinked from the cache.
 * The cache-lock is not held/needed.
 */
static void
kmem_slab_destroy(kmem_cache_t *cachep, kmem_slab_t *slabp)
{
	if (cachep->c_dtor
#if	SLAB_DEBUG_SUPPORT
		|| cachep->c_flags & (SLAB_POISON | SLAB_RED_ZONE)
#endif	/*SLAB_DEBUG_SUPPORT*/
	) {
		/* Doesn't use the bufctl ptrs to find objs. */
		unsigned long num = cachep->c_num;
		void *objp = slabp->s_mem;
		do {
#if	SLAB_DEBUG_SUPPORT
			if (cachep->c_flags & SLAB_RED_ZONE) {
				if (*((unsigned long*)(objp)) != SLAB_RED_MAGIC1)
					printk(KERN_ERR "kmem_slab_destroy: "
					       "Bad front redzone - %s\n",
					       cachep->c_name);
				objp += BYTES_PER_WORD;
				if (*((unsigned long*)(objp+cachep->c_org_size)) !=
				    SLAB_RED_MAGIC1)
					printk(KERN_ERR "kmem_slab_destroy: "
					       "Bad rear redzone - %s\n",
					       cachep->c_name);
			}
			if (cachep->c_dtor)
#endif	/*SLAB_DEBUG_SUPPORT*/
				(cachep->c_dtor)(objp, cachep, 0);
#if	SLAB_DEBUG_SUPPORT
			else if (cachep->c_flags & SLAB_POISON) {
				if (kmem_check_poison_obj(cachep, objp))
					printk(KERN_ERR "kmem_slab_destroy: "
					       "Bad poison - %s\n", cachep->c_name);
			}
			if (cachep->c_flags & SLAB_RED_ZONE)
				objp -= BYTES_PER_WORD;
#endif	/* SLAB_DEBUG_SUPPORT */
			objp += cachep->c_offset;
			if (!slabp->s_index)
				objp += sizeof(kmem_bufctl_t);
		} while (--num);
	}

	slabp->s_magic = SLAB_MAGIC_DESTROYED;
	if (slabp->s_index)
		kmem_cache_free(cachep->c_index_cachep, slabp->s_index);
	kmem_freepages(cachep, slabp->s_mem-slabp->s_offset);
	if (SLAB_OFF_SLAB(cachep->c_flags))
		kmem_cache_free(cache_slabp, slabp);
}

/* Cal the num objs, wastage, and bytes left over for a given slab size. */
static inline size_t
kmem_cache_cal_waste(unsigned long gfporder, size_t size, size_t extra,
		     unsigned long flags, size_t *left_over, unsigned long *num)
{
	size_t wastage = PAGE_SIZE<<gfporder;

	if (SLAB_OFF_SLAB(flags))
		gfporder = 0;
	else
		gfporder = slab_align_size;
	wastage -= gfporder;
	*num = wastage / size;
	wastage -= (*num * size);
	*left_over = wastage;

	return (wastage + gfporder + (extra * *num));
}

/* Create a cache:
 * Returns a ptr to the cache on success, NULL on failure.
 * Cannot be called within a int, but can be interrupted.
 * NOTE: The 'name' is assumed to be memory that is _not_  going to disappear.
 */
kmem_cache_t *
kmem_cache_create(const char *name, size_t size, size_t offset,
	unsigned long flags, void (*ctor)(void*, kmem_cache_t *, unsigned long),
	void (*dtor)(void*, kmem_cache_t *, unsigned long))
{
	const char *func_nm= KERN_ERR "kmem_create: ";
	kmem_cache_t	*searchp;
	kmem_cache_t	*cachep=NULL;
	size_t		extra;
	size_t		left_over;
	size_t		align;

	/* Sanity checks... */
#if	SLAB_MGMT_CHECKS
	if (!name) {
		printk("%sNULL ptr\n", func_nm);
		goto opps;
	}
	if (in_interrupt()) {
		printk("%sCalled during int - %s\n", func_nm, name);
		goto opps;
	}

	if (size < BYTES_PER_WORD) {
		printk("%sSize too small %d - %s\n", func_nm, (int) size, name);
		size = BYTES_PER_WORD;
	}

	if (size > ((1<<SLAB_OBJ_MAX_ORDER)*PAGE_SIZE)) {
		printk("%sSize too large %d - %s\n", func_nm, (int) size, name);
		goto opps;
	}

	if (dtor && !ctor) {
		/* Decon, but no con - doesn't make sense */
		printk("%sDecon but no con - %s\n", func_nm, name);
		goto opps;
	}

	if (offset < 0 || offset > size) {
		printk("%sOffset weird %d - %s\n", func_nm, (int) offset, name);
		offset = 0;
	}

#if	SLAB_DEBUG_SUPPORT
	if ((flags & SLAB_DEBUG_INITIAL) && !ctor) {
		/* No constructor, but inital state check requested */
		printk("%sNo con, but init state check requested - %s\n", func_nm, name);
		flags &= ~SLAB_DEBUG_INITIAL;
	}

	if ((flags & SLAB_POISON) && ctor) {
		/* request for poisoning, but we can't do that with a constructor */
		printk("%sPoisoning requested, but con given - %s\n", func_nm, name);
		flags &= ~SLAB_POISON;
	}
#if	0
	if ((flags & SLAB_HIGH_PACK) && ctor) {
		printk("%sHigh pack requested, but con given - %s\n", func_nm, name);
		flags &= ~SLAB_HIGH_PACK;
	}
	if ((flags & SLAB_HIGH_PACK) && (flags & (SLAB_POISON|SLAB_RED_ZONE))) {
		printk("%sHigh pack requested, but with poisoning/red-zoning - %s\n",
		       func_nm, name);
		flags &= ~SLAB_HIGH_PACK;
	}
#endif
#endif	/* SLAB_DEBUG_SUPPORT */
#endif	/* SLAB_MGMT_CHECKS */

	/* Always checks flags, a caller might be expecting debug
	 * support which isn't available.
	 */
	if (flags & ~SLAB_C_MASK) {
		printk("%sIllgl flg %lX - %s\n", func_nm, flags, name);
		flags &= SLAB_C_MASK;
	}

	/* Get cache's description obj. */
	cachep = (kmem_cache_t *) kmem_cache_alloc(&cache_cache, SLAB_KERNEL);
	if (!cachep)
		goto opps;
	memset(cachep, 0, sizeof(kmem_cache_t));

	/* Check that size is in terms of words.  This is needed to avoid
	 * unaligned accesses for some archs when redzoning is used, and makes
	 * sure any on-slab bufctl's are also correctly aligned.
	 */
	if (size & (BYTES_PER_WORD-1)) {
		size += (BYTES_PER_WORD-1);
		size &= ~(BYTES_PER_WORD-1);
		printk("%sForcing size word alignment - %s\n", func_nm, name);
	}

	cachep->c_org_size = size;
#if	SLAB_DEBUG_SUPPORT
	if (flags & SLAB_RED_ZONE) {
		/* There is no point trying to honour cache alignment when redzoning. */
		flags &= ~SLAB_HWCACHE_ALIGN;
		size += 2*BYTES_PER_WORD;		/* words for redzone */
	}
#endif	/* SLAB_DEBUG_SUPPORT */

	align = BYTES_PER_WORD;
	if (flags & SLAB_HWCACHE_ALIGN)
		align = L1_CACHE_BYTES;

	/* Determine if the slab management and/or bufclts are 'on' or 'off' slab. */
	extra = sizeof(kmem_bufctl_t);
	if (size < (PAGE_SIZE>>3)) {
		/* Size is small(ish).  Use packing where bufctl size per
		 * obj is low, and slab management is on-slab.
		 */
#if	0
		if ((flags & SLAB_HIGH_PACK)) {
			/* Special high packing for small objects
			 * (mainly for vm_mapping structs, but
			 * others can use it).
			 */
			if (size == (L1_CACHE_BYTES/4) || size == (L1_CACHE_BYTES/2) ||
			    size == L1_CACHE_BYTES) {
				/* The bufctl is stored with the object. */
				extra = 0;
			} else
				flags &= ~SLAB_HIGH_PACK;
		}
#endif
	} else {
		/* Size is large, assume best to place the slab management obj
		 * off-slab (should allow better packing of objs).
		 */
		flags |= SLAB_CFLGS_OFF_SLAB;
		if (!(size & ~PAGE_MASK) || size == (PAGE_SIZE/2)
		    || size == (PAGE_SIZE/4) || size == (PAGE_SIZE/8)) {
			/* To avoid waste the bufctls are off-slab... */
			flags |= SLAB_CFLGS_BUFCTL;
			extra = 0;
		} /* else slab management is off-slab, but freelist pointers are on. */
	}
	size += extra;

	if (flags & SLAB_HWCACHE_ALIGN) {
		/* Need to adjust size so that objs are cache aligned. */
		if (size > (L1_CACHE_BYTES/2)) {
			size_t words = size % L1_CACHE_BYTES;
			if (words)
				size += (L1_CACHE_BYTES-words);
		} else {
			/* Small obj size, can get at least two per cache line. */
			int num_per_line = L1_CACHE_BYTES/size;
			left_over = L1_CACHE_BYTES - (num_per_line*size);
			if (left_over) {
				/* Need to adjust size so objs cache align. */
				if (left_over%num_per_line) {
					/* Odd num of objs per line - fixup. */
					num_per_line--;
					left_over += size;
				}
				size += (left_over/num_per_line);
			}
		}
	} else if (!(size%L1_CACHE_BYTES)) {
		/* Size happens to cache align... */
		flags |= SLAB_HWCACHE_ALIGN;
		align = L1_CACHE_BYTES;
	}

	/* Cal size (in pages) of slabs, and the num of objs per slab.
	 * This could be made much more intelligent.  For now, try to avoid
	 * using high page-orders for slabs.  When the gfp() funcs are more
	 * friendly towards high-order requests, this should be changed.
	 */
	do {
		size_t wastage;
		unsigned int break_flag = 0;
cal_wastage:
		wastage = kmem_cache_cal_waste(cachep->c_gfporder, size, extra,
					       flags, &left_over, &cachep->c_num);
		if (!cachep->c_num)
			goto next;
		if (break_flag)
			break;
		if (SLAB_BUFCTL(flags) && cachep->c_num > bufctl_limit) {
			/* Oops, this num of objs will cause problems. */
			cachep->c_gfporder--;
			break_flag++;
			goto cal_wastage;
		}
		if (cachep->c_gfporder == SLAB_MAX_GFP_ORDER)
			break;

		/* Large num of objs is good, but v. large slabs are currently
		 * bad for the gfp()s.
		 */
		if (cachep->c_num <= SLAB_MIN_OBJS_PER_SLAB) {
			if (cachep->c_gfporder < slab_break_gfp_order)
				goto next;
		}

		/* Stop caches with small objs having a large num of pages. */
		if (left_over <= slab_align_size)
			break;
		if ((wastage*8) <= (PAGE_SIZE<<cachep->c_gfporder))
			break;	/* Acceptable internal fragmentation. */
next:
		cachep->c_gfporder++;
	} while (1);

	/* If the slab has been placed off-slab, and we have enough space then
	 * move it on-slab.  This is at the expense of any extra colouring.
	 */
	if ((flags & SLAB_CFLGS_OFF_SLAB) && !SLAB_BUFCTL(flags) &&
	    left_over >= slab_align_size) {
		flags &= ~SLAB_CFLGS_OFF_SLAB;
		left_over -= slab_align_size;
	}

	/* Offset must be a factor of the alignment. */
	offset += (align-1);
	offset &= ~(align-1);

	/* Mess around with the offset alignment. */
	if (!left_over) {
		offset = 0;
	} else if (left_over < offset) {
		offset = align;
		if (flags & SLAB_HWCACHE_ALIGN) {
			if (left_over < offset)
				offset = 0;
		} else {
			/* Offset is BYTES_PER_WORD, and left_over is at
			 * least BYTES_PER_WORD.
			 */
			if (left_over >= (BYTES_PER_WORD*2)) {
				offset >>= 1;
				if (left_over >= (BYTES_PER_WORD*4))
					offset >>= 1;
			}
		}
	} else if (!offset) {
		/* No offset requested, but space enough - give one. */
		offset = left_over/align;
		if (flags & SLAB_HWCACHE_ALIGN) {
			if (offset >= 8) {
				/* A large number of colours - use a larger alignment. */
				align <<= 1;
			}
		} else {
			if (offset >= 10) {
				align <<= 1;
				if (offset >= 16)
					align <<= 1;
			}
		}
		offset = align;
	}

#if	0
printk("%s: Left_over:%d Align:%d Size:%d\n", name, left_over, offset, size);
#endif

	if ((cachep->c_align = (unsigned long) offset))
		cachep->c_colour = (left_over/offset);
	cachep->c_colour_next = cachep->c_colour;

	/* If the bufctl's are on-slab, c_offset does not include the size of bufctl. */
	if (!SLAB_BUFCTL(flags))
		size -= sizeof(kmem_bufctl_t);
	else
		cachep->c_index_cachep =
			kmem_find_general_cachep(cachep->c_num*sizeof(kmem_bufctl_t));
	cachep->c_offset = (unsigned long) size;
	cachep->c_freep = kmem_slab_end(cachep);
	cachep->c_firstp = kmem_slab_end(cachep);
	cachep->c_lastp = kmem_slab_end(cachep);
	cachep->c_flags = flags;
	cachep->c_ctor = ctor;
	cachep->c_dtor = dtor;
	cachep->c_magic = SLAB_C_MAGIC;
	cachep->c_name = name;		/* Simply point to the name. */
	spin_lock_init(&cachep->c_spinlock);

	/* Need the semaphore to access the chain. */
	down(&cache_chain_sem);
	searchp = &cache_cache;
	do {
		/* The name field is constant - no lock needed. */
		if (!strcmp(searchp->c_name, name)) {
			printk("%sDup name - %s\n", func_nm, name);
			break;
		}
		searchp = searchp->c_nextp;
	} while (searchp != &cache_cache);

	/* There is no reason to lock our new cache before we
	 * link it in - no one knows about it yet...
	 */
	cachep->c_nextp = cache_cache.c_nextp;
	cache_cache.c_nextp = cachep;
	up(&cache_chain_sem);
opps:
	return cachep;
}

/* Shrink a cache.  Releases as many slabs as possible for a cache.
 * It is expected this function will be called by a module when it is
 * unloaded.  The cache is _not_ removed, this creates too many problems and
 * the cache-structure does not take up much room.  A module should keep its
 * cache pointer(s) in unloaded memory, so when reloaded it knows the cache
 * is available.  To help debugging, a zero exit status indicates all slabs
 * were released.
 */
int
kmem_cache_shrink(kmem_cache_t *cachep)
{
	kmem_cache_t	*searchp;
	kmem_slab_t	*slabp;
	int	ret;

	if (!cachep) {
		printk(KERN_ERR "kmem_shrink: NULL ptr\n");
		return 2;
	}
	if (in_interrupt()) {
		printk(KERN_ERR "kmem_shrink: Called during int - %s\n", cachep->c_name);
		return 2;
	}

	/* Find the cache in the chain of caches. */
	down(&cache_chain_sem);		/* Semaphore is needed. */
	searchp = &cache_cache;
	for (;searchp->c_nextp != &cache_cache; searchp = searchp->c_nextp) {
		if (searchp->c_nextp != cachep)
			continue;

		/* Accessing clock_searchp is safe - we hold the mutex. */
		if (cachep == clock_searchp)
			clock_searchp = cachep->c_nextp;
		goto found;
	}
	up(&cache_chain_sem);
	printk(KERN_ERR "kmem_shrink: Invalid cache addr %p\n", cachep);
	return 2;
found:
	/* Release the semaphore before getting the cache-lock.  This could
	 * mean multiple engines are shrinking the cache, but so what.
	 */
	up(&cache_chain_sem);
	spin_lock_irq(&cachep->c_spinlock);

	/* If the cache is growing, stop shrinking. */
	while (!cachep->c_growing) {
		slabp = cachep->c_lastp;
		if (slabp->s_inuse || slabp == kmem_slab_end(cachep))
			break;
		kmem_slab_unlink(slabp);
		spin_unlock_irq(&cachep->c_spinlock);
		kmem_slab_destroy(cachep, slabp);
		spin_lock_irq(&cachep->c_spinlock);
	}
	ret = 1;
	if (cachep->c_lastp == kmem_slab_end(cachep))
		ret--;		/* Cache is empty. */
	spin_unlock_irq(&cachep->c_spinlock);
	return ret;
}

/* Get the memory for a slab management obj. */
static inline kmem_slab_t *
kmem_cache_slabmgmt(kmem_cache_t *cachep, void *objp, int local_flags)
{
	kmem_slab_t	*slabp;

	if (SLAB_OFF_SLAB(cachep->c_flags)) {
		/* Slab management obj is off-slab. */
		slabp = kmem_cache_alloc(cache_slabp, local_flags);
	} else {
		/* Slab management at end of slab memory, placed so that
		 * the position is 'coloured'.
		 */
		void *end;
		end = objp + (cachep->c_num * cachep->c_offset);
		if (!SLAB_BUFCTL(cachep->c_flags))
			end += (cachep->c_num * sizeof(kmem_bufctl_t));
		slabp = (kmem_slab_t *) L1_CACHE_ALIGN((unsigned long)end);
	}

	if (slabp) {
		slabp->s_inuse = 0;
		slabp->s_dma = 0;
		slabp->s_index = NULL;
	}

	return slabp;
}

static inline void
kmem_cache_init_objs(kmem_cache_t * cachep, kmem_slab_t * slabp, void *objp,
				unsigned long ctor_flags)
{
	kmem_bufctl_t	**bufpp = &slabp->s_freep;
	unsigned long	num = cachep->c_num-1;

	do {
#if	SLAB_DEBUG_SUPPORT
		if (cachep->c_flags & SLAB_RED_ZONE) {
			*((unsigned long*)(objp)) = SLAB_RED_MAGIC1;
			objp += BYTES_PER_WORD;
			*((unsigned long*)(objp+cachep->c_org_size)) = SLAB_RED_MAGIC1;
		}
#endif	/* SLAB_DEBUG_SUPPORT */

		/* Constructors are not allowed to allocate memory from the same cache
		 * which they are a constructor for.  Otherwise, deadlock.
		 * They must also be threaded.
		 */
		if (cachep->c_ctor)
			cachep->c_ctor(objp, cachep, ctor_flags);
#if	SLAB_DEBUG_SUPPORT
		else if (cachep->c_flags & SLAB_POISON) {
			/* need to poison the objs */
			kmem_poison_obj(cachep, objp);
		}

		if (cachep->c_flags & SLAB_RED_ZONE) {
			if (*((unsigned long*)(objp+cachep->c_org_size)) !=
			    SLAB_RED_MAGIC1) {
				*((unsigned long*)(objp+cachep->c_org_size)) =
					SLAB_RED_MAGIC1;
				printk(KERN_ERR "kmem_init_obj: Bad rear redzone "
				       "after constructor - %s\n", cachep->c_name);
			}
			objp -= BYTES_PER_WORD;
			if (*((unsigned long*)(objp)) != SLAB_RED_MAGIC1) {
				*((unsigned long*)(objp)) = SLAB_RED_MAGIC1;
				printk(KERN_ERR "kmem_init_obj: Bad front redzone "
				       "after constructor - %s\n", cachep->c_name);
			}
		}
#endif	/* SLAB_DEBUG_SUPPORT */

		objp += cachep->c_offset;
		if (!slabp->s_index) {
			*bufpp = objp;
			objp += sizeof(kmem_bufctl_t);
		} else
			*bufpp = &slabp->s_index[num];
		bufpp = &(*bufpp)->buf_nextp;
	} while (num--);

	*bufpp = NULL;
}

/* Grow (by 1) the number of slabs within a cache.  This is called by
 * kmem_cache_alloc() when there are no active objs left in a cache.
 */
static int
kmem_cache_grow(kmem_cache_t * cachep, int flags)
{
	kmem_slab_t	*slabp;
	struct page	*page;
	void		*objp;
	size_t		 offset;
	unsigned int	 dma, local_flags;
	unsigned long	 ctor_flags;
	unsigned long	 save_flags;

	/* Be lazy and only check for valid flags here,
 	 * keeping it out of the critical path in kmem_cache_alloc().
	 */
	if (flags & ~(SLAB_DMA|SLAB_LEVEL_MASK|SLAB_NO_GROW)) {
		printk(KERN_WARNING "kmem_grow: Illegal flgs %X (correcting) - %s\n",
		       flags, cachep->c_name);
		flags &= (SLAB_DMA|SLAB_LEVEL_MASK|SLAB_NO_GROW);
	}

	if (flags & SLAB_NO_GROW)
		return 0;

	/* The test for missing atomic flag is performed here, rather than
	 * the more obvious place, simply to reduce the critical path length
	 * in kmem_cache_alloc().  If a caller is slightly mis-behaving they
	 * will eventually be caught here (where it matters).
	 */
	if (in_interrupt() && (flags & SLAB_LEVEL_MASK) != SLAB_ATOMIC) {
		printk(KERN_ERR "kmem_grow: Called nonatomically from int - %s\n",
		       cachep->c_name);
		flags &= ~SLAB_LEVEL_MASK;
		flags |= SLAB_ATOMIC;
	}
	ctor_flags = SLAB_CTOR_CONSTRUCTOR;
	local_flags = (flags & SLAB_LEVEL_MASK);
	if (local_flags == SLAB_ATOMIC) {
		/* Not allowed to sleep.  Need to tell a constructor about
		 * this - it might need to know...
		 */
		ctor_flags |= SLAB_CTOR_ATOMIC;
	}

	/* About to mess with non-constant members - lock. */
	spin_lock_irqsave(&cachep->c_spinlock, save_flags);

	/* Get colour for the slab, and cal the next value. */
	if (!(offset = cachep->c_colour_next--))
		cachep->c_colour_next = cachep->c_colour;
	offset *= cachep->c_align;
	cachep->c_dflags = SLAB_CFLGS_GROWN;

	cachep->c_growing++;
	spin_unlock_irqrestore(&cachep->c_spinlock, save_flags);

	/* A series of memory allocations for a new slab.
	 * Neither the cache-chain semaphore, or cache-lock, are
	 * held, but the incrementing c_growing prevents this
	 * this cache from being reaped or shrunk.
	 * Note: The cache could be selected in for reaping in
	 * kmem_cache_reap(), but when the final test is made the
	 * growing value will be seen.
	 */

	/* Get mem for the objs. */
	if (!(objp = kmem_getpages(cachep, flags, &dma)))
		goto failed;

	/* Get slab management. */
	if (!(slabp = kmem_cache_slabmgmt(cachep, objp+offset, local_flags)))
		goto opps1;
	if (dma)
		slabp->s_dma = 1;
	if (SLAB_BUFCTL(cachep->c_flags)) {
		slabp->s_index = kmem_cache_alloc(cachep->c_index_cachep, local_flags);
		if (!slabp->s_index)
			goto opps2;
	}

	/* Nasty!!!!!!  I hope this is OK. */
	dma = 1 << cachep->c_gfporder;
	page = &mem_map[MAP_NR(objp)];
	do {
		SLAB_SET_PAGE_CACHE(page, cachep);
		SLAB_SET_PAGE_SLAB(page, slabp);
		PageSetSlab(page);
		page++;
	} while (--dma);

	slabp->s_offset = offset;	/* It will fit... */
	objp += offset;		/* Address of first object. */
	slabp->s_mem = objp;

	/* For on-slab bufctls, c_offset is the distance between the start of
	 * an obj and its related bufctl.  For off-slab bufctls, c_offset is
	 * the distance between objs in the slab.
	 */
	kmem_cache_init_objs(cachep, slabp, objp, ctor_flags);

	spin_lock_irq(&cachep->c_spinlock);

	/* Make slab active. */
	slabp->s_magic = SLAB_MAGIC_ALLOC;
	kmem_slab_link_end(cachep, slabp);
	if (cachep->c_freep == kmem_slab_end(cachep))
		cachep->c_freep = slabp;
	SLAB_STATS_INC_GROWN(cachep);
	cachep->c_failures = 0;
	cachep->c_growing--;

	spin_unlock_irqrestore(&cachep->c_spinlock, save_flags);
	return 1;
opps2:
	if (SLAB_OFF_SLAB(cachep->c_flags))
		kmem_cache_free(cache_slabp, slabp);
opps1:
	kmem_freepages(cachep, objp); 
failed:
	spin_lock_irq(&cachep->c_spinlock);
	cachep->c_growing--;
	spin_unlock_irqrestore(&cachep->c_spinlock, save_flags);
	return 0;
}

static void
kmem_report_alloc_err(const char *str, kmem_cache_t * cachep)
{
	if (cachep)
		SLAB_STATS_INC_ERR(cachep);	/* this is atomic */
	printk(KERN_ERR "kmem_alloc: %s (name=%s)\n",
	       str, cachep ? cachep->c_name : "unknown");
}

static void
kmem_report_free_err(const char *str, const void *objp, kmem_cache_t * cachep)
{
	if (cachep)
		SLAB_STATS_INC_ERR(cachep);
	printk(KERN_ERR "kmem_free: %s (objp=%p, name=%s)\n",
	       str, objp, cachep ? cachep->c_name : "unknown");
}

/* Search for a slab whose objs are suitable for DMA.
 * Note: since testing the first free slab (in __kmem_cache_alloc()),
 * ints must not have been enabled, or the cache-lock released!
 */
static inline kmem_slab_t *
kmem_cache_search_dma(kmem_cache_t * cachep)
{
	kmem_slab_t	*slabp = cachep->c_freep->s_nextp;

	for (; slabp != kmem_slab_end(cachep); slabp = slabp->s_nextp) {
		if (!(slabp->s_dma))
			continue;
		kmem_slab_unlink(slabp);
		kmem_slab_link_free(cachep, slabp);
		cachep->c_freep = slabp;
		break;
	}
	return slabp;
}

#if	SLAB_DEBUG_SUPPORT
/* Perform extra freeing checks.  Currently, this check is only for caches
 * that use bufctl structures within the slab.  Those which use bufctl's
 * from the internal cache have a reasonable check when the address is
 * searched for.  Called with the cache-lock held.
 */
static void *
kmem_extra_free_checks(kmem_cache_t * cachep, kmem_bufctl_t *search_bufp,
		       kmem_bufctl_t *bufp, void * objp)
{
	if (SLAB_BUFCTL(cachep->c_flags))
		return objp;

	/* Check slab's freelist to see if this obj is there. */
	for (; search_bufp; search_bufp = search_bufp->buf_nextp) {
		if (search_bufp != bufp)
			continue;
		return NULL;
	}
	return objp;
}
#endif	/* SLAB_DEBUG_SUPPORT */

/* Called with cache lock held. */
static inline void
kmem_cache_full_free(kmem_cache_t *cachep, kmem_slab_t *slabp)
{
	if (slabp->s_nextp->s_inuse) {
		/* Not at correct position. */
		if (cachep->c_freep == slabp)
			cachep->c_freep = slabp->s_nextp;
		kmem_slab_unlink(slabp);
		kmem_slab_link_end(cachep, slabp);
	}
}

/* Called with cache lock held. */
static inline void
kmem_cache_one_free(kmem_cache_t *cachep, kmem_slab_t *slabp)
{
	if (slabp->s_nextp->s_inuse == cachep->c_num) {
		kmem_slab_unlink(slabp);
		kmem_slab_link_free(cachep, slabp);
	}
	cachep->c_freep = slabp;
}

/* Returns a ptr to an obj in the given cache. */
static inline void *
__kmem_cache_alloc(kmem_cache_t *cachep, int flags)
{
	kmem_slab_t	*slabp;
	kmem_bufctl_t	*bufp;
	void		*objp;
	unsigned long	save_flags;

	/* Sanity check. */
	if (!cachep)
		goto nul_ptr;
	spin_lock_irqsave(&cachep->c_spinlock, save_flags);
try_again:
	/* Get slab alloc is to come from. */
	slabp = cachep->c_freep;

	/* Magic is a sanity check _and_ says if we need a new slab. */
	if (slabp->s_magic != SLAB_MAGIC_ALLOC)
		goto alloc_new_slab;
	/* DMA requests are 'rare' - keep out of the critical path. */
	if (flags & SLAB_DMA)
		goto search_dma;
try_again_dma:
	SLAB_STATS_INC_ALLOCED(cachep);
	SLAB_STATS_INC_ACTIVE(cachep);
	SLAB_STATS_SET_HIGH(cachep);
	slabp->s_inuse++;
	bufp = slabp->s_freep;
	slabp->s_freep = bufp->buf_nextp;
	if (slabp->s_freep) {
ret_obj:
		if (!slabp->s_index) {
			bufp->buf_slabp = slabp;
			objp = ((void*)bufp) - cachep->c_offset;
finished:
			/* The lock is not needed by the red-zone or poison ops, and the
			 * obj has been removed from the slab.  Should be safe to drop
			 * the lock here.
			 */
			spin_unlock_irqrestore(&cachep->c_spinlock, save_flags);
#if	SLAB_DEBUG_SUPPORT
			if (cachep->c_flags & SLAB_RED_ZONE)
				goto red_zone;
ret_red:
			if ((cachep->c_flags & SLAB_POISON) && kmem_check_poison_obj(cachep, objp))
				kmem_report_alloc_err("Bad poison", cachep);
#endif	/* SLAB_DEBUG_SUPPORT */
			return objp;
		}
		/* Update index ptr. */
		objp = ((bufp-slabp->s_index)*cachep->c_offset) + slabp->s_mem;
		bufp->buf_objp = objp;
		goto finished;
	}
	cachep->c_freep = slabp->s_nextp;
	goto ret_obj;

#if	SLAB_DEBUG_SUPPORT
red_zone:
	/* Set alloc red-zone, and check old one. */
	if (xchg((unsigned long *)objp, SLAB_RED_MAGIC2) != SLAB_RED_MAGIC1)
		kmem_report_alloc_err("Bad front redzone", cachep);
	objp += BYTES_PER_WORD;
	if (xchg((unsigned long *)(objp+cachep->c_org_size), SLAB_RED_MAGIC2) != SLAB_RED_MAGIC1)
		kmem_report_alloc_err("Bad rear redzone", cachep);
	goto ret_red;
#endif	/* SLAB_DEBUG_SUPPORT */

search_dma:
	if (slabp->s_dma || (slabp = kmem_cache_search_dma(cachep))!=kmem_slab_end(cachep))
		goto try_again_dma;
alloc_new_slab:
	/* Either out of slabs, or magic number corruption. */
	if (slabp == kmem_slab_end(cachep)) {
		/* Need a new slab.  Release the lock before calling kmem_cache_grow().
		 * This allows objs to be released back into the cache while growing.
		 */
		spin_unlock_irqrestore(&cachep->c_spinlock, save_flags);
		if (kmem_cache_grow(cachep, flags)) {
			/* Someone may have stolen our objs.  Doesn't matter, we'll
			 * just come back here again.
			 */
			spin_lock_irq(&cachep->c_spinlock);
			goto try_again;
		}
		/* Couldn't grow, but some objs may have been freed. */
		spin_lock_irq(&cachep->c_spinlock);
		if (cachep->c_freep != kmem_slab_end(cachep)) {
			if ((flags & SLAB_ATOMIC) == 0) 
				goto try_again;
		}
	} else {
		/* Very serious error - maybe panic() here? */
		kmem_report_alloc_err("Bad slab magic (corrupt)", cachep);
	}
	spin_unlock_irqrestore(&cachep->c_spinlock, save_flags);
err_exit:
	return NULL;
nul_ptr:
	kmem_report_alloc_err("NULL ptr", NULL);
	goto err_exit;
}

/* Release an obj back to its cache.  If the obj has a constructed state,
 * it should be in this state _before_ it is released.
 */
static inline void
__kmem_cache_free(kmem_cache_t *cachep, const void *objp)
{
	kmem_slab_t	*slabp;
	kmem_bufctl_t	*bufp;
	unsigned long	save_flags;

	/* Basic sanity checks. */
	if (!cachep || !objp)
		goto null_addr;

#if	SLAB_DEBUG_SUPPORT
	/* A verify func is called without the cache-lock held. */
	if (cachep->c_flags & SLAB_DEBUG_INITIAL)
		goto init_state_check;
finished_initial:

	if (cachep->c_flags & SLAB_RED_ZONE)
		goto red_zone;
return_red:
#endif	/* SLAB_DEBUG_SUPPORT */

	spin_lock_irqsave(&cachep->c_spinlock, save_flags);

	if (SLAB_BUFCTL(cachep->c_flags))
		goto bufctl;
	bufp = (kmem_bufctl_t *)(objp+cachep->c_offset);

	/* Get slab for the object. */
#if	0
	/* _NASTY_IF/ELSE_, but avoids a 'distant' memory ref for some objects.
	 * Is this worth while? XXX
	 */
	if (cachep->c_flags & SLAB_HIGH_PACK)
		slabp = SLAB_GET_PAGE_SLAB(&mem_map[MAP_NR(bufp)]);
	else
#endif
		slabp = bufp->buf_slabp;

check_magic:
	if (slabp->s_magic != SLAB_MAGIC_ALLOC)		/* Sanity check. */
		goto bad_slab;

#if	SLAB_DEBUG_SUPPORT
	if (cachep->c_flags & SLAB_DEBUG_FREE)
		goto extra_checks;
passed_extra:
#endif	/* SLAB_DEBUG_SUPPORT */

	if (slabp->s_inuse) {		/* Sanity check. */
		SLAB_STATS_DEC_ACTIVE(cachep);
		slabp->s_inuse--;
		bufp->buf_nextp = slabp->s_freep;
		slabp->s_freep = bufp;
		if (bufp->buf_nextp) {
			if (slabp->s_inuse) {
				/* (hopefully) The most common case. */
finished:
#if	SLAB_DEBUG_SUPPORT
				if (cachep->c_flags & SLAB_POISON) {
					if (cachep->c_flags & SLAB_RED_ZONE)
						objp += BYTES_PER_WORD;
					kmem_poison_obj(cachep, objp);
				}
#endif	/* SLAB_DEBUG_SUPPORT */
				spin_unlock_irqrestore(&cachep->c_spinlock, save_flags);
				return;
			}
			kmem_cache_full_free(cachep, slabp);
			goto finished;
		}
		kmem_cache_one_free(cachep, slabp);
		goto finished;
	}

	/* Don't add to freelist. */
	spin_unlock_irqrestore(&cachep->c_spinlock, save_flags);
	kmem_report_free_err("free with no active objs", objp, cachep);
	return;
bufctl:
	/* No 'extra' checks are performed for objs stored this way, finding
	 * the obj is check enough.
	 */
	slabp = SLAB_GET_PAGE_SLAB(&mem_map[MAP_NR(objp)]);
	bufp =	&slabp->s_index[(objp - slabp->s_mem)/cachep->c_offset];
	if (bufp->buf_objp == objp)
		goto check_magic;
	spin_unlock_irqrestore(&cachep->c_spinlock, save_flags);
	kmem_report_free_err("Either bad obj addr or double free", objp, cachep);
	return;
#if	SLAB_DEBUG_SUPPORT
init_state_check:
	/* Need to call the slab's constructor so the
	 * caller can perform a verify of its state (debugging).
	 */
	cachep->c_ctor(objp, cachep, SLAB_CTOR_CONSTRUCTOR|SLAB_CTOR_VERIFY);
	goto finished_initial;
extra_checks:
	if (!kmem_extra_free_checks(cachep, slabp->s_freep, bufp, objp)) {
		spin_unlock_irqrestore(&cachep->c_spinlock, save_flags);
		kmem_report_free_err("Double free detected during checks", objp, cachep);
		return;
	}
	goto passed_extra;
red_zone:
	/* We do not hold the cache-lock while checking the red-zone.
	 */
	objp -= BYTES_PER_WORD;
	if (xchg((unsigned long *)objp, SLAB_RED_MAGIC1) != SLAB_RED_MAGIC2) {
		/* Either write before start of obj, or a double free. */
		kmem_report_free_err("Bad front redzone", objp, cachep);
	}
	if (xchg((unsigned long *)(objp+cachep->c_org_size+BYTES_PER_WORD), SLAB_RED_MAGIC1) != SLAB_RED_MAGIC2) {
		/* Either write past end of obj, or a double free. */
		kmem_report_free_err("Bad rear redzone", objp, cachep);
	}
	goto return_red;
#endif	/* SLAB_DEBUG_SUPPORT */

bad_slab:
	/* Slab doesn't contain the correct magic num. */
	if (slabp->s_magic == SLAB_MAGIC_DESTROYED) {
		/* Magic num says this is a destroyed slab. */
		kmem_report_free_err("free from inactive slab", objp, cachep);
	} else
		kmem_report_free_err("Bad obj addr", objp, cachep);
	spin_unlock_irqrestore(&cachep->c_spinlock, save_flags);

#if 1
/* FORCE A KERNEL DUMP WHEN THIS HAPPENS. SPEAK IN ALL CAPS. GET THE CALL CHAIN. */
*(int *) 0 = 0;
#endif

	return;
null_addr:
	kmem_report_free_err("NULL ptr", objp, cachep);
	return;
}

void *
kmem_cache_alloc(kmem_cache_t *cachep, int flags)
{
	return __kmem_cache_alloc(cachep, flags);
}

void
kmem_cache_free(kmem_cache_t *cachep, void *objp)
{
	__kmem_cache_free(cachep, objp);
}

void *
kmalloc(size_t size, int flags)
{
	cache_sizes_t	*csizep = cache_sizes;

	for (; csizep->cs_size; csizep++) {
		if (size > csizep->cs_size)
			continue;
		return __kmem_cache_alloc(csizep->cs_cachep, flags);
	}
	printk(KERN_ERR "kmalloc: Size (%lu) too large\n", (unsigned long) size);
	return NULL;
}

void
kfree(const void *objp)
{
	struct page *page;
	int	nr;

	if (!objp)
		goto null_ptr;
	nr = MAP_NR(objp);
	if (nr >= max_mapnr)
		goto bad_ptr;

	/* Assume we own the page structure - hence no locking.
	 * If someone is misbehaving (for example, calling us with a bad
	 * address), then access to the page structure can race with the
	 * kmem_slab_destroy() code.  Need to add a spin_lock to each page
	 * structure, which would be useful in threading the gfp() functions....
	 */
	page = &mem_map[nr];
	if (PageSlab(page)) {
		kmem_cache_t	*cachep;

		/* Here, we again assume the obj address is good.
		 * If it isn't, and happens to map onto another
		 * general cache page which has no active objs, then
		 * we race.
		 */
		cachep = SLAB_GET_PAGE_CACHE(page);
		if (cachep && (cachep->c_flags & SLAB_CFLGS_GENERAL)) {
			__kmem_cache_free(cachep, objp);
			return;
		}
	}
bad_ptr:
	printk(KERN_ERR "kfree: Bad obj %p\n", objp);

#if 1
/* FORCE A KERNEL DUMP WHEN THIS HAPPENS. SPEAK IN ALL CAPS. GET THE CALL CHAIN. */
*(int *) 0 = 0;
#endif

null_ptr:
	return;
}

void
kfree_s(const void *objp, size_t size)
{
	struct page *page;
	int	nr;

	if (!objp)
		goto null_ptr;
	nr = MAP_NR(objp);
	if (nr >= max_mapnr)
		goto null_ptr;
	/* See comment in kfree() */
	page = &mem_map[nr];
	if (PageSlab(page)) {
		kmem_cache_t	*cachep;
		/* See comment in kfree() */
		cachep = SLAB_GET_PAGE_CACHE(page);
		if (cachep && cachep->c_flags & SLAB_CFLGS_GENERAL) {
			if (size <= cachep->c_org_size) {	/* XXX better check */
				__kmem_cache_free(cachep, objp);
				return;
			}
		}
	}
null_ptr:
	printk(KERN_ERR "kfree_s: Bad obj %p\n", objp);
	return;
}

kmem_cache_t *
kmem_find_general_cachep(size_t size)
{
	cache_sizes_t	*csizep = cache_sizes;

	/* This function could be moved to the header file, and
	 * made inline so consumers can quickly determine what
	 * cache pointer they require.
	 */
	for (; csizep->cs_size; csizep++) {
		if (size > csizep->cs_size)
			continue;
		break;
	}
	return csizep->cs_cachep;
}


/* Called from try_to_free_page().
 * This function _cannot_ be called within a int, but it
 * can be interrupted.
 */
void
kmem_cache_reap(int gfp_mask)
{
	kmem_slab_t	*slabp;
	kmem_cache_t	*searchp;
	kmem_cache_t	*best_cachep;
	unsigned int	 scan;
	unsigned int	 reap_level;

	if (in_interrupt()) {
		printk("kmem_cache_reap() called within int!\n");
		return;
	}

	/* We really need a test semaphore op so we can avoid sleeping when
	 * !wait is true.
	 */
	down(&cache_chain_sem);

	scan = 10;
	reap_level = 0;

	best_cachep = NULL;
	searchp = clock_searchp;
	do {
		unsigned int	full_free;
		unsigned int	dma_flag;

		/* It's safe to test this without holding the cache-lock. */
		if (searchp->c_flags & SLAB_NO_REAP)
			goto next;
		spin_lock_irq(&searchp->c_spinlock);
		if (searchp->c_growing)
			goto next_unlock;
		if (searchp->c_dflags & SLAB_CFLGS_GROWN) {
			searchp->c_dflags &= ~SLAB_CFLGS_GROWN;
			goto next_unlock;
		}
		/* Sanity check for corruption of static values. */
		if (searchp->c_inuse || searchp->c_magic != SLAB_C_MAGIC) {
			spin_unlock_irq(&searchp->c_spinlock);
			printk(KERN_ERR "kmem_reap: Corrupted cache struct for %s\n", searchp->c_name);
			goto next;
		}
		dma_flag = 0;
		full_free = 0;

		/* Count the fully free slabs.  There should not be not many,
		 * since we are holding the cache lock.
		 */
		slabp = searchp->c_lastp;
		while (!slabp->s_inuse && slabp != kmem_slab_end(searchp)) {
			slabp = slabp->s_prevp;
			full_free++;
			if (slabp->s_dma)
				dma_flag++;
		}
		spin_unlock_irq(&searchp->c_spinlock);

		if ((gfp_mask & GFP_DMA) && !dma_flag)
			goto next;

		if (full_free) {
			if (full_free >= 10) {
				best_cachep = searchp;
				break;
			}

			/* Try to avoid slabs with constructors and/or
			 * more than one page per slab (as it can be difficult
			 * to get high orders from gfp()).
			 */
			if (full_free >= reap_level) {
				reap_level = full_free;
				best_cachep = searchp;
			}
		}
		goto next;
next_unlock:
		spin_unlock_irq(&searchp->c_spinlock);
next:
		searchp = searchp->c_nextp;
	} while (--scan && searchp != clock_searchp);

	clock_searchp = searchp;
	up(&cache_chain_sem);

	if (!best_cachep) {
		/* couldn't find anything to reap */
		return;
	}

	spin_lock_irq(&best_cachep->c_spinlock);
	while (!best_cachep->c_growing &&
	       !(slabp = best_cachep->c_lastp)->s_inuse &&
	       slabp != kmem_slab_end(best_cachep)) {
		if (gfp_mask & GFP_DMA) {
			do {
				if (slabp->s_dma)
					goto good_dma;
				slabp = slabp->s_prevp;
			} while (!slabp->s_inuse && slabp != kmem_slab_end(best_cachep));

			/* Didn't found a DMA slab (there was a free one -
			 * must have been become active).
			 */
			goto dma_fail;
good_dma:
		}
		if (slabp == best_cachep->c_freep)
			best_cachep->c_freep = slabp->s_nextp;
		kmem_slab_unlink(slabp);
		SLAB_STATS_INC_REAPED(best_cachep);

		/* Safe to drop the lock.  The slab is no longer linked to the
		 * cache.
		 */
		spin_unlock_irq(&best_cachep->c_spinlock);
		kmem_slab_destroy(best_cachep, slabp);
		spin_lock_irq(&best_cachep->c_spinlock);
	}
dma_fail:
	spin_unlock_irq(&best_cachep->c_spinlock);
	return;
}

#if	SLAB_SELFTEST
/* A few v. simple tests */
static void
kmem_self_test(void)
{
	kmem_cache_t	*test_cachep;

	printk(KERN_INFO "kmem_test() - start\n");
	test_cachep = kmem_cache_create("test-cachep", 16, 0, SLAB_RED_ZONE|SLAB_POISON, NULL, NULL);
	if (test_cachep) {
		char *objp = kmem_cache_alloc(test_cachep, SLAB_KERNEL);
		if (objp) {
			/* Write in front and past end, red-zone test. */
			*(objp-1) = 1;
			*(objp+16) = 1;
			kmem_cache_free(test_cachep, objp);

			/* Mess up poisoning. */
			*objp = 10;
			objp = kmem_cache_alloc(test_cachep, SLAB_KERNEL);
			kmem_cache_free(test_cachep, objp);

			/* Mess up poisoning (again). */
			*objp = 10;
			kmem_cache_shrink(test_cachep);
		}
	}
	printk(KERN_INFO "kmem_test() - finished\n");
}
#endif	/* SLAB_SELFTEST */

#if	defined(CONFIG_PROC_FS)
/* /proc/slabinfo
 * cache-name num-active-objs total-objs num-active-slabs total-slabs num-pages-per-slab
 */
int
get_slabinfo(char *buf)
{
	kmem_cache_t	*cachep;
	kmem_slab_t	*slabp;
	unsigned long	active_objs;
	unsigned long	save_flags;
	unsigned long	num_slabs;
	unsigned long	num_objs;
	int		len=0;
#if	SLAB_STATS
	unsigned long	active_slabs;
#endif	/* SLAB_STATS */

	__save_flags(save_flags);

	/* Output format version, so at least we can change it without _too_
	 * many complaints.
	 */
#if	SLAB_STATS
	len = sprintf(buf, "slabinfo - version: 1.0 (statistics)\n");
#else
	len = sprintf(buf, "slabinfo - version: 1.0\n");
#endif	/* SLAB_STATS */
	down(&cache_chain_sem);
	cachep = &cache_cache;
	do {
#if	SLAB_STATS
		active_slabs = 0;
#endif	/* SLAB_STATS */
		num_slabs = active_objs = 0;
		spin_lock_irq(&cachep->c_spinlock);
		for (slabp = cachep->c_firstp; slabp != kmem_slab_end(cachep); slabp = slabp->s_nextp) {
			active_objs += slabp->s_inuse;
			num_slabs++;
#if	SLAB_STATS
			if (slabp->s_inuse)
				active_slabs++;
#endif	/* SLAB_STATS */
		}
		num_objs = cachep->c_num*num_slabs;
#if	SLAB_STATS
		{
		unsigned long errors;
		unsigned long high = cachep->c_high_mark;
		unsigned long grown = cachep->c_grown;
		unsigned long reaped = cachep->c_reaped;
		unsigned long allocs = cachep->c_num_allocations;
		errors = (unsigned long) atomic_read(&cachep->c_errors);
		spin_unlock_irqrestore(&cachep->c_spinlock, save_flags);
		len += sprintf(buf+len, "%-16s %6lu %6lu %4lu %4lu %4lu %6lu %7lu %5lu %4lu %4lu\n",
				cachep->c_name, active_objs, num_objs, active_slabs, num_slabs,
				(1<<cachep->c_gfporder)*num_slabs,
				high, allocs, grown, reaped, errors);
		}
#else
		spin_unlock_irqrestore(&cachep->c_spinlock, save_flags);
		len += sprintf(buf+len, "%-17s %6lu %6lu\n", cachep->c_name, active_objs, num_objs);
#endif	/* SLAB_STATS */
	} while ((cachep = cachep->c_nextp) != &cache_cache);
	up(&cache_chain_sem);

	return len;
}
#endif	/* CONFIG_PROC_FS */
