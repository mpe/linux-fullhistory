/*
 *  linux/arch/sparc/mm/init.c
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
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

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/vac-ops.h>
#include <asm/page.h>
#include <asm/pgtable.h>

extern void scsi_mem_init(unsigned long);
extern void sound_mem_init(void);
extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);

extern int map_the_prom(int);

extern int invalid_segment, num_segmaps, num_contexts;

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
pte_t *__bad_pagetable(void)
{
	memset((void *) EMPTY_PGT, 0, PAGE_SIZE);
	return (pte_t *) EMPTY_PGT;
}

pte_t __bad_page(void)
{
	memset((void *) EMPTY_PGE, 0, PAGE_SIZE);
	return pte_mkdirty(mk_pte((unsigned long) EMPTY_PGE, PAGE_SHARED));
}

unsigned long __zero_page(void)
{
	memset((void *) ZERO_PGE, 0, PAGE_SIZE);
	return ZERO_PGE;
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
		if (mem_map[i] & MAP_PAGE_RESERVED)
			reserved++;
		else if (!mem_map[i])
			free++;
		else
			shared += mem_map[i]-1;
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
 * paging_init() sets up the page tables: in the alpha version this actually
 * unmaps the bootup page table (as we're now in KSEG, so we don't need it).
 *
 * The bootup sequence put the virtual page table into high memory: that
 * means that we cah change the L1 page table by just using VL1p below.
 */

unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	int pg_segmap = 0;
	unsigned long i, a, b, mask=0;
	register int num_segs, num_ctx;
	register char * c;

	num_segs = num_segmaps;
	num_ctx = num_contexts;

	num_segs -= 1;
	invalid_segment = num_segs;

/* On the sparc we first need to allocate the segmaps for the
 * PROM's virtual space, and make those segmaps unusable. We
 * map the PROM in ALL contexts therefore the break key and the
 * sync command work no matter what state you took the machine
 * out of
 */

	printk("mapping the prom...\n");
	num_segs = map_the_prom(num_segs);

	start_mem = PAGE_ALIGN(start_mem);

	/* ok, allocate the kernel pages, map them in all contexts
	 * (with help from the prom), and lock them. Isn't the sparc
	 * fun kiddies? TODO
	 */

	b=PGDIR_ALIGN(start_mem)>>18;
	c= (char *)0x0;

	printk("mapping kernel in all contexts...\n");

	for(a=0; a<b; a++)
	  {
	    for(i=0; i<num_contexts; i++)
	      {
		/* map the kernel virt_addrs */
		(*(romvec->pv_setctxt))(i, (char *) c, a);
	      }
	    c += 0x40000;
	  }

	/* Ok, since now mapped in all contexts, we can free up
	 * context zero to be used amongst user processes.
	 */

	/* free context 0 here TODO */

	/* invalidate all user pages and initialize the pte struct 
	 * for userland. TODO
	 */

	/* Make the kernel text unwritable and cacheable, the prom
	 * loaded out text as writable, only sneaky sunos kernels need
	 * self-modifying code.
	 */

	a= (unsigned long) etext;
	mask=~(PTE_NC|PTE_W);    /* make cacheable + not writable */

	printk("changing kernel text perms...\n");


	/* must do for every segment since kernel uses all contexts
	 * and unlike some sun kernels I know of, we can't hard wire
	 * context 0 just for the kernel, that is unnecessary.
	 */

	for(i=0; i<8; i++)
	  {
	    b=PAGE_ALIGN((unsigned long) trapbase);

	    switch_to_context(i);

	    for(;b<a; b+=4096)
	      {
		put_pte(b, (get_pte(b) & mask));
	      }
	  }

	invalidate(); /* flush the virtual address cache */

	printk("\nCurrently in context - ");
	for(i=0; i<num_contexts; i++)
	  {
	    switch_to_context(i);
	    printk("%d ", (int) i);
	  }

	switch_to_context(0);

	/* invalidate all user segmaps for virt addrs 0-KERNBASE */

	/* WRONG, now I just let the kernel sit in low addresses only
         * from 0 -- end_kernel just like i386-linux. This will make
         * mem-code a bit easier to cope with.
	 */

	printk("\ninvalidating user segmaps\n");
	for(i = 0; i<8; i++)
	  {
	    switch_to_context(i);
	    a=((unsigned long) &end);
	    for(a+=524288, pg_segmap=0; ++pg_segmap<=3584; a+=(1<<18))
	      put_segmap((unsigned long *) a, (invalid_segment&0x7f));
	  }

	printk("wheee! have I sold out yet?\n");

	invalidate();
	return free_area_init(start_mem, end_mem);
}

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
	return;
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = high_memory >> PAGE_SHIFT;
	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = buffermem;
	while (i-- > 0)  {
		if (mem_map[i] & MAP_PAGE_RESERVED)
			continue;
		val->totalram++;
		if (!mem_map[i])
			continue;
		val->sharedram += mem_map[i]-1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
	return;
}
