/*
 *  $Id: init.c,v 1.139 1998/12/29 19:53:49 cort Exp $
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
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
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>		/* for initrd_* */
#endif

#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/residual.h>
#include <asm/uaccess.h>
#include <asm/8xx_immap.h>
#include <asm/mbx.h>
#include <asm/smp.h>
#include <asm/bootx.h>
/* APUS includes */
#include <asm/setup.h>
#include <asm/amigahw.h>
/* END APUS includes */

int prom_trashed;
atomic_t next_mmu_context;
unsigned long *end_of_DRAM;
int mem_init_done;
extern pgd_t swapper_pg_dir[];
extern char _start[], _end[];
extern char etext[], _stext[];
extern char __init_begin, __init_end;
extern char __prep_begin, __prep_end;
extern char __pmac_begin, __pmac_end;
extern char __openfirmware_begin, __openfirmware_end;
char *klimit = _end;
struct device_node *memory_node;
unsigned long ioremap_base;
unsigned long ioremap_bot;
unsigned long avail_start;
struct pgtable_cache_struct quicklists;
extern int num_memory;
extern struct mem_info memory[NUM_MEMINFO];
extern boot_infos_t *boot_infos;

void MMU_init(void);
static void *MMU_get_page(void);
unsigned long *prep_find_end_of_memory(void);
unsigned long *pmac_find_end_of_memory(void);
unsigned long *apus_find_end_of_memory(void);
extern unsigned long *find_end_of_memory(void);
#ifdef CONFIG_MBX
unsigned long *mbx_find_end_of_memory(void);
#endif /* CONFIG_MBX */
static void mapin_ram(void);
void map_page(struct task_struct *, unsigned long va,
		     unsigned long pa, int flags);
extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);


/*
 * The following stuff defines a data structure for representing
 * areas of memory as an array of (address, length) pairs, and
 * procedures for manipulating them.
 */
#define MAX_MEM_REGIONS	32

struct mem_pieces {
	int n_regions;
	struct reg_property regions[MAX_MEM_REGIONS];
};
struct mem_pieces phys_mem;
struct mem_pieces phys_avail;
struct mem_pieces prom_mem;

static void remove_mem_piece(struct mem_pieces *, unsigned, unsigned, int);
void *find_mem_piece(unsigned, unsigned);
static void print_mem_pieces(struct mem_pieces *);
static void append_mem_piece(struct mem_pieces *, unsigned, unsigned);

extern struct task_struct *current_set[NR_CPUS];

PTE *Hash, *Hash_end;
unsigned long Hash_size, Hash_mask;
#ifndef CONFIG_8xx
unsigned long _SDR1;
static void hash_init(void);
union ubat {			/* BAT register values to be loaded */
	BAT	bat;
	P601_BAT bat_601;
	u32	word[2];
} BATS[4][2];			/* 4 pairs of IBAT, DBAT */

struct batrange {		/* stores address ranges mapped by BATs */
	unsigned long start;
	unsigned long limit;
	unsigned long phys;
} bat_addrs[4];
#endif /* CONFIG_8xx */

/*
 * this tells the system to map all of ram with the segregs
 * (i.e. page tables) instead of the bats.
 * -- Cort
 */
int __map_without_bats = 0;

/* optimization for 603 to load the tlb directly from the linux table -- Cort */
#define NO_RELOAD_HTAB 1 /* change in kernel/head.S too! */

void __bad_pte(pmd_t *pmd)
{
	printk("Bad pmd in pte_alloc: %08lx\n", pmd_val(*pmd));
	pmd_val(*pmd) = (unsigned long) BAD_PAGETABLE;
}

pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset)
{
        pte_t *pte/* = (pte_t *) __get_free_page(GFP_KERNEL)*/;

        if (pmd_none(*pmd)) {
		if ( (pte = (pte_t *) get_zero_page_fast()) == NULL  )
			if ((pte = (pte_t *) __get_free_page(GFP_KERNEL)))
				clear_page((unsigned long)pte);
                if (pte) {
                        pmd_val(*pmd) = (unsigned long)pte;
                        return pte + offset;
                }
		pmd_val(*pmd) = (unsigned long)BAD_PAGETABLE;
                return NULL;
        }
        /*free_page((unsigned long)pte);*/
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
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
unsigned long empty_bad_page_table;

pte_t * __bad_pagetable(void)
{
	__clear_user((void *)empty_bad_page_table, PAGE_SIZE);
	return (pte_t *) empty_bad_page_table;
}

unsigned long empty_bad_page;

pte_t __bad_page(void)
{
	__clear_user((void *)empty_bad_page, PAGE_SIZE);
	return pte_mkdirty(mk_pte(empty_bad_page, PAGE_SHARED));
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0, cached = 0;
	struct task_struct *p;

	printk("Mem-info:\n");
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
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
	printk("%d pages in page table cache\n",(int)pgtable_cache_size);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
	printk("%-8s %3s %3s %8s %8s %8s %9s %8s", "Process", "Pid", "Cnt",
	       "Ctx", "Ctx<<4", "Last Sys", "pc", "task");
#ifdef __SMP__
	printk(" %3s", "CPU");
#endif /* __SMP__ */
	printk("\n");
	for_each_task(p)
	{	
		printk("%-8.8s %3d %3d %8ld %8ld %8ld %c%08lx %08lx ",
		       p->comm,p->pid,
		       atomic_read(&p->mm->count),p->mm->context,
		       p->mm->context<<4, p->tss.last_syscall,
		       user_mode(p->tss.regs) ? 'u' : 'k', p->tss.regs->nip,
		       (ulong)p);
		{
			int iscur = 0;
#ifdef __SMP__
			printk("%3d ", p->processor);
			if ( (p->processor != NO_PROC_ID) &&
			     (p == current_set[p->processor]) )
			{
				iscur = 1;
				printk("current");
			}
#else		
			if ( p == current )
			{
				iscur = 1;
				printk("current");
			}
			
			if ( p == last_task_used_math )
			{
				if ( iscur )
					printk(",");
				printk("last math");
			}			
#endif /* __SMP__ */
			printk("\n");
		}
	}
}

void si_meminfo(struct sysinfo *val)
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

void *
ioremap(unsigned long addr, unsigned long size)
{
	return __ioremap(addr, size, _PAGE_NO_CACHE);
}

void *
__ioremap(unsigned long addr, unsigned long size, unsigned long flags)
{
	unsigned long p, v, i;

	/*
	 * Choose an address to map it to.
	 * Once the vmalloc system is running, we use it.
	 * Before then, we map addresses >= ioremap_base
	 * virt == phys; for addresses below this we use
	 * space going down from ioremap_base (ioremap_bot
	 * records where we're up to).
	 *
	 * We should also look out for a frame buffer and
	 * map it with a free BAT register, if there is one.
	 */
	p = addr & PAGE_MASK;
	size = PAGE_ALIGN(addr + size) - p;
	if (size == 0)
		return NULL;

	if (mem_init_done) {
		struct vm_struct *area;
		area = get_vm_area(size);
		if (area == 0)
			return NULL;
		v = VMALLOC_VMADDR(area->addr);
	} else {
		if (p >= ioremap_base)
			v = p;
		else
			v = (ioremap_bot -= size);
	}

	if ((flags & _PAGE_PRESENT) == 0)
		flags |= pgprot_val(PAGE_KERNEL);
	if (flags & (_PAGE_NO_CACHE | _PAGE_WRITETHRU))
		flags |= _PAGE_GUARDED;
	for (i = 0; i < size; i += PAGE_SIZE)
		map_page(&init_task, v+i, p+i, flags);

	return (void *) (v + (addr & ~PAGE_MASK));
}

void iounmap(void *addr)
{
	/* XXX todo */
}

unsigned long iopa(unsigned long addr)
{
	unsigned long idx;
	pmd_t *pd;
	pte_t *pg;
#ifndef CONFIG_8xx
	int b;
#endif
	idx = addr & ~PAGE_MASK;
	addr = addr & PAGE_MASK;

#ifndef CONFIG_8xx
	/* Check the BATs */
	for (b = 0; b < 4; ++b)
		if (addr >= bat_addrs[b].start && addr <= bat_addrs[b].limit)
#ifndef CONFIG_APUS
			return bat_addrs[b].phys | idx;
#else
			/* Do a more precise remapping of virtual address */
			/* --Carsten */
			return (bat_addrs[b].phys - bat_addrs[b].start + addr) | idx;
#endif /* CONFIG_APUS */
#endif /* CONFIG_8xx */
	/* Do we have a page table? */
	if (init_task.mm->pgd == NULL)
		return 0;

	/* Use upper 10 bits of addr to index the first level map */
	pd = (pmd_t *) (init_task.mm->pgd + (addr >> PGDIR_SHIFT));
	if (pmd_none(*pd))
		return 0;

	/* Use middle 10 bits of addr to index the second-level map */
	pg = pte_offset(pd, addr);
	return (pte_val(*pg) & PAGE_MASK) | idx;
}

void
map_page(struct task_struct *tsk, unsigned long va,
	 unsigned long pa, int flags)
{
	pmd_t *pd;
	pte_t *pg;
#ifndef CONFIG_8xx
	int b;
#endif
	if (tsk->mm->pgd == NULL) {
		/* Allocate upper level page map */
		tsk->mm->pgd = (pgd_t *) MMU_get_page();
	}
	/* Use upper 10 bits of VA to index the first level map */
	pd = (pmd_t *) (tsk->mm->pgd + (va >> PGDIR_SHIFT));
	if (pmd_none(*pd)) {
#ifndef CONFIG_8xx
		/*
		 * Need to allocate second-level table, but first
		 * check whether this address is already mapped by
		 * the BATs; if so, don't bother allocating the page.
		 */
		for (b = 0; b < 4; ++b) {
			if (va >= bat_addrs[b].start
			    && va <= bat_addrs[b].limit) {
				/* XXX should check the phys address matches */
				return;
			}
		}
#endif /* CONFIG_8xx */
		pg = (pte_t *) MMU_get_page();
		pmd_val(*pd) = (unsigned long) pg;
	}
	/* Use middle 10 bits of VA to index the second-level map */
	pg = pte_offset(pd, va);
	set_pte(pg, mk_pte_phys(pa & PAGE_MASK, __pgprot(flags)));
#ifndef CONFIG_8xx
	flush_hash_page(0, va);
#endif	
}

/*
 * TLB flushing:
 *
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *
 * since the hardware hash table functions as an extension of the
 * tlb as far as the linux tables are concerned, flush it too.
 *    -- Cort
 */

/*
 * Flush all tlb/hash table entries (except perhaps for those
 * mapping RAM starting at PAGE_OFFSET, since they never change).
 */
void
local_flush_tlb_all(void)
{
#ifndef CONFIG_8xx
	__clear_user(Hash, Hash_size);
	_tlbia();
#else
	asm volatile ("tlbia" : : );
#endif
}

/*
 * Flush all the (user) entries for the address space described
 * by mm.  We can't rely on mm->mmap describing all the entries
 * that might be in the hash table.
 */
void
local_flush_tlb_mm(struct mm_struct *mm)
{
#ifndef CONFIG_8xx
	mm->context = NO_CONTEXT;
	if (mm == current->mm)
		activate_context(current);
#else
	asm volatile ("tlbia" : : );
#endif
}

void
local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
#ifndef CONFIG_8xx
	if (vmaddr < TASK_SIZE)
		flush_hash_page(vma->vm_mm->context, vmaddr);
	else
		flush_hash_page(0, vmaddr);
#else
	asm volatile ("tlbia" : : );
#endif
}


