/*  $Id: setup.c,v 1.12 1997/08/28 02:23:19 ecd Exp $
 *  linux/arch/sparc64/kernel/setup.c
 *
 *  Copyright (C) 1995,1996  David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1997       Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <asm/smp.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/init.h>
#include <linux/inet.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/oplib.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/idprom.h>
#include <asm/head.h>

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
 * the sync callback which I set to the following function.
 * This should sync all filesystems and return, for now it just
 * prints out pretty messages and returns.
 */

extern unsigned long sparc64_ttable_tl0;
#if CONFIG_SUN_CONSOLE
extern void console_restore_palette(void);
#endif
asmlinkage void sys_sync(void);	/* it's really int */

/* Pretty sick eh? */
void prom_sync_me(long *args)
{
	unsigned long prom_tba, flags;

	save_and_cli(flags);
	__asm__ __volatile__("flushw; rdpr %%tba, %0\n\t" : "=r" (prom_tba));
	__asm__ __volatile__("wrpr %0, 0x0, %%tba\n\t" : : "r" (&sparc64_ttable_tl0));

#ifdef CONFIG_SUN_CONSOLE
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

	__asm__ __volatile__("flushw; wrpr %0, 0x0, %%tba\n\t" : : "r" (prom_tba));
	restore_flags(flags);

	return;
}

extern void rs_kgdb_hook(int tty_num); /* sparc/serial.c */

unsigned int boot_flags = 0;
#define BOOTME_DEBUG  0x1
#define BOOTME_SINGLE 0x2
#define BOOTME_KGDB   0x4

#ifdef CONFIG_SUN_CONSOLE
extern char *console_fb_path;
static int console_fb = 0;
#endif
static unsigned long memory_size = 0;

/* XXX Implement this at some point... */
void kernel_enter_debugger(void)
{
}

int obp_system_intr(void)
{
	if (boot_flags & BOOTME_DEBUG) {
		printk("OBP: system interrupted\n");
		prom_halt();
		return 1;
	}
	return 0;
}

/* 
 * Process kernel command line switches that are specific to the
 * SPARC or that require special low-level processing.
 */
__initfunc(static void process_switch(char c))
{
	switch (c) {
	case 'd':
		boot_flags |= BOOTME_DEBUG;
		break;
	case 's':
		boot_flags |= BOOTME_SINGLE;
		break;
	case 'h':
		prom_printf("boot_flags_init: Halt!\n");
		prom_halt();
		break;
	default:
		printk("Unknown boot switch (-%c)\n", c);
		break;
	}
}

__initfunc(static void boot_flags_init(char *commands))
{
	while (*commands) {
		/* Move to the start of the next "argument". */
		while (*commands && *commands == ' ')
			commands++;

		/* Process any command switches, otherwise skip it. */
		if (*commands == '\0')
			break;
		else if (*commands == '-') {
			commands++;
			while (*commands && *commands != ' ')
				process_switch(*commands++);
		} else if (strlen(commands) >= 9
			   && !strncmp(commands, "kgdb=tty", 8)) {
			boot_flags |= BOOTME_KGDB;
			switch (commands[8]) {
#ifdef CONFIG_SUN_SERIAL
			case 'a':
				rs_kgdb_hook(0);
				prom_printf("KGDB: Using serial line /dev/ttya.\n");
				break;
			case 'b':
				rs_kgdb_hook(1);
				prom_printf("KGDB: Using serial line /dev/ttyb.\n");
				break;
#endif
			default:
				printk("KGDB: Unknown tty line.\n");
				boot_flags &= ~BOOTME_KGDB;
				break;
			}
			commands += 9;
		} else {
#if CONFIG_SUN_CONSOLE
			if (!strncmp(commands, "console=", 8)) {
				commands += 8;
				if (!strncmp (commands, "ttya", 4)) {
					console_fb = 2;
					prom_printf ("Using /dev/ttya as console.\n");
				} else if (!strncmp (commands, "ttyb", 4)) {
					console_fb = 3;
					prom_printf ("Using /dev/ttyb as console.\n");
				} else {
					console_fb = 1;
					console_fb_path = commands;
				}
			} else
#endif
			if (!strncmp(commands, "mem=", 4)) {
				/*
				 * "mem=XXX[kKmM]" overrides the PROM-reported
				 * memory size.
				 */
				memory_size = simple_strtoul(commands + 4,
							     &commands, 0);
				if (*commands == 'K' || *commands == 'k') {
					memory_size <<= 10;
					commands++;
				} else if (*commands=='M' || *commands=='m') {
					memory_size <<= 20;
					commands++;
				}
			}
			while (*commands && *commands != ' ')
				commands++;
		}
	}
}

extern int prom_probe_memory(void);
extern unsigned long start, end;
extern void panic_setup(char *, int *);
extern unsigned long sun_serial_setup(unsigned long);

extern unsigned short root_flags;
extern unsigned short root_dev;
extern unsigned short ram_flags;
extern unsigned ramdisk_image;
extern unsigned ramdisk_size;
#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

extern int root_mountflags;

char saved_command_line[256];
char reboot_command[256];

#ifdef CONFIG_ROOT_NFS
extern char nfs_root_addrs[];
#endif

unsigned long phys_base;

static struct pt_regs fake_swapper_regs = { { 0, }, 0, 0, 0, 0 };

