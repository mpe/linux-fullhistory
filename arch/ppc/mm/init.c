/*
 *  arch/ppc/mm/init.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Ported to PPC by Gary Thomas
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>

#define SHOW_FAULTS
#undef  SHOW_FAULTS

#define SHOW_INVALIDATES
#undef  SHOW_INVALIDATES

#include <asm/pgtable.h>

extern pgd_t swapper_pg_dir[1024*8];

extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);

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
pte_t * __bad_pagetable(void)
{
	panic("__bad_pagetable");
#if 0
	extern char empty_bad_page_table[PAGE_SIZE];
	unsigned long dummy;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		"1:\tsw\t%2,(%0)\n\t"
		"subu\t%1,%1,1\n\t"
		"bne\t$0,%1,1b\n\t"
		"addiu\t%0,%0,1\n\t"
		".set\treorder"
		:"=r" (dummy),
		 "=r" (dummy)
		:"r" (pte_val(BAD_PAGE)),
		 "0" ((long) empty_bad_page_table),
		 "1" (PTRS_PER_PAGE));

	return (pte_t *) empty_bad_page_table;
#endif
}

pte_t __bad_page(void)
{
	panic("__bad_page");
#if 0
	extern char empty_bad_page[PAGE_SIZE];
	unsigned long dummy;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		"1:\tsw\t$0,(%0)\n\t"
		"subu\t%1,%1,1\n\t"
		"bne\t$0,%1,1b\n\t"
		"addiu\t%0,%0,1\n\t"
		".set\treorder"
		:"=r" (dummy),
		 "=r" (dummy)
		:"0" ((long) empty_bad_page),
		 "1" (PTRS_PER_PAGE));

	return pte_mkdirty(mk_pte((unsigned long) empty_bad_page, PAGE_SHARED));
#endif
}

unsigned long __zero_page(void)
{
	extern char empty_zero_page[PAGE_SIZE];
	bzero(empty_zero_page, PAGE_SIZE);
	return (unsigned long) empty_zero_page;
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = high_memory >> PAGE_SHIFT;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (!mem_map[i].count)
			free++;
		else
			shared += mem_map[i].count-1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
}

extern unsigned long free_area_init(unsigned long, unsigned long);

/*
 * paging_init() sets up the page tables - note that the first 4MB are
 * already mapped by head.S.
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
#if 0	
	pgd_t * pg_dir;
	pte_t * pg_table;
	unsigned long tmp;
	unsigned long address;

	start_mem = PAGE_ALIGN(start_mem);
	address = 0;
	pg_dir = swapper_pg_dir;
	while (address < end_mem) {
		if (pgd_none(pg_dir[0])) {
			pgd_set(pg_dir, (pte_t *) start_mem);
			start_mem += PAGE_SIZE;
		}
		/*
		 * also map it in at 0x00000000 for init
		 */
		pg_table = (pte_t *) pgd_page(pg_dir[0]);
		pgd_set(pg_dir, pg_table);
		pg_dir++;
		for (tmp = 0 ; tmp < PTRS_PER_PAGE ; tmp++,pg_table++) {
			if (address < end_mem)
				*pg_table = mk_pte(address, PAGE_SHARED);
			else
				pte_clear(pg_table);
			address += PAGE_SIZE;
		}
	}
#if KERNELBASE == KSEG0
	cacheflush();
#endif
	invalidate();
#endif	
	return free_area_init(start_mem, end_mem);
}

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
	int codepages = 0;
	int datapages = 0;
	unsigned long tmp;
	extern int etext;
	
	end_mem &= PAGE_MASK;
	high_memory = end_mem;

	/* mark usable pages in the mem_map[] */
	start_mem = PAGE_ALIGN(start_mem);

