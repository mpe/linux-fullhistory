#ifndef __ALPHA_MMU_CONTEXT_H
#define __ALPHA_MMU_CONTEXT_H

/*
 * get a new mmu context..
 *
 * Copyright (C) 1996, Linus Torvalds
 */

#include <asm/pgtable.h>

/*
 * The maximum ASN's the processor supports. On the EV4 this doesn't
 * matter as the pal-code doesn't use the ASNs anyway, on the EV5
 * EV5 this is 127.
 */
#define MAX_ASN 127

#define ASN_VERSION_SHIFT 32
#define ASN_VERSION_MASK ((~0UL) << ASN_VERSION_SHIFT)
#define ASN_FIRST_VERSION (1UL << ASN_VERSION_SHIFT)

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
	static unsigned long asn_cache = ASN_FIRST_VERSION;
	struct mm_struct * mm = p->mm;
	unsigned long asn = mm->context;

	/* Check if our ASN is of an older version and thus invalid */
	if ((asn_cache ^ asn) & ASN_VERSION_MASK) {
		/* get a new asn of the current version */
		asn = asn_cache++;
		/* check if it's legal.. */
		if ((asn & ~ASN_VERSION_MASK) > MAX_ASN) {
			/* start a new version, invalidate all old asn's */
			tbiap();
			asn_cache = (asn_cache & ASN_VERSION_MASK) + ASN_FIRST_VERSION;
			if (!asn_cache)
				asn_cache = ASN_FIRST_VERSION;
			asn = asn_cache++;
		}
		mm->context = asn;			/* full version + asn */
		p->tss.asn = asn & ~ASN_VERSION_MASK;	/* just asn */
	}
}

#endif
