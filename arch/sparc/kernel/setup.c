/*  $Id: setup.c,v 1.60 1996/04/04 16:30:28 tridge Exp $
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
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/major.h>

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
#include <asm/mbus.h>

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

unsigned int phys_bytes_of_ram, end_of_phys_memory;

unsigned long bios32_init(unsigned long memory_start, unsigned long memory_end)
{
	return memory_start;
}

/* Typing sync at the prom prompt calls the function pointed to by
 * romvec->pv_synchook which I set to the following function.
 * This should sync all filesystems and return, for now it just
 * prints out pretty messages and returns.
 */

extern unsigned long trapbase;
extern void breakpoint(void);
#if CONFIG_SUN_CONSOLE
extern void console_restore_palette(void);
#endif
asmlinkage void sys_sync(void);	/* it's really int */

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

#if CONFIG_SUN_CONSOLE
        console_restore_palette ();
#endif
	prom_printf("PROM SYNC COMMAND...\n");
	show_free_areas();
	if(current->pid != 0) {
		sti();
		sys_sync();
		cli();
	}
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

void kernel_enter_debugger(void)
{
	if (boot_flags & BOOTME_KGDB) {
		printk("KGDB: Entered\n");
		breakpoint();
	}
}

int obp_system_intr(void)
{
	if (boot_flags & BOOTME_KGDB) {
		printk("KGDB: system interrupted\n");
		breakpoint();
		return 1;
	}
	if (boot_flags & BOOTME_DEBUG) {
		printk("OBP: system interrupted\n");
		prom_halt();
		return 1;
	}
	return 0;
}

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
#if CONFIG_SUN_SERIAL
				if(commands[i+8]=='a')
					rs_kgdb_hook(0);
				else if(commands[i+8]=='b')
					rs_kgdb_hook(1);
				else
#endif
#if CONFIG_AP1000
                                if(commands[i+8]=='c')
				  printk("KGDB: ap1000+ debugging\n");
				else
#endif
				{
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
extern void sun4c_probe_vac(void);
extern void get_idprom(void);
extern char cputypval;
extern unsigned long start, end;
extern void panic_setup(char *, int *);

char saved_command_line[256];
enum sparc_cpu sparc_cpu_model;

struct tt_entry *sparc_ttable;

static struct pt_regs fake_swapper_regs = { 0, 0, 0, 0, { 0, } };

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	int total, i, panic_stuff[2], packed;

#if CONFIG_AP1000
        register_console(prom_printf);
	((char *)(&cputypval))[4] = 'm'; /* ugly :-( */
#endif

#if 0
	/* Always reboot on panic, but give 5 seconds to hit L1-A
	 * and look at debugging info if desired.
	 */
	panic_stuff[0] = 1;
	panic_stuff[1] = 5;
	panic_setup(0, panic_stuff);
#endif

	sparc_ttable = (struct tt_entry *) &start;

	/* Initialize PROM console and command line. */
	*cmdline_p = prom_getbootargs();
	strcpy(saved_command_line, *cmdline_p);

	/* Set sparc_cpu_model */
	sparc_cpu_model = sun_unknown;
	if(!strcmp(&cputypval,"sun4c")) { sparc_cpu_model=sun4c; }
	if(!strcmp(&cputypval,"sun4m")) { sparc_cpu_model=sun4m; }
	if(!strcmp(&cputypval,"sun4d")) { sparc_cpu_model=sun4d; }
	if(!strcmp(&cputypval,"sun4e")) { sparc_cpu_model=sun4e; }
	if(!strcmp(&cputypval,"sun4u")) { sparc_cpu_model=sun4u; }
	printk("ARCH: ");
	packed = 0;
	switch(sparc_cpu_model)
	  {
	  case sun4c:
		  printk("SUN4C\n");
		  sun4c_probe_vac();
		  packed = 0;
		  break;
          case sun4m:
		  printk("SUN4M\n");
		  packed = 1;
		  break;
	  case sun4d:
		  printk("SUN4D\n");
		  packed = 1;
		  break;
	  case sun4e:
		  printk("SUN4E\n");
		  packed = 0;
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

	if(!packed) {
		for(i=0; sp_banks[i].num_bytes != 0; i++)
			end_of_phys_memory = sp_banks[i].base_addr +
				sp_banks[i].num_bytes;
	} else {
		unsigned int sum = 0;

		for(i = 0; sp_banks[i].num_bytes != 0; i++)
			sum += sp_banks[i].num_bytes;

		end_of_phys_memory = sum;
	}

	prom_setsync(prom_sync_me);

	*memory_end_p = (end_of_phys_memory + PAGE_OFFSET);
	if(*memory_end_p > IOBASE_VADDR)
		*memory_end_p = IOBASE_VADDR;

	/* Due to stack alignment restrictions and assumptions... */
	init_task.mm->mmap->vm_page_prot = PAGE_SHARED;
	init_task.mm->mmap->vm_start = KERNBASE;
	init_task.mm->mmap->vm_end = *memory_end_p;
	init_task.tss.kregs = &fake_swapper_regs;

	{
		extern int serial_console;  /* in console.c, of course */
#if !CONFIG_SUN_SERIAL
		serial_console = 0;
#else
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
#endif
	}
#if 1
	/* XXX ROOT_DEV hack for kgdb - davem XXX */
#if 1
	ROOT_DEV = MKDEV(UNNAMED_MAJOR, 255); /* NFS */
#else
	ROOT_DEV = 0x801; /* SCSI DISK */
#endif

#endif
}

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	return -EIO;
}

/* BUFFER is PAGE_SIZE bytes long. */

extern char *sparc_cpu_type[];
extern char *sparc_fpu_type[];

extern char *smp_info(void);

int get_cpuinfo(char *buffer)
{
	int cpuid=get_cpuid();

	return sprintf(buffer, "cpu\t\t: %s\n"
            "fpu\t\t: %s\n"
            "promlib\t\t: Version %d Revision %d\n"
            "type\t\t: %s\n"
            "Elf Support\t: %s\n"   /* I can't remember when I do --ralp */
#ifndef __SMP__
            "BogoMips\t: %lu.%02lu\n"
#else
	    "Cpu0Bogo\t: %lu.%02lu\n"
	    "Cpu1Bogo\t: %lu.%02lu\n"
	    "Cpu2Bogo\t: %lu.%02lu\n"
	    "Cpu3Bogo\t: %lu.%02lu\n"
#endif
	    "%s"
#ifdef __SMP__
	    "%s"
#endif
	    ,
            sparc_cpu_type[cpuid],
            sparc_fpu_type[cpuid],
#if CONFIG_AP1000
            0, 0,
#else
            romvec->pv_romvers, prom_rev,
#endif
            &cputypval,
#if CONFIG_BINFMT_ELF
            "yes",
#else
            "no",
#endif
#ifndef __SMP__
            loops_per_sec/500000, (loops_per_sec/5000) % 100,
#else
	    cpu_data[0].udelay_val/500000, (cpu_data[0].udelay_val/5000)%100,
	    cpu_data[1].udelay_val/500000, (cpu_data[1].udelay_val/5000)%100,
	    cpu_data[2].udelay_val/500000, (cpu_data[2].udelay_val/5000)%100,
	    cpu_data[3].udelay_val/500000, (cpu_data[3].udelay_val/5000)%100,
#endif
	    mmu_info()
#ifdef __SMP__
	    , smp_info()
#endif
            );

}
