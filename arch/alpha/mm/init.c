/*
 *  linux/arch/alpha/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/hwrpb.h>
#include <asm/dma.h>

#define DEBUG_POISON 0

extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);

struct thread_struct original_pcb;

#ifndef __SMP__
struct pgtable_cache_struct quicklists;
#endif

void
__bad_pmd(pgd_t *pgd)
{
	printk("Bad pgd in pmd_alloc: %08lx\n", pgd_val(*pgd));
	pgd_set(pgd, BAD_PAGETABLE);
}

void
__bad_pte(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
	pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
}

pmd_t *
get_pmd_slow(pgd_t *pgd, unsigned long offset)
{
	pmd_t *pmd;

	pmd = (pmd_t *) __get_free_page(GFP_KERNEL);
	if (pgd_none(*pgd)) {
		if (pmd) {
			clear_page((unsigned long)pmd);
			pgd_set(pgd, pmd);
			return pmd + offset;
		}
		pgd_set(pgd, BAD_PAGETABLE);
		return NULL;
	}
	free_page((unsigned long)pmd);
	if (pgd_bad(*pgd)) {
		__bad_pmd(pgd);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + offset;
}

pte_t *
get_pte_slow(pmd_t *pmd, unsigned long offset)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_KERNEL);
	if (pmd_none(*pmd)) {
		if (pte) {
			clear_page((unsigned long)pte);
			pmd_set(pmd, pte);
			return pte + offset;
		}
		pmd_set(pmd, (pte_t *) BAD_PAGETABLE);
		return NULL;
	}
	free_page((unsigned long)pte);
	if (pmd_bad(*pmd)) {
		__bad_pte(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + offset;
}

int do_check_pgt_cache(int low, int high)
{
	int freed = 0;
        if(pgtable_cache_size > high) {
                do {
                        if(pgd_quicklist)
                                free_pgd_slow(get_pgd_fast()), freed++;
                        if(pmd_quicklist)
                                free_pmd_slow(get_pmd_fast()), freed++;
                        if(pte_quicklist)
                                free_pte_slow(get_pte_fast()), freed++;
                } while(pgtable_cache_size > low);
        }
        return freed;
}

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving an inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
pmd_t *
__bad_pagetable(void)
{
	memset((void *) EMPTY_PGT, 0, PAGE_SIZE);
	return (pmd_t *) EMPTY_PGT;
}

pte_t
__bad_page(void)
{
	memset((void *) EMPTY_PGE, 0, PAGE_SIZE);
	return pte_mkdirty(mk_pte((unsigned long) EMPTY_PGE, PAGE_SHARED));
}

void
show_mem(void)
{
	long i,free = 0,total = 0,reserved = 0;
	long shared = 0, cached = 0;

	printk("\nMem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!atomic_read(&mem_map[i].count))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
	}
	printk("%ld pages of RAM\n",total);
	printk("%ld free pages\n",free);
	printk("%ld reserved pages\n",reserved);
	printk("%ld pages shared\n",shared);
	printk("%ld pages swap cached\n",cached);
	printk("%ld pages in page table cache\n",pgtable_cache_size);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

extern unsigned long free_area_init(unsigned long, unsigned long);

static struct thread_struct *
load_PCB(struct thread_struct * pcb)
{
	register unsigned long sp __asm__("$30");
	pcb->ksp = sp;
	return __reload_tss(pcb);
}

/*
 * paging_init() sets up the page tables: in the alpha version this actually
 * unmaps the bootup page table (as we're now in KSEG, so we don't need it).
 */
unsigned long
paging_init(unsigned long start_mem, unsigned long end_mem)
{
	int i;
	unsigned long newptbr;
	struct memclust_struct * cluster;
	struct memdesc_struct * memdesc;
	struct thread_struct *original_pcb_ptr;

	/* initialize mem_map[] */
	start_mem = free_area_init(start_mem, end_mem);

	/* find free clusters, update mem_map[] accordingly */
	memdesc = (struct memdesc_struct *)
		(hwrpb->mddt_offset + (unsigned long) hwrpb);
	cluster = memdesc->cluster;
	for (i = memdesc->numclusters ; i > 0; i--, cluster++) {
		unsigned long pfn, nr;

		/* Bit 0 is console/PALcode reserved.  Bit 1 is
		   non-volatile memory -- we might want to mark
		   this for later */
		if (cluster->usage & 3)
			continue;
		pfn = cluster->start_pfn;
		nr = cluster->numpages;

		while (nr--)
			clear_bit(PG_reserved, &mem_map[pfn++].flags);
	}

	/* Initialize the kernel's page tables.  Linux puts the vptb in
	   the last slot of the L1 page table.  */
	memset((void *) ZERO_PAGE, 0, PAGE_SIZE);
	memset(swapper_pg_dir, 0, PAGE_SIZE);
	newptbr = ((unsigned long) swapper_pg_dir - PAGE_OFFSET) >> PAGE_SHIFT;
	pgd_val(swapper_pg_dir[1023]) =
		(newptbr << 32) | pgprot_val(PAGE_KERNEL);

	/* Set the vptb.  This is often done by the bootloader, but 
	   shouldn't be required.  */
	if (hwrpb->vptb != 0xfffffffe00000000) {
		wrvptptr(0xfffffffe00000000);
		hwrpb->vptb = 0xfffffffe00000000;
		hwrpb_update_checksum(hwrpb);
	}

	/* Also set up the real kernel PCB while we're at it.  */
	init_task.tss.ptbr = newptbr;
	init_task.tss.pal_flags = 1;	/* set FEN, clear everything else */
	init_task.tss.flags = 0;
	original_pcb_ptr = load_PCB(&init_task.tss);
	tbia();

	/* Save off the contents of the original PCB so that we can
	   restore the original console's page tables for a clean reboot.

	   Note that the PCB is supposed to be a physical address, but
	   since KSEG values also happen to work, folks get confused.
	   Check this here.  */

	if ((unsigned long)original_pcb_ptr < PAGE_OFFSET) {
		original_pcb_ptr = (struct thread_struct *)
		  phys_to_virt((unsigned long) original_pcb_ptr);
	}
	original_pcb = *original_pcb_ptr;

	return start_mem;
}

