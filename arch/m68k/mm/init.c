/*
 *  linux/arch/m68k/mm/init.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif

#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/bootinfo.h>
#include <asm/machdep.h>

extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);
extern unsigned long mm_phys_to_virt (unsigned long addr);
extern char *rd_start;
extern int rd_doload;

unsigned long ramdisk_length;

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
static unsigned long empty_bad_page_table;

pte_t *__bad_pagetable(void)
{
    memset((void *)empty_bad_page_table, 0, PAGE_SIZE);
    return (pte_t *)empty_bad_page_table;
}

static unsigned long empty_bad_page;

pte_t __bad_page(void)
{
    memset ((void *)empty_bad_page, 0, PAGE_SIZE);
    return pte_mkdirty(mk_pte(empty_bad_page, PAGE_SHARED));
}

unsigned long empty_zero_page;

void show_mem(void)
{
    unsigned long i;
    int free = 0, total = 0, reserved = 0, nonshared = 0, shared = 0;

    printk("\nMem-info:\n");
    show_free_areas();
    printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
    i = high_memory >> PAGE_SHIFT;
    while (i-- > 0) {
	total++;
	if (PageReserved(mem_map+i))
	    reserved++;
	else if (!mem_map[i].count)
	    free++;
	else if (mem_map[i].count == 1)
	    nonshared++;
	else
	    shared += mem_map[i].count-1;
    }
    printk("%d pages of RAM\n",total);
    printk("%d free pages\n",free);
    printk("%d reserved pages\n",reserved);
    printk("%d pages nonshared\n",nonshared);
    printk("%d pages shared\n",shared);
    show_buffers();
#ifdef CONFIG_NET
    show_net_buffers();
#endif
}

#if 0 /* The 68030 doesn't care about reserved bits. */
/*
 * Bits to add to page descriptors for "normal" caching mode.
 * For 68020/030 this is 0.
 * For 68040, this is _PAGE_CACHE040 (cachable, copyback)
 */
unsigned long mm_cachebits;
#endif

pte_t *kernel_page_table (unsigned long *memavailp)
{
	pte_t *ptablep;

	ptablep = (pte_t *)*memavailp;
	*memavailp += PAGE_SIZE;

	nocache_page ((unsigned long)ptablep);

	return ptablep;
}

static unsigned long map_chunk (unsigned long addr,
				unsigned long size,
				unsigned long *memavailp)
{
#define ONEMEG	(1024*1024)
#define L3TREESIZE (256*1024)

	int is040 = m68k_is040or060;
	static unsigned long mem_mapped = 0;
	static unsigned long virtaddr = 0;
	static pte_t *ktablep = NULL;
	unsigned long *kpointerp;
	unsigned long physaddr;
	extern pte_t *kpt;
	int pindex;   /* index into pointer table */
	pgd_t *page_dir = pgd_offset_k (virtaddr);

	if (!pgd_present (*page_dir)) {
		/* we need a new pointer table */
		kpointerp = (unsigned long *) get_kpointer_table ();
		pgd_set (page_dir, (pmd_t *) kpointerp);
		memset (kpointerp, 0, PTRS_PER_PMD * sizeof (pmd_t));
	}
	else
		kpointerp = (unsigned long *) pgd_page (*page_dir);

	/*
	 * pindex is the offset into the pointer table for the
	 * descriptors for the current virtual address being mapped.
	 */
	pindex = (virtaddr >> 18) & 0x7f;

#ifdef DEBUG
	printk ("mm=%ld, kernel_pg_dir=%p, kpointerp=%p, pindex=%d\n",
		mem_mapped, kernel_pg_dir, kpointerp, pindex);
#endif

	/*
	 * if this is running on an '040, we already allocated a page
	 * table for the first 4M.  The address is stored in kpt by
	 * arch/head.S
	 *
	 */
	if (is040 && mem_mapped == 0)
		ktablep = kpt;

	for (physaddr = addr;
	     physaddr < addr + size;
	     mem_mapped += L3TREESIZE, virtaddr += L3TREESIZE) {

#ifdef DEBUG
		printk ("pa=%#lx va=%#lx ", physaddr, virtaddr);
#endif

		if (pindex > 127 && mem_mapped >= 32*ONEMEG) {
			/* we need a new pointer table every 32M */
#ifdef DEBUG
			printk ("[new pointer]");
#endif

			kpointerp = (unsigned long *)get_kpointer_table ();
			pgd_set(pgd_offset_k(virtaddr), (pmd_t *)kpointerp);
			pindex = 0;
		}

		if (is040) {
			int i;
			unsigned long ktable;

			/* Don't map the first 4 MB again. The pagetables
			 * for this range have already been initialized
			 * in boot/head.S. Otherwise the pages used for
			 * tables would be reinitialized to copyback mode.
			 */

			if (mem_mapped < 4 * ONEMEG)
			{
#ifdef DEBUG
				printk ("Already initialized\n");
#endif
				physaddr += L3TREESIZE;
				pindex++;
				continue;
			}
#ifdef DEBUG
			printk ("[setup table]");
#endif

			/*
			 * 68040, use page tables pointed to by the
			 * kernel pointer table.
			 */

			if ((pindex & 15) == 0) {
				/* Need new page table every 4M on the '040 */
#ifdef DEBUG
				printk ("[new table]");
#endif
				ktablep = kernel_page_table (memavailp);
			}

			ktable = VTOP(ktablep);

			/*
			 * initialize section of the page table mapping
			 * this 256K portion.
			 */
			for (i = 0; i < 64; i++) {
				pte_val(ktablep[i]) = physaddr | _PAGE_PRESENT
					| _PAGE_CACHE040 | _PAGE_GLOBAL040;
				physaddr += PAGE_SIZE;
			}
			ktablep += 64;

			/*
			 * make the kernel pointer table point to the
			 * kernel page table.  Each entries point to a
			 * 64 entry section of the page table.
			 */

			kpointerp[pindex++] = ktable | _PAGE_TABLE;
		} else {
			/*
			 * 68030, use early termination page descriptors.
			 * Each one points to 64 pages (256K).
			 */
#ifdef DEBUG
			printk ("[early term] ");
#endif
			if (virtaddr == 0UL) {
				/* map the first 256K using a 64 entry
				 * 3rd level page table.
				 * UNMAP the first entry to trap
				 * zero page (NULL pointer) references
				 */
				int i;
				unsigned long *tbl;
				
				tbl = (unsigned long *)get_kpointer_table();

				kpointerp[pindex++] = VTOP(tbl) | _PAGE_TABLE;

				for (i = 0; i < 64; i++, physaddr += PAGE_SIZE)
					tbl[i] = physaddr | _PAGE_PRESENT;
				
				/* unmap the zero page */
				tbl[0] = 0;
			} else {
				/* not the first 256K */
				kpointerp[pindex++] = physaddr | _PAGE_PRESENT;
#ifdef DEBUG
				printk ("%lx=%lx ", VTOP(&kpointerp[pindex-1]),
					kpointerp[pindex-1]);
#endif
				physaddr += 64 * PAGE_SIZE;
			}
		}
#ifdef DEBUG
		printk ("\n");
#endif
	}

	return mem_mapped;
}