#if 0
_printk("Mem init - Start: %x, End: %x\n", start_mem, high_memory);
#endif
	for (tmp = KERNELBASE ; tmp < high_memory ; tmp += PAGE_SIZE)
	{
		if (tmp < start_mem)
		{
			set_bit(PG_reserved, &mem_map[MAP_NR(tmp)].flags);
			if (tmp < (unsigned long) &etext)
			{
				codepages++;
			} else
			{
				datapages++;
			}
			continue;
		}
		clear_bit(PG_reserved, &mem_map[MAP_NR(tmp)].flags);
		mem_map[MAP_NR(tmp)].count = 1;
		free_page(tmp);
	}
	tmp = nr_free_pages << PAGE_SHIFT;
	printk("Memory: %luk/%luk available (%dk kernel code, %dk data)\n",
		tmp >> 10,
		((int)high_memory - (int)KERNELBASE) >> 10,
		codepages << (PAGE_SHIFT-10),
		datapages << (PAGE_SHIFT-10));
	invalidate();
	return;
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = ((int)high_memory & 0x00FFFFFF) >> PAGE_SHIFT;
	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = buffermem;
	while (i-- > 0)  {
		if (PageReserved(mem_map+i))
			continue;
		val->totalram++;
		if (!mem_map[i].count)
			continue;
		val->sharedram += mem_map[i].count-1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
	return;
}

/* Kernel MMU setup & lowest level hardware support */

/* Hardwired MMU segments */

/* Segment 0x8XXXXXXX, 0xCXXXXXXX always mapped (for I/O) */
/* Segment 0x9XXXXXXX mapped during init */

BAT BAT0 =
   {
   	{
   		0x80000000>>17, 	/* bepi */
   		BL_256M,		/* bl */
   		1,			/* vs */
   		1,			/* vp */
   	},
   	{
   		0x80000000>>17,		/* brpn */
   		1,			/* w */
   		1,			/* i (cache disabled) */
   		0,			/* m */
   		1,			/* g */
   		BPP_RW			/* pp */
   	}
   };
BAT BAT1 =
   {
   	{
   		0xC0000000>>17, 	/* bepi */
   		BL_256M,		/* bl */
   		1,			/* vs */
   		1,			/* vp */
   	},
   	{
   		0xC0000000>>17,		/* brpn */
   		1,			/* w */
   		1,			/* i (cache disabled) */
   		0,			/* m */
   		1,			/* g */
   		BPP_RW			/* pp */
   	}
   };
BAT BAT2 =
   {
   	{
   		0x00000000>>17, 	/* bepi */
   		BL_256M,		/* bl */
   		0,			/* vs */
   		0,			/* vp */
   	},
   	{
   		0x00000000>>17,		/* brpn */
   		1,			/* w */
   		1,			/* i (cache disabled) */
   		0,			/* m */
   		0,			/* g */
   		BPP_RW			/* pp */
   	}
   };
BAT BAT3 =
   {
   	{
   		0x00000000>>17, 	/* bepi */
   		BL_256M,		/* bl */
   		0,			/* vs */
   		0,			/* vp */
   	},
   	{
   		0x00000000>>17,		/* brpn */
   		1,			/* w */
   		1,			/* i (cache disabled) */
   		0,			/* m */
   		0,			/* g */
   		BPP_RW			/* pp */
   	}
   };
BAT TMP_BAT2 =
   { /* 0x9XXXXXXX -> 0x0XXXXXXX */
   	{
   		0x90000000>>17, 	/* bepi */
   		BL_16M,			/* bl */
   		1,			/* vs */
   		1,			/* vp */
   	},
   	{
   		0x00000000>>17,		/* brpn */
   		0,			/* w */
   		0,			/* i (cache enabled) */
   		0,			/* m */
   		0,			/* g */
   		BPP_RW			/* pp */
   	}
   };

unsigned long _SDR1;		/* Hardware SDR1 image */
PTE *Hash;
int Hash_size, Hash_mask;
unsigned long *end_of_DRAM;
int cache_is_copyback = 1;
int kernel_pages_are_copyback = 1;
/* Note: these need to be in 'data' so they live over the boot */
unsigned char *BeBox_IO_page = 0;
unsigned long isBeBox[2] = {0, 0};

#define NUM_MAPPINGS 128
struct
   {
   	int va, pa, pg, task;
   } last_mappings[NUM_MAPPINGS];
int next_mapping = 0;

/* Generic linked list */
struct item
   {
   	struct item *next;
   };
   
#ifndef NULL   
#define NULL 0
#endif

#define MAX_CONTEXTS	16
#define MAX_MMU_PAGES	8

static struct item _free_pages;
static char mmu_pages[(MAX_MMU_PAGES+1)*MMU_PAGE_SIZE];

/*
 * Routines to support generic linked lists.
 */

MMU_free_item(struct item *hdr, struct item *elem)
{
	if (hdr->next == (struct item *)NULL)
	{ /* First item in list */
		elem->next = (struct item *)NULL;
	} else
	{
		elem->next = hdr->next;
	}
	hdr->next = elem;
}