/*
 * for each page addr in the range, call MMU_invalidate_page()
 * if the range is very large and the hash table is small it might be
 * faster to do a search of the hash table and just invalidate pages
 * that are in the range but that's for study later.
 * -- Cort
 */
void
local_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
#ifndef CONFIG_8xx
	start &= PAGE_MASK;

	if (end - start > 20 * PAGE_SIZE)
	{
		flush_tlb_mm(mm);
		return;
	}

	for (; start < end && start < TASK_SIZE; start += PAGE_SIZE)
	{
		flush_hash_page(mm->context, start);
	}
#else
	asm volatile ("tlbia" : : );
#endif
}

/*
 * The context counter has overflowed.
 * We set mm->context to NO_CONTEXT for all mm's in the system.
 * We assume we can get to all mm's by looking as tsk->mm for
 * all tasks in the system.
 */
void
mmu_context_overflow(void)
{
#ifndef CONFIG_8xx
	struct task_struct *tsk;

	printk(KERN_DEBUG "mmu_context_overflow\n");
	read_lock(&tasklist_lock);
 	for_each_task(tsk) {
		if (tsk->mm)
			tsk->mm->context = NO_CONTEXT;
	}
	read_unlock(&tasklist_lock);
	flush_hash_segments(0x10, 0xffffff);
	atomic_set(&next_mmu_context, 0);
	/* make sure current always has a context */
	current->mm->context = MUNGE_CONTEXT(atomic_inc_return(&next_mmu_context));
	set_context(current->mm->context);
#else
	/* We set the value to -1 because it is pre-incremented before
	 * before use.
	 */
	atomic_set(&next_mmu_context, -1);
#endif
}

/*
 * Scan a region for a piece of a given size with the required alignment.
 */
__initfunc(void *
find_mem_piece(unsigned size, unsigned align))
{
	int i;
	unsigned a, e;
	struct mem_pieces *mp = &phys_avail;

	for (i = 0; i < mp->n_regions; ++i) {
		a = mp->regions[i].address;
		e = a + mp->regions[i].size;
		a = (a + align - 1) & -align;
		if (a + size <= e) {
			remove_mem_piece(mp, a, size, 1);
			return __va(a);
		}
	}
	printk("Couldn't find %u bytes at %u alignment\n", size, align);
	abort();
	return NULL;
}

/*
 * Remove some memory from an array of pieces
 */
__initfunc(static void
remove_mem_piece(struct mem_pieces *mp, unsigned start, unsigned size,
		 int must_exist))
{
	int i, j;
	unsigned end, rs, re;
	struct reg_property *rp;

	end = start + size;
	for (i = 0, rp = mp->regions; i < mp->n_regions; ++i, ++rp) {
		if (end > rp->address && start < rp->address + rp->size)
			break;
	}
	if (i >= mp->n_regions) {
		if (must_exist)
			printk("remove_mem_piece: [%x,%x) not in any region\n",
			       start, end);
		return;
	}
	for (; i < mp->n_regions && end > rp->address; ++i, ++rp) {
		rs = rp->address;
		re = rs + rp->size;
		if (must_exist && (start < rs || end > re)) {
			printk("remove_mem_piece: bad overlap [%x,%x) with",
			       start, end);
			print_mem_pieces(mp);
			must_exist = 0;
		}
		if (start > rs) {
			rp->size = start - rs;
			if (end < re) {
				/* need to split this entry */
				if (mp->n_regions >= MAX_MEM_REGIONS)
					panic("eek... mem_pieces overflow");
				for (j = mp->n_regions; j > i + 1; --j)
					mp->regions[j] = mp->regions[j-1];
				++mp->n_regions;
				rp[1].address = end;
				rp[1].size = re - end;
			}
		} else {
			if (end < re) {
				rp->address = end;
				rp->size = re - end;
			} else {
				/* need to delete this entry */
				for (j = i; j < mp->n_regions - 1; ++j)
					mp->regions[j] = mp->regions[j+1];
				--mp->n_regions;
				--i;
				--rp;
			}
		}
	}
}

__initfunc(static void print_mem_pieces(struct mem_pieces *mp))
{
	int i;

	for (i = 0; i < mp->n_regions; ++i)
		printk(" [%x, %x)", mp->regions[i].address,
		       mp->regions[i].address + mp->regions[i].size);
	printk("\n");
}

/*
 * Add some memory to an array of pieces
 */
__initfunc(static void
	   append_mem_piece(struct mem_pieces *mp, unsigned start, unsigned size))
{
	struct reg_property *rp;

	if (mp->n_regions >= MAX_MEM_REGIONS)
		return;
	rp = &mp->regions[mp->n_regions++];
	rp->address = start;
	rp->size = size;
}

#ifndef CONFIG_8xx
static void hash_init(void);
static void get_mem_prop(char *, struct mem_pieces *);
static void sort_mem_pieces(struct mem_pieces *);
static void coalesce_mem_pieces(struct mem_pieces *);

__initfunc(static void sort_mem_pieces(struct mem_pieces *mp))
{
	unsigned long a, s;
	int i, j;

	for (i = 1; i < mp->n_regions; ++i) {
		a = mp->regions[i].address;
		s = mp->regions[i].size;
		for (j = i - 1; j >= 0; --j) {
			if (a >= mp->regions[j].address)
				break;
			mp->regions[j+1] = mp->regions[j];
		}
		mp->regions[j+1].address = a;
		mp->regions[j+1].size = s;
	}
}

__initfunc(static void coalesce_mem_pieces(struct mem_pieces *mp))
{
	unsigned long a, s, ns;
	int i, j, d;

	d = 0;
	for (i = 0; i < mp->n_regions; i = j) {
		a = mp->regions[i].address;
		s = mp->regions[i].size;
		for (j = i + 1; j < mp->n_regions
			     && mp->regions[j].address - a <= s; ++j) {
			ns = mp->regions[j].address + mp->regions[j].size - a;
			if (ns > s)
				s = ns;
		}
		mp->regions[d].address = a;
		mp->regions[d].size = s;
		++d;
	}
	mp->n_regions = d;
}

