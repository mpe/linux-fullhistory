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

extern struct linux_romvec *romvec;
extern int tbase_needs_unmapping;   /* We do the bug workaround in pagetables.c */

static void check_mmu(void)
{
  register struct linux_romvec *lvec;
  register int root_node;
  unsigned int present;

  lvec = romvec;

  root_node = (*(romvec->pv_nodeops->no_nextnode))(0);
  tbase_needs_unmapping=0;

  present = 0;
  (*(romvec->pv_nodeops->no_getprop))(root_node, "buserr-type", 
				      (char *) &present);
  if(present == 1)
    {
      tbase_needs_unmapping=1;
      printk("MMU bug found: not allowing trapbase to be cached\n");
    }

  return;
}


static void 
check_bugs(void)
{
  check_mmu();
}