struct item *
MMU_get_item(struct item *hdr)
{
	struct item *item;
	if ((item = hdr->next) != (struct item *)NULL)
	{
		item = hdr->next;
		hdr->next = item->next;
	}
	return (item);
}

/*
 * This code is called to create a minimal mapped environment.
 * It is called with the MMU on, but with only a BAT register
 * set up to cover the code/data.  After this routine runs,
 * the BAT mapping is withdrawn and all mappings must be complete.
 */

extern char _start[], _end[];
 
MMU_init()
{
	int i, p;
	SEGREG *segs;
	_printk("MMU init - started\n");
	find_end_of_memory();
	_printk("  Start at 0x%08X, End at 0x%08X, Hash at 0x%08X\n", _start, _end, Hash);
	_SDR1 = ((unsigned long)Hash & 0x00FFFFFF) | Hash_mask;
	p = (int)mmu_pages;
	p = (p + (MMU_PAGE_SIZE-1)) & ~(MMU_PAGE_SIZE-1);
	_free_pages.next = (struct item *)NULL;
	for (i = 0;  i < MAX_MMU_PAGES;  i++)
	{
		MMU_free_item(&_free_pages, (struct item *)p);
		p += MMU_PAGE_SIZE;
	}
	/* Force initial page tables */
#if 0	
	swapper_pg_dir = (pgd_t *)MMU_get_page();
#endif	
	init_task.tss.pg_tables = (unsigned long *)swapper_pg_dir;
	/* Segment registers */
	segs = init_task.tss.segs;
	for (i = 0;  i < 16;  i++)
	{
		segs[i].ks = 0;
		segs[i].kp = 1;
		segs[i].vsid = i;
	}
	/* Map kernel TEXT+DATA+BSS */
	end_of_DRAM = (unsigned long *)Hash;
	/* Hard map in any special local resources */
	if (isBeBox[0])
	{
		/* Map in one page for the BeBox motherboard I/O */
		end_of_DRAM = (unsigned long *)((unsigned long)end_of_DRAM - MMU_PAGE_SIZE);
#if 0		
		BeBox_IO_page = (unsigned char *)0x7FFFF000;
#endif
		BeBox_IO_page = (unsigned char *)end_of_DRAM;
		MMU_map_page(&init_task.tss, BeBox_IO_page, 0x7FFFF000, PAGE_KERNEL);
		MMU_disable_cache_for_page(&init_task.tss, BeBox_IO_page);
	}
	/* Other parts of the kernel expect ALL RAM to be mapped */	
	for (i = (int)_start;  i < (int)end_of_DRAM;  i += MMU_PAGE_SIZE)
	{
		MMU_map_page(&init_task.tss, i, i & 0x00FFFFFF, PAGE_KERNEL);
	}
	/* Map hardware HASH table */
	for (i = (int)Hash;  i < (int)Hash+Hash_size;  i += MMU_PAGE_SIZE)
	{
		MMU_map_page(&init_task.tss, i, i & 0x00FFFFFF, PAGE_KERNEL);
	}
#if 0 /* I'm not sure this is necessary */
	/* Clear all DRAM not explicitly used by kernel */
	bzero(_end, (unsigned long)end_of_DRAM-(unsigned long)_end);
#endif
	_printk("MMU init - done!\n");
}

pte *
MMU_get_page()
{
	pte *pg;
	if ((pg = (pte *)MMU_get_item(&_free_pages)))
	{
		bzero((char *)pg, MMU_PAGE_SIZE);
	}
	_printk("MMU Allocate Page at %08X\n", pg);
	return(pg);
}