/*
 * Read in a property describing some pieces of memory.
 */

__initfunc(static void get_mem_prop(char *name, struct mem_pieces *mp))
{
	struct reg_property *rp;
	int s;

	rp = (struct reg_property *) get_property(memory_node, name, &s);
	if (rp == NULL) {
		printk(KERN_ERR "error: couldn't get %s property on /memory\n",
		       name);
		abort();
	}
	mp->n_regions = s / sizeof(mp->regions[0]);
	memcpy(mp->regions, rp, s);

	/* Make sure the pieces are sorted. */
	sort_mem_pieces(mp);
	coalesce_mem_pieces(mp);
}

#endif /* CONFIG_8xx */

#ifndef CONFIG_8xx
/*
 * Set up one of the I/D BAT (block address translation) register pairs.
 * The parameters are not checked; in particular size must be a power
 * of 2 between 128k and 256M.
 */
__initfunc(void setbat(int index, unsigned long virt, unsigned long phys,
       unsigned int size, int flags))
{
	unsigned int bl;
	int wimgxpp;
	union ubat *bat = BATS[index];

	bl = (size >> 17) - 1;
	if ((_get_PVR() >> 16) != 1) {
		/* 603, 604, etc. */
		/* Do DBAT first */
		wimgxpp = flags & (_PAGE_WRITETHRU | _PAGE_NO_CACHE
				   | _PAGE_COHERENT | _PAGE_GUARDED);
		wimgxpp |= (flags & _PAGE_RW)? BPP_RW: BPP_RX;
		bat[1].word[0] = virt | (bl << 2) | 2; /* Vs=1, Vp=0 */
		bat[1].word[1] = phys | wimgxpp;
		if (flags & _PAGE_USER)
			bat[1].bat.batu.vp = 1;
		if (flags & _PAGE_GUARDED) {
			/* G bit must be zero in IBATs */
			bat[0].word[0] = bat[0].word[1] = 0;
		} else {
			/* make IBAT same as DBAT */
			bat[0] = bat[1];
		}
	} else {
		/* 601 cpu */
		if (bl > BL_8M)
			bl = BL_8M;
		wimgxpp = flags & (_PAGE_WRITETHRU | _PAGE_NO_CACHE
				   | _PAGE_COHERENT);
		wimgxpp |= (flags & _PAGE_RW)?
			((flags & _PAGE_USER)? PP_RWRW: PP_RWXX): PP_RXRX;
		bat->word[0] = virt | wimgxpp | 4;	/* Ks=0, Ku=1 */
		bat->word[1] = phys | bl | 0x40;	/* V=1 */
	}

	bat_addrs[index].start = virt;
	bat_addrs[index].limit = virt + ((bl + 1) << 17) - 1;
	bat_addrs[index].phys = phys;
}

#define IO_PAGE	(_PAGE_NO_CACHE | _PAGE_GUARDED | _PAGE_RW)
#ifdef __SMP__
#define RAM_PAGE (_PAGE_RW|_PAGE_COHERENT)
#else
#define RAM_PAGE (_PAGE_RW)
#endif
#endif /* CONFIG_8xx */

/*
 * Map in all of physical memory starting at KERNELBASE.
 */
#define PAGE_KERNEL_RO	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)