__initfunc(void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	unsigned long lowest_paddr;
	int total, i;

	/* Initialize PROM console and command line. */
	*cmdline_p = prom_getbootargs();
	strcpy(saved_command_line, *cmdline_p);

	printk("ARCH: SUN4U\n");

	boot_flags_init(*cmdline_p);

	idprom_init();
	total = prom_probe_memory();

	lowest_paddr = 0xffffffffffffffffUL;
	for(i=0; sp_banks[i].num_bytes != 0; i++) {
		if(sp_banks[i].base_addr < lowest_paddr)
			lowest_paddr = sp_banks[i].base_addr;
		end_of_phys_memory = sp_banks[i].base_addr +
			sp_banks[i].num_bytes;
		if (memory_size) {
			if (end_of_phys_memory > memory_size) {
				sp_banks[i].num_bytes -=
					(end_of_phys_memory - memory_size);
				end_of_phys_memory = memory_size;
				sp_banks[++i].base_addr = 0xdeadbeef;
				sp_banks[i].num_bytes = 0;
			}
		}
	}
	prom_setsync(prom_sync_me);

	/* In paging_init() we tip off this value to see if we need
	 * to change init_mm.pgd to point to the real alias mapping.
	 */
	phys_base = lowest_paddr;

	*memory_start_p = PAGE_ALIGN(((unsigned long) &end));
	*memory_end_p = (end_of_phys_memory + PAGE_OFFSET);

#ifdef DAVEM_DEBUGGING
	prom_printf("phys_base[%016lx] memory_start[%016lx] memory_end[%016lx]\n",
		    phys_base, *memory_start_p, *memory_end_p);
#endif

	if (!root_flags)
		root_mountflags &= ~MS_RDONLY;
	ROOT_DEV = to_kdev_t(root_dev);
#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = ram_flags & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((ram_flags & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((ram_flags & RAMDISK_LOAD_FLAG) != 0);	
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	if (ramdisk_image) {
		unsigned long start = 0;
		
		if (ramdisk_image >= (unsigned long)&end - 2 * PAGE_SIZE)
			ramdisk_image -= KERNBASE;
		initrd_start = ramdisk_image + phys_base + PAGE_OFFSET;
		initrd_end = initrd_start + ramdisk_size;
		if (initrd_end > *memory_end_p) {
			printk(KERN_CRIT "initrd extends beyond end of memory "
		                 	 "(0x%016lx > 0x%016lx)\ndisabling initrd\n",
		       			 initrd_end,*memory_end_p);
			initrd_start = 0;
		}
		if (initrd_start)
			start = ramdisk_image + KERNBASE;
		if (start >= *memory_start_p && start < *memory_start_p + 2 * PAGE_SIZE) {
			initrd_below_start_ok = 1;
			*memory_start_p = PAGE_ALIGN (start + ramdisk_size);
		}
	}
#endif	

	/* Due to stack alignment restrictions and assumptions... */
	init_task.mm->mmap->vm_page_prot = PAGE_SHARED;
	init_task.mm->mmap->vm_start = PAGE_OFFSET;
	init_task.mm->mmap->vm_end = *memory_end_p;
	init_task.mm->context = (unsigned long) NO_CONTEXT;
	init_task.tss.kregs = &fake_swapper_regs;

#ifdef CONFIG_ROOT_NFS	
	if (!*nfs_root_addrs) {
		int chosen = prom_finddevice ("/chosen");
		u32 cl, sv, gw;
		char *p = nfs_root_addrs;
		
		cl = prom_getintdefault (chosen, "client-ip", 0);
		sv = prom_getintdefault (chosen, "server-ip", 0);
		gw = prom_getintdefault (chosen, "gateway-ip", 0);
		if (cl && sv) {
			strcpy (p, in_ntoa (cl));
			p += strlen (p);
			*p++ = ':';
			strcpy (p, in_ntoa (sv));
			p += strlen (p);
			*p++ = ':';
			if (gw) {
				strcpy (p, in_ntoa (gw));
				p += strlen (p);
			}
			strcpy (p, "::::none");
		}
	}
#endif

#ifdef CONFIG_SUN_SERIAL
	*memory_start_p = sun_serial_setup(*memory_start_p); /* set this up ASAP */
#endif
	{
		extern int serial_console;  /* in console.c, of course */
#if !CONFIG_SUN_SERIAL
		serial_console = 0;
#else
		switch (console_fb) {
		case 0: /* Let's get our io devices from prom */
			{
				int idev = prom_query_input_device();
				int odev = prom_query_output_device();
				if (idev == PROMDEV_IKBD && odev == PROMDEV_OSCREEN) {
					serial_console = 0;
				} else if (idev == PROMDEV_ITTYA && odev == PROMDEV_OTTYA) {
					serial_console = 1;
				} else if (idev == PROMDEV_ITTYB && odev == PROMDEV_OTTYB) {
					serial_console = 2;
				} else {
					prom_printf("Inconsistent console\n");
					prom_halt();
				}
			}
			break;
		case 1: serial_console = 0; break; /* Force one of the framebuffers as console */
		case 2: serial_console = 1; break; /* Force ttya as console */
		case 3: serial_console = 2; break; /* Force ttyb as console */
		}
#endif
	}
}

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	return -EIO;
}

/* BUFFER is PAGE_SIZE bytes long. */

extern char *sparc_cpu_type[];
extern char *sparc_fpu_type[];

extern char *smp_info(void);
extern char *mmu_info(void);

int get_cpuinfo(char *buffer)
{
	int cpuid=smp_processor_id();

	return sprintf(buffer, "cpu\t\t: %s\n"
            "fpu\t\t: %s\n"
            "promlib\t\t: Version 3 Revision %d\n"
            "prom\t\t: %d.%d.%d\n"
            "type\t\t: sun4u\n"
	    "ncpus probed\t: %d\n"
	    "ncpus active\t: %d\n"
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
            prom_rev, prom_prev >> 16, (prom_prev >> 8) & 0xff, prom_prev & 0xff,
	    linux_num_cpus, smp_num_cpus,
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
