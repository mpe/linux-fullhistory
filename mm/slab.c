/*
 * linux/mm/slab.c
 * Written by Mark Hemment, 1996.
 * (markhe@nextd.demon.co.uk)
 */
/*
 * An implementation of the Slab Allocator as described in outline in;
 *	UNIX Internals: The New Frontiers by Uresh Vahalia
 *	Pub: Prentice Hall	ISBN 0-13-101908-2
 * or with a little more detail in;
 *	The Slab Allocator: An Object-Caching Kernel Memory Allocator
 *	Jeff Bonwick (Sun Microsystems).
 *      Presented at: USENIX Summer 1994 Technical Conference
 */

#include	<linux/slab.h>
#include	<linux/mm.h>
#include	<linux/interrupt.h>
#include	<asm/system.h>
#include	<asm/cache.h>

/* SLAB_MGMT_CHECKS	- define to enable extra checks in
 *                        kmem_cache_[create|destroy|shrink].
 *			  If you're not messing around with these funcs, then undef this.
 * SLAB_HIGH_PACK	- define to allow 'bufctl's to be stored within objs that do not
 *			  have a state.  This allows more objs per slab, but removes the
 *			  ability to sanity check an addr on release (if the addr is
 *                        within any slab, anywhere, kmem_cache_free() will accept it!).
 * SLAB_DEBUG_SUPPORT	- when defined, kmem_cache_create() will honour; SLAB_DEBUG_FREE,
 *			  SLAB_DEBUG_INITIAL and SLAB_RED_ZONE.
 */
#define		SLAB_MGMT_CHECKS
#undef		SLAB_HIGH_PACK
#define		SLAB_DEBUG_SUPPORT	/* undef this when your cache is stable */

#define	BYTES_PER_WORD	sizeof(void *)

/* legal flag mask for kmem_cache_create() */
#if	defined(SLAB_DEBUG_SUPPORT)
#define	SLAB_C_MASK		(SLAB_DEBUG_FREE|SLAB_DEBUG_INITIAL|SLAB_HWCACHE_ALIGN|SLAB_RED_ZONE)
#else
#define	SLAB_C_MASK		(SLAB_HWCACHE_ALIGN)
#endif	/* SLAB_DEBUG_SUPPORT */

/* Magic num for red zoning.
 * Placed in the first word after the end of an obj
 */
#define	SLAB_RED_MAGIC1		0x5A2CF071UL	/* when obj is active */
#define	SLAB_RED_MAGIC2		0x170FC2A5UL	/* when obj is inactive */

/* Used for linking objs within a slab.  How much of the struct is
 * used, and where its placed, depends on the packing used in a cache.
 * Don't mess with the order!
 */
typedef struct kmem_bufctl_s {
	struct kmem_bufctl_s	*buf_nextp;
	struct kmem_slab_s	*buf_slabp;
	void 			*buf_objp;	/* start of obj */
	struct kmem_bufctl_s	*buf_hnextp;
	struct kmem_bufctl_s	**buf_hashp;
} kmem_bufctl_t;

/* different portions of the bufctl are used - so need some macros */
#define	kmem_bufctl_offset(x) ((unsigned long)&((kmem_bufctl_t *)0)->x)
#define	kmem_bufctl_short_size	(kmem_bufctl_offset(buf_objp))
#define	kmem_bufctl_very_short_size	(kmem_bufctl_offset(buf_slabp))

/* Slab management struct.
 * Manages the objs in a slab.  Placed either at the end of mem allocated
 * for the slab, or from an internal obj cache (SLAB_CFLGS_OFF_SLAB).
 * Slabs are chain into a partially ordered list.  The linking ptrs must
 * be first in the struct!
 * The size of the struct is important(ish);  it should align well on
 * cache line(s)
 */
typedef struct kmem_slab_s {
	struct kmem_slab_s *s_nextp;
	struct kmem_slab_s *s_prevp;
	void		   *s_mem;	/* addr of mem allocated for slab */
	unsigned long	    s_jiffies;
	kmem_bufctl_t	   *s_freep;	/* ptr to first inactive obj in slab */
	unsigned long	    s_flags;
	unsigned long	    s_magic;
	unsigned long	    s_inuse;	/* num of objs active in slab */
} kmem_slab_t;

/* to test for end of slab chain */
#define	kmem_slab_end(x)	((kmem_slab_t*)&((x)->c_firstp))

/* s_magic */
#define	SLAB_MAGIC_ALLOC	0xA5C32F2BUL
#define	SLAB_MAGIC_UNALLOC	0xB2F23C5AUL

/* s_flags */
#define	SLAB_SFLGS_DMA		0x000001UL	/* slab's mem can do DMA */

/* cache struct - manages a cache.
 * c_lastp must appear immediately after c_firstp!
 */
struct kmem_cache_s {
	kmem_slab_t		 *c_freep;	/* first slab w. free objs */
	unsigned long	 	  c_flags;
	unsigned long		  c_offset;
	struct kmem_bufctl_s	**c_hashp;	/* ptr for off-slab bufctls */
	kmem_slab_t		 *c_firstp;	/* first slab in chain */
	kmem_slab_t		 *c_lastp;	/* last slab in chain */
	unsigned long		  c_hashbits;
	unsigned long		  c_num;	/* # of objs per slab */
	unsigned long		  c_gfporder;	/* order of pgs per slab (2^n) */
	unsigned long		  c_org_size;
	unsigned long		  c_magic;
	unsigned long		  c_inuse;	/* kept at zero */
	void (*c_ctor)(void *, int, unsigned long); /* constructor func */
	void (*c_dtor)(void *, int, unsigned long); /* de-constructor func */
	unsigned long		  c_align;	/* alignment of objs */
	unsigned long		  c_colour;	/* cache colouring range */
	unsigned long		  c_colour_next;/* cache colouring */
	const char		 *c_name;
	struct kmem_cache_s	 *c_nextp;
};

/* magic # for c_magic - used to detect out-of-slabs in __kmem_cache_alloc() */
#define	SLAB_C_MAGIC		0x4F17A36DUL

/* internal c_flags */
#define	SLAB_CFLGS_OFF_SLAB	0x010000UL	/* slab mgmt in own cache */
#define	SLAB_CFLGS_BUFCTL	0x020000UL	/* bufctls in own cache */
#define	SLAB_CFLGS_RELEASED	0x040000UL	/* cache is/being destroyed */

#if	defined(SLAB_HIGH_PACK)
#define	SLAB_CFLGS_PTR_IN_OBJ	0x080000UL	/* free ptr in obj */
#endif

#define	SLAB_OFF_SLAB(x)	((x) & SLAB_CFLGS_OFF_SLAB)
#define	SLAB_BUFCTL(x)		((x) & SLAB_CFLGS_BUFCTL)
#define	SLAB_RELEASED(x)	((x) & SLAB_CFLGS_RELEASED)
#if	defined(SLAB_HIGH_PACK)
#define	SLAB_PTR_IN_OBJ(x)	((x) & SLAB_CFLGS_PTR_IN_OBJ)
#else
#define	SLAB_PTR_IN_OBJ(x)	(0)
#endif

/* maximum size of an obj (in 2^order pages) */
#define	SLAB_OBJ_MAX_ORDER	5	/* 32 pages */

