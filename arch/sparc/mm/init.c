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
#include <asm/vaddrs.h>

extern void scsi_mem_init(unsigned long);
extern void sound_mem_init(void);
extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);

struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS];

/* The following number keeps track of which page table is to
 * next be allocated in a page.  This is necessary since there
 * are 16 page tables per page on the space.
 */
unsigned long ptr_in_current_pgd;

/* This keeps track of which physical segments are in use right now. */
unsigned int phys_seg_map[PSEG_ENTRIES];
unsigned int phys_seg_life[PSEG_ENTRIES];

/* Context allocation. */
struct task_struct *ctx_tasks[MAX_CTXS];
int ctx_tasks_last_frd;

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
	return (unsigned long) ZERO_PGE;
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
extern pgprot_t protection_map[16];

/*
 * paging_init() sets up the page tables: We call the MMU specific
 * init routine based upon the Sun model type on the Sparc.
 *
 */
extern unsigned long sun4c_paging_init(unsigned long, unsigned long);
extern unsigned long srmmu_paging_init(unsigned long, unsigned long);
extern unsigned long probe_devices(unsigned long);

unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	int i;

	switch(sparc_cpu_model) {
	case sun4c:
		start_mem = sun4c_paging_init(start_mem, end_mem);
		break;
	case sun4m:
	case sun4d:
	case sun4e:
		start_mem = srmmu_paging_init(start_mem, end_mem);
		break;
	default:
		printk("paging_init: Cannot init paging on this Sparc\n");
		printk("paging_init: sparc_cpu_model = %d\n", sparc_cpu_model);
		printk("paging_init: Halting...\n");
		halt();
	};

	/* Initialize context map. */
	for(i=0; i<MAX_CTXS; i++) ctx_tasks[i] = NULL;

	/* Initialize the protection map */
	protection_map[0] = __P000;
	protection_map[1] = __P001;
	protection_map[2] = __P010;
	protection_map[3] = __P011;
	protection_map[4] = __P100;
	protection_map[5] = __P101;
	protection_map[6] = __P110;
	protection_map[7] = __P111;
	protection_map[8] = __S000;
	protection_map[9] = __S001;
	protection_map[10] = __S010;
	protection_map[11] = __S011;
	protection_map[12] = __S100;
	protection_map[13] = __S101;
	protection_map[14] = __S110;
	protection_map[15] = __S111;

	start_mem = probe_devices(start_mem);

	return start_mem;
}

extern unsigned long sun4c_test_wp(unsigned long);
extern void srmmu_test_wp(void);

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
	int codepages = 0;
	int reservedpages = 0;
	int datapages = 0;
	unsigned long tmp2, addr;
	extern char etext;

	end_mem &= PAGE_MASK;
	high_memory = end_mem;

	start_mem = PAGE_ALIGN(start_mem);

	addr = PAGE_OFFSET;
	while(addr < start_mem) {
		mem_map[MAP_NR(addr)] = MAP_PAGE_RESERVED;
		addr += PAGE_SIZE;
	}

	for(addr = start_mem; addr < end_mem; addr += PAGE_SIZE)
			mem_map[MAP_NR(addr)] = 0;

#ifdef CONFIG_SCSI
	scsi_mem_init(high_memory);
#endif

	for (addr = PAGE_OFFSET; addr < end_mem; addr += PAGE_SIZE) {
		if(mem_map[MAP_NR(addr)]) {
			if (addr < (unsigned long) &etext)
				codepages++;
			else if(addr < start_mem)
				datapages++;
			else
				reservedpages++;
			continue;
		}
		mem_map[MAP_NR(addr)] = 1;
		free_page(addr);
	}

	tmp2 = nr_free_pages << PAGE_SHIFT;

	printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data)\n",
	       tmp2 >> 10,
	       (high_memory - PAGE_OFFSET) >> 10,
	       codepages << (PAGE_SHIFT-10),
	       reservedpages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10));

/* Heh, test write protection just like the i386, this is bogus but it is
 * fun to do ;)
 */
	switch(sparc_cpu_model) {
	case sun4c:
		start_mem = sun4c_test_wp(start_mem);
		break;
	case sun4m:
	case sun4d:
	case sun4e:
		srmmu_test_wp();
		break;
	default:
		printk("mem_init: Could not test WP bit on this machine.\n");
		printk("mem_init: sparc_cpu_model = %d\n", sparc_cpu_model);
		printk("mem_init: Halting...\n");
		halt();
	};

#ifdef DEBUG_MEMINIT
	printk("Breaker breaker...Roger roger.... Over and out...\n");
#endif
	invalidate();

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
