/*  include/asm-sparc/bugs.h:  Sparc probes for various bugs.
 *
 *  Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */

#define CONFIG_BUGSPARC

#include <asm/openprom.h>
#include <asm/page.h>

extern pgd_t swapper_pg_dir[16384];

static void check_mmu(void)
{
  register struct linux_romvec *lvec;
  register int root_node;
  unsigned int present;

  lvec = romvec;

  root_node = (*(romvec->pv_nodeops->no_nextnode))(0);

  present = 0;
  (*(romvec->pv_nodeops->no_getprop))(root_node, "buserr-type", 
				      (char *) &present);
  if(present == 1)
    {
      printk("MMU bug found: uncaching trap table\n");
      for(present = (unsigned long) &trapbase; present < (unsigned long)
	  &swapper_pg_dir; present+=PAGE_SIZE)
	      put_pte(present, (get_pte(present) | PTE_NC));
    }

  return;
}


static void 
check_bugs(void)
{
  check_mmu();
}
