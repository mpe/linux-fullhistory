/*
 *  linux/arch/alpha/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
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
#include <linux/delay.h>
#include <linux/config.h>	/* CONFIG_ALPHA_LCA etc */

#include <asm/segment.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/hwrpb.h>
#include <asm/dma.h>
#include <asm/io.h>

struct hae hae = {
	0,
	(unsigned long*) HAE_ADDRESS
};

struct hwrpb_struct *hwrpb;

unsigned char aux_device_present = 0xaa;

/*
 * This is setup by the secondary bootstrap loader.  Because
 * the zero page is zeroed out as soon as the vm system is
 * initialized, we need to copy things out into a more permanent
 * place.
 */
#define PARAM			ZERO_PAGE
#define COMMAND_LINE		((char*)(PARAM + 0x0000))
#define COMMAND_LINE_SIZE	256

static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

/*
 * The format of "screen_info" is strange, and due to early
 * i386-setup code. This is just enough to make the console
 * code think we're on a VGA color display.
 */
struct screen_info screen_info = {
	0, 25,			/* orig-x, orig-y */
	{ 0, 0 },		/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	80,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
	25,			/* orig-video-lines */
	1,			/* orig-video-isVGA */
	16			/* orig-video-points */
};

/*
 * Initialize Programmable Interval Timers with standard values.  Some
 * drivers depend on them being initialized (e.g., joystick driver).
 */
static void init_pit (void)
{
#if 0
    /*
     * Leave refresh timer alone---nobody should depend on
     * a particular value anyway.
     */
    outb(0x54, 0x43);	/* counter 1: refresh timer */
    outb(0x18, 0x41);
#endif

    outb(0x36, 0x43);	/* counter 0: system timer */
    outb(0x00, 0x40);
    outb(0x00, 0x40);

    outb(0xb6, 0x43);	/* counter 2: speaker */
    outb(0x31, 0x42);
    outb(0x13, 0x42);
}

static unsigned long find_end_memory(void)
{
	int i;
	unsigned long high = 0;
	struct memclust_struct * cluster;
	struct memdesc_struct * memdesc;

	memdesc = (struct memdesc_struct *)
	  (INIT_HWRPB->mddt_offset + (unsigned long) INIT_HWRPB);
	cluster = memdesc->cluster;
	for (i = memdesc->numclusters ; i > 0; i--, cluster++) {
		unsigned long tmp;
		tmp = (cluster->start_pfn + cluster->numpages) << PAGE_SHIFT;
		if (tmp > high)
			high = tmp;
	}
	/* round it up to an even number of pages.. */
	high = (high + PAGE_SIZE) & (PAGE_MASK*2);
	return PAGE_OFFSET + high;
}

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	extern int _end;

	init_pit();

	hwrpb = (struct hwrpb_struct*)(IDENT_ADDR + INIT_HWRPB->phys_addr);

	set_hae(hae.cache);	/* sync HAE register w/hae_cache */
	wrmces(0x7);		/* reset enable correctable error reports */

	ROOT_DEV = to_kdev_t(0x0802);		/* sda2 */
	command_line[COMMAND_LINE_SIZE - 1] = '\0';

	/* Hack for Jensen... since we're restricted to 8 or 16 
	 * chars for boot flags depending on the boot mode,
	 * we need some shorthand.  This should do for 
	 * installation.  Later we'll add other abbreviations
	 * as well...
	 */
	if (strcmp(COMMAND_LINE, "INSTALL") == 0) {
		strcpy(command_line, "root=/dev/fd0 load_ramdisk=1");
		strcpy(saved_command_line, command_line);
	} else {
		strcpy(command_line, COMMAND_LINE);
		strcpy(saved_command_line, COMMAND_LINE);
	}
	printk("Command line: %s\n", command_line);

	*cmdline_p = command_line;
	*memory_start_p = (unsigned long) &_end;
	*memory_end_p = find_end_memory();

#if defined(CONFIG_ALPHA_LCA)
	*memory_start_p = lca_init(*memory_start_p, *memory_end_p);
#elif defined(CONFIG_ALPHA_APECS)
	*memory_start_p = apecs_init(*memory_start_p, *memory_end_p);
#elif defined(CONFIG_ALPHA_CIA)
	*memory_start_p = cia_init(*memory_start_p, *memory_end_p);
#endif
}

/*
 * BUFFER is PAGE_SIZE bytes long.
 */
int get_cpuinfo(char *buffer)
{
	const char *cpu_name[] = {
		"EV3", "EV4", "Unknown 1", "LCA4", "EV5", "EV45"
	};
#	define SYSTYPE_NAME_BIAS	20
	const char *systype_name[] = {
		"Cabriolet", "EB66P", "-18", "-17", "-16", "-15",
		"-14", "-13", "-12", "-11", "-10", "-9", "-8",
		"-7", "-6", "-5", "-4", "-3", "-2", "-1", "0",
		"ADU", "Cobra", "Ruby", "Flamingo", "5", "Jensen",
		"Pelican", "8", "Sable", "AXPvme", "Noname",
		"Turbolaser", "Avanti", "Mustang", "Alcor", "16",
		"Mikasa", "18", "EB66", "EB64+", "21", "22", "23",
		"24", "25", "EB164"
	};
	struct percpu_struct *cpu;
	unsigned int cpu_index;
	long sysname_index;
	extern struct unaligned_stat {
		unsigned long count, va, pc;
	} unaligned[2];
#	define N(a)	(sizeof(a)/sizeof(a[0]))

	cpu = (struct percpu_struct*)((char*)hwrpb + hwrpb->processor_offset);
	cpu_index = (unsigned) (cpu->type - 1);
	sysname_index = hwrpb->sys_type + SYSTYPE_NAME_BIAS;

	return sprintf(buffer,
		       "cpu\t\t\t: Alpha\n"
		       "cpu model\t\t: %s\n"
		       "cpu variation\t\t: %ld\n"
		       "cpu revision\t\t: %ld\n"
		       "cpu serial number\t: %s\n"
		       "system type\t\t: %s\n"
		       "system variation\t: %ld\n"
		       "system revision\t\t: %ld\n"
		       "system serial number\t: %s\n"
		       "cycle frequency [Hz]\t: %lu\n"
		       "timer frequency [Hz]\t: %lu.%02lu\n"
		       "page size [bytes]\t: %ld\n"
		       "phys. address bits\t: %ld\n"
		       "max. addr. space #\t: %ld\n"
		       "BogoMIPS\t\t: %lu.%02lu\n"
		       "kernel unaligned acc\t: %ld (pc=%lx,va=%lx)\n"
		       "user unaligned acc\t: %ld (pc=%lx,va=%lx)\n",

		       (cpu_index < N(cpu_name)
			? cpu_name[cpu_index] : "Unknown"),
		       cpu->variation, cpu->revision, (char*)cpu->serial_no,
		       (sysname_index < N(systype_name)
			? systype_name[sysname_index] : "Unknown"),
		       hwrpb->sys_variation, hwrpb->sys_revision,
		       (char*)hwrpb->ssn,
		       hwrpb->cycle_freq,
		       hwrpb->intr_freq / 4096,
		       (100 * hwrpb->intr_freq / 4096) % 100,
		       hwrpb->pagesize,
		       hwrpb->pa_bits,
		       hwrpb->max_asn,
		       loops_per_sec / 500000, (loops_per_sec / 5000) % 100,
		       unaligned[0].count, unaligned[0].pc, unaligned[0].va,
		       unaligned[1].count, unaligned[1].pc, unaligned[1].va);
#       undef N
}
