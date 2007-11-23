/*
 * arch/i386/mm/ioremap.c
 *
 * Re-map IO memory to kernel address space so that we can access it.
 * This is needed for high PCI addresses that aren't mapped in the
 * 640k-1MB IO memory area on PC's
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 */

#include <linux/vmalloc.h>

#include <asm/io.h>

static inline void remap_area_pte(pte_t * pte, unsigned long address, unsigned long size,
	unsigned long phys_addr)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		if (!pte_none(*pte))
			printk("remap_area_pte: page already exists\n");
		set_pte(pte, mk_pte_phys(phys_addr, PAGE_KERNEL));
		address += PAGE_SIZE;
		phys_addr += PAGE_SIZE;
		pte++;
	} while (address < end);
}

static inline int remap_area_pmd(pmd_t * pmd, unsigned long address, unsigned long size,
	unsigned long phys_addr)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	phys_addr -= address;
	do {
		pte_t * pte = pte_alloc_kernel(pmd, address);
		if (!pte)
			return -ENOMEM;
		remap_area_pte(pte, address, end - address, address + phys_addr);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

static int remap_area_pages(unsigned long address, unsigned long phys_addr, unsigned long size)
{
	pgd_t * dir;
	unsigned long end = address + size;

	phys_addr -= address;
	dir = pgd_offset(&init_mm, address);
	flush_cache_all();
	while (address < end) {
		pmd_t *pmd = pmd_alloc_kernel(dir, address);
		if (!pmd)
			return -ENOMEM;
		if (remap_area_pmd(pmd, address, end - address, phys_addr + address))
			return -ENOMEM;
		set_pgdir(address, *dir);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	flush_tlb_all();
	return 0;
}

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 */
void * ioremap(unsigned long phys_addr, unsigned long size)
{
	void * addr;
	struct vm_struct * area;

	if (phys_addr < virt_to_phys(high_memory))
		return phys_to_virt(phys_addr);
	if (phys_addr & ~PAGE_MASK)
		return NULL;
	size = PAGE_ALIGN(size);
	if (!size || size > phys_addr + size)
		return NULL;
	area = get_vm_area(size);
	if (!area)
		return NULL;
	addr = area->addr;
	if (remap_area_pages(VMALLOC_VMADDR(addr), phys_addr, size)) {
		vfree(addr);
		return NULL;
	}
	return addr;
}

void iounmap(void *addr)
{
	if (addr > high_memory)
		return vfree(addr);
}
