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

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/hwrpb.h>
#include <asm/dma.h>
#include <asm/io.h>

struct hae hae = {
	0,
	(unsigned long*) HAE_ADDRESS
};

struct hwrpb_struct *hwrpb;

unsigned char aux_device_present;

/*
 * This is setup by the secondary bootstrap loader.  Because
 * the zero page is zeroed out as soon as the vm system is
 * initialized, we need to copy things out into a more permanent
 * place.
 */
#define PARAM			ZERO_PGE
#define COMMAND_LINE		((char*)(PARAM + 0x0000))
#define COMMAND_LINE_SIZE	256

static char command_line[COMMAND_LINE_SIZE] = { 0, };

/*
 * The format of "screen_info" is strange, and due to early
 * i386-setup code. This is just enough to make the console
 * code think we're on a EGA+ colour display.
 */
struct screen_info screen_info = {
	0, 0,			/* orig-x, orig-y */
	{ 0, 0 },		/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	80,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
	25			/* orig-video-lines */
};

static unsigned long find_end_memory(void)
{
	int i;
	unsigned long high = 0;
	struct memclust_struct * cluster;
	struct memdesc_struct * memdesc;

	memdesc = (struct memdesc_struct *) (INIT_HWRPB->mddt_offset + (unsigned long) INIT_HWRPB);
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

	hwrpb = (struct hwrpb_struct*)(IDENT_ADDR + INIT_HWRPB->phys_addr);

	set_hae(hae.cache);		/* sync HAE register w/hae_cache */

	ROOT_DEV = 0x0802;		/* sda2 */
#ifndef CONFIG_PCI
	aux_device_present = 0xaa;
#else
	aux_device_present = 0x00;
#endif
	command_line[COMMAND_LINE_SIZE - 1] = '\0';
	strcpy(command_line, COMMAND_LINE);

	*cmdline_p = command_line;
	*memory_start_p = (unsigned long) &_end;
	*memory_end_p = find_end_memory();

#ifdef CONFIG_PCI
	*memory_start_p = lca_init(*memory_start_p, *memory_end_p);
#endif
}

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	return -EIO;
}


/*
 * BUFFER is PAGE_SIZE bytes long.
 */
int get_cpuinfo(char *buffer)
{
	const char *cpu_name[] = {
		"EV3", "EV4", "Unknown 1", "LCA4", "EV5", "EV45"
	};
	const char *systype_name[] = {
		"ADU", "Cobra", "Ruby", "Flamingo", "Unknown 1", "Jensen",
		"Pelican", "Unknown 2", "Sable", "AXPvme", "Noname",
		"Turbolaser", "Avanti", "Mustang", "Alcor", "Unknown 3",
		"Mikasa", "Unknown3", "EB66", "EB64+"
	};
	struct percpu_struct *cpu;
	unsigned int cpu_index, system_index;
#	define N(a)	(sizeof(a)/sizeof(a[0]))

	cpu = (struct percpu_struct*)((char*)hwrpb + hwrpb->processor_offset);
	cpu_index = (unsigned) (cpu->type - 1);
	system_index = (unsigned) (hwrpb->sys_type - 1);

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
		       "BogoMIPS\t\t: %lu.%02lu\n",

		       (cpu_index < N(cpu_name) ? cpu_name[cpu_index] : "Unknown"),
		       cpu->variation, cpu->revision, (char*)cpu->serial_no,
		       (system_index < N(systype_name) ? systype_name[system_index] : "Unknown"),
		       hwrpb->sys_variation, hwrpb->sys_revision,
		       (char*)hwrpb->ssn,
		       hwrpb->cycle_freq,
		       hwrpb->intr_freq / 4096,
		       (100 * hwrpb->intr_freq / 4096) % 100,
		       hwrpb->pagesize,
		       hwrpb->pa_bits,
		       hwrpb->max_asn,
		       loops_per_sec / 500000, (loops_per_sec / 5000) % 100);
#       undef N
}
