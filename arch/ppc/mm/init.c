/*
 *  arch/ppc/mm/init.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Ported to PPC by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
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
#include <asm/pgtable.h>
#include <asm/residual.h>

extern pgd_t swapper_pg_dir[1024];
extern unsigned long empty_zero_page[1024];

extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);
void flush_hash_table(void);

#undef HASHSTATS

unsigned long _SDR1;		/* Hardware SDR1 image */
PTE *Hash;
int Hash_size, Hash_mask;
unsigned long *end_of_DRAM;
int cache_is_copyback = 1;
int kernel_pages_are_copyback = 1;
/* Note: these need to be in 'data' so they live over the boot */
unsigned char *BeBox_IO_page = 0;
unsigned long isBeBox[2] = {0, 0};

#ifdef HASHSTATS
extern unsigned long *hashhits;
#endif



pte_t * __bad_pagetable(void)
{
	panic("__bad_pagetable");
}

pte_t __bad_page(void)
{
	panic("__bad_page");
}

void show_mem(void)
{
	struct task_struct *p;
	unsigned long i,free = 0,total = 0,reserved = 0;
	unsigned long shared = 0;
	PTE *ptr;
	unsigned long full = 0, overflow = 0;
	unsigned int ti;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = MAP_NR(high_memory);
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (!atomic_read(&mem_map[i].count))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
	}
	printk("%lu pages of RAM\n",total);
	printk("%lu free pages\n",free);
	printk("%lu reserved pages\n",reserved);
	printk("%lu pages shared\n",shared);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
#ifdef HASHSTATS
	printk("Hash Hits %u entries (buckets)\n",(Hash_size/sizeof(struct _PTE))/8);
	for ( i = 0; i < (Hash_size/sizeof(struct _PTE))/8; i++ ) {
		if ( hashhits[i] >= 20 )
			printk("[%lu] \t %lu\n", i,hashhits[i]);
	}
#endif
  
	for ( ptr = Hash ; ptr <= Hash+Hash_size ; ptr++) {
		if (ptr->v) {
			full++;
			if (ptr->h == 1)
				overflow++;
		}
	}
	printk("Hash Table: %dkB Buckets: %dk PTEs: %d/%d (%%%d full) %d overflowed\n",
	       Hash_size>>10,	(Hash_size/(sizeof(PTE)*8)) >> 10,
	       full,Hash_size/sizeof(PTE),
	       (full*100)/(Hash_size/sizeof(PTE)),
	       overflow);
	printk(" Task  context    vsid0\n");
	read_lock(&tasklist_lock);
	for_each_task(p) {
		printk("%5d %8x %8x\n",
		       p->pid,p->mm->context,
		       ((SEGREG *)p->tss.segs)[0].vsid);
	}
	read_unlock(&tasklist_lock);
}

extern unsigned long free_area_init(unsigned long, unsigned long);

unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	return free_area_init(start_mem, end_mem);
}

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
  int codepages = 0;
  int datapages = 0;
  unsigned long tmp;
  extern int etext;
	
  end_mem &= PAGE_MASK;
  high_memory = (void *)end_mem;
  max_mapnr = MAP_NR(end_mem);
  /* clear the zero-page */
  memset(empty_zero_page, 0, PAGE_SIZE);
	
	/* mark usable pages in the mem_map[] */
  start_mem = PAGE_ALIGN(start_mem);

  for (tmp = KERNELBASE ; tmp < (long)high_memory ; tmp += PAGE_SIZE)
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
      atomic_set(&mem_map[MAP_NR(tmp)].count, 1);
      free_page(tmp);
    }
  tmp = nr_free_pages << PAGE_SHIFT;
  printk("Memory: %luk/%luk available (%dk kernel code, %dk data)\n",
	 tmp >> 10,
	 ((int)high_memory - (int)KERNELBASE) >> 10,
	 codepages << (PAGE_SHIFT-10),
	 datapages << (PAGE_SHIFT-10));
  /*	invalidate();*/
  return;
}

