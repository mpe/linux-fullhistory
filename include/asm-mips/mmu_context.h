/* $Id: mmu_context.h,v 1.3 1998/10/16 19:22:54 ralf Exp $
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

/* Fuck.  The f-word is here so you can grep for it :-)  */
extern unsigned long asid_cache;

/* I patch, therefore I am ...  */
#define ASID_INC(asid)						\
 ({ unsigned long __asid = asid;				\
   __asm__("1:\taddiu\t%0,0\t\t\t\t# patched\n\t"		\
           ".section\t__asid_inc,\"a\"\n\t"			\
           ".word\t1b\n\t"					\
           ".previous"						\
           :"=r" (__asid)					\
           :"0" (__asid));					\
   __asid; })
#define ASID_MASK(asid)						\
 ({ unsigned long __asid = asid;				\
   __asm__("1:\tandi\t%0,%1,0\t\t\t# patched\n\t"			\
           ".section\t__asid_mask,\"a\"\n\t"			\
           ".word\t1b\n\t"					\
           ".previous"						\
           :"=r" (__asid)					\
           :"r" (__asid));					\
   __asid; })
#define ASID_VERSION_MASK					\
 ({ unsigned long __asid;					\
   __asm__("1:\tli\t%0,0\t\t\t\t# patched\n\t"			\
           ".section\t__asid_version_mask,\"a\"\n\t"		\
           ".word\t1b\n\t"					\
           ".previous"						\
           :"=r" (__asid));					\
   __asid; })
#define ASID_FIRST_VERSION					\
 ({ unsigned long __asid = asid;				\
   __asm__("1:\tli\t%0,0\t\t\t\t# patched\n\t"			\
           ".section\t__asid_first_version,\"a\"\n\t"		\
           ".word\t1b\n\t"					\
           ".previous"						\
           :"=r" (__asid));					\
   __asid; })

#define ASID_FIRST_VERSION_R3000 0x1000
#define ASID_FIRST_VERSION_R4000 0x100

extern inline void
get_new_mmu_context(struct mm_struct *mm, unsigned long asid)
{
	if (!ASID_MASK((asid = ASID_INC(asid)))) {
		flush_tlb_all(); /* start new asid cycle */
		if (!asid)      /* fix version if needed */
			asid = ASID_FIRST_VERSION;
	}
	mm->context = asid_cache = asid;
}

extern inline void
get_mmu_context(struct task_struct *p)
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

extern void __asid_setup(unsigned int inc, unsigned int mask,
                         unsigned int version_mask, unsigned int first_version);

extern inline void r3000_asid_setup(void)
{
	__asid_setup(0x40, 0xfc0, 0xf000, ASID_FIRST_VERSION_R3000);
}

extern inline void r6000_asid_setup(void)
{
	panic("r6000_asid_setup: implement me");	/* No idea ...  */
}

extern inline void tfp_asid_setup(void)
{
	panic("tfp_asid_setup: implement me");	/* No idea ...  */
}

extern inline void r4xx0_asid_setup(void)
{
	__asid_setup(1, 0xff, 0xff00, ASID_FIRST_VERSION_R4000);
}

/* R10000 has the same ASID mechanism as the R4000.  */
#define andes_asid_setup r4xx0_asid_setup

#endif /* __ASM_MIPS_MMU_CONTEXT_H */
