/*  $Id: setup.c,v 1.41 1995/11/25 00:58:21 davem Exp $
 *  linux/arch/sparc/kernel/setup.c
 *
 *  Copyright (C) 1995  David S. Miller (davem@caip.rutgers.edu)
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
#include <asm/kgdb.h>
#include <asm/processor.h>
#include <asm/oplib.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/traps.h>
#include <asm/vaddrs.h>
#include <asm/kdebug.h>

struct screen_info screen_info = {
	0, 0,			/* orig-x, orig-y */
	{ 0, 0, },		/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	128,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
	54,			/* orig-video-lines */
	0,                      /* orig-video-isVGA */
	16                      /* orig-video-points */
};

char wp_works_ok = 0;

unsigned long bios32_init(unsigned long memory_start, unsigned long memory_end)
{
	return memory_start;
}

/* Typing sync at the prom promptcalls the function pointed to by
 * romvec->pv_synchook which I set to the following function.
 * This should sync all filesystems and return, for now it just
 * prints out pretty messages and returns.
 */

extern unsigned long trapbase;

/* Pretty sick eh? */
void prom_sync_me(void)
{
	unsigned long prom_tbr, flags;

	save_flags(flags); cli();
	__asm__ __volatile__("rd %%tbr, %0\n\t" : "=r" (prom_tbr));
	__asm__ __volatile__("wr %0, 0x0, %%tbr\n\t"
			     "nop\n\t"
			     "nop\n\t"
			     "nop\n\t" : : "r" (&trapbase));

	prom_printf("PROM SYNC COMMAND...\n");
	show_free_areas();
	prom_printf("Returning to prom\n");

	__asm__ __volatile__("wr %0, 0x0, %%tbr\n\t"
			     "nop\n\t"
			     "nop\n\t"
			     "nop\n\t" : : "r" (prom_tbr));
	restore_flags(flags);

	return;
}

extern void rs_kgdb_hook(int tty_num); /* sparc/serial.c */

unsigned int boot_flags;
#define BOOTME_DEBUG  0x1
#define BOOTME_SINGLE 0x2
#define BOOTME_KGDB   0x4

/* This routine does no error checking, make sure your string is sane
 * before calling this!
 * XXX This is cheese, make generic and better.
 */
void
boot_flags_init(char *commands)
{
	int i;
	for(i=0; i<strlen(commands); i++) {
		if(commands[i]=='-') {
			switch(commands[i+1]) {
			case 'd':
				boot_flags |= BOOTME_DEBUG;
				break;
			case 's':
				boot_flags |= BOOTME_SINGLE;
				break;
			case 'h':
				prom_printf("boot_flags_init: Found halt flag, doing so now...\n");
				halt();
				break;
			default:
				printk("boot_flags_init: Unknown boot arg (-%c)\n",
				       commands[i+1]);
				break;
			};
		} else {
			if(commands[i]=='k' && commands[i+1]=='g' &&
			   commands[i+2]=='d' && commands[i+3]=='b' &&
			   commands[i+4]=='=' && commands[i+5]=='t' &&
			   commands[i+6]=='t' && commands[i+7]=='y') {
				printk("KGDB: Using serial line /dev/tty%c for "
				       "session\n", commands[i+8]);
				boot_flags |= BOOTME_KGDB;
				if(commands[i+8]=='a')
					rs_kgdb_hook(0);
				else if(commands[i+8]=='b')
					rs_kgdb_hook(1);
				else {
					printk("KGDB: whoops bogon tty line "
					       "requested, disabling session\n");
					boot_flags &= (~BOOTME_KGDB);
				}
			}
		}
	}
	return;
}

/* This routine will in the future do all the nasty prom stuff
 * to probe for the mmu type and its parameters, etc. This will
 * also be where SMP things happen plus the Sparc specific memory
 * physical memory probe as on the alpha.
 */

