#ifndef __PPC_MMU_CONTEXT_H
#define __PPC_MMU_CONTEXT_H

/* the way contexts are handled on the ppc they are vsid's and
   don't need any special treatment right now.
   perhaps I can defer flushing the tlb by keeping a list of
   zombie vsid/context's and handling that through destroy_context
   later -- Cort

   The MPC8xx has only 16 contexts.  We rotate through them on each
   task switch.  A better way would be to keep track of tasks that
   own contexts, and implement an LRU usage.  That way very active
   tasks don't always have to pay the TLB reload overhead.  The
   kernel pages are mapped shared, so the kernel can run on behalf
   of any task that makes a kernel entry.  Shared does not mean they
   are not protected, just that the ASID comparison is not performed.
        -- Dan
 */

#ifdef CONFIG_8xx
#define NO_CONTEXT      16
#define LAST_CONTEXT    15
#else
#define NO_CONTEXT      0
#define LAST_CONTEXT    0xfffff
#endif

extern int next_mmu_context;
extern void mmu_context_overflow(void);

#ifndef CONFIG_8xx
extern void set_context(int context);
#else
#define set_context(context)    do { } while (0)
#endif

#ifndef CONFIG_8xx
/*
 * Allocating context numbers this way tends to spread out
 * the entries in the hash table better than a simple linear
 * allocation.
 */
#define MUNGE_CONTEXT(n)        (((n) * 897) & LAST_CONTEXT)
#else
#define MUNGE_CONTEXT(n)        (n)
#endif

/*
 * Get a new mmu context for task tsk if necessary.
 */
#define get_mmu_context(tsk)					\
do { 								\
	struct mm_struct *mm = (tsk)->mm;			\
	if (mm->context == NO_CONTEXT) {			\
		if (next_mmu_context == LAST_CONTEXT)		\
			mmu_context_overflow();			\
		mm->context = MUNGE_CONTEXT(++next_mmu_context);\
	 	if ( tsk == current )                           \
			set_context(mm->context);               \
	}							\
} while (0)

/*
 * Set up the context for a new address space.
 */
#define init_new_context(mm)	((mm)->context = NO_CONTEXT)

/*
 * We're finished using the context for an address space.
 */
#ifdef CONFIG_8xx
#define destroy_context(mm)     ((mm)->context = NO_CONTEXT)
#else
#define destroy_context(mm)     do { } while (0)
#endif

/*
 * compute the vsid from the context and segment
 * segments > 7 are kernel segments and their
 * vsid is the segment -- Cort
 */
#define	VSID_FROM_CONTEXT(segment,context) \
   ((segment < 8) ? ((segment) | (context)<<4) : (segment))

#endif