__initfunc(static void mapin_ram(void))
{
	int i;
	unsigned long v, p, s, f;
#ifndef CONFIG_8xx

	if (!__map_without_bats) {
		unsigned long tot, mem_base, bl, done;
		unsigned long max_size = (256<<20);
		unsigned long align;

		/* Set up BAT2 and if necessary BAT3 to cover RAM. */
		mem_base = __pa(KERNELBASE);

		/* Make sure we don't map a block larger than the
		   smallest alignment of the physical address. */
		/* alignment of mem_base */
		align = ~(mem_base-1) & mem_base;
		/* set BAT block size to MIN(max_size, align) */
		if (align && align < max_size)
			max_size = align;

		tot = (unsigned long)end_of_DRAM - KERNELBASE;
		for (bl = 128<<10; bl < max_size; bl <<= 1) {
			if (bl * 2 > tot)
				break;
		}

		setbat(2, KERNELBASE, mem_base, bl, RAM_PAGE);
		done = (unsigned long)bat_addrs[2].limit - KERNELBASE + 1;
		if (done < tot) {
			/* use BAT3 to cover a bit more */
			tot -= done;
			for (bl = 128<<10; bl < max_size; bl <<= 1)
				if (bl * 2 > tot)
					break;
			setbat(3, KERNELBASE+done, mem_base+done, bl, 
			       RAM_PAGE);
		}
	}

	v = KERNELBASE;
	for (i = 0; i < phys_mem.n_regions; ++i) {
		p = phys_mem.regions[i].address;
		for (s = 0; s < phys_mem.regions[i].size; s += PAGE_SIZE) {
			f = _PAGE_PRESENT | _PAGE_ACCESSED;
			if ((char *) v < _stext || (char *) v >= etext)
				f |= _PAGE_RW | _PAGE_DIRTY | _PAGE_HWWRITE;
			else
				/* On the powerpc, no user access
				   forces R/W kernel access */
				f |= _PAGE_USER;
#else	/* CONFIG_8xx */
            for (i = 0; i < phys_mem.n_regions; ++i) {
                    v = (ulong)__va(phys_mem.regions[i].address);
                    p = phys_mem.regions[i].address;
                    for (s = 0; s < phys_mem.regions[i].size; s += PAGE_SIZE) {
                        /* On the MPC8xx, we want the page shared so we
                         * don't get ASID compares on kernel space.
                         */
                            f = _PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_SHARED;

                        /* I don't really need the rest of this code, but
                         * I grabbed it because I think the line:
                         *      f |= _PAGE_USER
                         * is incorrect.  It needs to be set to bits we
                         * don't define to cause a kernel read-only.  On
                         * the MPC8xx, the PAGE_DIRTY takes care of that
                         * for us (along with the RW software state).
                         */
                            if ((char *) v < _stext || (char *) v >= etext)
                                    f |= _PAGE_RW | _PAGE_DIRTY | _PAGE_HWWRITE;
#endif /* CONFIG_8xx */
			map_page(&init_task, v, p, f);
			v += PAGE_SIZE;
			p += PAGE_SIZE;
		}
	}	    
}

__initfunc(static void *MMU_get_page(void))
{
	void *p;

	if (mem_init_done) {
		p = (void *) __get_free_page(GFP_KERNEL);
		if (p == 0)
			panic("couldn't get a page in MMU_get_page");
	} else {
		p = find_mem_piece(PAGE_SIZE, PAGE_SIZE);
	}
	/*memset(p, 0, PAGE_SIZE);*/
	__clear_user(p, PAGE_SIZE);
	return p;
}

__initfunc(void free_initmem(void))
{
	unsigned long a;
	unsigned long num_freed_pages = 0, num_prep_pages = 0,
		num_pmac_pages = 0, num_openfirmware_pages = 0;
#define FREESEC(START,END,CNT) do { \
	a = (unsigned long)(&START); \
	for (; a < (unsigned long)(&END); a += PAGE_SIZE) { \
	  	clear_bit(PG_reserved, &mem_map[MAP_NR(a)].flags); \
		atomic_set(&mem_map[MAP_NR(a)].count, 1); \
		free_page(a); \
		CNT++; \
	} \
} while (0)

	FREESEC(__init_begin,__init_end,num_freed_pages);
	switch (_machine)
	{
	case _MACH_Pmac:
		FREESEC(__prep_begin,__prep_end,num_prep_pages);
		break;
	case _MACH_chrp:
		FREESEC(__pmac_begin,__pmac_end,num_pmac_pages);
		FREESEC(__prep_begin,__prep_end,num_prep_pages);
		break;
	case _MACH_prep:
		FREESEC(__pmac_begin,__pmac_end,num_pmac_pages);
		FREESEC(__openfirmware_begin,__openfirmware_end,num_openfirmware_pages);
		break;
	case _MACH_mbx:
		FREESEC(__pmac_begin,__pmac_end,num_pmac_pages);
		FREESEC(__openfirmware_begin,__openfirmware_end,num_openfirmware_pages);
		FREESEC(__prep_begin,__prep_end,num_prep_pages);
		break;
	}
	
	printk ("Freeing unused kernel memory: %ldk init",
		(num_freed_pages * PAGE_SIZE) >> 10);
	if ( num_prep_pages )
		printk(" %ldk prep",(num_prep_pages*PAGE_SIZE)>>10);
	if ( num_pmac_pages )
		printk(" %ldk pmac",(num_pmac_pages*PAGE_SIZE)>>10);
	if ( num_openfirmware_pages )
		printk(" %ldk open firmware",(num_openfirmware_pages*PAGE_SIZE)>>10);
	printk("\n");
}

/*
 * Do very early mm setup such as finding the size of memory
 * and setting up the hash table.
 * A lot of this is prep/pmac specific but a lot of it could
 * still be merged.
 * -- Cort
 */
__initfunc(void MMU_init(void))
{

#ifdef __SMP__
	if ( first_cpu_booted ) return;
#endif /* __SMP__ */
	
#ifndef CONFIG_8xx
	if (have_of)
		end_of_DRAM = pmac_find_end_of_memory();
#ifdef CONFIG_APUS
	else if (_machine == _MACH_apus )
		end_of_DRAM = apus_find_end_of_memory();
#endif
	else /* prep */
		end_of_DRAM = prep_find_end_of_memory();

        hash_init();
        _SDR1 = __pa(Hash) | (Hash_mask >> 10);
	ioremap_base = 0xf8000000;

	/* Map in all of RAM starting at KERNELBASE */
	mapin_ram();

	/*
	 * Setup the bat mappings we're going to load that cover
	 * the io areas.  RAM was mapped by mapin_ram().
	 * -- Cort
	 */
	switch (_machine) {
	case _MACH_prep:
		setbat(0, 0x80000000, 0x80000000, 0x10000000, IO_PAGE);
		setbat(1, 0xd0000000, 0xc0000000, 0x10000000, IO_PAGE);
		break;
	case _MACH_chrp:
		setbat(0, 0xf8000000, 0xf8000000, 0x08000000, IO_PAGE);
		break;
	case _MACH_Pmac:
		{
			unsigned long base = 0xf3000000;
			struct device_node *macio = find_devices("mac-io");
			if (macio && macio->n_addrs)
				base = macio->addrs[0].address;
			setbat(0, base, base, 0x100000, IO_PAGE);
			ioremap_base = 0xf0000000;
		}
		break;
	case _MACH_apus:
		/* Map PPC exception vectors. */
		setbat(0, 0xfff00000, 0xfff00000, 0x00020000, RAM_PAGE);
		/* Map chip and ZorroII memory */
		setbat(1, zTwoBase,   0x00000000, 0x01000000, IO_PAGE);
		/* Note: a temporary hack in arch/ppc/amiga/setup.c
		   (kernel_map) remaps individual IO regions to
		   0x90000000. */
		break;
	}
	ioremap_bot = ioremap_base;
#else /* CONFIG_8xx */
#ifdef CONFIG_MBX
	end_of_DRAM = mbx_find_end_of_memory();
#endif /* CONFIG_MBX */
        /* Map in all of RAM starting at KERNELBASE */
        mapin_ram();

        /* Now map in some of the I/O space that is generically needed
         * or shared with multiple devices.
         * All of this fits into the same 4Mbyte region, so it only
         * requires one page table page.
         */
        ioremap(NVRAM_ADDR, NVRAM_SIZE);
        ioremap(MBX_CSR_ADDR, MBX_CSR_SIZE);
        ioremap(IMAP_ADDR, IMAP_SIZE);
        ioremap(PCI_CSR_ADDR, PCI_CSR_SIZE);
	/* ide needs to be able to get at PCI space -- Cort */
        ioremap(0x80000000, 0x4000);
#endif /* CONFIG_8xx */
}

/*
 * Find some memory for setup_arch to return.
 * We use the largest chunk of available memory as the area
 * that setup_arch returns, making sure that there are at
 * least 32 pages unused before this for MMU_get_page to use.
 */
__initfunc(unsigned long find_available_memory(void))
{
	int i, rn;
	unsigned long a, free;
	unsigned long start, end;

	if (_machine == _MACH_mbx) {
		/* Return the first, not the last region, because we
                 * may not yet have properly initialized the additonal
                 * memory DIMM.
                 */
                a = PAGE_ALIGN(phys_avail.regions[0].address);
                avail_start = (unsigned long) __va(a);
                return avail_start;
        }
	
	rn = 0;
	for (i = 1; i < phys_avail.n_regions; ++i)
		if (phys_avail.regions[i].size > phys_avail.regions[rn].size)
			rn = i;
	free = 0;
	for (i = 0; i < rn; ++i) {
		start = phys_avail.regions[i].address;
		end = start + phys_avail.regions[i].size;
		free += (end & PAGE_MASK) - PAGE_ALIGN(start);
	}
	a = PAGE_ALIGN(phys_avail.regions[rn].address);
	if (free < 32 * PAGE_SIZE)
		a += 32 * PAGE_SIZE - free;
	avail_start = (unsigned long) __va(a);
	return avail_start;
}

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
__initfunc(unsigned long paging_init(unsigned long start_mem, unsigned long end_mem))
{
	extern unsigned long free_area_init(unsigned long, unsigned long);
	/*
	 * Grab some memory for bad_page and bad_pagetable to use.
	 */
	empty_bad_page = PAGE_ALIGN(start_mem);
	empty_bad_page_table = empty_bad_page + PAGE_SIZE;
	start_mem = empty_bad_page + 2 * PAGE_SIZE;

	/* note: free_area_init uses its second argument
	   to size the mem_map array. */
	start_mem = free_area_init(start_mem, end_mem);
	return start_mem;
}

__initfunc(void mem_init(unsigned long start_mem, unsigned long end_mem))
{
	unsigned long addr;
	int i;
	unsigned long a, lim;
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;
	extern unsigned int rtas_data, rtas_size;

	end_mem &= PAGE_MASK;
	high_memory = (void *) end_mem;
	max_mapnr = MAP_NR(high_memory);

	/* mark usable pages in the mem_map[] */
	start_mem = PAGE_ALIGN(start_mem);

	num_physpages = max_mapnr;	/* RAM is assumed contiguous */
	remove_mem_piece(&phys_avail, __pa(avail_start),
			 start_mem - avail_start, 1);

	for (addr = PAGE_OFFSET; addr < end_mem; addr += PAGE_SIZE)
		set_bit(PG_reserved, &mem_map[MAP_NR(addr)].flags);

	for (i = 0; i < phys_avail.n_regions; ++i) {
		a = (unsigned long) __va(phys_avail.regions[i].address);
		lim = a + phys_avail.regions[i].size;
		a = PAGE_ALIGN(a);
		for (; a < lim; a += PAGE_SIZE)
			clear_bit(PG_reserved, &mem_map[MAP_NR(a)].flags);
	}
	phys_avail.n_regions = 0;

#ifdef CONFIG_BLK_DEV_INITRD
	/* if we are booted from BootX with an initial ramdisk,
	   make sure the ramdisk pages aren't reserved. */
	if (initrd_start) {
		for (a = initrd_start; a < initrd_end; a += PAGE_SIZE)
			clear_bit(PG_reserved, &mem_map[MAP_NR(a)].flags);
	}
#endif /* CONFIG_BLK_DEV_INITRD */
	
	/* free the prom's memory - no-op on prep */
	for (i = 0; i < prom_mem.n_regions; ++i) {
		a = (unsigned long) __va(prom_mem.regions[i].address);
		lim = a + prom_mem.regions[i].size;
		a = PAGE_ALIGN(a);
		for (; a < lim; a += PAGE_SIZE)
			clear_bit(PG_reserved, &mem_map[MAP_NR(a)].flags);
	}

	prom_trashed = 1;
	
	for (addr = PAGE_OFFSET; addr < end_mem; addr += PAGE_SIZE) {
		if (PageReserved(mem_map + MAP_NR(addr))) {
			if (addr < (ulong) etext)
				codepages++;
			else if (addr >= (unsigned long)&__init_begin
				 && addr < (unsigned long)&__init_end)
                                initpages++;
                        else if (addr < (ulong) start_mem)
				datapages++;
			continue;
		}
		atomic_set(&mem_map[MAP_NR(addr)].count, 1);
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start ||
		    addr < (initrd_start & PAGE_MASK) || addr >= initrd_end)
#endif /* CONFIG_BLK_DEV_INITRD */
#ifndef CONFIG_8xx		  
			if ( !rtas_data ||
			     addr < (rtas_data & PAGE_MASK) ||
			     addr >= (rtas_data+rtas_size))
#endif /* CONFIG_8xx */
				free_page(addr);
	}

        printk("Memory: %luk available (%dk kernel code, %dk data, %dk init) [%08x,%08lx]\n",
	       (unsigned long) nr_free_pages << (PAGE_SHIFT-10),
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10), 
	       initpages << (PAGE_SHIFT-10),
	       PAGE_OFFSET, end_mem);
	mem_init_done = 1;
}

