/* vac.c:   Routines for flushing various amount of the Sparc VAC
            (virtual address cache).

   Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
*/

#include <asm/vac-ops.h>
#include <asm/page.h>

/* Flush all VAC entries for the current context */

extern int vac_do_hw_vac_flushes, vac_size, vac_linesize;
extern int vac_entries_per_context, vac_entries_per_segment;
extern int vac_entries_per_page;

void
flush_vac_context()
{
  register int entries_left, offset;
  register char* address;

  entries_left = vac_entries_per_context;
  address = (char *) 0;

  if(vac_do_hw_vac_flushes)
    {
      while(entries_left-- >=0)
	{
	  hw_flush_vac_context_entry(address);
	  address += PAGE_SIZE;
	}
    }
  else
    {
      offset = vac_linesize;
      while(entries_left-- >=0)
	{
	  sw_flush_vac_context_entry(address);
	  address += offset;
	}
    }
}

void
flush_vac_segment(register unsigned int segment)
{
  register int entries_left, offset;
  register char* address = (char *) 0;
  
  entries_left = vac_entries_per_segment;
  __asm__ __volatile__("sll %0, 18, %1\n\t"
		       "sra %1, 0x2, %1\n\t"
		       : "=r" (segment) : "0" (address));

  if(vac_do_hw_vac_flushes)
    {
      while(entries_left-- >=0)
	{
	  hw_flush_vac_segment_entry(address);
	  address += PAGE_SIZE;
	}
    }
  else
    {
      offset = vac_linesize;
      while(entries_left-- >=0)
	{
	  sw_flush_vac_segment_entry(address);
	  address += offset;
	}
    }
}

void
flush_vac_page(register unsigned int addr)
{
  register int entries_left, offset;

  if(vac_do_hw_vac_flushes)
    {
      hw_flush_vac_page_entry((unsigned long *) addr);
    }
  else
    {
      entries_left = vac_entries_per_page;
      offset = vac_linesize;
      while(entries_left-- >=0)
	{
	  sw_flush_vac_page_entry((unsigned long *) addr);
	  addr += offset;
	}
    }
}
  
