/* loadmmu.c:  This code loads up all the mm function pointers once the
 *             machine type has been determined.  It also sets the static
 *             mmu values such as PAGE_NONE, etc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>

void (*invalidate)(void);

unsigned int pmd_shift, pmd_size, pmd_mask;
unsigned int (*pmd_align)(unsigned int);
unsigned int pgdir_shift, pgdir_size, pgdir_mask;
unsigned int (*pgdir_align)(unsigned int);
unsigned int ptrs_per_pte, ptrs_per_pmd, ptrs_per_pgd;

pgprot_t page_none, page_shared, page_copy, page_readonly, page_kernel;
pgprot_t page_invalid;

/* Grrr... function pointers galore... */
unsigned long (*pte_page)(pte_t);
unsigned long (*pmd_page)(pmd_t);
unsigned long (*pgd_page)(pgd_t);

void (*sparc_update_rootmmu_dir)(struct task_struct *, pgd_t *pgdir);
unsigned long (*(vmalloc_start))(void);
void (*switch_to_context)(int);

int (*pte_none)(pte_t);
int (*pte_present)(pte_t);
int (*pte_inuse)(pte_t *);
void (*pte_clear)(pte_t *);
void (*pte_reuse)(pte_t *);

int (*pmd_none)(pmd_t);
int (*pmd_bad)(pmd_t);
int (*pmd_present)(pmd_t);
int (*pmd_inuse)(pmd_t *);
void (*pmd_clear)(pmd_t *);
void (*pmd_reuse)(pmd_t *);

int (*pgd_none)(pgd_t);
int (*pgd_bad)(pgd_t);
int (*pgd_present)(pgd_t);
int (*pgd_inuse)(pgd_t *);
void (*pgd_clear)(pgd_t *);
void (*pgd_reuse)(pgd_t *);

pte_t (*mk_pte)(unsigned long, pgprot_t);
void (*pgd_set)(pgd_t *, pte_t *);
pte_t (*pte_modify)(pte_t, pgprot_t);
pgd_t * (*pgd_offset)(struct task_struct *, unsigned long);
pmd_t * (*pmd_offset)(pgd_t *, unsigned long);
pte_t * (*pte_offset)(pmd_t *, unsigned long);
void (*pte_free_kernel)(pte_t *);
pte_t * (*pte_alloc_kernel)(pmd_t *, unsigned long);

void (*pmd_free_kernel)(pmd_t *);
pmd_t * (*pmd_alloc_kernel)(pgd_t *, unsigned long);
void (*pte_free)(pte_t *);
pte_t * (*pte_alloc)(pmd_t *, unsigned long);

void (*pmd_free)(pmd_t *);
pmd_t * (*pmd_alloc)(pgd_t *, unsigned long);
void (*pgd_free)(pgd_t *);

pgd_t * (*pgd_alloc)(void);

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
int (*pte_read)(pte_t);
int (*pte_write)(pte_t);
int (*pte_exec)(pte_t);
int (*pte_dirty)(pte_t);
int (*pte_young)(pte_t);
int (*pte_cow)(pte_t);

pte_t (*pte_wrprotect)(pte_t);
pte_t (*pte_rdprotect)(pte_t);
pte_t (*pte_exprotect)(pte_t);
pte_t (*pte_mkclean)(pte_t);
pte_t (*pte_mkold)(pte_t);
pte_t (*pte_uncow)(pte_t);
pte_t (*pte_mkwrite)(pte_t);
pte_t (*pte_mkread)(pte_t);
pte_t (*pte_mkexec)(pte_t);
pte_t (*pte_mkdirty)(pte_t);
pte_t (*pte_mkyoung)(pte_t);
pte_t (*pte_mkcow)(pte_t);

extern void ld_mmu_sun4c(void);
extern void ld_mmu_srmmu(void);

void
load_mmu(void)
{
	switch(sparc_cpu_model) {
	case sun4c:
		ld_mmu_sun4c();
		break;
	case sun4m:
	case sun4d:
	case sun4e:
		ld_mmu_srmmu();
		break;
	default:
		printk("load_mmu:  MMU support not available for this architecture\n");
		printk("load_mmu:  sparc_cpu_model = %d\n", (int) sparc_cpu_model);
		printk("load_mmu:  Halting...\n");
		halt();
	};
	return;
}