MMU_map_page(struct thread_struct *tss, unsigned long va, unsigned long pa, int flags)
{
	pte *pd, *pg;
#if 0
if (va < (unsigned long)0x90000000)	
  printk("Thread: %x, Map VA: %08x -> PA: %08X, Flags: %x\n", tss, va, pa, flags);
#endif
	if ((pte **)tss->pg_tables == (pte **)NULL)
	{ /* Allocate upper level page map */
		(pte **)tss->pg_tables = (pte **)MMU_get_page();
		if ((pte **)tss->pg_tables == (pte **)NULL)
		{
			_panic("Out of MMU pages (PD)\n");
		}
	}
	/* Use upper 10 bits of VA to index the first level map */
	pd = ((pte **)tss->pg_tables)[(va>>PD_SHIFT)&PD_MASK];
	pd = (pte *)((int)pd & 0xFFFFF000);
	if (pd == (pte *)NULL)
	{ /* Need to allocate second-level table */
		pd = (pte *)MMU_get_page();
		if (pd == (pte *)NULL)
		{
			_panic("Out of MMU pages (PG)\n");
		}
		((pte **)tss->pg_tables)[(va>>PD_SHIFT)&PD_MASK] = (pte *)((unsigned long)pd | _PAGE_TABLE);
	}
	/* Use middle 10 bits of VA to index the second-level map */
	pg = &pd[(va>>PT_SHIFT)&PT_MASK];
	*(long *)pg = 0;  /* Clear out entry */
	pg->page_num = pa>>PG_SHIFT;
	pg->flags = flags;
	MMU_hash_page(tss, va, pg);
}

/*
 * Insert(create) a hardware page table entry
 */
MMU_hash_page(struct thread_struct *tss, unsigned long va, pte *pg)
{
	int hash, page_index, segment, i, h, _h, api, vsid, perms;
	PTE *_pte, *empty, *slot;
	PTE *slot0, *slot1;
	extern char _etext;
/* TEMP */
if (va < KERNELBASE)		
{
	last_mappings[next_mapping].va = va;
	last_mappings[next_mapping].pa = pg?*(int *)pg:0;
	last_mappings[next_mapping].pg = pg;
	last_mappings[next_mapping].task = current->pid;
	if (++next_mapping == NUM_MAPPINGS) next_mapping = 0;
}
/* TEMP */	
	page_index = ((int)va & 0x0FFFF000) >> 12;
	segment = (unsigned int)va >> 28;
	api = page_index >> 10;
	vsid = ((SEGREG *)tss->segs)[segment].vsid;
	empty = slot = (PTE *)NULL;
	for (_h = 0;  _h < 2;  _h++)
	{
		hash = page_index ^ vsid;		
		if (_h)
		{
			hash = ~hash;  /* Secondary hash uses ones-complement */
		}
		hash &= 0x3FF | (Hash_mask << 10);
		hash *= 8;  /* Eight entries / hash bucket */
		_pte = &Hash[hash];
		/* Save slot addresses in case we have to purge */
		if (_h)
		{
			slot1 = _pte;
		} else
		{
			slot0 = _pte;
		}
		for (i = 0;  i < 8;  i++, _pte++)
		{
			if (_pte->v && _pte->vsid == vsid && _pte->h == _h && _pte->api == api)
			{ /* Found it! */
				h = _h;
				slot = _pte;
				goto found_it;
			}
			if ((empty == (PTE *)NULL) && !_pte->v)
			{
				h = _h;
				empty = _pte;
			}
		}
	}
	if (slot == (PTE *)NULL)
	{
		if (pg == (pte *)NULL)
		{
			return (0);
		}
		if (empty == (PTE *)NULL)
		{ /* Table is totally full! */
printk("Map VA: %08X, Slot: %08X[%08X/%08X], H: %d\n", va, slot, slot0, slot1, h);
printk("Slot0:\n");
_pte = slot0;
for (i = 0;  i < 8;  i++, _pte++)
{
	printk("  V: %d, VSID: %05x, H: %d, RPN: %04x, R: %d, C: %d, PP: %x\n", _pte->v, _pte->vsid, _pte->h, _pte->rpn, _pte->r, _pte->c, _pte->pp);
}
printk("Slot1:\n");
_pte = slot1;
for (i = 0;  i < 8;  i++, _pte++)
{
	printk("  V: %d, VSID: %05x, H: %d, RPN: %04x, R: %d, C: %d, PP: %x\n", _pte->v, _pte->vsid, _pte->h, _pte->rpn, _pte->r, _pte->c, _pte->pp);
}
cnpause();
printk("Last mappings:\n");
for (i = 0;  i < NUM_MAPPINGS;  i++)
{
	printk("  VA: %08x, PA: %08X, TASK: %08X\n",
		last_mappings[next_mapping].va,
		last_mappings[next_mapping].pa,
		last_mappings[next_mapping].task);
	if (++next_mapping == NUM_MAPPINGS) next_mapping = 0;
}
cnpause();
			_panic("Hash table full!\n");
		}
		slot = empty;
	}
found_it:
#if 0
_printk("Map VA: %08X, Slot: %08X[%08X/%08X], H: %d\n", va, slot, slot0, slot1, h);	
#endif
	_tlbie(va); /* Clear TLB */
	if (pg)
	{ /* Fill in table */
		slot->v = 1;
		slot->vsid = vsid;
		slot->h = h;
		slot->api = api;
		if (((pg->page_num << 12) & 0xF0000000) == KERNELBASE)
		{
			slot->rpn = pg->page_num - (KERNELBASE>>12);
		} else
		{
			slot->rpn = pg->page_num;
		}
		slot->r = 0;
		slot->c = 0;
		slot->i = 0;
		slot->g = 0;
		if (cache_is_copyback)
		{
			if (kernel_pages_are_copyback || (pg->flags & _PAGE_USER) || (va < (unsigned long)&_etext))
			{ /* All User & Kernel TEXT pages are copy-back */
				slot->w = 0;
				slot->m = 1;
			} else
			{ /* Kernel DATA pages are write-thru */
				slot->w = 1;
				slot->m = 0;
			}
		} else
		{
			slot->w = 1;
			slot->m = 0;
		}
		if (pg->flags & _PAGE_USER)
		{
			if (pg->flags & _PAGE_RW)
			{ /* Read/write page */
				perms = PP_RWRW;
			} else
			{ /* Read only page */
				perms = PP_RWRX;
				perms = PP_RXRX;
			}
		} else
		{ /* Kernel pages */
			perms = PP_RWRW;
			perms = PP_RWXX;
		}
#ifdef SHOW_FAULTS
if (va < KERNELBASE)		
printk("VA: %08X, PA: %08X, Flags: %x, Perms: %d, Vsid: %x\n", va, pg->page_num<<12, pg->flags, perms, vsid);
#endif
		slot->pp = perms;
		return (0);
	} else
	{ /* Pull entry from tables */
		int flags = 0;
		if (slot->r) flags |= _PAGE_ACCESSED;
		if (slot->c) flags |= _PAGE_DIRTY;
		slot->v = 0;
#ifdef SHOW_FAULTS
printk("Pull VA: %08X, Flags: %x\n", va, flags);
#endif
		return (flags);
	}
}