#ifdef CONFIG_MBX
/*
 * This is a big hack right now, but it may turn into something real
 * someday.
 *
 * For the MBX860 (at this time anyway), there is nothing to initialize
 * associated the PROM.  Rather than include all of the prom.c
 * functions in the image just to get prom_init, all we really need right
 * now is the initialization of the physical memory region.
 */
__initfunc(unsigned long *mbx_find_end_of_memory(void))
{
	unsigned long kstart, ksize;
	bd_t	*binfo;
	volatile memctl8xx_t	*mcp;
	unsigned long *ret;
	
	binfo = (bd_t *)res;

	/*
	 * The MBX does weird things with the mmaps for ram.
	 * If there's no DIMM, it puts the onboard DRAM at
	 * 0, if there is a DIMM it sticks it at 0 and puts
	 * the DRAM at the end of the DIMM.
	 *
	 * In fact, it might be the best idea to just read the DRAM
	 * config registers and set the mem areas accordingly.
	 */
	mcp = (memctl8xx_t *)(&(((immap_t *)IMAP_ADDR)->im_memctl));
	append_mem_piece(&phys_mem, 0, binfo->bi_memsize);
#if 0
	phys_mem.regions[0].address = 0;
	phys_mem.regions[0].size = binfo->bi_memsize;	
	phys_mem.n_regions = 1;
#endif	
	
	ret = __va(phys_mem.regions[0].address+
		   phys_mem.regions[0].size);

	phys_avail = phys_mem;

	kstart = __pa(_stext);	/* should be 0 */
	ksize = PAGE_ALIGN(_end - _stext);
	remove_mem_piece(&phys_avail, kstart, ksize, 0);
	return ret;
}
#endif /* CONFIG_MBX */
#ifndef CONFIG_8xx
/*
 * On systems with Open Firmware, collect information about
 * physical RAM and which pieces are already in use.
 * At this point, we have (at least) the first 8MB mapped with a BAT.
 * Our text, data, bss use something over 1MB, starting at 0.
 * Open Firmware may be using 1MB at the 4MB point.
 */