extern void load_mmu(void);
extern int prom_probe_memory(void);
extern void probe_vac(void);
extern void get_idprom(void);
extern unsigned int end_of_phys_memory;
extern char cputypval;
extern unsigned long start, end, bootup_stack, bootup_kstack;


char sparc_command_line[256];  /* Should be enough */
enum sparc_cpu sparc_cpu_model;

struct tt_entry *sparc_ttable;

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	int total, i;

	sparc_ttable = (struct tt_entry *) &start;

	/* Initialize PROM console and command line. */
	*cmdline_p = prom_getbootargs();

	/* Set sparc_cpu_model */
	sparc_cpu_model = sun_unknown;
	if(!strcmp(&cputypval,"sun4c")) { sparc_cpu_model=sun4c; }
	if(!strcmp(&cputypval,"sun4m")) { sparc_cpu_model=sun4m; }
	if(!strcmp(&cputypval,"sun4d")) { sparc_cpu_model=sun4d; }
	if(!strcmp(&cputypval,"sun4e")) { sparc_cpu_model=sun4e; }
	if(!strcmp(&cputypval,"sun4u")) { sparc_cpu_model=sun4u; }
	printk("ARCH: ");
	switch(sparc_cpu_model)
	  {
	  case sun4c:
		  printk("SUN4C\n");
		  probe_vac();
		  break;
          case sun4m:
		  printk("SUN4M\n");
		  break;
	  case sun4d:
		  printk("SUN4D\n");
		  break;
	  case sun4e:
		  printk("SUN4E\n");
		  break;
	  case sun4u:
		  printk("SUN4U\n");
		  break;
	  default:
		  printk("UNKNOWN!\n");
		  break;
	  };

	boot_flags_init(*cmdline_p);
	if((boot_flags&BOOTME_DEBUG) && (linux_dbvec!=0) && 
	   ((*(short *)linux_dbvec) != -1)) {
		printk("Booted under KADB. Syncing trap table.\n");
		(*(linux_dbvec->teach_debugger))();
	}
	if((boot_flags & BOOTME_KGDB)) {
		set_debug_traps();
		breakpoint();
	}

	get_idprom();
	load_mmu();
	total = prom_probe_memory();
	*memory_start_p = (((unsigned long) &end));
	printk("Physical Memory: %d bytes (in hex %08lx)\n", (int) total,
		    (unsigned long) total);

	for(i=0; sp_banks[i].num_bytes != 0; i++) {
#if 0
		printk("Bank %d:  base 0x%x  bytes %d\n", i,
			    (unsigned int) sp_banks[i].base_addr, 
			    (int) sp_banks[i].num_bytes);
#endif
		end_of_phys_memory = sp_banks[i].base_addr + sp_banks[i].num_bytes;
	}

	prom_setsync(prom_sync_me);

	/* Due to stack alignment restrictions and assumptions... */
	init_task.mm->mmap->vm_page_prot = PAGE_SHARED;

	*memory_end_p = (end_of_phys_memory + PAGE_OFFSET);

	{
		extern int serial_console;  /* in console.c, of course */
		int idev = prom_query_input_device();
		int odev = prom_query_output_device();
		if (idev == PROMDEV_IKBD && odev == PROMDEV_OSCREEN) {
			serial_console = 0;
		} else if (idev == PROMDEV_ITTYA && odev == PROMDEV_OTTYA) {
			serial_console = 1;
		} else if (idev == PROMDEV_ITTYB && odev == PROMDEV_OTTYB) {
			prom_printf("Console on ttyb is not supported\n");
			prom_halt();
		} else {
			prom_printf("Inconsistent console\n");
			prom_halt();
		}
	}
}

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	return -EIO;
}

/*
 * BUFFER is PAGE_SIZE bytes long.
 *
 * XXX Need to do better than this! XXX
 */
int get_cpuinfo(char *buffer)
{
	return sprintf(buffer, "Sparc RISC\n");
}