/*
 * Disable cache for a particular page
 */
MMU_disable_cache_for_page(struct thread_struct *tss, unsigned long va)
{
	int hash, page_index, segment, i, h, _h, api, vsid, perms;
	PTE *_pte, *empty, *slot;
	PTE *slot0, *slot1;
	extern char _etext;
	page_index = ((int)va & 0x0FFFF000) >> 12;
	segment = (unsigned int)va >> 28;
	api = page_index >> 10;
	vsid = ((SEGREG *)tss->segs)[segment].vsid;
	empty = slot = (PTE *)NULL;
	for (_h = 0;  _h < 2;  _h++)
	{
		hash = page_index ^ vsid;		
		if (_h)
		{
			hash = ~hash;  /* Secondary hash uses ones-complement */
		}
		hash &= 0x3FF | (Hash_mask << 10);
		hash *= 8;  /* Eight entries / hash bucket */
		_pte = &Hash[hash];
		/* Save slot addresses in case we have to purge */
		if (_h)
		{
			slot1 = _pte;
		} else
		{
			slot0 = _pte;
		}
		for (i = 0;  i < 8;  i++, _pte++)
		{
			if (_pte->v && _pte->vsid == vsid && _pte->h == _h && _pte->api == api)
			{ /* Found it! */
				h = _h;
				slot = _pte;
				goto found_it;
			}
			if ((empty == (PTE *)NULL) && !_pte->v)
			{
				h = _h;
				empty = _pte;
			}
		}
	}
found_it:	
	_tlbie(va); /* Clear TLB */
	slot->i = 1;
	slot->m = 0;
}

/*
 * Invalidate a hardware [hash] page table entry
 * Note: this should never be called [currently] for kernel addresses.
 */