__initfunc(unsigned long *pmac_find_end_of_memory(void))
{
	unsigned long a, total;
	unsigned long kstart, ksize;
	int i;

	memory_node = find_devices("memory");
	if (memory_node == NULL) {
		printk(KERN_ERR "can't find memory node\n");
		abort();
	}

	/*
	 * Find out where physical memory is, and check that it
	 * starts at 0 and is contiguous.  It seems that RAM is
	 * always physically contiguous on Power Macintoshes,
	 * because MacOS can't cope if it isn't.
	 *
	 * Supporting discontiguous physical memory isn't hard,
	 * it just makes the virtual <-> physical mapping functions
	 * more complicated (or else you end up wasting space
	 * in mem_map).
	 */
	get_mem_prop("reg", &phys_mem);
	if (phys_mem.n_regions == 0)
		panic("No RAM??");
	a = phys_mem.regions[0].address;
	if (a != 0)
		panic("RAM doesn't start at physical address 0");
	total = phys_mem.regions[0].size;
	if (phys_mem.n_regions > 1) {
		printk("RAM starting at 0x%x is not contiguous\n",
		       phys_mem.regions[1].address);
		printk("Using RAM from 0 to 0x%lx\n", total-1);
		phys_mem.n_regions = 1;
	}

	if (boot_infos == 0) {
		/* record which bits the prom is using */
		get_mem_prop("available", &phys_avail);
	} else {
		/* booted from BootX - it's all available (after klimit) */
		phys_avail = phys_mem;
	}
	prom_mem = phys_mem;
	for (i = 0; i < phys_avail.n_regions; ++i)
		remove_mem_piece(&prom_mem, phys_avail.regions[i].address,
				 phys_avail.regions[i].size, 1);

	/*
	 * phys_avail records memory we can use now.
	 * prom_mem records memory allocated by the prom that we
	 * don't want to use now, but we'll reclaim later.
	 * Make sure the kernel text/data/bss is in neither.
	 */
	kstart = __pa(_stext);	/* should be 0 */
	ksize = PAGE_ALIGN(klimit - _stext);
	remove_mem_piece(&phys_avail, kstart, ksize, 0);
	remove_mem_piece(&prom_mem, kstart, ksize, 0);
	remove_mem_piece(&phys_avail, 0, 0x4000, 0);
	remove_mem_piece(&prom_mem, 0, 0x4000, 0);

	return __va(total);
}

/*
 * This finds the amount of physical ram and does necessary
 * setup for prep.  This is pretty architecture specific so
 * this will likely stay separate from the pmac.
 * -- Cort
 */
__initfunc(unsigned long *prep_find_end_of_memory(void))
{
	unsigned long kstart, ksize;
	unsigned long total;
	total = res->TotalMemory;

	if (total == 0 )
	{
		/*
		 * I need a way to probe the amount of memory if the residual
		 * data doesn't contain it. -- Cort
		 */
		printk("Ramsize from residual data was 0 -- Probing for value\n");
		total = 0x02000000;
		printk("Ramsize default to be %ldM\n", total>>20);
	}
	append_mem_piece(&phys_mem, 0, total);
	phys_avail = phys_mem;
	kstart = __pa(_stext);	/* should be 0 */
	ksize = PAGE_ALIGN(klimit - _stext);
	remove_mem_piece(&phys_avail, kstart, ksize, 0);
	remove_mem_piece(&phys_avail, 0, 0x4000, 0);

	return (__va(total));
}

