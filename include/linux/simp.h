/*
 * include/linux/simp.h  -- simple allocator for cached objects
 *
 * This is meant as a faster and simpler (not full-featured) replacement
 * for SLAB, thus the name "simp" :-)
 *
 * (C) 1997 Thomas Schoebel-Theuer
 */

#ifndef SIMP_H
#define SIMP_H

/* used for constructors / destructors */
typedef void (*structor)(void *);

/* create an object cache */
/* positive clearable_offset means the next two pointers at that offset
 * can be internally used for freelist pointers when the object is
 * deallocated / not in use;
 * if there is no space inside the element that can be reused for
 * this purpose, supply -1. Using positive offsets is essential for
 * saving space with very small-sized objects.
 *
 * Note for big-sized objects in the range of whole pages, use
 * the fast Linux page allocator instead, directly.
 */
extern struct simp * simp_create(char * name, long size, long clearable_offset,
				 structor first_ctor, 
				 structor again_ctor, 
				 structor dtor);

/* alloc / dealloc routines */
extern void * simp_alloc(struct simp * simp);
extern void simp_free(void * objp);

/* garbage collection */
extern long simp_garbage(void);

#endif