/* maximum num of pages for a slab (avoids trying to ask for too may contigious pages) */
#define	SLAB_MAX_GFP_ORDER	5	/* 32 pages */

/* the 'prefered' minimum num of objs per slab - maybe less for large objs */
#define	SLAB_MIN_OBJS_PER_SLAB	4

/* if the num of objs per slab is <= SLAB_MIN_OBJS_PER_SLAB,
 * then the page order must be less than this before trying the next order
 */
#define	SLAB_BREAK_GFP_ORDER	2

/* size of hash tables for caches which use off-slab bufctls (SLAB_CFLGS_BUFCTL) */
#define	KMEM_HASH_SIZE	128

/* size description struct for general-caches */
typedef struct cache_sizes {
	unsigned long	 cs_size;
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
#if	PAGE_SIZE == 8192
	{16384,		NULL},
#endif
	{0,		NULL}
};

/* Names for the general-caches.
 * Not placed into the sizes struct for a good reason; the
 * string ptr is not needed while searching in kmem_alloc()/
 * kmem_free(), and would 'get-in-the-way' - think about it.
 */
static char *cache_sizes_name[] = {
#if	PAGE_SIZE == 4096
	"cache-32",
#endif
	"cache-64",
	"cache-128",
	"cache-256",
	"cache-512",
	"cache-1024",
	"cache-2048",
	"cache-4096",
#if	PAGE_SIZE == 4096
	"cache-8192"
#elif	PAGE_SIZE == 8192
	"cache-8192",
	"cache-16384"
#else
#error	Your page size is not supported for the general-caches - please fix
#endif
};

static void kmem_hash_ctor(void *ptr, int , unsigned long);	/* fwd ref */
extern kmem_cache_t	cache_cache;				/* fwd ref */

/* internal cache of hash objs, only used when bufctls are off-slab */
static	kmem_cache_t	cache_hash = {
/* freep, flags */		kmem_slab_end(&cache_hash), 0,
/* offset, hashp */		sizeof(kmem_bufctl_t*)*KMEM_HASH_SIZE, NULL,
/* firstp, lastp */		kmem_slab_end(&cache_hash), kmem_slab_end(&cache_hash),
/* hashbits, num, gfporder */	0, 0, 0,
/* org_size, magic */		sizeof(kmem_bufctl_t*)*KMEM_HASH_SIZE, SLAB_C_MAGIC,
/* inuse, ctor, dtor, align */	0, kmem_hash_ctor, NULL, L1_CACHE_BYTES,
/* colour, colour_next */	0, 0,
/* name, nextp */		"hash_cache", &cache_cache
};

/* internal cache of freelist mgmnt objs, only use when bufctls are off-slab */
static	kmem_cache_t	cache_bufctl = {
/* freep, flags */		kmem_slab_end(&cache_bufctl), 0,
/* offset, hashp */		sizeof(kmem_bufctl_t), NULL,
/* firstp, lastp */		kmem_slab_end(&cache_bufctl), kmem_slab_end(&cache_bufctl),
/* hashbits, num, gfporder */	0, 0, 0,
/* org_size, magic */		sizeof(kmem_bufctl_t), SLAB_C_MAGIC,
/* inuse, ctor, dtor, align */	0, NULL, NULL, BYTES_PER_WORD*2,
/* colour, colour_next */	0, 0,
/* name, nextp */		"bufctl_cache", &cache_hash
};

/* internal cache of slab mngmnt objs, only used when slab mgmt is off-slab */
static	kmem_cache_t	cache_slab = {
/* freep, flags */		kmem_slab_end(&cache_slab), 0,
/* offset, hashp */		sizeof(kmem_slab_t), NULL,
/* firstp, lastp */		kmem_slab_end(&cache_slab), kmem_slab_end(&cache_slab),
/* hashbits, num, gfporder */	0, 0, 0,
/* org_size, magic */		sizeof(kmem_slab_t), SLAB_C_MAGIC,
/* inuse, ctor, dtor, align */	0, NULL, NULL, L1_CACHE_BYTES,
/* colour, colour_next */	0, 0,
/* name, nextp */		"slab_cache", &cache_bufctl
};

/* internal cache of cache description objs */
static	kmem_cache_t	cache_cache = {
/* freep, flags */		kmem_slab_end(&cache_cache), 0,
/* offset, hashp */		sizeof(kmem_cache_t), NULL,
/* firstp, lastp */		kmem_slab_end(&cache_cache), kmem_slab_end(&cache_cache),
/* hashbits, num, gfporder */	0, 0, 0,
/* org_size, magic */		sizeof(kmem_cache_t), SLAB_C_MAGIC,
/* inuse, ctor, dtor, align */	0, NULL, NULL, L1_CACHE_BYTES,
/* colour, colour_next */	0, 0,
/* name */			"kmem_cache",
/* nextp */			&cache_slab
};

/* constructor for hash tables */
static void kmem_hash_ctor(void *ptr, int size, unsigned long flags)
{
	memset(ptr, 0, sizeof(kmem_bufctl_t*)*KMEM_HASH_SIZE);
}

/* place maintainer for reaping */
static	kmem_cache_t	*clock_searchp = &cache_cache;

/* Init an internal cache */
static void
kmem_own_cache_init(kmem_cache_t *cachep)
{
	unsigned long	size, i;

	if (cachep->c_inuse || cachep->c_magic != SLAB_C_MAGIC) {
		panic("Bad init of internal cache %s", cachep->c_name);
		/* NOTREACHED */
	}
	size = cachep->c_offset + kmem_bufctl_short_size;
	i = size % cachep->c_align;
	if (i)
		size += (cachep->c_align-i);
	cachep->c_offset = size-kmem_bufctl_short_size;
	
	i = ((PAGE_SIZE<<cachep->c_gfporder)-sizeof(kmem_slab_t));
	cachep->c_num = i / size;	/* num of objs per slab */

	/* cache colouring */
	cachep->c_colour = 1 + (i-(cachep->c_num*size))/cachep->c_align;
	cachep->c_colour_next = cachep->c_colour;
}

/* Initialisation - setup all internal caches */
long
kmem_cache_init(long start, long end)
{
	/* sanity */
#define	kmem_cache_offset(x) ((unsigned long)&((kmem_cache_t *)0)->x)
#define	kmem_slab_offset(x) ((unsigned long)&((kmem_slab_t *)0)->x)
	if (((kmem_cache_offset(c_magic)-kmem_cache_offset(c_firstp)) != kmem_slab_offset(s_magic)) ||
	    ((kmem_cache_offset(c_inuse)-kmem_cache_offset(c_firstp)) != kmem_slab_offset(s_inuse))) {
		/* Offsets to the magic are incorrect, either the structures have
		 * been incorrectly changed, or adjustments are needed for your
		 * architecture.
		 */
		panic("kmem_cache_init(): Offsets are different - been messed with!\n");
		/* NOTREACHED */
	}
#undef	kmem_cache_offset
#undef	kmem_slab_offset

	kmem_own_cache_init(&cache_cache);
	kmem_own_cache_init(&cache_slab);
	kmem_own_cache_init(&cache_bufctl);
	kmem_own_cache_init(&cache_hash);
	return start;
}