#ifdef CONFIG_APUS
#define HARDWARE_MAPPED_SIZE (512*1024)
__initfunc(unsigned long *apus_find_end_of_memory(void))
{
	int shadow = 0;

	/* The memory size reported by ADOS excludes the 512KB
	   reserved for PPC exception registers and possibly 512KB
	   containing a shadow of the ADOS ROM. */
	{
		unsigned long size = memory[0].size;

		/* If 2MB aligned, size was probably user
                   specified. We can't tell anything about shadowing
                   in this case so skip shadow assignment. */
		if (0 != (size & 0x1fffff)){
			/* Align to 512KB to ensure correct handling
			   of both memfile and system specified
			   sizes. */
			size = ((size+0x0007ffff) & 0xfff80000);
			/* If memory is 1MB aligned, assume
                           shadowing. */
			shadow = !(size & 0x80000);
		}

		/* Add the chunk that ADOS does not see. by aligning
                   the size to the nearest 2MB limit upwards.  */
		memory[0].size = ((size+0x001fffff) & 0xffe00000);
	}

	/* Now register the memory block. */
	{
		unsigned long kstart, ksize;

		append_mem_piece(&phys_mem, memory[0].addr, memory[0].size);
		phys_avail = phys_mem;
		kstart = __pa(_stext);
		ksize = PAGE_ALIGN(klimit - _stext);
		remove_mem_piece(&phys_avail, kstart, ksize, 0);
	}

	/* Remove the memory chunks that are controlled by special
           Phase5 hardware. */
	{
		unsigned long top = memory[0].addr + memory[0].size;

		/* Remove the upper 512KB if it contains a shadow of
		   the ADOS ROM. FIXME: It might be possible to
		   disable this shadow HW. Check the booter
		   (ppc_boot.c) */
		if (shadow)
		{
			top -= HARDWARE_MAPPED_SIZE;
			remove_mem_piece(&phys_avail, top,
					 HARDWARE_MAPPED_SIZE, 0);
		}

		/* Remove the upper 512KB where the PPC exception
                   vectors are mapped. */
		top -= HARDWARE_MAPPED_SIZE;
#if 0
		/* This would be neat, but it breaks on A3000 machines!? */
		remove_mem_piece(&phys_avail, top, 16384, 0);
#else
		remove_mem_piece(&phys_avail, top, HARDWARE_MAPPED_SIZE, 0);
#endif

	}

	/* Linux/APUS only handles one block of memory -- the one on
	   the PowerUP board. Other system memory is horrible slow in
	   comparison. The user can use other memory for swapping
	   using the z2ram device. */
	return __va(memory[0].addr + memory[0].size);
}
#endif /* CONFIG_APUS */

/*
 * Initialize the hash table and patch the instructions in head.S.
 */
__initfunc(static void hash_init(void))
{
	int Hash_bits;
	unsigned long h, ramsize;

	extern unsigned int hash_page_patch_A[], hash_page_patch_B[],
		hash_page_patch_C[], hash_page[];

	/*
	 * Allow 64k of hash table for every 16MB of memory,
	 * up to a maximum of 2MB.
	 */
	ramsize = (ulong)end_of_DRAM - KERNELBASE;
	for (h = 64<<10; h < ramsize / 256 && h < 2<<20; h *= 2)
		;
	Hash_size = h;
	Hash_mask = (h >> 6) - 1;
	
#ifdef NO_RELOAD_HTAB
	/* shrink the htab since we don't use it on 603's -- Cort */
	switch (_get_PVR()>>16) {
	case 3: /* 603 */
	case 6: /* 603e */
	case 7: /* 603ev */
		Hash_size = 0;
		Hash_mask = 0;
		break;
	default:
	        /* on 601/4 let things be */
		break;
 	}
#endif /* NO_RELOAD_HTAB */
	
	/* Find some memory for the hash table. */
	if ( Hash_size )
		Hash = find_mem_piece(Hash_size, Hash_size);
	else
		Hash = 0;

	printk("Total memory = %ldMB; using %ldkB for hash table (at %p)\n",
	       ramsize >> 20, Hash_size >> 10, Hash);
	if ( Hash_size )
	{
#ifdef CONFIG_APUS
#define b(x) ((unsigned int*)(((unsigned long)(x)) - KERNELBASE + 0xfff00000))
#else
#define b(x) (x)
#endif
		/*memset(Hash, 0, Hash_size);*/
		__clear_user(Hash, Hash_size);
		
		Hash_end = (PTE *) ((unsigned long)Hash + Hash_size);

		/*
		 * Patch up the instructions in head.S:hash_page
		 */
		Hash_bits = ffz(~Hash_size) - 6;
		*b(hash_page_patch_A) = (*b(hash_page_patch_A) & ~0xffff)
			| (__pa(Hash) >> 16);
		*b(hash_page_patch_A + 1) = (*b(hash_page_patch_A + 1)& ~0x7c0)
			| ((26 - Hash_bits) << 6);
		if (Hash_bits > 16)
			Hash_bits = 16;
		*b(hash_page_patch_A + 2) = (*b(hash_page_patch_A + 2)& ~0x7c0)
			| ((26 - Hash_bits) << 6);
		*b(hash_page_patch_B) = (*b(hash_page_patch_B) & ~0xffff)
			| (Hash_mask >> 10);
		*b(hash_page_patch_C) = (*b(hash_page_patch_C) & ~0xffff)
			| (Hash_mask >> 10);
#if 0	/* see hash_page in head.S, note also patch_C ref below */
		*b(hash_page_patch_D) = (*b(hash_page_patch_D) & ~0xffff)
			| (Hash_mask >> 10);
#endif
		/*
		 * Ensure that the locations we've patched have been written
		 * out from the data cache and invalidated in the instruction
		 * cache, on those machines with split caches.
		 */
		flush_icache_range((unsigned long) b(hash_page_patch_A),
				   (unsigned long) b(hash_page_patch_C + 1));
	}
	else {
		Hash_end = 0;
		/*
		 * Put a blr (procedure return) instruction at the
		 * start of hash_page, since we can still get DSI
		 * exceptions on a 603.
		 */
		*b(hash_page) = 0x4e800020;
		flush_icache_range((unsigned long) b(hash_page),
				   (unsigned long) b(hash_page + 1));
	}
}
#endif /* ndef CONFIG_8xx */

