/*
 *  arch/ppc/mm/init.c
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
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
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/residual.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>		/* for initrd_* */
#endif

int prom_trashed;
int next_mmu_context;
unsigned long _SDR1;
PTE *Hash, *Hash_end;
unsigned long Hash_size, Hash_mask;
unsigned long *end_of_DRAM;
int mem_init_done;
extern pgd_t swapper_pg_dir[];
extern char _start[], _end[];
extern char etext[], _stext[];
extern char __init_begin, __init_end;
extern RESIDUAL res;

/* Hardwired MMU segments */
#if defined(CONFIG_PREP) || defined(CONFIG_PMAC)
#define MMU_SEGMENT_1		0x80000000
#define MMU_SEGMENT_2		0xc0000000
#endif /* CONFIG_PREP || CONFIG_PMAC */
#ifdef CONFIG_CHRP
#define MMU_SEGMENT_1		0xf0000000	/* LongTrail */
#define MMU_SEGMENT_2		0xc0000000
#endif /* CONFIG_CHRP */


void *find_mem_piece(unsigned, unsigned);
static void mapin_ram(void);
static void inherit_prom_translations(void);
static void hash_init(void);
static void *MMU_get_page(void);
void map_page(struct task_struct *, unsigned long va,
		     unsigned long pa, int flags);
extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);
extern unsigned long *find_end_of_memory(void);

/*
 * this tells the prep system to map all of ram with the segregs
 * instead of the bats.  I'd like to get this to apply to the
 * pmac as well then have everything use the bats -- Cort
 */
#undef MAP_RAM_WITH_SEGREGS 1 

/*
 * these are used to setup the initial page tables
 * They can waste up to an entire page since the
 * I'll fix this shortly -- Cort
 */
#define MAX_MMU_PAGES	16
unsigned int probingmem = 0;
unsigned int mmu_pages_count = 0;
char mmu_pages[(MAX_MMU_PAGES+1)*PAGE_SIZE];

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
	memset((void *)empty_bad_page_table, 0, PAGE_SIZE);
	return (pte_t *) empty_bad_page_table;
}

unsigned long empty_bad_page;

pte_t __bad_page(void)
{
	memset((void *)empty_bad_page, 0, PAGE_SIZE);
	return pte_mkdirty(mk_pte(empty_bad_page, PAGE_SHARED));
}

#define MAX_MEM_REGIONS	32
phandle memory_pkg;

struct mem_pieces {
	int n_regions;
	struct reg_property regions[MAX_MEM_REGIONS];
};
struct mem_pieces phys_mem;
struct mem_pieces phys_avail;
struct mem_pieces prom_mem;

static void get_mem_prop(char *, struct mem_pieces *);
static void remove_mem_piece(struct mem_pieces *, unsigned, unsigned, int);
static void print_mem_pieces(struct mem_pieces *);

unsigned long avail_start;

/*
 * Read in a property describing some pieces of memory.
 */
