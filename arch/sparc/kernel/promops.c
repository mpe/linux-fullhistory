/* promops.c:  Prom node tree operations and Prom Vector initialization
 *             initialization routines.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/openprom.h>

extern struct linux_romvec *romvec;

/* This gets called from head.S upon bootup to initialize the
 * prom vector pointer for the rest of the kernel.
 */

void
init_prom(struct linux_romvec *r_ptr)
{
  romvec = r_ptr;
  
  return;
}
