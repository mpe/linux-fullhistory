/*
 *  linux/arch/m68k/mm/kmap.c
 *
 *  Copyright (C) 1997 Roman Hodek
 */

#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/malloc.h>

#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>


extern pte_t *kernel_page_table (unsigned long *memavailp);

/* Granularity of kernel_map() allocations */
#define KMAP_STEP	(256*1024)

/* Size of pool of KMAP structures; that is needed, because kernel_map() can
 * be called at times where kmalloc() isn't initialized yet. */
#define	KMAP_POOL_SIZE	16

/* structure for maintainance of kmap regions */
typedef struct kmap {
	struct kmap *next, *prev;	/* linking of list */
	unsigned long addr;			/* start address of region */
	unsigned long mapaddr;		/* address returned to user */
	unsigned long size;			/* size of region */
	unsigned free : 1;			/* flag whether free or allocated */
	unsigned kmalloced : 1;		/* flag whether got this from kmalloc() */
	unsigned pool_alloc : 1;	/* flag whether got this is alloced in pool */
} KMAP;

KMAP kmap_pool[KMAP_POOL_SIZE] = {
	{ NULL, NULL, KMAP_START, KMAP_START, KMAP_END-KMAP_START, 1, 0, 1 },
	{ NULL, NULL, 0, 0, 0, 0, 0, 0 },
};

/*
 * anchor of kmap region list
 *
 * The list is always ordered by addresses, and regions are always adjacent,
 * i.e. there must be no holes between them!
 */
KMAP *kmap_regions = &kmap_pool[0];

/* for protecting the kmap_regions list against races */
static struct semaphore kmap_sem = MUTEX;



/*
 * Low-level allocation and freeing of KMAP structures
 */
static KMAP *alloc_kmap( int use_kmalloc )
{
	KMAP *p;
	int i;

	/* first try to get from the pool if possible */
	for( i = 0; i < KMAP_POOL_SIZE; ++i ) {
		if (!kmap_pool[i].pool_alloc) {
			kmap_pool[i].kmalloced = 0;
			kmap_pool[i].pool_alloc = 1;
			return( &kmap_pool[i] );
		}
	}
	
	if (use_kmalloc && (p = (KMAP *)kmalloc( sizeof(KMAP), GFP_KERNEL ))) {
		p->kmalloced = 1;
		return( p );
	}
	
	return( NULL );
}

static void free_kmap( KMAP *p )
{
	if (p->kmalloced)
		kfree( p );
	else
		p->pool_alloc = 0;
}


/*
 * Get a free region from the kmap address range
 */
static KMAP *kmap_get_region( unsigned long size, int use_kmalloc )
{
	KMAP *p, *q;

	/* look for a suitable free region */
	for( p = kmap_regions; p; p = p->next )
		if (p->free && p->size >= size)
			break;
	if (!p) {
		printk( KERN_ERR "kernel_map: address space for "
				"allocations exhausted\n" );
		return( NULL );
	}
	
	if (p->size > size) {
		/* if free region is bigger than we need, split off the rear free part
		 * into a new region */
		if (!(q = alloc_kmap( use_kmalloc ))) {
			printk( KERN_ERR "kernel_map: out of memory\n" );
			return( NULL );
		}
		q->addr = p->addr + size;
		q->size = p->size - size;
		p->size = size;
		q->free = 1;

		q->prev = p;
		q->next = p->next;
		p->next = q;
		if (q->next) q->next->prev = q;
	}
	
	p->free = 0;
	return( p );
}


/*
 * Free a kernel_map region again
 */