#ifdef __SMP__
/*
 * paging_init_secondary(), called ONLY by secondary CPUs,
 * sets up current->tss contents appropriately and does a load_PCB.
 * note that current should be pointing at the idle thread task struct
 * for this CPU.
 */
void
paging_init_secondary(void)
{
	current->tss.ptbr = init_task.tss.ptbr;
	current->tss.pal_flags = 1;
	current->tss.flags = 0;
	load_PCB(&current->tss);
	tbia();

	return;
}
#endif /* __SMP__ */

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM)
void
srm_paging_stop (void)
{
	/* Move the vptb back to where the SRM console expects it.  */
	swapper_pg_dir[1] = swapper_pg_dir[1023];
	tbia();
	wrvptptr(0x200000000);
	hwrpb->vptb = 0x200000000;
	hwrpb_update_checksum(hwrpb);

	/* Reload the page tables that the console had in use.  */
	load_PCB(&original_pcb);
	tbia();
}
#endif

#if DEBUG_POISON
static void
kill_page(unsigned long pg)
{
	unsigned long *p = (unsigned long *)pg;
	unsigned long i = PAGE_SIZE, v = 0xdeadbeefdeadbeef;
	do {
		p[0] = v;
		p[1] = v;
		p[2] = v;
		p[3] = v;
		p[4] = v;
		p[5] = v;
		p[6] = v;
		p[7] = v;
		i -= 64;
		p += 8;
	} while (i != 0);
}
#else
#define kill_page(pg)
#endif

void
mem_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long tmp;

	end_mem &= PAGE_MASK;
	max_mapnr = num_physpages = MAP_NR(end_mem);
	high_memory = (void *) end_mem;
	start_mem = PAGE_ALIGN(start_mem);

	/*
	 * Mark the pages used by the kernel as reserved.
	 */
	tmp = KERNEL_START;
	while (tmp < start_mem) {
		set_bit(PG_reserved, &mem_map[MAP_NR(tmp)].flags);
		tmp += PAGE_SIZE;
	}

	for (tmp = PAGE_OFFSET ; tmp < end_mem ; tmp += PAGE_SIZE) {
		if (tmp >= MAX_DMA_ADDRESS)
			clear_bit(PG_DMA, &mem_map[MAP_NR(tmp)].flags);
		if (PageReserved(mem_map+MAP_NR(tmp)))
			continue;
		atomic_set(&mem_map[MAP_NR(tmp)].count, 1);
#ifdef CONFIG_BLK_DEV_INITRD
		if (initrd_start && tmp >= initrd_start && tmp < initrd_end)
			continue;
#endif
		kill_page(tmp);
		free_page(tmp);
	}
	tmp = nr_free_pages << PAGE_SHIFT;
	printk("Memory: %luk available\n", tmp >> 10);
	return;
}

void
free_initmem (void)
{
	extern char __init_begin, __init_end;
	unsigned long addr;

	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		mem_map[MAP_NR(addr)].flags &= ~(1 << PG_reserved);
		atomic_set(&mem_map[MAP_NR(addr)].count, 1);
		kill_page(addr);
		free_page(addr);
	}
	printk ("Freeing unused kernel memory: %ldk freed\n",
		(&__init_end - &__init_begin) >> 10);
}

void
si_meminfo(struct sysinfo *val)
{
	int i;

	i = max_mapnr;
	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = buffermem;
	while (i-- > 0)  {
		if (PageReserved(mem_map+i))
			continue;
		val->totalram++;
		if (!atomic_read(&mem_map[i].count))
			continue;
		val->sharedram += atomic_read(&mem_map[i].count) - 1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
	return;
}