extern unsigned long free_area_init(unsigned long, unsigned long);

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

/*
 * paging_init() continues the virtual memory environment setup which
 * was begun by the code in arch/head.S.
 * The parameters are pointers to where to stick the starting and ending
 * addresses  of available kernel virtual memory.
 */
unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	int chunk;
	unsigned long mem_avail = 0;
	/* pointer to page table for kernel stacks */
	extern unsigned long availmem;

#ifdef DEBUG
	{
		extern pte_t *kpt;
		printk ("start of paging_init (%p, %p, %lx, %lx, %lx)\n",
			kernel_pg_dir, kpt, availmem, start_mem, end_mem);
	}
#endif

#if 0
	/*
	 * Setup cache bits
	 */
	mm_cachebits = m68k_is040or060 ? _PAGE_CACHE040 : 0;

	/* Initialize protection map.  */
	protection_map[0] = PAGE_READONLY;
	protection_map[1] = PAGE_READONLY;
	protection_map[2] = PAGE_COPY;
	protection_map[3] = PAGE_COPY;
	protection_map[4] = PAGE_READONLY;
	protection_map[5] = PAGE_READONLY;
	protection_map[6] = PAGE_COPY;
	protection_map[7] = PAGE_COPY;
	protection_map[8] = PAGE_READONLY;
	protection_map[9] = PAGE_READONLY;
	protection_map[10] = PAGE_SHARED;
	protection_map[11] = PAGE_SHARED;
	protection_map[12] = PAGE_READONLY;
	protection_map[13] = PAGE_READONLY;
	protection_map[14] = PAGE_SHARED;
	protection_map[15] = PAGE_SHARED;
#endif

	/*
	 * Map the physical memory available into the kernel virtual
	 * address space.  It may allocate some memory for page
	 * tables and thus modify availmem.
	 */

	for (chunk = 0; chunk < boot_info.num_memory; chunk++) {
		mem_avail = map_chunk (boot_info.memory[chunk].addr,
				       boot_info.memory[chunk].size,
				       &availmem);

	}
	flush_tlb_all();
#ifdef DEBUG
	printk ("memory available is %ldKB\n", mem_avail >> 10);
#endif

	/*
	 * virtual address after end of kernel
	 * "availmem" is setup by the code in head.S.
	 */
	start_mem = availmem;

#ifdef DEBUG
	printk ("start_mem is %#lx\nvirtual_end is %#lx\n",
		start_mem, end_mem);
#endif

	/*
	 * initialize the bad page table and bad page to point
	 * to a couple of allocated pages
	 */
	empty_bad_page_table = start_mem;
	start_mem += PAGE_SIZE;
	empty_bad_page = start_mem;
	start_mem += PAGE_SIZE;
	empty_zero_page = start_mem;
	start_mem += PAGE_SIZE;
	memset((void *)empty_zero_page, 0, PAGE_SIZE);

#if 0
	/* 
	 * allocate the "swapper" page directory and
	 * record in task 0 (swapper) tss 
	 */
	swapper_pg_dir = (pgd_t *)get_kpointer_table();

	init_mm.pgd = swapper_pg_dir;