static void kmap_put_region( KMAP *p )
{
	KMAP *q;

	p->free = 1;

	/* merge with previous region if possible */
	q = p->prev;
	if (q && q->free) {
		if (q->addr + q->size != p->addr) {
			printk( KERN_ERR "kernel_malloc: allocation list destroyed\n" );
			return;
		}
		q->size += p->size;
		q->next = p->next;
		if (p->next) p->next->prev = q;
		free_kmap( p );
		p = q;
	}

	/* merge with following region if possible */
	q = p->next;
	if (q && q->free) {
		if (p->addr + p->size != q->addr) {
			printk( KERN_ERR "kernel_malloc: allocation list destroyed\n" );
			return;
		}
		p->size += q->size;
		p->next = q->next;
		if (q->next) q->next->prev = p;
		free_kmap( q );
	}
}


/*
 * kernel_map() helpers
 */
static inline pte_t *
pte_alloc_kernel_map(pmd_t *pmd, unsigned long address,
		     unsigned long *memavailp)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t *page = kernel_page_table(memavailp);
		if (pmd_none(*pmd)) {
			if (page) {
				pmd_set(pmd, page);
				memset( page, 0, PAGE_SIZE );
				return page + address;
			}
			pmd_set(pmd, BAD_PAGETABLE);
			return NULL;
		}
		if (memavailp)
			panic("kernel_map: slept during init?!?");
		cache_page((unsigned long) page);
		free_page((unsigned long) page);
	}
	if (pmd_bad(*pmd)) {
		printk( KERN_ERR "Bad pmd in pte_alloc_kernel_map: %08lx\n",
		       pmd_val(*pmd));
		pmd_set(pmd, BAD_PAGETABLE);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

static inline void
kernel_map_pte(pte_t *pte, unsigned long address, unsigned long size,
	       unsigned long phys_addr, pgprot_t prot)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_val(*pte) = phys_addr + pgprot_val(prot);
		address += PAGE_SIZE;
		phys_addr += PAGE_SIZE;
		pte++;
	} while (address < end);
}

static inline int
kernel_map_pmd (pmd_t *pmd, unsigned long address, unsigned long size,
		unsigned long phys_addr, pgprot_t prot,
		unsigned long *memavailp)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	phys_addr -= address;

	if (CPU_IS_040_OR_060) {
		do {
			pte_t *pte = pte_alloc_kernel_map(pmd, address, memavailp);
			if (!pte)
				return -ENOMEM;
			kernel_map_pte(pte, address, end - address,
				       address + phys_addr, prot);
			address = (address + PMD_SIZE) & PMD_MASK;
			pmd++;
		} while (address < end);
	} else {
		/* On the 68030 we use early termination page descriptors.
		   Each one points to 64 pages (256K). */
		int i = (address >> (PMD_SHIFT-4)) & 15;
		do {
			(&pmd_val(*pmd))[i++] = (address + phys_addr) | pgprot_val(prot);
			address += PMD_SIZE / 16;
		} while (address < end);
	}
	return 0;
}


/*
 * Map some physical address range into the kernel address space. The
 * code is copied and adapted from map_chunk().
 */
/* Rewritten by Andreas Schwab to remove all races. */

