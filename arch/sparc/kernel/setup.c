/*
 *  linux/arch/alpha/kernel/setup.c
 *
 *  Copyright (C) 1995  David S. Miller (davem@caip.rutgers.edu)
 */

/*
 * bootup setup stuff..
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/openprom.h>   /* for console registration + cheese */

extern void get_idprom(void);
extern void probe_devices(void);

/*
 * Gcc is hard to keep happy ;-)
 */
struct screen_info screen_info = {
	0, 0,			/* orig-x, orig-y */
	{ 0, 0, },		/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	80,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
	25			/* orig-video-lines */
};

/* At least I hide the sneaky floppy_track_buffer in my dirty assembly
 * code. ;-)
 */

unsigned long bios32_init(unsigned long memory_start, unsigned long memory_end)
{
	return memory_start;
}

/* Lame prom console routines, gets registered below. Thanks for the
 * tip Linus.
 */

void sparc_console_print(const char * p)
{
  unsigned char c;

	while ((c = *(p++)) != 0)
	  {
	    if (c == '\n') romvec->pv_putchar('\r');
	    (*(romvec->pv_putchar))(c);
	  }

  return;

}

/* This routine will in the future do all the nasty prom stuff
 * to probe for the mmu type and it's parameters, etc. This will
 * also be where SMP things happen plus the Sparc specific memory
 * physical memory probe as on the alpha.
 */

extern void register_console(void (*proc)(const char *));

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	extern int _end;

	register_console(sparc_console_print);

	printk("Sparc PROM-Console registered...\n");

	printk("calling get_idprom...\n");
	get_idprom();     /* probe_devices expects this to be done */

	printk("calling probe_devices...\n");
	probe_devices();  /* cpu/fpu, mmu probes */

	*memory_start_p = (((unsigned long) &end));
}

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	return -EIO;
}