static void
get_mem_prop(char *name, struct mem_pieces *mp)
{
	int s, i;

	s = (int) call_prom("getprop", 4, 1, memory_pkg, name,
			    mp->regions, sizeof(mp->regions));
	if (s < sizeof(mp->regions[0])) {
		printk("getprop /memory %s returned %d\n", name, s);
		abort();
	}
	mp->n_regions = s / sizeof(mp->regions[0]);

	/*
	 * Make sure the pieces are sorted.
	 */
	for (i = 1; i < mp->n_regions; ++i) {
		unsigned long a, s;
		int j;

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

/*
 * Remove some memory from an array of pieces
 */
static void
remove_mem_piece(struct mem_pieces *mp, unsigned start, unsigned size,
		 int must_exist)
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

static void
print_mem_pieces(struct mem_pieces *mp)
{
	int i;

	for (i = 0; i < mp->n_regions; ++i)
		printk(" [%x, %x)", mp->regions[i].address,
		       mp->regions[i].address + mp->regions[i].size);
	printk("\n");
}

void *
find_mem_piece(unsigned size, unsigned align)
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
 * Collect information about RAM and which pieces are already in use.
 * At this point, we have the first 8MB mapped with a BAT.
 * Our text, data, bss use something over 1MB, starting at 0.
 * Open Firmware may be using 1MB at the 4MB point.
 */
unsigned long *pmac_find_end_of_memory(void)
{
	unsigned long a, total;
	unsigned long kstart, ksize;
	extern char _stext[], _end[];
	int i;

	memory_pkg = call_prom("finddevice", 1, 1, "/memory");
	if (memory_pkg == (void *) -1)
		panic("can't find memory package");

	/*
	 * Find out where physical memory is, and check that it
	 * starts at 0 and is contiguous.  It seems that RAM is
	 * always physically contiguous on Power Macintoshes,
	 * because MacOS can't cope if it isn't.
	 */
	get_mem_prop("reg", &phys_mem);
	if (phys_mem.n_regions == 0)
		panic("No RAM??");
	a = phys_mem.regions[0].address;
	if (a != 0)
		panic("RAM doesn't start at physical address 0");
	total = phys_mem.regions[0].size;
	for (i = 1; i < phys_mem.n_regions; ++i) {
		a = phys_mem.regions[i].address;
		if (a != total) {
			printk("RAM starting at 0x%lx is not contiguous\n", a);
			printk("Using RAM from 0 to 0x%lx\n", total-1);
			phys_mem.n_regions = i;
			break;
		}
		total += phys_mem.regions[i].size;
	}

	/* record which bits the prom is using */
	get_mem_prop("available", &phys_avail);
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
	ksize = PAGE_ALIGN(_end - _stext);
	remove_mem_piece(&phys_avail, kstart, ksize, 0);
	remove_mem_piece(&prom_mem, kstart, ksize, 0);

	return __va(total);
}

/*
 * Find some memory for setup_arch to return.
 * We use the last chunk of available memory as the area
 * that setup_arch returns, making sure that there are at
 * least 32 pages unused before this for MMU_get_page to use.
 */
unsigned long find_available_memory(void)
{
	int i;
	unsigned long a, free;
	unsigned long start, end;
	
	if ( _machine != _MACH_Pmac )
		return 0;

	free = 0;
	for (i = 0; i < phys_avail.n_regions - 1; ++i) {
		start = phys_avail.regions[i].address;
		end = start + phys_avail.regions[i].size;
		free += (end & PAGE_MASK) - PAGE_ALIGN(start);
	}
	a = PAGE_ALIGN(phys_avail.regions[i].address);
	if (free < 32 * PAGE_SIZE)
		a += 32 * PAGE_SIZE - free;
	avail_start = (unsigned long) __va(a);
	return avail_start;
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;
	struct task_struct *p;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (!atomic_read(&mem_map[i].count))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
	printk("%-8s %3s %3s %8s %8s %8s %9s %8s\n", "Process", "Pid", "Cnt",
	       "Ctx", "Ctx<<4", "Last Sys", "pc", "task");
	for_each_task(p)
	{	
		printk("%-8.8s %3d %3d %8ld %8ld %8ld %c%08lx %08lx ",
		       p->comm,p->pid,
		       p->mm->count,p->mm->context,
		       p->mm->context<<4, p->tss.last_syscall,
		       user_mode(p->tss.regs) ? 'u' : 'k', p->tss.regs->nip,
		       (ulong)p);
		if ( p == current )
			printk("current");
		if ( p == last_task_used_math )
		{
			if ( p == current )
				printk(",");
			printk("last math");
		}

		printk("\n");
	}
}

extern unsigned long free_area_init(unsigned long, unsigned long);

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	/*
	 * Grab some memory for bad_page and bad_pagetable to use.
	 */
	empty_bad_page = start_mem;
	empty_bad_page_table = start_mem + PAGE_SIZE;
	start_mem += 2 * PAGE_SIZE;

	/* note: free_area_init uses its second argument
	   to size the mem_map array. */
	start_mem = free_area_init(start_mem, end_mem);
	return start_mem;
}

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long addr;
	int i;
	unsigned long a, lim;
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;

	end_mem &= PAGE_MASK;
	high_memory = (void *) end_mem;
	max_mapnr = MAP_NR(high_memory);
	num_physpages = max_mapnr;	/* RAM is assumed contiguous */

	/* mark usable pages in the mem_map[] */
	start_mem = PAGE_ALIGN(start_mem);

	if ( _machine == _MACH_Pmac )
	{
		remove_mem_piece(&phys_avail, __pa(avail_start),
				 start_mem - avail_start, 1);
		
		for (addr = KERNELBASE ; addr < end_mem; addr += PAGE_SIZE)
			set_bit(PG_reserved, &mem_map[MAP_NR(addr)].flags);
		
		for (i = 0; i < phys_avail.n_regions; ++i) {
			a = (unsigned long) __va(phys_avail.regions[i].address);
			lim = a + phys_avail.regions[i].size;
			a = PAGE_ALIGN(a);
			for (; a < lim; a += PAGE_SIZE)
				clear_bit(PG_reserved, &mem_map[MAP_NR(a)].flags);
		}
		phys_avail.n_regions = 0;
		
		/* free the prom's memory */
		for (i = 0; i < prom_mem.n_regions; ++i) {
			a = (unsigned long) __va(prom_mem.regions[i].address);
			lim = a + prom_mem.regions[i].size;
			a = PAGE_ALIGN(a);
			for (; a < lim; a += PAGE_SIZE)
				clear_bit(PG_reserved, &mem_map[MAP_NR(a)].flags);
		}
		prom_trashed = 1;
	}
	else /* prep */
	{
		/* mark mem used by kernel as reserved, mark other unreserved */
		for (addr = PAGE_OFFSET ; addr < end_mem; addr += PAGE_SIZE)
		{
			/* skip hash table gap */
			if ( (addr > (ulong)_end) && (addr < (ulong)Hash))
				continue;
			if ( addr < (ulong) /*Hash_end*/ start_mem )
				set_bit(PG_reserved, &mem_map[MAP_NR(addr)].flags);
			else
				clear_bit(PG_reserved, &mem_map[MAP_NR(addr)].flags);
		}
	}

	for (addr = PAGE_OFFSET; addr < end_mem; addr += PAGE_SIZE) {
		if (PageReserved(mem_map + MAP_NR(addr))) {
			if (addr < (ulong) etext)
				codepages++;
			else if((addr >= (unsigned long)&__init_begin && addr < (unsigned long)&__init_end))
                                initpages++;
                        else if (addr < (ulong) start_mem)
				datapages++;
			continue;
		}
		atomic_set(&mem_map[MAP_NR(addr)].count, 1);
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start ||
		    (addr < initrd_start || addr >= initrd_end))
#endif /* CONFIG_BLK_DEV_INITRD */
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

/*
 * this should reclaim gap between _end[] and hash table
 * as well as unused mmu_pages[] on prep systems.
 * When I get around to it, I'll put initialization functions
 * (called only at boot) in their own .section and free that -- Cort
 */
void free_initmem(void)
{
	unsigned long a;
	unsigned long num_freed_pages = 0;

	/* free unused mmu_pages[] */
	a = PAGE_ALIGN( (unsigned long) mmu_pages) + (mmu_pages_count*PAGE_SIZE);
	for ( ; a < PAGE_ALIGN((unsigned long)mmu_pages)+(MAX_MMU_PAGES*PAGE_SIZE); a += PAGE_SIZE )
	{
		clear_bit( PG_reserved, &mem_map[MAP_NR(a)].flags );
		atomic_set(&mem_map[MAP_NR(a)].count, 1);
		free_page(a);
		num_freed_pages++;
	}

	a = (unsigned long)(&__init_begin);
	for (; a < (unsigned long)(&__init_end); a += PAGE_SIZE) {
		mem_map[MAP_NR(a)].flags &= ~(1 << PG_reserved);
		atomic_set(&mem_map[MAP_NR(a)].count, 1);
		free_page(a);
	}

	printk ("Freeing unused kernel memory: %ldk freed\n",
		(num_freed_pages * PAGE_SIZE) >> 10);
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

BAT BAT0 =
{
  {
    MMU_SEGMENT_1>>17, 	/* bepi */
    BL_256M,		/* bl */
    1,			/* vs -- supervisor mode valid */
    1,			/* vp -- user mode valid */
  },
  {
    MMU_SEGMENT_1>>17,		/* brpn */
    1,			/* write-through */
    1,			/* cache-inhibited */
    0,			/* memory coherence */
    1,			/* guarded */
    BPP_RW		/* protection */
  }
};
BAT BAT1 =
{
  {
    MMU_SEGMENT_2>>17, 	/* bepi */
    BL_256M,		/* bl */
    1,			/* vs */
    1,			/* vp */
  },
  {
    MMU_SEGMENT_2>>17,		/* brpn */
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
    0x90000000>>17, 	/* bepi */
    BL_256M, /* this gets set to amount of phys ram */
    1,			/* vs */
    0,			/* vp */
  },
  {
    0x00000000>>17,		/* brpn */
    0,			/* w */
    0,			/* i */
#ifdef __SMP__    
    1,			/* m */
#else    
    0,			/* m */
#endif    
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
    0,			/* w */
    0,			/* i (cache disabled) */
    1,			/* m */
    0,			/* g */
    BPP_RW			/* pp */
  }
};

P601_BAT BAT0_601 =
{
  {
    0x80000000>>17, 	/* bepi */
    1,1,0, /* wim */
    1, 0, /* vs, vp */
    BPP_RW, /* pp */
  },
  {
    0x80000000>>17,		/* brpn */
    1, /* v */
    BL_8M, /* bl */
  }
};

P601_BAT BAT1_601 =
{
  {
    MMU_SEGMENT_2>>17, 	/* bepi */
    1,1,0, /* wim */
    1, 0, /* vs, vp */
    BPP_RW, /* pp */
  },
  {
    MMU_SEGMENT_2>>17,		/* brpn */
    1, /* v */
    BL_8M, /* bl */
  }
};

P601_BAT BAT2_601 =
{
  {
    0x90000000>>17, 	/* bepi */
    0,0,0, /* wim */
    1, 0, /* vs, vp */
    BPP_RW, /* pp */
  },
  {
    0x00000000>>17,		/* brpn */
    1, /* v */
    BL_8M, /* bl */
  }
};

P601_BAT BAT3_601 =
{
  {
    0x90800000>>17, 	/* bepi */
    0,0,0, /* wim */
    1, 0, /* vs, vp */
    BPP_RW, /* pp */
  },
  {
    0x00800000>>17,		/* brpn */
    1, /* v */
    BL_8M, /* bl */
  }
};

/*
 * This finds the amount of physical ram and does necessary
 * setup for prep.  This is pretty architecture specific so
 * this will likely stay seperate from the pmac.
 * -- Cort
 */
unsigned long *prep_find_end_of_memory(void)
{
	int i;
	
	if (res.TotalMemory == 0 )
	{
		/*
		 * I need a way to probe the amount of memory if the residual
		 * data doesn't contain it. -- Cort
		 */
		printk("Ramsize from residual data was 0 -- Probing for value\n");
		res.TotalMemory = 0x03000000;
		printk("Ramsize default to be %ldM\n", res.TotalMemory>>20);
	}

	/* NOTE: everything below here is moving to mapin_ram() */

	
	/*
	 * if this is a 601, we can only map sizes of 8M with the BAT's
	 * so we have to map what we can't map with the bats with the segregs
	 * head.S will copy in the appropriate BAT's according to the processor
	 * since the 601_BAT{2,3} structures are already setup to map
	 * the first 16M correctly
	 * -- Cort
	 */
#ifndef MAP_RAM_WITH_SEGREGS     /* don't need to do it twice */
	if ( _get_PVR() == 1 )
	{
		/* map in rest of ram with seg regs */
		if ( res.TotalMemory > 0x01000000 /* 16M */)
		{
			for (i = KERNELBASE+0x01000000;
			     i < KERNELBASE+res.TotalMemory;  i += PAGE_SIZE)
				map_page(&init_task, i, __pa(i),
					 _PAGE_PRESENT| _PAGE_RW|_PAGE_DIRTY|_PAGE_ACCESSED);
		}
	}
#endif /* MAP_RAM_WITH_SEGREGS */
  
#ifdef MAP_RAM_WITH_SEGREGS
	/* turn off bat mapping kernel since being done with segregs */
	memset(&BAT2, sizeof(BAT2), 0);
	memset(&BAT2_601, sizeof(BAT2), 0); /* in case we're on a 601 */
	memset(&BAT3_601, sizeof(BAT2), 0);
	/* map all of ram for kernel with segregs */
	for (i = KERNELBASE;  i < KERNELBASE+res.TotalMemory;  i += PAGE_SIZE)
	{
		if ( i < (unsigned long)etext )
			map_page(&init_task, i, __pa(i),
				 _PAGE_PRESENT/*| _PAGE_RW*/|_PAGE_DIRTY|_PAGE_ACCESSED);
		else
			map_page(&init_task, i, __pa(i),
				 _PAGE_PRESENT| _PAGE_RW|_PAGE_DIRTY|_PAGE_ACCESSED);
	}
#endif /* MAP_RAM_WITH_SEGREGS */
	
	return (__va(res.TotalMemory));
}


/*
 * Map in all of physical memory starting at KERNELBASE.
 */
extern int n_mem_regions;
extern struct reg_property mem_regions[];

#define PAGE_KERNEL_RO	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)

static void mapin_ram()
{
    int i;
    unsigned long v, p, s, f;

    if ( _machine == _MACH_Pmac )
    {
	    v = KERNELBASE;
	    for (i = 0; i < phys_mem.n_regions; ++i) {
		    p = phys_mem.regions[i].address;
		    for (s = 0; s < phys_mem.regions[i].size; s += PAGE_SIZE) {
			    f = _PAGE_PRESENT | _PAGE_ACCESSED;
			    if ((char *) v < _stext || (char *) v >= etext)
				    f |= _PAGE_RW | _PAGE_DIRTY | _PAGE_HWWRITE;
			    else
				    /* On the powerpc, no user access forces R/W kernel access */
				    f |= _PAGE_USER;
			    map_page(&init_task, v, p, f);
			    v += PAGE_SIZE;
			    p += PAGE_SIZE;
		    }
	    }
    }
    else /* prep */
    {
	    /* setup the bat2 mapping to cover physical ram */
	    BAT2.batu.bl = 0x1; /* 256k mapping */
	    for ( f = 256*1024 /* 256k */ ;
		  (f <= res.TotalMemory) && (f <= 256*1024*1024);
		  f *= 2 )
		    BAT2.batu.bl = (BAT2.batu.bl << 1) | BAT2.batu.bl;
	    /*
	     * let ibm get to the device mem from user mode since
	     * the X for them needs it right now -- Cort
	     */
	    if ( _machine == _MACH_IBM )
		    BAT0.batu.vp = BAT1.batu.vp = 1;
	    
    }
}

#define MAX_PROM_TRANSLATIONS	64

static struct translation_property prom_translations[MAX_PROM_TRANSLATIONS];
int n_translations;
phandle mmu_pkg;
extern ihandle prom_chosen;

static void inherit_prom_translations()
{
	int s, i, f;
	unsigned long v, p, n;
	struct translation_property *tp;
	ihandle mmu_inst;

	if ((int) call_prom("getprop", 4, 1, prom_chosen, "mmu",
			    &mmu_inst, sizeof(mmu_inst)) != sizeof(mmu_inst))
		panic("couldn't get /chosen mmu property");
	mmu_pkg = call_prom("instance-to-package", 1, 1, mmu_inst);
	if (mmu_pkg == (phandle) -1)
		panic("couldn't get mmu package");
	s = (int) call_prom("getprop", 4, 1, mmu_pkg, "translations",
			    &prom_translations, sizeof(prom_translations));
	if (s < sizeof(prom_translations[0]))
		panic("couldn't get mmu translations property");
	n_translations = s / sizeof(prom_translations[0]);

	for (tp = prom_translations, i = 0; i < n_translations; ++i, ++tp) {
		/* ignore stuff mapped down low */
		if (tp->virt < 0x10000000 && tp->phys < 0x10000000)
			continue;
		/* map PPC mmu flags to linux mm flags */
		f = (tp->flags & (_PAGE_NO_CACHE | _PAGE_WRITETHRU
				  | _PAGE_COHERENT | _PAGE_GUARDED))
			| pgprot_val(PAGE_KERNEL);
		/* add these pages to the mappings */
		v = tp->virt;
		p = tp->phys;
		n = tp->size;
		for (; n != 0; n -= PAGE_SIZE) {
			map_page(&init_task, v, p, f);
			v += PAGE_SIZE;
			p += PAGE_SIZE;
		}
	}
}

/*
 * Initialize the hash table and patch the instructions in head.S.
 */
static void hash_init(void)
{
	int Hash_bits;
	unsigned long h;

	extern unsigned int hash_page_patch_A[], hash_page_patch_B[],
		hash_page_patch_C[];

	/*
	 * Allow 64k of hash table for every 16MB of memory,
	 * up to a maximum of 2MB.
	 */
	for (h = 64<<10; h < (ulong)__pa(end_of_DRAM) / 256 && h < 2<<20; h *= 2)
		;
	Hash_size = h;
	Hash_mask = (h >> 6) - 1;

	/* Find some memory for the hash table. */
	if ( is_prep )
		/* align htab on a Hash_size boundry above _end[] */
		Hash = (PTE *)_ALIGN( (unsigned long)&_end, Hash_size);
	else /* pmac */
		Hash = find_mem_piece(Hash_size, Hash_size);
	
	printk("Total memory = %ldMB; using %ldkB for hash table (at %p)\n",
	       __pa(end_of_DRAM) >> 20, Hash_size >> 10, Hash);
	memset(Hash, 0, Hash_size);
	Hash_end = (PTE *) ((unsigned long)Hash + Hash_size);

	/*
	 * Patch up the instructions in head.S:hash_page
	 */
	Hash_bits = ffz(~Hash_size) - 6;
	hash_page_patch_A[0] = (hash_page_patch_A[0] & ~0xffff)
		| (__pa(Hash) >> 16);
	hash_page_patch_A[1] = (hash_page_patch_A[1] & ~0x7c0)
		| ((26 - Hash_bits) << 6);
	if (Hash_bits > 16)
		Hash_bits = 16;
	hash_page_patch_A[2] = (hash_page_patch_A[2] & ~0x7c0)
		| ((26 - Hash_bits) << 6);
	hash_page_patch_B[0] = (hash_page_patch_B[0] & ~0xffff)
		| (Hash_mask >> 10);
	hash_page_patch_C[0] = (hash_page_patch_C[0] & ~0xffff)
		| (Hash_mask >> 10);

	/*
	 * Ensure that the locations we've patched have been written
	 * out from the data cache and invalidated in the instruction
	 * cache, on those machines with split caches.
	 */
	flush_icache_range((unsigned long) hash_page_patch_A,
			   (unsigned long) (hash_page_patch_C + 1));
}


/*
 * Do very early mm setup such as finding the size of memory
 * and setting up the hash table.
 * A lot of this is prep/pmac specific but a lot of it could
 * still be merged.
 * -- Cort
 */
void
MMU_init(void)
{
	if ( _machine == _MACH_Pmac )
		end_of_DRAM = pmac_find_end_of_memory();
	else /* prep and chrp */
		end_of_DRAM = prep_find_end_of_memory();
		
        hash_init();
        _SDR1 = __pa(Hash) | (Hash_mask >> 10);

	/* Map in all of RAM starting at KERNELBASE */
	mapin_ram();
	if ( _machine == _MACH_Pmac )
		/* Copy mappings from the prom */
		inherit_prom_translations();
}

static void *
MMU_get_page()
{
	void *p;

	if (mem_init_done) {
		p = (void *) __get_free_page(GFP_KERNEL);
		if (p == 0)
			panic("couldn't get a page in MMU_get_page");
	} else {
		if ( is_prep || (_machine == _MACH_chrp) )
		{
			mmu_pages_count++;
			if ( mmu_pages_count > MAX_MMU_PAGES )
				printk("out of mmu pages!\n");
			p = (pte *)(PAGE_ALIGN((unsigned long)mmu_pages)+
				    (mmu_pages_count+PAGE_SIZE));
		}
		else /* pmac */
		{
			p = find_mem_piece(PAGE_SIZE, PAGE_SIZE);
		}
	}
	memset(p, 0, PAGE_SIZE);
	return p;
}

void *
ioremap(unsigned long addr, unsigned long size)
{
	unsigned long p, end = addr + size;

	/*
	 * BAT mappings on prep cover this already so don't waste
	 * space with it. -- Cort
	 */
	if ( is_prep )
		if ( ((addr >= 0xc0000000) && (end < (0xc0000000+(256<<20)))) ||
		     ((addr >= 0x80000000) && (end < (0x80000000+(256<<20)))) )
			return (void *)addr;
	for (p = addr & PAGE_MASK; p < end; p += PAGE_SIZE)
		map_page(&init_task, p, p, pgprot_val(PAGE_KERNEL_CI) | _PAGE_GUARDED);
	return (void *) addr;
}

extern void iounmap(unsigned long *addr)
{
	/*
	 * BAT mappings on prep cover this already so don't waste
	 * space with it. -- Cort
	 */
	if ( is_prep )
		if ( (((unsigned long)addr >= 0xc0000000) && ((unsigned long)addr < (0xc0000000+(256<<20)))) ||
		     (((unsigned long)addr >= 0x80000000) && ((unsigned long)addr < (0x80000000+(256<<20)))) )
			return;
	/* else unmap it */
}

void
map_page(struct task_struct *tsk, unsigned long va,
	 unsigned long pa, int flags)
{
	pmd_t *pd;
	pte_t *pg;
	

	if (tsk->mm->pgd == NULL) {
		/* Allocate upper level page map */
		tsk->mm->pgd = (pgd_t *) MMU_get_page();
	}
	/* Use upper 10 bits of VA to index the first level map */
	pd = (pmd_t *) (tsk->mm->pgd + (va >> PGDIR_SHIFT));
	if (pmd_none(*pd)) {
		/* Need to allocate second-level table */
		pg = (pte_t *) MMU_get_page();
		pmd_val(*pd) = (unsigned long) pg;
	}
	/* Use middle 10 bits of VA to index the second-level map */
	pg = pte_offset(pd, va);
	set_pte(pg, mk_pte_phys(pa & PAGE_MASK, __pgprot(flags)));
	/*flush_hash_page(va >> 28, va);*/
	flush_hash_page(0, va);
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
 * Flush all tlb/hash table entries except for the kernel's.
 * We use the fact that only kernel mappings use VSIDs 0 - 15.
 */
void
flush_tlb_all(void)
{
	struct task_struct *tsk;

	read_lock(&tasklist_lock);
	for_each_task(tsk) {
		if (tsk->mm)
			tsk->mm->context = NO_CONTEXT;
	}
	read_unlock(&tasklist_lock);
	get_mmu_context(current);
	set_context(current->mm->context);
}


/*
 * Flush all the (user) entries for the address space described
 * by mm.  We can't rely on mm->mmap describing all the entries
 * that might be in the hash table.
 */
void
flush_tlb_mm(struct mm_struct *mm)
{
	mm->context = NO_CONTEXT;
	if (mm == current->mm) {
		get_mmu_context(current);
		/* done by get_mmu_context() now -- Cort */
		/*set_context(current->mm->context);*/
	}
}

/* for each page addr in the range, call MMU_invalidate_page()
   if the range is very large and the hash table is small it might be faster to
   do a search of the hash table and just invalidate pages that are in the range
   but that's for study later.
        -- Cort
   */
void
flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	start &= PAGE_MASK;
	for (; start < end && start < TASK_SIZE; start += PAGE_SIZE)
	{
		flush_hash_page(mm->context, start);
	}
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
	struct task_struct *tsk;

	printk(KERN_DEBUG "mmu_context_overflow\n");
	read_lock(&tasklist_lock);
 	for_each_task(tsk) {
		if (tsk->mm)
			tsk->mm->context = NO_CONTEXT;
	}
	read_unlock(&tasklist_lock);
	flush_hash_segments(0x10, 0xffffff);
	next_mmu_context = 0;
	/* make sure current always has a context */
	current->mm->context = MUNGE_CONTEXT(++next_mmu_context);
	set_context(current->mm->context);
}