unsigned long kernel_map(unsigned long phys_addr, unsigned long size,
			 int cacheflag, unsigned long *memavailp)
{
	unsigned long retaddr, from, end;
	pgd_t *dir;
	pgprot_t prot;
	KMAP *kmap;

	/* Round down 'phys_addr' to 256 KB and adjust size */
	retaddr = phys_addr & (KMAP_STEP-1);
	size += retaddr;
	phys_addr &= ~(KMAP_STEP-1);
	/* Round up the size to 256 KB. It doesn't hurt if too much is
	   mapped... */
	size = (size + KMAP_STEP - 1) & ~(KMAP_STEP-1);
	
	down( &kmap_sem );
	if (!(kmap = kmap_get_region( size, memavailp == NULL )))
		return( 0 );
	from = kmap->addr;
	retaddr += from;
	kmap->mapaddr = retaddr;
	end = from + size;
	up( &kmap_sem );

	if (CPU_IS_040_OR_060) {
		pgprot_val(prot) = (_PAGE_PRESENT | _PAGE_GLOBAL040 |
				    _PAGE_ACCESSED | _PAGE_DIRTY);
		switch (cacheflag) {
		case KERNELMAP_FULL_CACHING:
			pgprot_val(prot) |= _PAGE_CACHE040;
			break;
		case KERNELMAP_NOCACHE_SER:
		default:
			pgprot_val(prot) |= _PAGE_NOCACHE_S;
			break;
		case KERNELMAP_NOCACHE_NONSER:
			pgprot_val(prot) |= _PAGE_NOCACHE;
			break;
		case KERNELMAP_NO_COPYBACK:
			pgprot_val(prot) |= _PAGE_CACHE040W;
			break;
		}
	} else
		pgprot_val(prot) = (_PAGE_PRESENT | _PAGE_ACCESSED |
				    _PAGE_DIRTY |
				    ((cacheflag == KERNELMAP_FULL_CACHING ||
				      cacheflag == KERNELMAP_NO_COPYBACK)
				     ? 0 : _PAGE_NOCACHE030));

	phys_addr -= from;
	dir = pgd_offset_k(from);
	while (from < end) {
		pmd_t *pmd = pmd_alloc_kernel(dir, from);

		if (kernel_map_pmd(pmd, from, end - from, phys_addr + from,
				   prot, memavailp)) {
			printk( KERN_ERR "kernel_map: out of memory\n" );
			return 0UL;
		}
		from = (from + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}

	return retaddr;
}


/*
 * kernel_unmap() helpers
 */
static inline void pte_free_kernel_unmap( pmd_t *pmd )
{
	unsigned long page = pmd_page(*pmd);
	mem_map_t *pagemap = &mem_map[MAP_NR(page)];
	
	pmd_clear(pmd);
	cache_page(page);

	if (PageReserved( pagemap )) {
		/* need to unreserve pages that were allocated with memavailp != NULL;
		 * this works only if 'page' is page-aligned */
		if (page & ~PAGE_MASK)
			return;
		clear_bit( PG_reserved, &pagemap->flags );
		atomic_set( &pagemap->count, 1 );
	}
	free_page( page );
}

/*
 * This not only unmaps the requested region, but also loops over the whole
 * pmd to determine whether the other pte's are clear (so that the page can be
 * freed.) If so, it returns 1, 0 otherwise.
 */
static inline int
kernel_unmap_pte_range(pmd_t * pmd, unsigned long address, unsigned long size)
{
	pte_t *pte;
	unsigned long addr2, end, end2;
	int all_clear = 1;

	if (pmd_none(*pmd))
		return( 0 );
	if (pmd_bad(*pmd)) {
		printk( KERN_ERR "kernel_unmap_pte_range: bad pmd (%08lx)\n",
				pmd_val(*pmd) );
		pmd_clear(pmd);
		return( 0 );
	}
	address &= ~PMD_MASK;
	addr2 = 0;
	pte = pte_offset(pmd, addr2);
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	end2 = addr2 + PMD_SIZE;
	while( addr2 < end2 ) {
		if (!pte_none(*pte)) {
			if (address <= addr2 && addr2 < end)
				pte_clear(pte);
			else
				all_clear = 0;
		}
		++pte;
		addr2 += PAGE_SIZE;
	}
	return( all_clear );
}

static inline void
kernel_unmap_pmd_range(pgd_t * dir, unsigned long address, unsigned long size)
{
	pmd_t * pmd;
	unsigned long end;

	if (pgd_none(*dir))
		return;
	if (pgd_bad(*dir)) {
		printk( KERN_ERR "kernel_unmap_pmd_range: bad pgd (%08lx)\n",
				pgd_val(*dir) );
		pgd_clear(dir);
		return;
	}
	pmd = pmd_offset(dir, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	
	if (CPU_IS_040_OR_060) {
		do {
			if (kernel_unmap_pte_range(pmd, address, end - address))
				pte_free_kernel_unmap( pmd );
			address = (address + PMD_SIZE) & PMD_MASK;
			pmd++;
		} while (address < end);
	} else {
		/* On the 68030 clear the early termination descriptors */
		int i = (address >> (PMD_SHIFT-4)) & 15;
		do {
			(&pmd_val(*pmd))[i++] = 0;
			address += PMD_SIZE / 16;
		} while (address < end);
	}
}

/*
 * Unmap a kernel_map()ed region again
 */
void kernel_unmap( unsigned long addr )
{
	unsigned long end;
	pgd_t *dir;
	KMAP *p;

	down( &kmap_sem );
	
	/* find region for 'addr' in list; must search for mapaddr! */
	for( p = kmap_regions; p; p = p->next )
		if (!p->free && p->mapaddr == addr)
			break;
	if (!p) {
		printk( KERN_ERR "kernel_unmap: trying to free invalid region\n" );
		return;
	}
	addr = p->addr;
	end = addr + p->size;
	kmap_put_region( p );

	dir = pgd_offset_k( addr );
	while( addr < end ) {
		kernel_unmap_pmd_range( dir, addr, end - addr );
		addr = (addr + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	
	up( &kmap_sem );
	/* flushing for a range would do, but there's no such function for kernel
	 * address space... */
	flush_tlb_all();
}


/*
 * kernel_set_cachemode() helpers
 */
static inline void set_cmode_pte( pmd_t *pmd, unsigned long address,
				  unsigned long size, unsigned cmode )
{	pte_t *pte;
	unsigned long end;

	if (pmd_none(*pmd))
		return;

	pte = pte_offset( pmd, address );
	address &= ~PMD_MASK;
	end = address + size;
	if (end >= PMD_SIZE)
		end = PMD_SIZE;

	for( ; address < end; pte++ ) {
		pte_val(*pte) = (pte_val(*pte) & ~_PAGE_NOCACHE) | cmode;
		address += PAGE_SIZE;
	}
}


static inline void set_cmode_pmd( pgd_t *dir, unsigned long address,
				  unsigned long size, unsigned cmode )
{
	pmd_t *pmd;
	unsigned long end;

	if (pgd_none(*dir))
		return;

	pmd = pmd_offset( dir, address );
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;

	if ((pmd_val(*pmd) & _DESCTYPE_MASK) == _PAGE_PRESENT) {
		/* 68030 early termination descriptor */
		pmd_val(*pmd) = (pmd_val(*pmd) & ~_PAGE_NOCACHE) | cmode;
		return;
	}
	else {
		/* "normal" tables */
		for( ; address < end; pmd++ ) {
			set_cmode_pte( pmd, address, end - address, cmode );
			address = (address + PMD_SIZE) & PMD_MASK;
		}
	}
}


/*
 * Set new cache mode for some kernel address space.
 * The caller must push data for that range itself, if such data may already
 * be in the cache.
 */
void kernel_set_cachemode( unsigned long address, unsigned long size,
						   unsigned cmode )
{
	pgd_t *dir = pgd_offset_k( address );
	unsigned long end = address + size;
	
	if (CPU_IS_040_OR_060) {
		switch( cmode ) {
		  case KERNELMAP_FULL_CACHING:
			cmode = _PAGE_CACHE040;
			break;
		  case KERNELMAP_NOCACHE_SER:
		  default:
			cmode = _PAGE_NOCACHE_S;
			break;
		  case KERNELMAP_NOCACHE_NONSER:
			cmode = _PAGE_NOCACHE;
			break;
		  case KERNELMAP_NO_COPYBACK:
			cmode = _PAGE_CACHE040W;
			break;
		}
	} else
		cmode = ((cmode == KERNELMAP_FULL_CACHING ||
				  cmode == KERNELMAP_NO_COPYBACK)    ?
			 0 : _PAGE_NOCACHE030);

	for( ; address < end; dir++ ) {
		set_cmode_pmd( dir, address, end - address, cmode );
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
	}
	/* flushing for a range would do, but there's no such function for kernel
	 * address space... */
	flush_tlb_all();
}