#endif

	memset (swapper_pg_dir, 0, sizeof(pgd_t)*PTRS_PER_PGD);
	task[0]->tss.pagedir_v = (unsigned long *)swapper_pg_dir;
	task[0]->tss.pagedir_p = VTOP (swapper_pg_dir);

#ifdef DEBUG
	printk ("task 0 pagedir at %p virt, %#lx phys\n",
		task[0]->tss.pagedir_v, task[0]->tss.pagedir_p);
#endif

	/* setup CPU root pointer for swapper task */
	task[0]->tss.crp[0] = 0x80000000 | _PAGE_SHORT;
	task[0]->tss.crp[1] = task[0]->tss.pagedir_p;

	if (m68k_is040or060)
		asm ("movel %0,%/d0\n\t"
		     ".long 0x4e7b0806" /* movec d0,urp */
		     : /* no outputs */
		     : "g" (task[0]->tss.crp[1])
		     : "d0");
	else
		asm ("pmove %0@,%/crp"
		     : /* no outputs */
		     : "a" (task[0]->tss.crp));

#ifdef DEBUG
	printk ("set crp\n");
#endif

	/*
	 * Set up SFC/DFC registers (user data space)
	 */
	set_fs (USER_DS);

#ifdef DEBUG
	printk ("before free_area_init\n");
#endif

#ifndef CONFIG_BLK_DEV_INITRD
	/*
	 * Since the initialization of the ramdisk's has been changed
	 * so it fits the new driver initialization scheme, we have to
	 * make room for our preloaded image here, instead of doing it
	 * in rd_init() as we cannot kmalloc() a block large enough
	 * for the image.
	 */

	ramdisk_length = boot_info.ramdisk_size * 1024;

	if ((ramdisk_length > 0) && (ROOT_DEV == 0)) {
	  char *rdp;	     /* current location of ramdisk */

	  rd_start = (char *) start_mem;

	  /* get current address of ramdisk */
	  rdp = (char *)mm_phys_to_virt (boot_info.ramdisk_addr);

	  /* copy the ram disk image */
	  memcpy (rd_start, rdp, ramdisk_length);
	  start_mem += ramdisk_length;
	  rd_doload = 1;     /* tell rd_load to load this thing */
	}
#endif

	return free_area_init (start_mem, end_mem);
}

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
	int codepages = 0;
	int datapages = 0;
	unsigned long tmp;
	extern int _etext;

	end_mem &= PAGE_MASK;
	high_memory = end_mem;

	start_mem = PAGE_ALIGN(start_mem);
	while (start_mem < high_memory) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(start_mem)].flags);
		start_mem += PAGE_SIZE;
	}

#ifdef CONFIG_ATARI

	if (MACH_IS_ATARI) {

		/* If the page with physical address 0 isn't the first kernel
		 * code page, it has to be reserved because the first 2 KB of
		 * ST-Ram can only be accessed from supervisor mode by
		 * hardware.
		 */

		unsigned long virt0 = PTOV( 0 ), adr;
		extern unsigned long rsvd_stram_beg, rsvd_stram_end;
		
		if (virt0 != 0) {

			set_bit(PG_reserved, &mem_map[MAP_NR(virt0)].flags);

			/* Also, reserve all pages that have been marked by
			 * stram_alloc() (e.g. for the screen memory). (This may
			 * treat the first ST-Ram page a second time, but that
			 * doesn't hurt...) */
			
			rsvd_stram_end += PAGE_SIZE - 1;
			rsvd_stram_end &= PAGE_MASK;
			rsvd_stram_beg &= PAGE_MASK;
			for( adr = rsvd_stram_beg; adr < rsvd_stram_end; adr += PAGE_SIZE )
				set_bit(PG_reserved, &mem_map[MAP_NR(adr)].flags);
		}
	}
	
#endif
#ifdef DEBUG
	printk ("task[0] root table is %p\n", task[0]->tss.pagedir_v);
#endif

	for (tmp = 0 ; tmp < end_mem ; tmp += PAGE_SIZE) {
		if (VTOP (tmp) >= mach_max_dma_address)
			clear_bit(PG_DMA, &mem_map[MAP_NR(tmp)].flags);
		if (PageReserved(mem_map+MAP_NR(tmp))) {
			if (tmp < (unsigned long)&_etext)
				codepages++;
			else
				datapages++;
			continue;
		}
		mem_map[MAP_NR(tmp)].count = 1;
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start || (tmp < initrd_start || tmp >= initrd_end))
#endif
			free_page(tmp);
	}
	tmp = nr_free_pages << PAGE_SHIFT;
	printk("Memory: %luk/%luk available (%dk kernel code, %dk data)\n",
	       tmp >> 10,
	       high_memory >> 10,
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10));
}

void si_meminfo(struct sysinfo *val)
{
    unsigned long i;

    i = high_memory >> PAGE_SHIFT;
    val->totalram = 0;
    val->sharedram = 0;
    val->freeram = nr_free_pages << PAGE_SHIFT;
    val->bufferram = buffermem;
    while (i-- > 0) {
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
