#ifndef __ALPHA_MMU_CONTEXT_H
#define __ALPHA_MMU_CONTEXT_H

/*
 * get a new mmu context..
 *
 * Copyright (C) 1996, Linus Torvalds
 */

#include <linux/config.h>
#include <asm/system.h>

/*
 * The maximum ASN's the processor supports.  On the EV4 this is 63
 * but the PAL-code doesn't actually use this information.  On the
 * EV5 this is 127.
 *
 * On the EV4, the ASNs are more-or-less useless anyway, as they are
 * only used as a icache tag, not for TB entries.  On the EV5 ASN's
 * also validate the TB entries, and thus make a lot more sense.
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
#ifdef CONFIG_ALPHA_EV5
#define MAX_ASN 127
#else
#define MAX_ASN 63
#define BROKEN_ASN 1
#endif

extern unsigned long asn_cache;

#define ASN_VERSION_SHIFT 16
#define ASN_VERSION_MASK ((~0UL) << ASN_VERSION_SHIFT)
#define ASN_FIRST_VERSION (1UL << ASN_VERSION_SHIFT)

extern inline void get_new_mmu_context(struct task_struct *p,
	struct mm_struct *mm,
	unsigned long asn)
{
	/* check if it's legal.. */
	if ((asn & ~ASN_VERSION_MASK) > MAX_ASN) {
		/* start a new version, invalidate all old asn's */
		tbiap(); imb();
		asn = (asn & ASN_VERSION_MASK) + ASN_FIRST_VERSION;
		if (!asn)
			asn = ASN_FIRST_VERSION;
	}
	asn_cache = asn + 1;
	mm->context = asn;			/* full version + asn */
	p->tss.asn = asn & ~ASN_VERSION_MASK;	/* just asn */
}

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
extern inline void get_mmu_context(struct task_struct *p)
{
#ifndef BROKEN_ASN
	struct mm_struct * mm = p->mm;

	if (mm) {
		unsigned long asn = asn_cache;
		/* Check if our ASN is of an older version and thus invalid */
		if ((mm->context ^ asn) & ASN_VERSION_MASK)
			get_new_mmu_context(p, mm, asn);
	}
#endif
}

#endif
