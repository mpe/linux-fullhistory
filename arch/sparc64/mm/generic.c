/* $Id: generic.c,v 1.3 1998/10/27 23:28:07 davem Exp $
 * generic.c: Generic Sparc mm routines that are not dependent upon
 *            MMU type but are Sparc specific.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/swap.h>

#include <asm/pgtable.h>
#include <asm/page.h>


/* Allocate a block of RAM which is aligned to its size.
 * This procedure can be used until the call to mem_init().
 */
void *sparc_init_alloc(unsigned long *kbrk, unsigned long size)
{
        unsigned long mask = size - 1;
        unsigned long ret;

        if(!size)
                return 0x0;
        if(size & mask) {
                prom_printf("panic: sparc_init_alloc botch\n");
                prom_halt();
        }
        ret = (*kbrk + mask) & ~mask;
        *kbrk = ret + size;
        memset((void*) ret, 0, size);
        return (void*) ret;
}

static inline void forget_pte(pte_t page)
{
	if (pte_none(page))
		return;
	if (pte_present(page)) {
		unsigned long addr = pte_page(page);
		if (MAP_NR(addr) >= max_mapnr || PageReserved(mem_map+MAP_NR(addr)))
			return;
		/* 
		 * free_page() used to be able to clear swap cache
		 * entries.  We may now have to do it manually.  
		 */
		free_page_and_swap_cache(addr);
		return;
	}
	swap_free(pte_val(page));
}

/* Remap IO memory, the same way as remap_page_range(), but use
 * the obio memory space.
 *
 * They use a pgprot that sets PAGE_IO and does not check the
 * mem_map table as this is independent of normal memory.
 */
static inline void io_remap_pte_range(pte_t * pte, unsigned long address, unsigned long size,
	unsigned long offset, pgprot_t prot, int space)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		pte_t oldpage;
		pte_t entry;
		unsigned long curend = address + PAGE_SIZE;
		
		entry = mk_pte_io(offset, prot, space);
		offset += PAGE_SIZE;
		if (!(address & 0xffff)) {
			if (!(address & 0x3fffff) && !(offset & 0x3fffff) && end >= address + 0x400000) {
				entry = mk_pte_io(offset, __pgprot(pgprot_val (prot) | _PAGE_SZ4MB), space);
				curend = address + 0x400000;
				offset += 0x400000 - PAGE_SIZE;
			} else if (!(address & 0x7ffff) && !(offset & 0x7ffff) && end >= address + 0x80000) {
				entry = mk_pte_io(offset, __pgprot(pgprot_val (prot) | _PAGE_SZ512K), space);
				curend = address + 0x80000;
				offset += 0x80000 - PAGE_SIZE;
			} else if (!(offset & 0xffff) && end >= address + 0x10000) {
				entry = mk_pte_io(offset, __pgprot(pgprot_val (prot) | _PAGE_SZ64K), space);
				curend = address + 0x10000;
				offset += 0x10000 - PAGE_SIZE;
			}
		}
		do {
			oldpage = *pte;
			pte_clear(pte);
			set_pte(pte, entry);
			forget_pte(oldpage);
			address += PAGE_SIZE;
			pte++;
		} while (address < curend);
	} while (address < end);
}

static inline int io_remap_pmd_range(pmd_t * pmd, unsigned long address, unsigned long size,
	unsigned long offset, pgprot_t prot, int space)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	offset -= address;
	do {
		pte_t * pte = pte_alloc(pmd, address);
		if (!pte)
			return -ENOMEM;
		io_remap_pte_range(pte, address, end - address, address + offset, prot, space);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

int io_remap_page_range(unsigned long from, unsigned long offset, unsigned long size, pgprot_t prot, int space)
{
	int error = 0;
	pgd_t * dir;
	unsigned long beg = from;
	unsigned long end = from + size;

	prot = __pgprot(pg_iobits);
	offset -= from;
	dir = pgd_offset(current->mm, from);
	flush_cache_range(current->mm, beg, end);
	while (from < end) {
		pmd_t *pmd = pmd_alloc(dir, from);
		error = -ENOMEM;
		if (!pmd)
			break;
		error = io_remap_pmd_range(pmd, from, end - from, offset + from, prot, space);
		if (error)
			break;
		from = (from + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	flush_tlb_range(current->mm, beg, end);
	return error;
}
