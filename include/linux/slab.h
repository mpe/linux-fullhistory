/*
 * linux/mm/slab.h
 * Written by Mark Hemment, 1996.
 * (markhe@nextd.demon.co.uk)
 */

#if	!defined(_LINUX_SLAB_H)
#define	_LINUX_SLAB_H

#if	defined(__KERNEL__)

typedef struct kmem_cache_s kmem_cache_t;

#include	<linux/mm.h>

/* flags for kmem_cache_alloc() */
#define	SLAB_BUFFER		GFP_BUFFER	/* 0x00 */
#define	SLAB_ATOMIC		GFP_ATOMIC	/* 0x01 */
#define	SLAB_USER		GFP_USER	/* 0x02 */
#define	SLAB_KERNEL		GFP_KERNEL	/* 0x03 */
#define	SLAB_NOBUFFER		GFP_NOBUFFER	/* 0x04 */
#define	SLAB_NFS		GFP_NFS		/* 0x05 */
#define	SLAB_DMA		GFP_DMA		/* 0x08 */
#define	SLAB_LEVEL_MASK		GFP_LEVEL_MASK	/* 0x0f */
#define	SLAB_NO_GROW		0x00001000UL	/* don't add another slab during an alloc */

/* flags to pass to kmem_cache_create().
 * The first 3 are only valid when the allocator has been build
 * SLAB_DEBUG_SUPPORT.
 */
#define	SLAB_DEBUG_FREE		0x00000100UL	/* Peform time consuming ptr checks on free */
#define	SLAB_DEBUG_INITIAL	0x00000200UL	/* Call constructor, on release, to conform state */
#define	SLAB_RED_ZONE		0x00000400UL	/* Red zone objs in a cache */
#define	SLAB_HWCACHE_ALIGN	0x00000800UL	/* align objs on an hw cache line */

/* flags passed to a constructor func */
#define	SLAB_CTOR_CONSTRUCTOR	0x001UL		/* if not set, then deconstructor */
#define SLAB_CTOR_ATOMIC	0x002UL		/* tell constructor it can't sleep */
#define	SLAB_DTOR_ATOMIC	0x002UL		/* tell deconstructor it can't sleep */
#define	SLAB_CTOR_VERIFY	0x004UL		/* tell constructor it's a verify call */

/* prototypes */
extern long kmem_cache_init(long, long);
extern void kmem_cache_sizes_init(void);
extern struct kmem_cache_s *kmem_cache_create(const char *, unsigned long, unsigned long, unsigned long, void (*)(void *, int, unsigned long), void (*)(void *, int, unsigned long));
extern int kmem_cache_destroy(struct kmem_cache_s *);
extern int kmem_cache_shrink(struct kmem_cache_s *, int);
extern void *kmem_cache_alloc(struct kmem_cache_s *, unsigned long);
extern void kmem_cache_free(struct kmem_cache_s *, void *);
extern void *kmem_alloc(unsigned long, unsigned long);
extern void kmem_free(void *, unsigned long);
extern int kmem_cache_reap(int, int, int);
extern int get_slabinfo(char *);

/* System wide slabs. */
extern kmem_cache_t *vm_area_cachep;

#endif	/* __KERNEL__ */

#endif	/* _LINUX_SLAB_H */
