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
#include <asm/pgtable.h>

extern unsigned long pg0[1024];		/* page table for 0-4MB for everybody */
extern struct sparc_phys_banks sp_banks[14];

extern void die_if_kernel(char *,struct pt_regs *,long);

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
  register unsigned long bytes, base_paddr, tally;
  register int i;

  bytes = tally = 0;
  base_paddr = 0;
  i=0;
  lprom = romvec;
  switch(lprom->pv_romvers)
    {
    case 0:
      mlist=(*(lprom->pv_v0mem.v0_totphys));
      bytes = tally = mlist->num_bytes;
      base_paddr = (unsigned long) mlist->start_adr;

      sp_banks[0].base_addr = base_paddr;
      sp_banks[0].num_bytes = bytes;

      if(mlist->theres_more != (void *)0) {
	  i++;
	  mlist=mlist->theres_more;
	  bytes=mlist->num_bytes;
	  tally += bytes;
	  sp_banks[i].base_addr = (unsigned long) mlist->start_adr;
	  sp_banks[i].num_bytes = mlist->num_bytes;
	}
      break;
    case 2:
      printk("no v2 memory probe support yet.\n");
      (*(lprom->pv_halt))();
      break;
    }

  i++;
  sp_banks[i].base_addr = 0xdeadbeef;
  sp_banks[i].num_bytes = 0;

  return tally;
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