void free_initmem(void)
{
	/* To be written */
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
   		0x80000000>>17, 	/* bepi */
   		BL_256M,		/* bl */
   		1,			/* vs -- supervisor mode valid */
   		1,			/* vp -- user mode valid */
   	},
   	{
   		0x80000000>>17,		/* brpn */
   		1,			/* write-through */
   		1,			/* cache-inhibited */
   		0,			/* memory coherence */
   		1,			/* guarded */
   		BPP_RW			/* protection */
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
   		0x90000000>>17, 	/* bepi */
		BL_256M, /* this gets set to amount of phys ram */
   		1,			/* vs */
   		0,			/* vp */
   	},
   	{
   		0x00000000>>17,		/* brpn */
   		0,			/* w */
   		0,			/* i */
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
   		BL_256M,		/* bl */
   		1,			/* vs */
   		1,			/* vp */
   	},
   	{
   		0x00000000>>17,		/* brpn */
   		1,			/* w */
   		0,			/* i (cache enabled) */
   		0,			/* m */
   		0,			/* g */
   		BPP_RW			/* pp */
   	}
   };


#ifndef NULL
#define NULL 0
#endif

/*
 * This code is called to create a minimal mapped environment.
 * It is called with the MMU on, but with only a BAT register
 * set up to cover the code/data.  After this routine runs,
 * the BAT mapping is withdrawn and all mappings must be complete.
 */




extern char _start[], _end[];
 
void MMU_init(void)
{
  extern RESIDUAL res;
  extern unsigned long resptr;
  int i, p;
  SEGREG *segs;
  
  /* copy residual data */
  if ( resptr )
    memcpy( &res, (void *)(resptr+KERNELBASE), sizeof(RESIDUAL) );
  else
    bzero( &res, sizeof(RESIDUAL) ); /* clearing bss probably clears this but... */
  
  end_of_DRAM = (unsigned long *)find_end_of_memory();
  _SDR1 = ((unsigned long)Hash - KERNELBASE) | Hash_mask;
#if 0	
  printk("Hash      %08x\n",(unsigned long)Hash);
  printk("Hash_mask %08x\n",Hash_mask);
  printk("Hash_size %08x\n",Hash_size);	
  printk("SDR1      %08x\n",_SDR1);
#endif
  /* Segment registers */
  segs = (SEGREG *)init_task.tss.segs;
  for (i = 0;  i < 16;  i++)
  {
    segs[i].ks = 0;
    segs[i].kp = 1;
#if 1
    if ( i < 8 )
      segs[i].vsid = i+10000;
    else
#else
    if ( i < 8 )
      segs[i].vsid = i<<5;
#endif		  
      segs[i].vsid = i;
  }
  
  
	
  /* Hard map in any special local resources */
  if (isBeBox[0])
    {
      /* Map in one page for the BeBox motherboard I/O */
      end_of_DRAM = (unsigned long *)((unsigned long)end_of_DRAM - PAGE_SIZE);
#if 0		
      BeBox_IO_page = (unsigned char *)0x7FFFF000;
#endif
		BeBox_IO_page = (unsigned char *)end_of_DRAM;
		MMU_disable_cache_for_page(&init_task.tss, BeBox_IO_page);
    }
}

/*
 * Insert(create) a hardware page table entry
 */
int inline MMU_hash_page(struct thread_struct *tss, unsigned long va, pte *pg)
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
  
  if ( (va <= _etext) && (va >= KERNELBASE))
  {
    printk("MMU_hash_page: called on kernel page mapped with bats va %x\n",
	   va);
  }

  /* check first hash bucket */
  h = 0;
  hash = page_index ^ vsid;
  hash &= 0x3FF | (Hash_mask << 10);
  hash *= 8;			/* 8 entries in each bucket */
  _pte = &Hash[hash];
  slot0 = _pte;
  for (i = 0;  i < 8;  i++, _pte++)
    {
      if (_pte->v && _pte->vsid == vsid && _pte->h == h && _pte->api == api)
	{
	  slot = _pte;
	  goto found_it;
	}
      if ((empty == NULL) && (!_pte->v))
	{
	  empty = _pte;
	  _h = h;
	}
    }

  /* check second hash bucket */
  h = 1;
  hash = page_index ^ vsid;
  hash = ~hash;
  hash &= 0x3FF | (Hash_mask << 10);
  hash *= 8;			/* 8 entries in each bucket */
  _pte = &Hash[hash];
  slot1 = _pte;
  for (i = 0;  i < 8;  i++, _pte++)
    {
      if (_pte->v && _pte->vsid == vsid && _pte->h == h && _pte->api == api)
	{
	  slot = _pte;
	  goto found_it;
	}
      if ((empty == NULL) && (!_pte->v))
	{
	  empty = _pte;
	  _h = h;
	}
    }

  if (empty == (PTE *)NULL)
  { 
#if 1
    printk("Both hash buckets full! va %x vsid %x current %s (%d)\n",
	   va,vsid,current->comm,current->pid);
#endif
    slot = slot1;
    h = 1;
  }
  else
  {
    slot = empty;
    h = _h;
  }
