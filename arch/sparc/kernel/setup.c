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
#include <asm/processor.h>
#include <asm/oplib.h>    /* The PROM is your friend... */
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/traps.h>
#include <asm/vaddrs.h>
#include <asm/kdebug.h>

extern unsigned long probe_devices(unsigned long);

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

char wp_works_ok = 0;

unsigned long bios32_init(unsigned long memory_start, unsigned long memory_end)
{
	return memory_start;
}

/* Lame prom console routines, gets registered below. Thanks for the
 * tip Linus.  We now use a generic putchar prom routine through the
 * linux prom library.
 */

void sparc_console_print(const char * p)
{
  unsigned char c;

	while ((c = *(p++)) != 0)
	  {
	    if (c == '\n')
	      prom_putchar('\r');
	    prom_putchar(c);
	  }

  return;

}

/* Typing sync at the prom promptcalls the function pointed to by
 * romvec->pv_synchook which I set to the following function.
 * This should sync all filesystems and return, for now it just
 * prints out pretty messages and returns.
 */
void prom_sync_me(void)
{
	printk("PROM SYNC COMMAND...\n");
	show_free_areas();
	printk("Returning to prom\n");
	return;
}

unsigned int boot_flags;
#define BOOTME_DEBUG  0x1
#define BOOTME_SINGLE 0x2
#define BOOTME_KGDB   0x3

/* This routine does no error checking, make sure your string is sane
 * before calling this!
 * XXX This is cheese, make generic and better.
 */
void
boot_flags_init(char *commands)
{
	int i;
	for(i=0; i<strlen(commands); i++)
		if(commands[i]=='-')
			switch(commands[i+1]) {
			case 'd':
				boot_flags |= BOOTME_DEBUG;
				break;
			case 's':
				boot_flags |= BOOTME_SINGLE;
				break;
			case 'h':
				printk("boot_flags_init: Found halt flag, doing so now...\n");
				halt();
				break;
			case 'k':
				printk("Found KGDB boot flag...\n");
				boot_flags |= BOOTME_KGDB;
				break;
			default:
				printk("boot_flags_init: Unknown boot arg (-%c)\n",
				       commands[i+1]);
				break;
			};

	return;
}

/* This routine will in the future do all the nasty prom stuff
 * to probe for the mmu type and its parameters, etc. This will
 * also be where SMP things happen plus the Sparc specific memory
 * physical memory probe as on the alpha.
 */

extern void register_console(void (*proc)(const char *));
extern void load_mmu(void);
extern int prom_probe_memory(void);
extern void probe_mmu(int node);
extern void get_idprom(void);
extern void srmmu_patch_fhandlers(void);
extern unsigned int prom_iface_vers, end_of_phys_memory, phys_bytes_of_ram;
extern char cputypval;
extern unsigned long start;
char sparc_command_line[256];  /* Should be enough */
enum sparc_cpu sparc_cpu_model;

struct tt_entry *sparc_ttable;

/* #define DEBUG_CMDLINE */

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	int counter, total, i, node;
	char devtype[64];

	sparc_ttable = (struct tt_entry *) &start;

	register_console(sparc_console_print);

	/* Initialize PROM console and command line. */
	*cmdline_p = prom_getbootargs();
	boot_flags_init(*cmdline_p);

	/* Synchronize with debugger if necessary.  Grrr, have to check
	 * the boot flags too. ;(
	 */
	if((boot_flags&BOOTME_DEBUG) && (linux_dbvec!=0) && 
	   ((*(short *)linux_dbvec) != -1)) {
		printk("Booted under debugger. Syncing up trap table.\n");
		/* Sync us up... */
		(*(linux_dbvec->teach_debugger))();

		SP_ENTER_DEBUGGER;
	}

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
		  memset(phys_seg_map, 0x0, sizeof(phys_seg_map[PSEG_ENTRIES]));
		  put_segmap(IOBASE_VADDR, IOBASE_SUN4C_SEGMAP);
		  phys_seg_map[IOBASE_SUN4C_SEGMAP] = PSEG_RSV;
		  node = prom_root_node;

		  printk("SUN4C\n");
		  break;
          case sun4m:
		  node = prom_getchild(prom_root_node);
		  prom_getproperty(node, "device_type", devtype, sizeof(devtype));
		  while(strcmp(devtype, "cpu") != 0) {
			  node = prom_getsibling(node);
			  prom_getproperty(node, "device_type", devtype,
					   sizeof(devtype));
		  }
		  /* Patch trap table. */
		  srmmu_patch_fhandlers();
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

	/* probe_devices() expects this to be done. */
	get_idprom();

	/* Probe the mmu constants. */
	probe_mmu(node);

	/* Set pointers to memory management routines. */
	load_mmu();

	/* Probe for memory. */
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

	/* Set prom sync hook pointer */
	prom_setsync(prom_sync_me);

	init_task.mm->start_code = PAGE_OFFSET;
	init_task.mm->end_code = PAGE_OFFSET + (unsigned long) &etext;
	init_task.mm->end_data = PAGE_OFFSET + (unsigned long) &edata;
	init_task.mm->brk = PAGE_OFFSET + (unsigned long) &end;
	init_task.mm->mmap->vm_page_prot = PAGE_SHARED;

	/* Grrr, wish I knew why I have to do this ;-( */
	for(counter=1; counter<NR_TASKS; counter++) {
		task[counter] = NULL;
	}

	*memory_end_p = (((unsigned long) (total) + PAGE_OFFSET));

	return;
}

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	return -EIO;
}