MMU_invalidate_page(struct mm_struct *mm, unsigned long va, pte *pg)
{
	int hash, page_index, segment, i, h, _h, api, vsid, perms;
	PTE *_pte, *slot;
	int flags = 0;
	page_index = ((int)va & 0x0FFFF000) >> 12;
	segment = (unsigned int)va >> 28;
	api = page_index >> 10;
	vsid = mm->context | segment;
	slot = (PTE *)NULL;
	for (_h = 0;  _h < 2;  _h++)
	{
		hash = page_index ^ vsid;		
		if (_h)
		{
			hash = ~hash;  /* Secondary hash uses ones-complement */
		}
		hash &= 0x3FF | (Hash_mask << 10);
		hash *= 8;  /* Eight entries / hash bucket */
		_pte = &Hash[hash];
		for (i = 0;  i < 8;  i++, _pte++)
		{
			if (_pte->v && _pte->vsid == vsid && _pte->h == _h && _pte->api == api)
			{ /* Found it! */
				_tlbie(va); /* Clear TLB */
				if (_pte->r) flags |= _PAGE_ACCESSED;
				if (_pte->c) flags |= _PAGE_DIRTY;
				_pte->v = 0;
#ifdef SHOW_FAULTS
printk("Pull VA: %08X, Flags: %x\n", va, flags);
#endif
				return (flags);
			}
		}
	}
	return (flags);
}

/*
 * Invalidate the MMU [hardware] tables (for current task?)
 */
void
invalidate(void)
{
	int i, j, flags;
	unsigned long address;
	pgd_t *pgd;
	pte_t *_pte;
	static long _invalidates;
#ifdef SHOW_INVALIDATES
printk("invalidate()\n");
#endif
	_invalidates++;
#if 0 /* Unnecessary */
	_tlbia();  /* Flush TLB entries */
#endif
	pgd = pgd_offset(current->mm, 0);
	if (!pgd) return;  /* No map? */
	address = 0;
	for (i = 0 ; (i < PTRS_PER_PGD) && (address < KERNELBASE); i++)
	{
		if (*(long *)pgd)
		{
			/* I know there are only two levels, but the macros don't */
			_pte = pte_offset(pmd_offset(pgd,0),0);
			if (_pte)
			{
				for (j = 0;  j < PTRS_PER_PTE;  j++)
				{
					if (pte_present(*_pte))
					{
						flags = MMU_hash_page(&current->tss, address, 0);
						((pte *)_pte)->flags |= flags;
					}
					_pte++;
					address += PAGE_SIZE;
				}
			} else
			{
				address += PAGE_SIZE*PTRS_PER_PTE;
			}
		} else
		{
			address += PAGE_SIZE*PTRS_PER_PTE;
		}
		pgd++;
	}
} 

/*
 * Invalidate the MMU [hardware] tables (for current task?)
 */
void
flush_cache_mm(struct mm_struct *mm)
{
	int i, j, flags;
	unsigned long address;
	pgd_t *pgd;
	pte_t *_pte;
	static long _invalidates;
#ifdef SHOW_INVALIDATES
printk("invalidate_mm(%x)\n", mm);
#endif
if (!mm) return;	
	_invalidates++;
#if 0 /* Unnecessary */
	_tlbia();  /* Flush TLB entries */
#endif
	pgd = pgd_offset(mm, 0);
	if (!pgd) return;  /* No map? */
	address = 0;
	for (i = 0 ; (i < PTRS_PER_PGD) && (address < KERNELBASE); i++)
	{
		if (*(long *)pgd)
		{
			/* I know there are only two levels, but the macros don't */
			_pte = pte_offset(pmd_offset(pgd,0),0);
			if (_pte)
			{
				for (j = 0;  j < PTRS_PER_PTE;  j++)
				{
					if (pte_present(*_pte))
					{
						flags = MMU_invalidate_page(mm, address, 0);
						((pte *)_pte)->flags |= flags;
					}
					_pte++;
					address += PAGE_SIZE;
				}
			} else
			{
				address += PAGE_SIZE*PTRS_PER_PTE;
			}
		} else
		{
			address += PAGE_SIZE*PTRS_PER_PTE;
		}
		pgd++;
	}
} 

/*
 * Invalidate the MMU [hardware] tables (for current task?)
 */
