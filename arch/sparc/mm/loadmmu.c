/* $Id: loadmmu.c,v 1.23 1996/02/21 17:56:35 miguel Exp $
 * loadmmu.c:  This code loads up all the mm function pointers once the
 *             machine type has been determined.  It also sets the static
 *             mmu values such as PAGE_NONE, etc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>

#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>

struct ctx_list *ctx_list_pool;
struct ctx_list ctx_free;
struct ctx_list ctx_used;

unsigned long (*alloc_kernel_stack)(struct task_struct *tsk);
void (*free_kernel_stack)(unsigned long stack);
struct task_struct *(*alloc_task_struct)(void);
void (*free_task_struct)(struct task_struct *tsk);

void (*quick_kernel_fault)(unsigned long);

void (*mmu_exit_hook)(void);
void (*mmu_flush_hook)(void);

char *(*mmu_lockarea)(char *, unsigned long);
void  (*mmu_unlockarea)(char *, unsigned long);
char *(*mmu_get_scsi_buffer)(char *, unsigned long, struct linux_sbus *sbus);
void  (*mmu_release_scsi_buffer)(char *, unsigned long, struct linux_sbus *sbus);


void (*update_mmu_cache)(struct vm_area_struct *vma, unsigned long address, pte_t pte);

void (*invalidate_all)(void);
void (*invalidate_mm)(struct mm_struct *);
void (*invalidate_range)(struct mm_struct *, unsigned long start, unsigned long end);
void (*invalidate_page)(struct vm_area_struct *, unsigned long address);

void (*set_pte)(pte_t *pteptr, pte_t pteval);

unsigned int pmd_shift, pmd_size, pmd_mask;
unsigned int (*pmd_align)(unsigned int);
unsigned int pgdir_shift, pgdir_size, pgdir_mask;
unsigned int (*pgdir_align)(unsigned int);
unsigned int ptrs_per_pte, ptrs_per_pmd, ptrs_per_pgd;
unsigned int pg_iobits;

pgprot_t page_none, page_shared, page_copy, page_readonly, page_kernel;

unsigned long (*pte_page)(pte_t);
unsigned long (*pmd_page)(pmd_t);
unsigned long (*pgd_page)(pgd_t);

void (*sparc_update_rootmmu_dir)(struct task_struct *, pgd_t *pgdir);
unsigned long (*(vmalloc_start))(void);
void (*switch_to_context)(struct task_struct *tsk);

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
pte_t (*mk_pte_io)(unsigned long, pgprot_t);
void (*pgd_set)(pgd_t *, pmd_t *);
pte_t (*pte_modify)(pte_t, pgprot_t);
pgd_t * (*pgd_offset)(struct mm_struct *, unsigned long);
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

int (*pte_write)(pte_t);
int (*pte_dirty)(pte_t);
int (*pte_young)(pte_t);

pte_t (*pte_wrprotect)(pte_t);
pte_t (*pte_mkclean)(pte_t);
pte_t (*pte_mkold)(pte_t);
pte_t (*pte_mkwrite)(pte_t);
pte_t (*pte_mkdirty)(pte_t);
pte_t (*pte_mkyoung)(pte_t);

char *(*mmu_info)(void);

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
		printk("load_mmu:MMU support not available for this architecture\n");
		printk("load_mmu:sparc_cpu_model = %d\n", (int) sparc_cpu_model);
		printk("load_mmu:Halting...\n");
		panic("load_mmu()");
	};
}
