/*
 * arch/sparc/kernel/traps.c
 *
 * Copyright 1994 David S. Miller (davem@caip.rutgers.edu)
 */

/*
 * I hate traps on the sparc, grrr...
 */


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

extern unsigned int *trapbase;

void trap_init(void)
{

  /* load up the trap table */

  __asm__("wr %0, 0x0, %%tbr\n\t"
	  "nop; nop; nop\n\t" : :
	  "r" (trapbase));

  return;
}

