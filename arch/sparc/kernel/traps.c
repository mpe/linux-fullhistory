/*
 * arch/sparc/kernel/traps.c
 *
 * Copyright 1994 David S. Miller (davem@caip.rutgers.edu)
 */

/*
 * I hate traps on the sparc, grrr...
 */

#include <linux/sched.h>  /* for jiffies */
#include <linux/kernel.h>

void do_hw_interrupt(unsigned long type, unsigned long vector)
{
  if (vector == 14) {
    jiffies++;
    return;
  }

  /* Just print garbage for everything else for now. */

  printk("Unimplemented Sparc TRAP, vector = %lx type = %lx\n", vector, type);

  return;
}

extern unsigned long *trapbase;

void trap_init(void)
{

  /* load up the trap table */

#if 0 /* not yet */
  __asm__("wr %0, 0x0, %%tbr\n\t"
	  "nop; nop; nop\n\t" : :
	  "r" (trapbase));
#endif

  return;
}

void die_if_kernel(char * str, struct pt_regs * regs, long err)
{
  return;
}
