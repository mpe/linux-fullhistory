#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/signal.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/openprom.h>
#include <asm/page.h>

extern unsigned long pg0[1024];		/* page table for 0-4MB for everybody */

extern void scsi_mem_init(unsigned long);
extern void sound_mem_init(void);
extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);

/* Sparc stuff... I know this is a ugly place to put the PROM vector, don't
 * remind me.
 */
extern char* trapbase;
extern unsigned int end[], etext[], msgbuf[];
struct linux_romvec *romvec;

/* foo */

int tbase_needs_unmapping;

/* At boot time we determine these two values necessary for setting
 * up the segment maps and page table entries (pte's).
 */

int num_segmaps, num_contexts;
int invalid_segment;

/* various Virtual Address Cache parameters we find at boot time... */

int vac_size, vac_linesize, vac_do_hw_vac_flushes;
int vac_entries_per_context, vac_entries_per_segment;
int vac_entries_per_page;

/*
 * Define this if things work differently on a i386 and a i486:
 * it will (on a i486) warn about kernel memory accesses that are
 * done without a 'verify_area(VERIFY_WRITE,..)'
 */
#undef CONFIG_TEST_VERIFY_AREA

/* Traverse the memory lists in the prom to see how much physical we
 * have.
 */

unsigned long
probe_memory(void)
{
  register struct linux_romvec *lprom;
  register struct linux_mlist_v0 *mlist;
  register unsigned long bytes, base_paddr;
  register int i;

  bytes = 0;
  base_paddr = 0;
  lprom = romvec;
  switch(lprom->pv_romvers)
    {
    case 0:
      mlist=(*(lprom->pv_v0mem.v0_totphys));
      bytes=mlist->num_bytes;
      base_paddr = (unsigned long) mlist->start_adr;
      printk("Bank 1: starting at 0x%x holding %d bytes\n", 
	     (unsigned int) base_paddr, (int) bytes);
      i=1;
      if(mlist->theres_more != (void *)0)
	{
	  i++;
	  mlist=mlist->theres_more;
	  bytes+=mlist->num_bytes;
	  printk("Bank %d: starting at 0x%x holding %d bytes\n", i,
		 (unsigned int) mlist->start_adr, (int) mlist->num_bytes);
	}
      break;
    case 2:
      printk("no v2 memory probe support yet.\n");
      (*(lprom->pv_halt))();
      break;
    }
  printk("Physical memory: %d bytes  starting at va 0x%x\n",
	 (unsigned int) bytes, (int) base_paddr);

  return bytes;
}

/* Sparc routine to reserve the mapping of the open boot prom */

/* uncomment this for FAME and FORTUNE! */
/* #define DEBUG_MAP_PROM */

int
map_the_prom(int curr_num_segs)
{
  register unsigned long prom_va_begin;
  register unsigned long prom_va_end;
  register int segmap_entry, i;

  prom_va_begin = LINUX_OPPROM_BEGVM;
  prom_va_end   = LINUX_OPPROM_ENDVM;

#ifdef DEBUG_MAP_PROM
  printk("\ncurr_num_segs = 0x%x\n", curr_num_segs);
#endif

  while( prom_va_begin < prom_va_end)
    {
      segmap_entry=get_segmap(prom_va_begin);

      curr_num_segs = ((segmap_entry<curr_num_segs) 
		       ? segmap_entry : curr_num_segs);

      for(i = num_contexts; --i > 0;)
	  (*romvec->pv_setctxt)(i, (char *) prom_va_begin,
				segmap_entry);

      if(segmap_entry == invalid_segment)
	{

#ifdef DEBUG_MAP_PROM
	  printk("invalid_segments, virt_addr 0x%x\n", prom_va_begin);
#endif

	  prom_va_begin += 0x40000;  /* num bytes per segment entry */
	  continue;
	}

      /* DUH, prom maps itself so that users can access it. This is
       * broken.
       */

#ifdef DEBUG_MAP_PROM
      printk("making segmap for prom privileged, va = 0x%x\n",
	     prom_va_begin);
#endif

      for(i = 0x40; --i >= 0; prom_va_begin+=4096)
	{
	  put_pte(prom_va_begin, get_pte(prom_va_begin) | 0x20000000);
	}

    }

  printk("Mapped the PROM in all contexts...\n");

#ifdef DEBUG_MAP_PROM
  printk("curr_num_segs = 0x%x\n", curr_num_segs);
#endif

  return curr_num_segs;

}

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
	die_if_kernel("Oops", regs, error_code);
	do_exit(SIGKILL);
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
unsigned long __bad_pagetable(void)
{
	extern char empty_bad_page_table[PAGE_SIZE];

	return (unsigned long) empty_bad_page_table;
}

unsigned long __bad_page(void)
{
	extern char empty_bad_page[PAGE_SIZE];

	return (unsigned long) empty_bad_page;
}

unsigned long __zero_page(void)
{
	extern char empty_zero_page[PAGE_SIZE];

	return (unsigned long) empty_zero_page;
}

void show_mem(void)
{
	int i=0,free = 0,total = 0,reserved = 0;
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
 * paging_init() sets up the page tables - note that the first 4MB are
 * already mapped by head.S.
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	int pg_segmap;
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
	    for(i=1; i<num_contexts; i++)
	      {
		/* map the kernel virt_addrs */
		(*(romvec->pv_setctxt))(i, (char *) c, a);
		c += 4096;
	      }
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
	b=PAGE_ALIGN((unsigned long) msgbuf);
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

void mem_init(unsigned long start_low_mem,
	      unsigned long start_mem, unsigned long end_mem)
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