void
flush_cache_page(struct vm_area_struct *vma, long va)
{
	int i, j, flags;
	unsigned long address;
	pgd_t *pgd;
	pte_t *_pte;
	static long _invalidates;
	struct mm_struct *mm = vma->vm_mm;
#ifdef SHOW_INVALIDATES
printk("invalidate_page(%x[%x], %x)\n", vma, mm, va);
#endif
if (!mm) return;  /* In case VMA lookup fails */	
	_invalidates++;
#if 0 /* Unnecessary */
	_tlbia();  /* Flush TLB entries */
#endif
/* Note: this could be MUCH better */
	pgd = pgd_offset(mm, 0);
	if (!pgd) return;  /* No map? */
	address = 0;
	for (i = 0 ; (i < PTRS_PER_PGD) && (address < KERNELBASE); i++)
	{
		if (*(long *)pgd)
		{
			/* I know there are only two levels, but the macros don't */
			_pte = pte_offset(pmd_offset(pgd,0),0);
			if (_pte)
			{
				for (j = 0;  j < PTRS_PER_PTE;  j++)
				{
					if ((va == address) && pte_present(*_pte))
					{
						flags = MMU_invalidate_page(mm, address, 0);
						((pte *)_pte)->flags |= flags;
					}
					_pte++;
					address += PAGE_SIZE;
				}
			} else
			{
				address += PAGE_SIZE*PTRS_PER_PTE;
			}
		} else
		{
			address += PAGE_SIZE*PTRS_PER_PTE;
		}
		pgd++;
	}
} 

/*
 * Invalidate the MMU [hardware] tables (for current task?)
 */
void
flush_cache_range(struct mm_struct *mm, unsigned long va_start, unsigned long va_end)
{
	int i, j, flags;
	unsigned long address;
	pgd_t *pgd;
	pte_t *_pte;
	static long _invalidates;
#ifdef SHOW_INVALIDATES
printk("invalidate_range(%x, %x, %x)\n", mm, va_start, va_end);
#endif
if (!mm) return;	
	_invalidates++;
#if 0 /* Unnecessary */
	_tlbia();  /* Flush TLB entries */
#endif
/* Note: this could be MUCH better */
	pgd = pgd_offset(mm, 0);
	if (!pgd) return;  /* No map? */
	address = 0;
	for (i = 0 ; (i < PTRS_PER_PGD) && (address < KERNELBASE); i++)
	{
		if (*(long *)pgd)
		{
			/* I know there are only two levels, but the macros don't */
			_pte = pte_offset(pmd_offset(pgd,0),0);
			if (_pte)
			{
				for (j = 0;  j < PTRS_PER_PTE;  j++)
				{
					if ((va_start <= address) && (va_end > address) && pte_present(*_pte))
					{
						flags = MMU_invalidate_page(mm, address, 0);
						((pte *)_pte)->flags |= flags;
					}
					_pte++;
					address += PAGE_SIZE;
				}
			} else
			{
				address += PAGE_SIZE*PTRS_PER_PTE;
			}
		} else
		{
			address += PAGE_SIZE*PTRS_PER_PTE;
		}
		pgd++;
	}
} 

void
cache_mode(char *str, int *ints)
{
	cache_is_copyback = ints[0];
}

_verify_addr(long va)
{
	int hash, page_index, segment, i, h, _h, api, vsid, perms;
	struct thread_struct *tss = &current->tss;
	PTE *_pte, *empty, *slot;
	PTE *slot0, *slot1;
	page_index = ((int)va & 0x0FFFF000) >> 12;
	segment = (unsigned int)va >> 28;
	api = page_index >> 10;
	vsid = ((SEGREG *)tss->segs)[segment].vsid;
	empty = slot = (PTE *)NULL;
	printk("Segment = %x/%x\n", *(long *)&tss->segs[segment], _get_SRx(segment));
	for (_h = 0;  _h < 2;  _h++)
	{
		hash = page_index ^ vsid;		
		if (_h)
		{
			hash = ~hash;  /* Secondary hash uses ones-complement */
		}
		hash &= 0x3FF | (Hash_mask << 10);
		hash *= 8;  /* Eight entries / hash bucket */
		_pte = &Hash[hash];
		dump_buf(_pte, 64);
		for (i = 0;  i < 8;  i++, _pte++)
		{
			if (_pte->v && _pte->vsid == vsid && _pte->h == _h && _pte->api == api)
			{ /* Found it! */
				h = _h;
				slot = _pte;
				printk("Found at %x\n", slot);
				goto found_it;
			}
			if ((empty == (PTE *)NULL) && !_pte->v)
			{
				h = _h;
				empty = _pte;
			}
		}
	}
found_it:	
	cnpause();
}

flush_cache_all()
{
	printk("flush_cache_all()\n");
	invalidate();
}

flush_tlb_all() {}
flush_tlb_mm() {}
flush_tlb_page() {}
flush_tlb_range() {}
flush_page_to_ram() {}
