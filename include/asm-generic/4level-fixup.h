#ifndef _4LEVEL_FIXUP_H
#define _4LEVEL_FIXUP_H

#define __ARCH_HAS_4LEVEL_HACK

#define PUD_SIZE			PGDIR_SIZE
#define PUD_MASK			PGDIR_MASK
#define PTRS_PER_PUD			1

#define pud_t				pgd_t

#define pmd_alloc(mm, pud, address)			\
({	pmd_t *ret;					\
	if (pgd_none(*pud))				\
 		ret = __pmd_alloc(mm, pud, address);	\
 	else						\
		ret = pmd_offset(pud, address);		\
 	ret;						\
})

#define pud_alloc(mm, pgd, address)	(pgd)
#define pud_offset(pgd, start)		(pgd)
#define pud_none(pud)			0
#define pud_bad(pud)			0
#define pud_present(pud)		1
#define pud_ERROR(pud)			do { } while (0)
#define pud_clear(pud)			pgd_clear(pud)

#undef pud_free_tlb
#define pud_free_tlb(tlb, x)            do { } while (0)
#define pud_free(x)			do { } while (0)
#define __pud_free_tlb(tlb, x)		do { } while (0)

#endif
