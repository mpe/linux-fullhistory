#ifndef __ALPHA_MMU_CONTEXT_H
#define __ALPHA_MMU_CONTEXT_H

/*
 * get a new mmu context..
 *
 * Copyright (C) 1996, Linus Torvalds
 */

#include <linux/config.h>
#include <asm/system.h>
#include <asm/machvec.h>

/*
 * The maximum ASN's the processor supports.  On the EV4 this is 63
 * but the PAL-code doesn't actually use this information.  On the
 * EV5 this is 127, and EV6 has 255.
 *
 * On the EV4, the ASNs are more-or-less useless anyway, as they are
 * only used as an icache tag, not for TB entries.  On the EV5 and EV6,
 * ASN's also validate the TB entries, and thus make a lot more sense.
 *
 * The EV4 ASN's don't even match the architecture manual, ugh.  And
 * I quote: "If a processor implements address space numbers (ASNs),
 * and the old PTE has the Address Space Match (ASM) bit clear (ASNs
 * in use) and the Valid bit set, then entries can also effectively be
 * made coherent by assigning a new, unused ASN to the currently
 * running process and not reusing the previous ASN before calling the
 * appropriate PALcode routine to invalidate the translation buffer
 * (TB)". 
 *
 * In short, the EV4 has a "kind of" ASN capability, but it doesn't actually
 * work correctly and can thus not be used (explaining the lack of PAL-code
 * support).
 */
#define EV4_MAX_ASN 63
#define EV5_MAX_ASN 127
#define EV6_MAX_ASN 255

#ifdef CONFIG_ALPHA_GENERIC
# define MAX_ASN	(alpha_mv.max_asn)
#else
# ifdef CONFIG_ALPHA_EV4
#  define MAX_ASN	EV4_MAX_ASN
# elif defined(CONFIG_ALPHA_EV5)
#  define MAX_ASN	EV5_MAX_ASN
# else
#  define MAX_ASN	EV6_MAX_ASN
# endif
#endif

/*
 * cpu_last_asn(processor):
 * 63                                            0
 * +-------------+----------------+--------------+
 * | asn version | this processor | hardware asn |
 * +-------------+----------------+--------------+
 */

#ifdef __SMP__
#include <asm/smp.h>
#define cpu_last_asn(cpuid)	(cpu_data[cpuid].last_asn)
#else
extern unsigned long last_asn;
#define cpu_last_asn(cpuid)	last_asn
#endif /* __SMP__ */

#define WIDTH_HARDWARE_ASN	8
#define WIDTH_THIS_PROCESSOR	5
#define ASN_FIRST_VERSION (1UL << (WIDTH_THIS_PROCESSOR + WIDTH_HARDWARE_ASN))
#define HARDWARE_ASN_MASK ((1UL << WIDTH_HARDWARE_ASN) - 1)

/*
 * NOTE! The way this is set up, the high bits of the "asn_cache" (and
 * the "mm->context") are the ASN _version_ code. A version of 0 is
 * always considered invalid, so to invalidate another process you only
 * need to do "p->mm->context = 0".
 *
 * If we need more ASN's than the processor has, we invalidate the old
 * user TLB's (tbiap()) and start a new ASN version. That will automatically
 * force a new asn for any other processes the next time they want to
 * run.
 */

#ifndef __EXTERN_INLINE
#define __EXTERN_INLINE extern inline
#define __MMU_EXTERN_INLINE
#endif

extern void get_new_mmu_context(struct task_struct *p, struct mm_struct *mm);

static inline unsigned long
__get_new_mmu_context(struct task_struct *p, struct mm_struct *mm)
{
	unsigned long asn = cpu_last_asn(smp_processor_id());
	unsigned long next = asn + 1;

	if ((next ^ asn) & ~MAX_ASN) {
		tbiap();
		next = (asn & ~HARDWARE_ASN_MASK) + ASN_FIRST_VERSION;
	}
	cpu_last_asn(smp_processor_id()) = next;
	mm->context = next;                      /* full version + asn */
	return next;
}

__EXTERN_INLINE void
ev4_get_mmu_context(struct task_struct *p)
{
	/* As described, ASN's are broken.  But we can optimize for
	   switching between threads -- if the mm is unchanged from
	   current we needn't flush.  */
	if (current->mm != p->mm)
		tbiap();
}

__EXTERN_INLINE void
ev5_get_mmu_context(struct task_struct *p)
{
	/* Check if our ASN is of an older version, or on a different CPU,
	   and thus invalid.  */

	long asn = cpu_last_asn(smp_processor_id());
	struct mm_struct *mm = p->mm;
	long mmc = mm->context;
	
	if ((p->tss.mm_context ^ asn) & ~HARDWARE_ASN_MASK) {
		if ((mmc ^ asn) & ~HARDWARE_ASN_MASK)
			mmc = __get_new_mmu_context(p, mm);
		p->tss.mm_context = mmc;
		p->tss.asn = mmc & HARDWARE_ASN_MASK;
	}
}

#ifdef CONFIG_ALPHA_GENERIC
# define get_mmu_context		(alpha_mv.mv_get_mmu_context)
#else
# ifdef CONFIG_ALPHA_EV4
#  define get_mmu_context		ev4_get_mmu_context
# else
#  define get_mmu_context		ev5_get_mmu_context
# endif
#endif

extern inline void
init_new_context(struct mm_struct *mm)
{
	mm->context = 0;
}

extern inline void
destroy_context(struct mm_struct *mm)
{
	/* Nothing to do.  */
}

#ifdef __MMU_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __MMU_EXTERN_INLINE
#endif

/*
 * Force a context reload. This is needed when we change the page
 * table pointer or when we update the ASN of the current process.
 */

/* Don't get into trouble with dueling __EXTERN_INLINEs.  */
#ifndef __EXTERN_INLINE
#include <asm/io.h>
#endif

extern inline unsigned long
__reload_tss(struct thread_struct *tss)
{
	register unsigned long a0 __asm__("$16");
	register unsigned long v0 __asm__("$0");

	a0 = virt_to_phys(tss);
	__asm__ __volatile__(
		"call_pal %2 #__reload_tss"
		: "=r"(v0), "=r"(a0)
		: "i"(PAL_swpctx), "r"(a0)
		: "$1", "$16", "$22", "$23", "$24", "$25");

	return v0;
}

extern inline void
reload_context(struct task_struct *task)
{
	__reload_tss(&task->tss);
}

/*
 * After setting current->mm to a new value, activate the context for the
 * new mm so we see the new mappings.
 */

extern inline void
activate_context(struct task_struct *task)
{
	get_new_mmu_context(task, task->mm);
	reload_context(task);
}

#endif /* __ALPHA_MMU_CONTEXT_H */