found_it:
#ifdef HASHSTATS
  hashhits[hash]++;
#endif
  _tlbie(va); /* Clear TLB */
    /* Fill in table */
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
      slot->pp = perms;
      return (0);
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
 * invalidate a hardware hash table pte
 */
inline void MMU_invalidate_page(struct mm_struct *mm, unsigned long va)
{
	int hash, page_index, segment, i, h, _h, api, vsid, perms;
	PTE *_pte, *slot;
	int flags = 0;
	page_index = ((int)va & 0x0FFFF000) >> 12;
	segment = (unsigned int)va >> 28;
	api = page_index >> 10;
	vsid = mm->context | segment;
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
				return (flags);
			}
		}
	}
	_tlbie(va);
	return (flags);
}


inline void
flush_cache_all(void)
{
}
inline void
flush_cache_mm(struct mm_struct *mm)
{
} 
inline void
flush_cache_page(struct vm_area_struct *vma, long va)
{
} 
inline void
flush_cache_range(struct mm_struct *mm, unsigned long va_start, unsigned long va_end)
{
} 

inline void
cache_mode(char *str, int *ints)
{
	cache_is_copyback = ints[0];
}

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *
 * since the hardware hash table functions as an extension of the
 * tlb as far as the linux tables are concerned, flush them too.
 *    -- Cort
 */
inline void
flush_tlb(void)
{
  PTE *ptep;
  int context = current->mm->context;
  struct vm_area_struct *v;
  unsigned int i;
  
  v = current->mm->mmap;

  /* for every virtual memory address in the current context -- flush
     the hash table */
  while ( v != NULL )
  {
    for ( i = v->vm_start ; i <= v->vm_end; i += PAGE_SIZE)
    {
      MMU_invalidate_page(v->vm_mm,i);
    }
    v = v->vm_next;
  }

  _tlbia();
}

/* flush all tlb/hash table entries except for kernels

   although the kernel is mapped with the bats, it's dynamic areas
   obtained via kmalloc are mapped by the seg regs
                      -- Cort
   */
inline void
flush_tlb_all(void)
{
  PTE *ptep;

  /* flush hash table */
  for ( ptep = Hash ; ptep < (PTE *)((unsigned long)Hash+Hash_size) ; ptep++ )
  {
    /* if not kernel vsids 0-7 (vsid greater than that for process 0)*/
    if ( (ptep->vsid > 7 ) && (ptep->v))
    {
      ptep->v = 0;
    }
  }

  _tlbia();
}

inline void
flush_tlb_mm(struct mm_struct *mm)
{
  PTE *ptep;
  int context = mm->context;
  struct vm_area_struct *v;
  unsigned int i;

  v = mm->mmap;
  while ( v != NULL )
  {
    for ( i = v->vm_start ; i <= v->vm_end; i += PAGE_SIZE)
    {
      MMU_invalidate_page(v->vm_mm,i);
    }
    v = v->vm_next;
  }

  _tlbia();
}


inline void
flush_tlb_page(struct vm_area_struct *vma, long vmaddr)
{
  MMU_invalidate_page(vma->vm_mm,vmaddr);
}


/* for each page addr in the range, call mmu_invalidat_page()
   if the range is very large and the hash table is small it might be faster to
   do a search of the hash table and just invalidate pages that are in the range
   but that's for study later.
        -- Cort
   */
inline void
flush_tlb_range(struct mm_struct *mm, long start, long end)
{
  long i;
  for ( i = PAGE_ALIGN(start-PAGE_SIZE) ; i < PAGE_ALIGN(end) ; i += PAGE_SIZE)
  {
    MMU_invalidate_page(mm,i);
  }
}

inline void
flush_page_to_ram(unsigned long page)
{
}