/* Initialisation - setup general caches */
void
kmem_cache_sizes_init(void)
{
	unsigned long	i;

	i = sizeof(cache_sizes)/sizeof(cache_sizes[0])-1;
	while (i--)
		cache_sizes[i].cs_cachep = kmem_cache_create(cache_sizes_name[i],
							     cache_sizes[i].cs_size,
							     0, 0, NULL, NULL);
}

/* Interface to system's page allocator.
 * dma pts to non-zero if all of the mem is suitable for DMA
 */
static inline void *
kmem_getpages(const kmem_cache_t *cachep, unsigned long flags, unsigned int *dma)
{
	struct page *page;
	void	*addr;

	addr = (void*) __get_free_pages(flags & SLAB_LEVEL_MASK, \
				cachep->c_gfporder, flags & SLAB_DMA); 
	*dma = 1<<cachep->c_gfporder;
	if (!(flags & SLAB_DMA) && addr) {
		/* need to check if can dma */
		page = mem_map + MAP_NR(addr);
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

/* Interface to system's page release */
static inline void
kmem_freepages(kmem_cache_t *cachep, void *addr)
{
	free_pages((unsigned long)addr, cachep->c_gfporder); 
}

/* Hashing function - used for caches with off-slab bufctls */
static inline int
kmem_hash(const kmem_cache_t *cachep, const void *objp)
{
	return (((unsigned long)objp >> cachep->c_hashbits) & (KMEM_HASH_SIZE-1));
}

/* Link bufctl into a hash table - used for caches with off-slab bufctls 
 * - called with ints disabled
 */
static inline void *
kmem_add_to_hash(kmem_cache_t *cachep, kmem_bufctl_t *bufp)
{
	kmem_bufctl_t **bufpp = bufp->buf_hashp;

	bufp->buf_hnextp = *bufpp;
	return (*bufpp = bufp)->buf_objp;
}

/* Find bufcntl for given obj addr, and unlink.
 * - called with ints disabled
 */
static inline kmem_bufctl_t *
kmem_remove_from_hash(kmem_cache_t *cachep, const void *objp)
{
	kmem_bufctl_t	*bufp;
	kmem_bufctl_t	**bufpp = &cachep->c_hashp[kmem_hash(cachep, objp)];

	for (;*bufpp; bufpp = &(*bufpp)->buf_hnextp) {
		if ((*bufpp)->buf_objp != objp)
			continue;
		bufp = *bufpp;
		*bufpp = bufp->buf_hnextp;
		return bufp;
	}
	return NULL;
}

/* Three slab chain funcs - all called with ints disabled */
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
	slabp->s_nextp = kmem_slab_end(cachep);
	slabp->s_prevp = cachep->c_lastp;
	kmem_slab_end(cachep)->s_prevp = slabp;
	slabp->s_prevp->s_nextp = slabp;
}

static inline void
kmem_slab_link_free(kmem_cache_t *cachep, kmem_slab_t *slabp)
{
	kmem_slab_t	*nextp = cachep->c_freep;

	slabp->s_nextp = nextp;
	cachep->c_freep = slabp;
	slabp->s_prevp = nextp->s_prevp;
	nextp->s_prevp = slabp;
	slabp->s_prevp->s_nextp = slabp;
}

/* Cal the num objs, wastage, and bytes left over for a given slab size */
static int
kmem_cache_cal_waste(unsigned long gfporder, unsigned long size,
		     unsigned long extra, unsigned long flags,
		     unsigned long *left_over, unsigned long *num)
{
	unsigned long	wastage;

	wastage = PAGE_SIZE << gfporder;
	gfporder = 0;
	if (!SLAB_OFF_SLAB(flags))
		gfporder = sizeof(kmem_slab_t);
	wastage -= gfporder;
	*num = wastage / size;
	wastage -= (*num * size);
	*left_over = wastage;

	wastage += (extra * *num);
	wastage += gfporder;

	return wastage;
}

/* Create a cache
 * Returns a ptr to the cache on success, NULL on failure.
 * Cannot be called within a int, but can be interrupted.
 * NOTE: The 'name' is assumed to be memory that is _not_  going to disappear.
 */
kmem_cache_t *
kmem_cache_create(const char *name, unsigned long size, unsigned long align,
		  unsigned long flags, void (*ctor)(void*, int, unsigned long),
		  void (*dtor)(void*, int, unsigned long))
{
	const char *func_nm="kmem_create: ";
	kmem_cache_t	*searchp, *cachep;
	unsigned long	words, i;
	unsigned long	num, left_over;

	/* sanity checks */
#if	defined(SLAB_MGMT_CHECKS)
	if (!name) {
		printk(KERN_ERR "%sNULL ptr\n", func_nm);
		return NULL;
	}
	if (in_interrupt()) {
		printk(KERN_ERR "%sCalled during int - %s\n", func_nm, name);
		return NULL;
	}

	if (size < kmem_bufctl_very_short_size) {
		printk(KERN_WARNING "%sSize too small %lu - %s\n", func_nm, size, name);
		size = kmem_bufctl_very_short_size;
	}

	if (size > ((1<<SLAB_OBJ_MAX_ORDER)*PAGE_SIZE)) {
		printk(KERN_ERR "%sSize too large %lu - %s\n", func_nm, size, name);
		return NULL;
	}
#endif	/* SLAB_MGMT_CHECKS */

	/* always checks flags, a caller might be expecting debug support which
	 * isn't available
	 */
	if (flags & ~SLAB_C_MASK) {
		/* Illegal flags */
		printk(KERN_WARNING "%sIllgl flg %lX - %s\n", func_nm, flags, name);
		flags &= SLAB_C_MASK;
	}

#if	defined(SLAB_MGMT_CHECKS)
	if (align < 0 || align >= size) {
		printk(KERN_WARNING "%sAlign weired %lu - %s\n", func_nm, align, name);
		align = 0;
	}

	if (dtor && !ctor) {
		/* Descon, but no con - doesn't make sense */
		printk(KERN_ERR "%sDecon but no con - %s\n", func_nm, name);
		return NULL;
	}

	if ((flags & SLAB_DEBUG_INITIAL) && !ctor) {
		/* No constructor, but inital state check requested */
		printk(KERN_WARNING "%sNo con, but init state check requested - %s\n",
		       func_nm, name);
		flags &= ~SLAB_DEBUG_INITIAL;
	}
#endif	/* SLAB_MGMT_CHECKS */

	/* get cache's description obj */
	cachep = (kmem_cache_t *) kmem_cache_alloc(&cache_cache, SLAB_KERNEL);
	if (!cachep)
		goto opps;

	/* remember original size, so can be passed to a constructor or decon.
	 * Allows the same con/decon to be used for caches of similar objs
	 * that have a different size data buffer assoicated with them
	 */
	cachep->c_org_size = size;

#if	defined(SLAB_DEBUG_SUPPORT)
	if (flags & SLAB_RED_ZONE)
		size += BYTES_PER_WORD;		/* word for redzone */
#endif	/* SLAB_DEBUG_SUPPORT */

	/* Make a guess if slab mngmnt obj and/or bufctls are 'on' or 'off' slab */
	i = kmem_bufctl_short_size;
	if (size < (PAGE_SIZE>>3)) {
		/* Size is small(ish).  Use format where bufctl size per
		 * obj is low, and slab mngmnt is on-slab
		 */
		if (!ctor && !dtor && !(flags & SLAB_RED_ZONE)) {
			/* the objs in this cache have no state - can store
			 * store freelist ptr within obj. (redzoning is a state)
			 */
#if	defined(SLAB_HIGH_PACK)
			i=0;
			flags |= SLAB_CFLGS_PTR_IN_OBJ;
#else
			i = kmem_bufctl_very_short_size;
#endif
		}
	} else {
		/* Size is large, assume best to place the slab mngmnt obj
		 * off-slab (should allow better packing of objs)
		 */
		flags |= SLAB_CFLGS_OFF_SLAB;
		if (!(size & ~PAGE_MASK) ||
		    size == (PAGE_SIZE+PAGE_SIZE/2) ||
		    size == (PAGE_SIZE/2) ||
		    size == (PAGE_SIZE/4) ||
		    size == (PAGE_SIZE/8)) {
			/* to avoid waste the bufctls are off-slab */
			flags |= SLAB_CFLGS_BUFCTL;
			/* get hash table for cache */
			cachep->c_hashp = kmem_cache_alloc(&cache_hash, SLAB_KERNEL);
			if (cachep->c_hashp == NULL) {
				kmem_cache_free(&cache_cache, cachep);
				goto opps;
			}
			i = 0;
			cachep->c_hashbits = PAGE_SHIFT;
			if (size <= (PAGE_SIZE/2)) {
				cachep->c_hashbits--;
				if (size <= (PAGE_SIZE/4)) cachep->c_hashbits--;
				if (size <= (PAGE_SIZE/8)) cachep->c_hashbits -= 2;
			}
		}  /* else slab mngmnt is off-slab, but freelist ptrs are on */
	}
	size += i;

	/* Adjust the mem used for objs so they will align correctly.
	 * Force objs to start on word boundaries, but caller may specify
	 * h/w cache line boundaries.  This 'alignment' is slightly different
	 * to the 'align' argument.  Objs may be requested to start on h/w
	 * lines (as that is how the members of the obj have been organised),
	 * but the 'align' may be quite high (say 64) as the first 64 bytes
	 * are commonly accessed/modified within a loop (stops h/w line
	 * thrashing).  The 'align' is the slab colouring.
	 */
	words = BYTES_PER_WORD;
	if (flags & SLAB_HWCACHE_ALIGN)
		words = L1_CACHE_BYTES;
	words--;
	size += words;
	size = size & ~words;
	/* alignment might not be a factor of the boundary alignment - fix-up */
	align += words;
	align = align & ~words;


	/* Cal size (in pages) of slabs, and the num of objs per slab.
	 * This could be made much more intelligent. */
	cachep->c_gfporder=0;
	do {
		unsigned long wastage;
		wastage = kmem_cache_cal_waste(cachep->c_gfporder, size, i,
					       flags, &left_over, &num);
		if (!num)
			goto next;
		if (SLAB_PTR_IN_OBJ(flags))
			break;
		if (cachep->c_gfporder == SLAB_MAX_GFP_ORDER)
			break;
		/* large num of objs is good, but v. large slabs are bad for the
		 * VM sub-system
		 */
		if (num <= SLAB_MIN_OBJS_PER_SLAB) {
			if (cachep->c_gfporder < SLAB_BREAK_GFP_ORDER)
				goto next;
		}
		/* stop caches with small objs having a large num of pages */
		if (left_over <= sizeof(kmem_slab_t))
			break;
		if ((wastage*8) <= (PAGE_SIZE<<cachep->c_gfporder))
			break;	/* acceptable wastage */
next:
		cachep->c_gfporder++;
	} while (1);
	cachep->c_num = num;

	/* try with requested alignment, but reduce it if that will
	 * allow at least some alignment words
	 */
	words++;
	if (left_over < align)
		align = (left_over / words) * words;
	else if (!align && words <= left_over) {
		/* no alignment given, but space enough - give one */
		align = words;
		if (words == BYTES_PER_WORD) {
			if (BYTES_PER_WORD*4 <= left_over)
				align += align;
			if (BYTES_PER_WORD*8 <= left_over)
				align += align;
		}
	}
	cachep->c_align = align;

#if	0
	printk("Size:%lu Orig:%lu Left:%lu Align %lu Pages:%d - %s\n",
	       size, cachep->c_org_size, left_over, align, 1<<cachep->c_gfporder, name);
	if (SLAB_OFF_SLAB(flags)) printk("OFF SLAB\n");
	if (SLAB_BUFCTL(flags)) printk("BUFCTL PTRS\n");
#endif

	/* if the bufctl's are on-slab, c_offset does not inc the size of the bufctl */
	if (!SLAB_BUFCTL(flags))
		size -= kmem_bufctl_short_size;
	cachep->c_freep = kmem_slab_end(cachep);
	cachep->c_flags = flags;
	cachep->c_offset = size;
	cachep->c_firstp = kmem_slab_end(cachep);
	cachep->c_lastp = kmem_slab_end(cachep);
	cachep->c_ctor = ctor;
	cachep->c_dtor = dtor;
	cachep->c_magic = SLAB_C_MAGIC;
	cachep->c_inuse = 0;		/* always zero */
	cachep->c_name = name;		/* simply point to the name */

	cachep->c_colour = 1;
	if (align) 
		cachep->c_colour += (left_over/align);
	cachep->c_colour_next = cachep->c_colour;

	/* warn on dup cache names */
	searchp = &cache_cache;
	do {
		if (!strcmp(searchp->c_name, name)) {
			printk(KERN_WARNING "%sDup name - %s\n", func_nm, name);
			break;
		}
		searchp = searchp->c_nextp;
	} while (searchp != &cache_cache);
	cachep->c_nextp = cache_cache.c_nextp;
	cache_cache.c_nextp = cachep;
	return cachep;
opps:
	printk(KERN_WARNING "%sOut of mem creating cache %s\n", func_nm, name);
	return NULL;
}

/* Destroy all the objs in a slab, and release the mem back to the system.
 * Before calling the slab must have been unlinked
 */
static void
kmem_slab_destroy(kmem_cache_t *cachep, kmem_slab_t *slabp, unsigned long flags)
{
	if (cachep->c_dtor || SLAB_BUFCTL(cachep->c_flags)) {
		kmem_bufctl_t	*bufp = slabp->s_freep;

		/* for each obj in slab... */
		while (bufp) {
			kmem_bufctl_t	*freep;
			if (cachep->c_dtor) {
				void	*objp = ((void*)bufp)-cachep->c_offset;
				if (SLAB_BUFCTL(cachep->c_flags))
					objp = bufp->buf_objp;
				(cachep->c_dtor)(objp, cachep->c_org_size, flags);
			}
			freep = bufp;
			bufp = bufp->buf_nextp;
			if (SLAB_BUFCTL(cachep->c_flags))
				kmem_cache_free(&cache_bufctl, freep);
		}
	}

	slabp->s_magic = SLAB_MAGIC_UNALLOC;
	kmem_freepages(cachep, slabp->s_mem);
	if (SLAB_OFF_SLAB(cachep->c_flags))
		kmem_cache_free(&cache_slab, slabp);
}

/* Destroy (remove) a cache.
 * All objs in the cache should be inactive
 */
int
kmem_cache_destroy(kmem_cache_t *cachep)
{
	kmem_cache_t	**searchp;
	kmem_slab_t	*slabp;
	unsigned long	save_flags;

#if	defined(SLAB_MGMT_CHECKS)
	if (!cachep) {
		printk(KERN_ERR "kmem_dest: NULL ptr\n");
		goto err_end;
	}

	if (in_interrupt()) {
		printk(KERN_ERR "kmem_dest: Called during int - %s\n", cachep->c_name);
err_end:
		return 1;
	}
#endif	/* SLAB_MGMT_CHECKS */

	/* unlink the cache from the chain of active caches.
	 * Note: the chain is never modified during an int
	 */
	searchp = &(cache_cache.c_nextp);
	for (;*searchp != &cache_cache; searchp = &((*searchp)->c_nextp)) {
		if (*searchp != cachep)
			continue;
		goto good_cache;
	}
	printk(KERN_ERR "kmem_dest: Invalid cache addr %p\n", cachep);
	return 1;
good_cache:
	/* disable cache so attempts to allocated from an int can
	 * be caught.
	 */
	save_flags(save_flags);
	cli();
	if (cachep->c_freep != kmem_slab_end(cachep)) {
		restore_flags(save_flags);
		printk(KERN_ERR "kmem_dest: active cache - %s\n", cachep->c_name);
		return 2;
	}
	*searchp = cachep->c_nextp;	/* remove from cache chain */
	cachep->c_flags |= SLAB_CFLGS_RELEASED;
	cachep->c_freep = kmem_slab_end(cachep);
	if (cachep == clock_searchp)
		clock_searchp = cachep->c_nextp;
	restore_flags(save_flags);

	while ((slabp = cachep->c_firstp) != kmem_slab_end(cachep)) {
		kmem_slab_unlink(slabp);
		kmem_slab_destroy(cachep, slabp, 0);
	}

	if (SLAB_BUFCTL(cachep->c_flags))
		kmem_cache_free(&cache_hash, cachep->c_hashp);
	kmem_cache_free(&cache_cache, cachep);
	return 0;
}

/* Shrink a cache, ie. remove _all_ inactive slabs.
 * Can be called when a user of a cache knows they are not going to be
 * needing any new objs for a while.
 * NOTE: This func is probably going to disappear - let me know if you
 * are using it!
 */
int
kmem_cache_shrink(kmem_cache_t *cachep, int wait)
{
	kmem_slab_t	*slabp;
	unsigned long	dtor_flags;
	unsigned long	save_flags, num_freed=0;

#if	defined(SLAB_MGMT_CHECKS)
	if (!cachep) {
		printk(KERN_ERR "kmem_shrink: NULL ptr\n");
		goto end;
	}

	if (in_interrupt()) {
		printk(KERN_ERR "kmem_shrink: Called during int - %s\n", cachep->c_name);
		goto end;
	}
#endif	/* SLAB_MGMT_CHECKS */

	dtor_flags = 0;
	if (!wait)	/* not allowed to wait */
		dtor_flags = SLAB_DTOR_ATOMIC;

	save_flags(save_flags);
	while (0) {
		cli();
		slabp = cachep->c_lastp;
		if (slabp == kmem_slab_end(cachep) || slabp->s_inuse) {
			restore_flags(save_flags);
			goto end;
		}
		kmem_slab_unlink(slabp);
		if (cachep->c_freep == slabp)
			cachep->c_freep = kmem_slab_end(cachep);
		restore_flags(save_flags);
		num_freed++;
		kmem_slab_destroy(cachep, slabp, dtor_flags);
	}
end:
	return num_freed;
}

/* Search for a slab whose objs are suitable for DMA.
 * Note: since testing the first free slab (in __kmem_cache_alloc()),
 * ints must not have been enabled!
 */
static inline kmem_slab_t *
kmem_cache_search_dma(kmem_cache_t *cachep)
{
	kmem_slab_t	*slabp = cachep->c_freep->s_nextp;

	for (; slabp != kmem_slab_end(cachep); slabp = slabp->s_nextp) {
		if (!(slabp->s_flags & SLAB_SFLGS_DMA))
			continue;
		kmem_slab_unlink(slabp);
		kmem_slab_link_free(cachep, slabp);
		return slabp;
	}
	return NULL;
}

/* get the mem for a slab mgmt obj */
static inline kmem_slab_t *
kmem_cache_slabmgmt(kmem_cache_t *cachep, void *objp, unsigned long local_flags, unsigned long offset)
{
	kmem_slab_t	*slabp;

	if (SLAB_OFF_SLAB(cachep->c_flags)) {
		/* slab mngmnt obj is off-slab */
		if (!(slabp = kmem_cache_alloc(&cache_slab, local_flags)))
			return NULL;
	} else {
		/* slab mngmnt at end of slab mem */
		slabp = objp + (PAGE_SIZE << cachep->c_gfporder);
		slabp--;
		if (!SLAB_PTR_IN_OBJ(cachep->c_flags)) {
			/* A bit of extra help for the L1 cache; try to position the slab
			 * mgmnt struct at different offsets within the gap at the end
			 * of a slab.  This helps avoid thrashing the h/w cache lines,
			 * that map to the end of a page, too much...
			 */
			unsigned long gap = cachep->c_offset;
			if (!SLAB_BUFCTL(cachep->c_flags))
				gap += kmem_bufctl_short_size;
			gap = (PAGE_SIZE << cachep->c_gfporder)-((gap*cachep->c_num)+offset+sizeof(*slabp));
			gap /= (sizeof(*slabp)/2); 
			gap *= (sizeof(*slabp)/2); 
			slabp = (((void*)slabp)-gap);
		}
	}

	slabp->s_flags = slabp->s_inuse = slabp->s_jiffies = 0;

	return slabp;
}

static inline int
kmem_cache_init_objs(kmem_cache_t *cachep, kmem_slab_t *slabp, void *objp,
		     unsigned long local_flags, unsigned long ctor_flags)
{
	kmem_bufctl_t	**bufpp = &slabp->s_freep;
	unsigned long	num = cachep->c_num;

	do {
		if (SLAB_BUFCTL(cachep->c_flags)) {
			if (!(*bufpp = kmem_cache_alloc(&cache_bufctl, local_flags))) {
				kmem_slab_destroy(cachep, slabp, 0);
				return 1;
			}
			(*bufpp)->buf_objp = objp;
			(*bufpp)->buf_hashp = &cachep->c_hashp[kmem_hash(cachep, objp)];
		}

		if (cachep->c_ctor)
			cachep->c_ctor(objp, cachep->c_org_size, ctor_flags);

#if	defined(SLAB_DEBUG_SUPPORT)
		if (cachep->c_flags & SLAB_RED_ZONE)
			*((unsigned long*)(objp+cachep->c_org_size)) = SLAB_RED_MAGIC1;
#endif	/* SLAB_DEBUG_SUPPORT */

		objp += cachep->c_offset;
		if (!SLAB_BUFCTL(cachep->c_flags)) {
			*bufpp = objp;
			objp += kmem_bufctl_short_size;
		}
		if (!SLAB_PTR_IN_OBJ(cachep->c_flags))
			(*bufpp)->buf_slabp = slabp;
		bufpp = &(*bufpp)->buf_nextp;
	} while (--num);
	*bufpp = NULL;
	return 0;
}

/* Grow (by 1) the number of slabs within a cache.
 * This is called by kmem_cache_alloc() when there are no
 * inactive objs left in a cache
 */
static void
kmem_cache_grow(kmem_cache_t *cachep, unsigned long flags)
{
	kmem_slab_t	*slabp;
	void		*objp;
	unsigned int	offset, dma;
	unsigned long	ctor_flags, local_flags, save_flags;

	if (flags & SLAB_NO_GROW)
		return; /* caller doesn't want us to grow */

	save_flags(save_flags);
	/* The test for missing atomic flag is performed here, rather than
	 * the more obvious place, simply to reduce the critical path length
	 * in kmem_cache_alloc().  If a caller is slightly mis-behaving,
	 * will eventually be caught here (where it matters)
	 */
	if (in_interrupt() && (flags & SLAB_LEVEL_MASK) != SLAB_ATOMIC) {
		static int count = 0;
		if (count < 8) {
			printk(KERN_ERR "kmem_grow: Called nonatomically from "
			       "int - %s\n", cachep->c_name);
			count++;
		}
		flags &= ~SLAB_LEVEL_MASK;
		flags |= SLAB_ATOMIC;
	}
	local_flags = (flags & SLAB_LEVEL_MASK);
	ctor_flags = SLAB_CTOR_CONSTRUCTOR;
	if ((flags & SLAB_LEVEL_MASK) == SLAB_ATOMIC) {
		/* Not allowed to sleep.
		 * Need to tell a constructor about this - it
		 * might need to know....
		 */
		ctor_flags |= SLAB_CTOR_ATOMIC;
	}

	slabp = NULL;
	/* get mem for the objs */
	if (!(objp = kmem_getpages(cachep, flags, &dma)))
		goto opps1;

	/* get colour for the slab, and cal the next value */
	cli();
	if (!(offset = --(cachep->c_colour_next)))
		cachep->c_colour_next = cachep->c_colour;
	restore_flags(save_flags);
	offset *= cachep->c_align;

	/* get slab mgmt */
	if (!(slabp = kmem_cache_slabmgmt(cachep, objp, local_flags, offset)))
		goto opps2;
	if (dma)
		slabp->s_flags = SLAB_SFLGS_DMA;
	
	slabp->s_mem = objp;
	objp += offset;		/* address of first object */

	/* For on-slab bufctls, c_offset is the distance between the start of
	 * an obj and its related bufctl.  For off-slab bufctls, c_offset is
	 * the distance between objs in the slab.
	 * Reason for bufctl at end of obj (when on slab), as opposed to the front;
	 * if stored within the obj (has no state), and the obj is 'used' after being
	 * freed then (normally) most activity occurs at the beginning of the obj.
	 * By keeping the bufctl ptr away from the front, should reduce the chance of
	 * corruption.  Also, allows easier alignment of objs onto cache lines when
	 * bufctl is not stored with the objs.
	 * Downsize; if, while an obj is active, a write is made past its end, then the
	 * bufctl will be corrupted :(
	 */
	if (kmem_cache_init_objs(cachep, slabp, objp, local_flags, ctor_flags))
		goto no_objs;

	cli();
	/* make slab active */
	slabp->s_magic = SLAB_MAGIC_ALLOC;
	kmem_slab_link_end(cachep, slabp);
	if (cachep->c_freep == kmem_slab_end(cachep))
		cachep->c_freep = slabp;
	restore_flags(save_flags);
	return;
no_objs:
	kmem_freepages(cachep, slabp->s_mem); 
opps2:
	kmem_freepages(cachep, objp); 
opps1:
	if (slabp && SLAB_OFF_SLAB(cachep->c_flags))
		kmem_cache_free(&cache_slab, slabp);
	/* printk("kmem_alloc: Out of mem - %s\n", cachep->c_name); */
	return;
}

#if	defined(SLAB_DEBUG_SUPPORT)
/* Perform extra freeing checks.
 * Currently, this check is only for caches that use bufctl structures
 * within the slab.  Those which use bufctl's from the internal cache
 * have a reasonable check when the address is searched for.
 */
static void *
kmem_extra_free_checks(const kmem_cache_t *cachep, kmem_bufctl_t *search_bufp,
		       const kmem_bufctl_t *bufp, void * objp)
{
	if (SLAB_BUFCTL(cachep->c_flags))
		goto end;

	/* check slab's freelist to see if this obj is there */
	for (; search_bufp; search_bufp = search_bufp->buf_nextp) {
		if (search_bufp != bufp)
			continue;
		printk(KERN_ERR "kmem_free: Double free detected during checking "
		       "%p - %s\n", objp, cachep->c_name);
		return NULL;
	}
end:
	return objp;
}
#endif	/* SLAB_DEBUG_SUPPORT */

static inline void
kmem_cache_full_free(kmem_cache_t *cachep, kmem_slab_t *slabp)
{
	if (!slabp->s_nextp->s_inuse)
		return;		/* at correct position */
	slabp->s_jiffies = jiffies;	/* set release time */
	if (cachep->c_freep == slabp)
		cachep->c_freep = slabp->s_nextp;
	kmem_slab_unlink(slabp);
	kmem_slab_link_end(cachep, slabp);

	return;
}

static inline void
kmem_cache_one_free(kmem_cache_t *cachep, kmem_slab_t *slabp)
{
	if (slabp->s_nextp->s_inuse != cachep->c_num) {
		cachep->c_freep = slabp;
		return;
	}
	kmem_slab_unlink(slabp);
	kmem_slab_link_free(cachep, slabp);
	return;
}

/* Returns a ptr to an obj in the given cache.
 * The obj is in the initial state (if there is one)
 */
static inline void *
__kmem_cache_alloc(kmem_cache_t *cachep, unsigned long flags)
{
	kmem_slab_t	*slabp;
	kmem_bufctl_t	*bufp;
	void		*objp;
	unsigned long	save_flags;

	/* sanity check */
	if (!cachep)
		goto nul_ptr;
	save_flags(save_flags);
	cli();
	/* get slab alloc is to come from */
	slabp = cachep->c_freep;

	/* magic is a sanity check _and_ says if we need a new slab */
	if (slabp->s_magic != SLAB_MAGIC_ALLOC)
		goto alloc_new_slab;
try_again:
	/* DMA allocations are 'rare' - keep out of critical path */
	if (flags & SLAB_DMA)
		goto search_dma;
try_again_dma:
	slabp->s_inuse++;
	bufp = slabp->s_freep;
	slabp->s_freep = bufp->buf_nextp;
	if (!SLAB_BUFCTL(cachep->c_flags)) {
		/* Nasty - we want the 'if' to be taken in the common case */
		if (slabp->s_freep) {
short_finished:
			objp = ((void*)bufp) - cachep->c_offset;
			restore_flags(save_flags);
#if	defined(SLAB_DEBUG_SUPPORT)
			if (cachep->c_flags & SLAB_RED_ZONE)
				goto red_zone;
#endif	/* SLAB_DEBUG_SUPPORT */
			return objp;
		} else {
			cachep->c_freep = slabp->s_nextp;
			goto short_finished;
		}
	}

	if (!slabp->s_freep)
		cachep->c_freep = slabp->s_nextp;

	/* link into hash chain */
	objp = kmem_add_to_hash(cachep, bufp);
	restore_flags(save_flags);
#if	defined(SLAB_DEBUG_SUPPORT)
	if (!(cachep->c_flags & SLAB_RED_ZONE))
#endif	/* SLAB_DEBUG_SUPPORT */
		return objp;

#if	defined(SLAB_DEBUG_SUPPORT)
red_zone:
	/* set alloc red-zone, and check old one */
	if (xchg((unsigned long *)(objp+cachep->c_org_size), SLAB_RED_MAGIC2) != SLAB_RED_MAGIC1)
		printk(KERN_ERR "kmem_alloc: Bad redzone %p - %s\n",
		       objp, cachep->c_name);
	return objp;
#endif	/* SLAB_DEBUG_SUPPORT */

search_dma:
	if (slabp->s_flags & SLAB_SFLGS_DMA)
		goto try_again_dma;
	/* need to search... */
	if ((slabp = kmem_cache_search_dma(cachep)))
		goto try_again_dma;
alloc_new_slab:
	/* Either out of slabs, or magic number corruption */
	if (slabp != kmem_slab_end(cachep))
		goto bad_slab;
	/* need a new slab */
	restore_flags(save_flags);
	if (SLAB_RELEASED(cachep->c_flags)) {
		printk(KERN_ERR "kmem_alloc: destroyed cache\n");
		goto end;
	}

	/* Be lazy and only check for valid flags
	 * here (keeping it out of the critical path above)
	 */
	if (flags & ~(SLAB_DMA|SLAB_LEVEL_MASK|SLAB_NO_GROW)) {
		printk(KERN_ERR "kmem_alloc: Illegal flgs %lX (correcting) - %s\n",
		       flags, cachep->c_name);
		flags &= (SLAB_DMA|SLAB_LEVEL_MASK|SLAB_NO_GROW);
	}

	kmem_cache_grow(cachep, flags);
	cli();
	if ((slabp=cachep->c_freep) != kmem_slab_end(cachep))
		goto try_again;
	restore_flags(save_flags);
end:
	return NULL;
bad_slab:
	/* v. serious error - maybe panic() here? */
	printk(KERN_ERR "kmem_alloc: Bad slab magic (corruption) - %s\n",
	       cachep->c_name);
	goto end;
nul_ptr:
	printk(KERN_ERR "kmem_alloc: NULL ptr\n");
	goto end;
}

/* Release an obj back to its cache.
 * If the obj has a constructed state, it should be
 * in this state _before_ it is released.
 */
static inline void
__kmem_cache_free(kmem_cache_t *cachep, void *objp)
{
	kmem_slab_t	*slabp;
	kmem_bufctl_t	*bufp;
	unsigned long	save_flags;

	/* basic sanity checks */
	if (!cachep)
		goto nul_cache;
	if (!objp)
		goto nul_obj;

	save_flags(save_flags);
#if	defined(SLAB_DEBUG_SUPPORT)
	if (cachep->c_flags & SLAB_DEBUG_INITIAL)
		goto init_state_check;
finished_initial:
#endif	/* SLAB_DEBUG_SUPPORT */

	if (SLAB_BUFCTL(cachep->c_flags))
		goto bufctl;

	bufp = (kmem_bufctl_t *)(objp+cachep->c_offset);

	/* get slab for the obj */
	if (SLAB_PTR_IN_OBJ(cachep->c_flags)) {
		/* if SLAB_HIGH_PACK is undef, the below is optimised away */		
		slabp = (kmem_slab_t *)((((unsigned long)objp)&PAGE_MASK)+PAGE_SIZE);
		slabp--;
	} else
		slabp = (kmem_slab_t *) bufp->buf_slabp;

	if (slabp->s_magic != SLAB_MAGIC_ALLOC)		/* sanity check */
		goto bad_obj;
	cli();

#if	defined(SLAB_DEBUG_SUPPORT)
	if (cachep->c_flags & (SLAB_DEBUG_FREE|SLAB_RED_ZONE))
		goto extra_checks;
#endif	/* SLAB_DEBUG_SUPPORT */

passed_extra:
	if (!slabp->s_inuse)			/* sanity check */
		goto too_many;
	bufp->buf_nextp = slabp->s_freep;
	slabp->s_freep = bufp;
	if (--(slabp->s_inuse)) {
		if (bufp->buf_nextp) {
			restore_flags(save_flags);
			return;
		}
		kmem_cache_one_free(cachep, slabp);
		restore_flags(save_flags);
		return;
	}
	kmem_cache_full_free(cachep, slabp);
	restore_flags(save_flags);
	return;
bufctl:
	/* Off-slab bufctls.  Need to search hash for bufctl, and hence the slab.
	 * No 'extra' checks are performed for objs stored this way, finding
	 * the obj a check enough
	 */
	cli();
	if ((bufp = kmem_remove_from_hash(cachep, objp))) {
		slabp = (kmem_slab_t *) bufp->buf_slabp;
#if	defined(SLAB_DEBUG_SUPPORT)
		if (cachep->c_flags & SLAB_RED_ZONE)
			goto red_zone;
#endif	/* SLAB_DEBUG_SUPPORT */
		goto passed_extra;
	}
	restore_flags(save_flags);
	printk(KERN_ERR "kmem_free: Either bad obj addr or double free: %p - %s\n",
	       objp, cachep->c_name);
	return;
#if	defined(SLAB_DEBUG_SUPPORT)
red_zone:
	if (xchg((unsigned long *)(objp+cachep->c_org_size), SLAB_RED_MAGIC1) != SLAB_RED_MAGIC2) {
		/* Either write past end of the object, or a double free */
		printk(KERN_ERR "kmem_free: Bad redzone %p - %s\n",
		       objp, cachep->c_name);
	}
	goto passed_extra;
init_state_check:
	/* Need to call the slab's constructor so that
	 * the caller can perform a verify of its state (debugging)
	 */
	cachep->c_ctor(objp, cachep->c_org_size, SLAB_CTOR_CONSTRUCTOR|SLAB_CTOR_VERIFY);
	goto finished_initial;
extra_checks:
	if ((cachep->c_flags & SLAB_DEBUG_FREE) &&
	    (objp != kmem_extra_free_checks(cachep, slabp->s_freep, bufp, objp))) {
		restore_flags(save_flags);
		return;
	}
	if (cachep->c_flags & SLAB_RED_ZONE)
		goto red_zone;
	goto passed_extra;
#endif	/* SLAB_DEBUG_SUPPORT */
bad_obj:
	/* The addr of the slab doesn't contain the correct
	 * magic num
	 */
	if (slabp->s_magic == SLAB_MAGIC_UNALLOC) {
		/* magic num says this is an unalloc slab */
		printk(KERN_ERR "kmem_free: obj %p from destroyed slab - %s\n",
		       objp, cachep->c_name);
		return;
	}
	printk(KERN_ERR "kmem_free: Bad obj %p - %s\n", objp, cachep->c_name);
	return;
too_many:
	/* don't add to freelist */
	restore_flags(save_flags);
	printk(KERN_ERR "kmem_free: obj free for slab with no active objs - %s\n",
	       cachep->c_name);
	return;
nul_obj:
	printk(KERN_ERR "kmem_free: NULL obj - %s\n", cachep->c_name);
	return;
nul_cache:
	printk(KERN_ERR "kmem_free: NULL cache ptr\n");
	return;
}

void *
kmem_cache_alloc(kmem_cache_t *cachep, unsigned long flags)
{
	return __kmem_cache_alloc(cachep, flags);
}

void
kmem_cache_free(kmem_cache_t *cachep, void *objp)
{
	__kmem_cache_free(cachep, objp);
}

void *
kmem_alloc(unsigned long size, unsigned long flags)
{
	cache_sizes_t	*cachep = cache_sizes;

	for (; cachep->cs_size; cachep++) {
		if (size > cachep->cs_size)
			continue;
		/* should the inline version be used here? */
		return kmem_cache_alloc(cachep->cs_cachep, flags);
	}
	printk(KERN_ERR "kmem_alloc: Size (%lu) too large\n", size);
	return NULL;
}

void
kmem_free(void *objp, unsigned long size)
{
	cache_sizes_t	*cachep = cache_sizes;

	for (; cachep->cs_size; cachep++) {
		if (size > cachep->cs_size)
			continue;
		/* should the inline version be used here? */
		kmem_cache_free(cachep->cs_cachep, objp);
		return;
	}
	printk(KERN_ERR "kmem_free: Size (%lu) too large - strange\n", size);
}



/* Called from try_to_free_page().
 * Ideal solution would have a weight for each cache, based on;
 *	o num of fully free slabs
 *	o if the objs have a constructor/deconstructor
 *	o length of time slabs have been fully free (ie. ageing)
 * This function _cannot_ be called within a int, but it
 * can be interrupted.
 */
int
kmem_cache_reap(int pri, int dma, int wait)
{
	unsigned long	 dtor_flags = 0;
	unsigned long	 best_jiffie;
	unsigned long	 now;
	int		count = 8;
	kmem_slab_t	*best_slabp = NULL;
	kmem_cache_t	*best_cachep = NULL;
	kmem_slab_t	*slabp;
	kmem_cache_t	*searchp;
	unsigned long	save_flags;

	/* 'pri' maps to the number of caches to examine, not the number of slabs.
	 * This avoids only checking the jiffies for slabs in one cache at the
	 * expensive spending more cycles
	 */
	pri = (9 - pri);
	if (!wait)	/* not allowed to wait */
		dtor_flags = SLAB_DTOR_ATOMIC;
	searchp = clock_searchp;
	save_flags(save_flags);
	now = jiffies;
	best_jiffie = now - (2*HZ);	/* 2secs - avoid heavy thrashing */
	while (pri--) {
		kmem_slab_t	*local_slabp;
		unsigned long	local_jiffie;
		if (searchp == &cache_cache)
			goto next;

		/* sanity check for corruption */
		if (searchp->c_inuse || searchp->c_magic != SLAB_C_MAGIC) {
			printk(KERN_ERR "kmem_reap: Corrupted cache struct for %s\n",
			       searchp->c_name);
			goto next;
		}

		local_slabp = NULL;
		local_jiffie = now - (2*HZ);
		cli();
		/* As the fully free slabs, within a cache, have no particular
		 * order, we need to test them all.  Infact, we only check 'count'
		 * slabs.
		 */
		slabp = searchp->c_lastp;
		for (;count && slabp != kmem_slab_end(searchp) && !slabp->s_inuse; slabp = slabp->s_prevp, count--) {
			if (slabp->s_jiffies >= local_jiffie)
				continue;

			/* weight caches with a con/decon */
			if ((searchp->c_ctor || searchp->c_dtor) && slabp->s_jiffies >= (local_jiffie - (2*HZ)))
				continue;

			/* weight caches with high page orders.  Avoids stressing the
			 * VM sub-system by reducing the frequency requests for a large
			 * num of contigious pages
			 */
			if (searchp->c_gfporder > 1 && slabp->s_jiffies >= (local_jiffie - (4*HZ)))
				continue;

			local_jiffie = slabp->s_jiffies;
			local_slabp = slabp;
			if (!searchp->c_gfporder && (now-local_jiffie) >= (300*HZ)) {
				/* an old, one page slab.  Make a quick get away... */
				pri = 0;
				break;
			}
		}
		if (local_slabp) {
			if (!count || local_jiffie < best_jiffie) {
				best_slabp = local_slabp;
				best_jiffie = local_jiffie;
				best_cachep = searchp;
				if (!count)
					break;
			}
		}
		restore_flags(save_flags);
next:
		searchp = searchp->c_nextp;
		if (searchp == clock_searchp)
			break;
		count = 8;	/* # of slabs at which we force a reap */
	}

	/* only move along with we didn't find an over allocated cache */
	if (count)
		clock_searchp = clock_searchp->c_nextp;

	if (!best_slabp)
		return 0;

	cli();
	if (best_slabp->s_inuse) {
		/* an object in our selected slab has been
		 * allocated.  This souldn't happen v. often, so we
		 * simply fail - which isn't ideal but will do.
		 * NOTE: No test for the case where an obj has been
		 * allocated from the slab, and then freed.  While
		 * this would change our idea of the best slab to
		 * reap, it's not worth the re-calculation effort.
		 */
		restore_flags(save_flags);
		return 0;
	}

	if (best_cachep->c_freep == best_slabp)
		best_cachep->c_freep = best_slabp->s_nextp;
	kmem_slab_unlink(best_slabp);

	restore_flags(save_flags);
	kmem_slab_destroy(best_cachep, best_slabp, dtor_flags);

	return 1;
}

/* /proc/slabinfo
 *  cache-name num-active-objs total-objs num-active-slabs total-slabs num-pages-per-slab
 */
int
get_slabinfo(char *buf)
{
	kmem_cache_t	*cachep;
	kmem_slab_t	*slabp;
	unsigned long	active_objs;
	unsigned long	num_slabs, active_slabs;
	unsigned long	save_flags;
	int		len=0;

	/* output format version, so at least we can change it without _too_
	 * many complaints
	 */
	len = sprintf(buf, "slabinfo - version: 1.0\n");
	save_flags(save_flags);
	cachep = &cache_cache;
	do {
		active_slabs = num_slabs = active_objs = 0;
		cli();
		for (slabp = cachep->c_firstp;
		     slabp != kmem_slab_end(cachep);
		     slabp = slabp->s_nextp) {
			num_slabs++;
			active_objs += slabp->s_inuse;
			if (slabp->s_inuse)
				active_slabs++;
		}
		restore_flags(save_flags);
		len += sprintf(buf+len, "%-20s%lu %lu %lu %lu %d\n", cachep->c_name,
			       active_objs, cachep->c_num*num_slabs,
			       active_slabs, num_slabs, 1<<cachep->c_gfporder);
	} while ((cachep = cachep->c_nextp) != &cache_cache);
	return len;
}
