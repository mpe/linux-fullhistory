/* $Id: mmu_context.h,v 1.4 1998/05/07 00:40:04 ralf Exp $
 *
 * Switch a MMU context.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998 by Ralf Baechle
 */
#ifndef __ASM_MIPS_MMU_CONTEXT_H
#define __ASM_MIPS_MMU_CONTEXT_H

#define MAX_ASID 255

extern unsigned long asid_cache;

#define ASID_VERSION_SHIFT 16
#define ASID_VERSION_MASK  ((~0UL) << ASID_VERSION_SHIFT)
#define ASID_FIRST_VERSION (1UL << ASID_VERSION_SHIFT)

extern inline void get_new_mmu_context(struct mm_struct *mm, unsigned long asid)
{
	/* check if it's legal.. */
	if ((asid & ~ASID_VERSION_MASK) > MAX_ASID) {
		/* start a new version, invalidate all old asid's */
		flush_tlb_all();
		asid = (asid & ASID_VERSION_MASK) + ASID_FIRST_VERSION;
		if (!asid)
			asid = ASID_FIRST_VERSION;
	}
	asid_cache = asid + 1;
	mm->context = asid;			 /* full version + asid */
}

extern inline void get_mmu_context(struct task_struct *p)
{
	struct mm_struct *mm = p->mm;

	if (mm) {
		unsigned long asid = asid_cache;
		/* Check if our ASID is of an older version and thus invalid */
		if ((mm->context ^ asid) & ASID_VERSION_MASK)
			get_new_mmu_context(mm, asid);
	}
}

/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */
extern inline void init_new_context(struct mm_struct *mm)
{
	mm->context = 0;
}

/*
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
extern inline void destroy_context(struct mm_struct *mm)
{
	mm->context = 0;
}

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
extern inline void activate_context(struct task_struct *tsk)
{
	get_mmu_context(tsk);
	set_entryhi(tsk->mm->context);
}

#endif /* __ASM_MIPS_MMU_CONTEXT_H */
