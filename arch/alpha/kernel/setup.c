/*
 *  linux/arch/alpha/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * Bootup setup stuff.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/config.h>	/* CONFIG_ALPHA_LCA etc */
#include <linux/mc146818rtc.h>
#include <linux/console.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/string.h>

#ifdef CONFIG_RTC
#include <linux/timex.h>
#endif
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/hwrpb.h>
#include <asm/dma.h>
#include <asm/io.h>


#include "proto.h"

#if 1
# define DBG_SRM(args)         printk args
#else
# define DBG_SRM(args)
#endif

struct hwrpb_struct *hwrpb;
unsigned long srm_hae;

#ifdef CONFIG_ALPHA_GENERIC
struct alpha_machine_vector alpha_mv;
int alpha_using_srm, alpha_use_srm_setup;
#endif

unsigned char aux_device_present = 0xaa;

#define N(a) (sizeof(a)/sizeof(a[0]))

static unsigned long find_end_memory(void);
static struct alpha_machine_vector *get_sysvec(long, long, long);
static struct alpha_machine_vector *get_sysvec_byname(const char *);
static void get_sysnames(long, long, char **, char **);

/*
 * This is setup by the secondary bootstrap loader.  Because
 * the zero page is zeroed out as soon as the vm system is
 * initialized, we need to copy things out into a more permanent
 * place.
 */
#define PARAM			ZERO_PAGE
#define COMMAND_LINE		((char*)(PARAM + 0x0000))
#define COMMAND_LINE_SIZE	256
#define INITRD_START		(*(unsigned long *) (PARAM+0x100))
#define INITRD_SIZE		(*(unsigned long *) (PARAM+0x108))

static char command_line[COMMAND_LINE_SIZE];
char saved_command_line[COMMAND_LINE_SIZE];

/*
 * The format of "screen_info" is strange, and due to early
 * i386-setup code. This is just enough to make the console
 * code think we're on a VGA color display.
 */

struct screen_info screen_info = {
	orig_x: 0,
	orig_y: 25,
	orig_video_cols: 80,
	orig_video_lines: 25,
	orig_video_isVGA: 1,
	orig_video_points: 16
};

/*
 * Declare all of the machine vectors.
 */

extern struct alpha_machine_vector alcor_mv;
extern struct alpha_machine_vector alphabook1_mv;
extern struct alpha_machine_vector avanti_mv;
extern struct alpha_machine_vector cabriolet_mv;
extern struct alpha_machine_vector dp264_mv;
extern struct alpha_machine_vector eb164_mv;
extern struct alpha_machine_vector eb64p_mv;
extern struct alpha_machine_vector eb66_mv;
extern struct alpha_machine_vector eb66p_mv;
extern struct alpha_machine_vector jensen_mv;
extern struct alpha_machine_vector lx164_mv;
extern struct alpha_machine_vector miata_mv;
extern struct alpha_machine_vector mikasa_mv;
extern struct alpha_machine_vector mikasa_primo_mv;
extern struct alpha_machine_vector noname_mv;
extern struct alpha_machine_vector noritake_mv;
extern struct alpha_machine_vector noritake_primo_mv;
extern struct alpha_machine_vector p2k_mv;
extern struct alpha_machine_vector pc164_mv;
extern struct alpha_machine_vector rawhide_mv;
extern struct alpha_machine_vector ruffian_mv;
extern struct alpha_machine_vector sable_mv;
extern struct alpha_machine_vector sable_gamma_mv;
extern struct alpha_machine_vector sx164_mv;
extern struct alpha_machine_vector takara_mv;
extern struct alpha_machine_vector xl_mv;
extern struct alpha_machine_vector xlt_mv;


void __init
setup_arch(char **cmdline_p, unsigned long * memory_start_p,
	   unsigned long * memory_end_p)
{
	extern char _end[];

	struct alpha_machine_vector *vec = NULL;
	struct percpu_struct *cpu;
	char *type_name, *var_name, *p;

	hwrpb = (struct hwrpb_struct*)(IDENT_ADDR + INIT_HWRPB->phys_addr);

	/* 
	 * Locate the command line.
	 */

	/* Hack for Jensen... since we're restricted to 8 or 16 chars for
	   boot flags depending on the boot mode, we need some shorthand.
	   This should do for installation.  Later we'll add other
	   abbreviations as well... */
	if (strcmp(COMMAND_LINE, "INSTALL") == 0) {
		strcpy(command_line, "root=/dev/fd0 load_ramdisk=1");
	} else {
		strncpy(command_line, COMMAND_LINE, sizeof command_line);
		command_line[sizeof(command_line)-1] = 0;
	}
	strcpy(saved_command_line, command_line);
	*cmdline_p = command_line;

	/* 
	 * Process command-line arguments.
	 */

	for (p = strtok(command_line, " \t"); p ; p = strtok(NULL, " \t")) {
#ifndef alpha_use_srm_setup
		/* Allow a command-line option to respect the
		   SRM's configuration.  */
		if (strncmp(p, "srm_setup=", 10) == 0) {
			alpha_use_srm_setup = (p[10] != '0');
			continue;
		}
#endif

		if (strncmp(p, "alpha_mv=", 9) == 0) {
			vec = get_sysvec_byname(p+9);
			continue;
		}
	}

	/* Replace the command line, not that we've killed it with strtok.  */
	strcpy(command_line, saved_command_line);

	/*
	 * Indentify and reconfigure for the current system.
	 */

	get_sysnames(hwrpb->sys_type, hwrpb->sys_variation,
		     &type_name, &var_name);
	if (*var_name == '0')
		var_name = "";

	if (!vec) {
		cpu = (struct percpu_struct*)
			((char*)hwrpb + hwrpb->processor_offset);
		vec = get_sysvec(hwrpb->sys_type, hwrpb->sys_variation,
				 cpu->type);
	}

#ifdef CONFIG_ALPHA_GENERIC
	if (!vec) {
		panic("Unsupported system type: %s%s%s (%ld %ld)\n",
		      type_name, (*var_name ? " variation " : ""), var_name,
		      hwrpb->sys_type, hwrpb->sys_variation);
	}
	alpha_mv = *vec;

	/* Assume that we've booted from SRM if we havn't booted from MILO.
	   Detect the later by looking for "MILO" in the system serial nr.  */
	alpha_using_srm = strncmp((const char *)hwrpb->ssn, "MILO", 4) != 0;
#else
	/* Once we're sure we can reliably identify systems, we should
	   simply panic as we do above.  */
	if (vec != &alpha_mv) {
		printk("WARNING: Not configured for system type: %s%s%s "
		       "(%ld %ld)\nContinuing with trepidation...\n",
		       type_name, (*var_name ? " variation " : ""), var_name,
		       hwrpb->sys_type, hwrpb->sys_variation);
	}
#endif

	printk("Booting on %s%s%s using machine vector %s\n",
	       type_name, (*var_name ? " variation " : ""),
	       var_name, alpha_mv.vector_name);
	printk("Command line: %s\n", command_line);

	/* 
	 * Sync with the HAE
	 */

	/* Save the SRM's current value for restoration.  */
	srm_hae = *alpha_mv.hae_register;
	__set_hae(alpha_mv.hae_cache);

	/* Reset enable correctable error reports.  */
	wrmces(0x7);

	/* Find our memory.  */
	*memory_end_p = find_end_memory();
	*memory_start_p = (unsigned long) _end;

#ifdef CONFIG_BLK_DEV_INITRD
	initrd_start = INITRD_START;
	if (initrd_start) {
		initrd_end = initrd_start+INITRD_SIZE;
		printk("Initial ramdisk at: 0x%p (%lu bytes)\n",
		       (void *) initrd_start, INITRD_SIZE);

		if (initrd_end > *memory_end_p) {
			printk("initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       initrd_end, memory_end_p);
			initrd_start = initrd_end = 0;
		}
	}
#endif

	/* Initialize the machine.  Usually has to do with setting up
	   DMA windows and the like.  */
	if (alpha_mv.init_arch)
		alpha_mv.init_arch(memory_start_p, memory_end_p);

	/* 
	 * Give us a default console.  TGA users will see nothing until
	 * chr_dev_init is called, rather late in the boot sequence.
	 */

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif

	/* Default root filesystem to sda2.  */
	ROOT_DEV = to_kdev_t(0x0802);

 	/*
	 * Check ASN in HWRPB for validity, report if bad.
	 * FIXME: how was this failing?  Should we trust it instead,
	 * and copy the value into alpha_mv.max_asn?
 	 */

 	if (hwrpb->max_asn != MAX_ASN) {
		printk("Max ASN from HWRPB is bad (0x%lx)\n", hwrpb->max_asn);
 	}

	/*
	 * Identify the flock of penguins.
	 */

#ifdef __SMP__
	setup_smp();
#endif
}

static unsigned long __init
find_end_memory(void)
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

	/* Round it up to an even number of pages. */
	high = (high + PAGE_SIZE) & (PAGE_MASK*2);
	return PAGE_OFFSET + high;
}


static char sys_unknown[] = "Unknown";
static char systype_names[][16] = {
	"0",
	"ADU", "Cobra", "Ruby", "Flamingo", "Mannequin", "Jensen",
	"Pelican", "Morgan", "Sable", "Medulla", "Noname",
	"Turbolaser", "Avanti", "Mustang", "Alcor", "Tradewind",
	"Mikasa", "EB64", "EB66", "EB64+", "AlphaBook1",
	"Rawhide", "K2", "Lynx", "XL", "EB164", "Noritake",
	"Cortex", "29", "Miata", "XXM", "Takara", "Yukon",
	"Tsunami", "Wildfire", "CUSCO"
};

static char unofficial_names[][8] = {"100", "Ruffian"};

static char eb164_names[][8] = {"EB164", "PC164", "LX164", "SX164"};
static int eb164_indices[] = {0,0,0,1,1,1,1,1,2,2,2,2,3,3,3,3};

static char alcor_names[][16] = {"Alcor", "Maverick", "Bret"};
static int alcor_indices[] = {0,0,0,1,1,1,0,0,0,0,0,0,2,2,2,2,2,2};

static char eb64p_names[][16] = {"EB64+", "Cabriolet", "AlphaPCI64"};
static int eb64p_indices[] = {0,0,1,2};

static char eb66_names[][8] = {"EB66", "EB66+"};
static int eb66_indices[] = {0,0,1};

static char rawhide_names[][16] = {
	"Dodge", "Wrangler", "Durango", "Tincup", "DaVinci"
};
static int rawhide_indices[] = {0,0,0,1,1,2,2,3,3,4,4};


static struct alpha_machine_vector * __init
get_sysvec(long type, long variation, long cpu)
{
#ifdef CONFIG_ALPHA_GENERIC
	static struct alpha_machine_vector *systype_vecs[] __initlocaldata =
	{
		NULL,		/* 0 */
		NULL,		/* ADU */
		NULL,		/* Cobra */
		NULL,		/* Ruby */
		NULL,		/* Flamingo */
		NULL,		/* Mannequin */
		&jensen_mv,
		NULL, 		/* Pelican */
		NULL,		/* Morgan */
		NULL,		/* Sable -- see below.  */
		NULL,		/* Medulla */
		&noname_mv,
		NULL,		/* Turbolaser */
		&avanti_mv,
		NULL,		/* Mustang */
		&alcor_mv,	/* Alcor, Bret, Maverick.  */
		NULL,		/* Tradewind */
		NULL,		/* Mikasa -- see below.  */
		NULL,		/* EB64 */
		NULL,		/* EB66 -- see variation.  */
		NULL,		/* EB64+ -- see variation.  */
		&alphabook1_mv,
		&rawhide_mv,
		NULL,		/* K2 */
		NULL,		/* Lynx */
		&xl_mv,
		NULL,		/* EB164 -- see variation.  */
		NULL,		/* Noritake -- see below.  */
		NULL,		/* Cortex */
		NULL,		/* 29 */
		&miata_mv,
		NULL,		/* XXM */
		&takara_mv,
		NULL,		/* Yukon */
		&dp264_mv,
		NULL,		/* Wildfire */
		NULL,		/* CUSCO */
	};

	static struct alpha_machine_vector *unofficial_vecs[] __initlocaldata =
	{
		NULL,		/* 100 */
		&ruffian_mv,
	};

	static struct alpha_machine_vector *alcor_vecs[] __initlocaldata = 
	{
		&alcor_mv, &xlt_mv, &xlt_mv
	};

	static struct alpha_machine_vector *eb164_vecs[] __initlocaldata =
	{
		&eb164_mv, &pc164_mv, &lx164_mv, &sx164_mv
	};

	static struct alpha_machine_vector *eb64p_vecs[] __initlocaldata =
	{
		&eb64p_mv,
		&cabriolet_mv,
		NULL		/* AlphaPCI64 */
	};

	static struct alpha_machine_vector *eb66_vecs[] __initlocaldata =
	{
		&eb66_mv,
		&eb66p_mv
	};

	/* ??? Do we need to distinguish between Rawhides?  */

	struct alpha_machine_vector *vec;

	/* Restore real CABRIO and EB66+ family names, ie EB64+ and EB66 */
	if (type < 0)
		type = -type;

	/* Search the system tables first... */
	vec = NULL;
	if (type < N(systype_vecs)) {
		vec = systype_vecs[type];
	} else if ((type > ST_UNOFFICIAL_BIAS) &&
		   (type - ST_UNOFFICIAL_BIAS) < N(unofficial_vecs)) {
		vec = unofficial_vecs[type - ST_UNOFFICIAL_BIAS];
	}

	/* If we've not found one, try for a variation.  */

	if (!vec) {
		/* Member ID is a bit-field. */
		long member = (variation >> 10) & 0x3f;

		switch (type) {
		case ST_DEC_ALCOR:
			if (member < N(alcor_indices))
				vec = alcor_vecs[alcor_indices[member]];
			break;
		case ST_DEC_EB164:
			if (member < N(eb164_indices))
				vec = eb164_vecs[eb164_indices[member]];
			break;
		case ST_DEC_EB64P:
			if (member < N(eb64p_indices))
				vec = eb64p_vecs[eb64p_indices[member]];
			break;
		case ST_DEC_EB66:
			if (member < N(eb66_indices))
				vec = eb66_vecs[eb66_indices[member]];
			break;
		case ST_DEC_1000:
			cpu &= 0xffffffff;
			if (cpu == EV5_CPU || cpu == EV56_CPU)
				vec = &mikasa_primo_mv;
			else
				vec = &mikasa_mv;
			break;
		case ST_DEC_NORITAKE:
			cpu &= 0xffffffff;
			if (cpu == EV5_CPU || cpu == EV56_CPU)
				vec = &noritake_primo_mv;
			else
				vec = &noritake_mv;
			break;
		case ST_DEC_2100_A500:
			cpu &= 0xffffffff;
			if (cpu == EV5_CPU || cpu == EV56_CPU)
				vec = &sable_gamma_mv;
			else
				vec = &sable_mv;
			break;
		}
	}
	return vec;
#else
	/* TODO: verify that the system is of the type for which we
	   were configured.  For now, cop out and return success.  */
	return &alpha_mv;
#endif /* GENERIC */
}

static struct alpha_machine_vector * __init
get_sysvec_byname(const char *name)
{
#ifdef CONFIG_ALPHA_GENERIC
	static struct alpha_machine_vector *all_vecs[] __initlocaldata =
	{
		&alcor_mv,
		&alphabook1_mv,
		&avanti_mv,
		&cabriolet_mv,
		&dp264_mv,
		&eb164_mv,
		&eb64p_mv,
		&eb66_mv,
		&eb66p_mv,
		&jensen_mv,
		&lx164_mv,
		&miata_mv,
		&mikasa_mv,
		&mikasa_primo_mv,
		&noname_mv,
		&noritake_mv,
		&noritake_primo_mv,
		&p2k_mv,
		&pc164_mv,
		&rawhide_mv,
		&ruffian_mv,
		&sable_mv,
		&sable_gamma_mv,
		&sx164_mv,
		&takara_mv,
		&xl_mv,
		&xlt_mv
	};

	int i, n = sizeof(all_vecs)/sizeof(*all_vecs);
	for (i = 0; i < n; ++i) {
		struct alpha_machine_vector *mv = all_vecs[i];
		if (strcasecmp(mv->vector_name, name) == 0)
			return mv;
	}
	return NULL;
#else
	if (strcasecmp(alpha_mv.vector_name, name) == 0)
		return &alpha_mv;
	return NULL;
#endif
}

static void
get_sysnames(long type, long variation,
	     char **type_name, char **variation_name)
{
	long member;

	/* Restore real CABRIO and EB66+ family names, ie EB64+ and EB66 */
	if (type < 0)
		type = -type;

	/* If not in the tables, make it UNKNOWN,
	   else set type name to family */
	if (type < N(systype_names)) {
		*type_name = systype_names[type];
	} else if ((type > ST_UNOFFICIAL_BIAS) &&
		   (type - ST_UNOFFICIAL_BIAS) < N(unofficial_names)) {
		*type_name = unofficial_names[type - ST_UNOFFICIAL_BIAS];
	} else {
		*type_name = sys_unknown;
		*variation_name = sys_unknown;
		return;
	}

	/* Set variation to "0"; if variation is zero, done */
	*variation_name = systype_names[0];
	if (variation == 0) {
		return;
	}

	member = (variation >> 10) & 0x3f; /* member ID is a bit-field */

	switch (type) { /* select by family */
	default: /* default to variation "0" for now */
		break;
	case ST_DEC_EB164:
		if (member < N(eb164_indices))
			*variation_name = eb164_names[eb164_indices[member]];
		break;
	case ST_DEC_ALCOR:
		if (member < N(alcor_indices))
			*variation_name = alcor_names[alcor_indices[member]];
		break;
	case ST_DEC_EB64P:
		if (member < N(eb64p_indices))
			*variation_name = eb64p_names[eb64p_indices[member]];
		break;
	case ST_DEC_EB66:
		if (member < N(eb66_indices))
			*variation_name = eb66_names[eb66_indices[member]];
		break;
	case ST_DEC_RAWHIDE:
		if (member < N(rawhide_indices))
			*variation_name = rawhide_names[rawhide_indices[member]];
		break;
	}
}

/*
 * A change was made to the HWRPB via an ECO and the following code
 * tracks a part of the ECO.  In HWRPB versions less than 5, the ECO
 * was not implemented in the console firmware.  If it's revision 5 or
 * greater we can get the name of the platform as an ASCII string from
 * the HWRPB.  That's what this function does.  It checks the revision
 * level and if the string is in the HWRPB it returns the address of
 * the string--a pointer to the name of the platform.
 *
 * Returns:
 *      - Pointer to a ASCII string if it's in the HWRPB
 *      - Pointer to a blank string if the data is not in the HWRPB.
 */

static char *
platform_string(void)
{
	struct dsr_struct *dsr;
	static char unk_system_string[] = "N/A";

	/* Go to the console for the string pointer.
	 * If the rpb_vers is not 5 or greater the rpb
	 * is old and does not have this data in it.
	 */
	if (hwrpb->revision < 5)
		return (unk_system_string);
	else {
		/* The Dynamic System Recognition struct
		 * has the system platform name starting
		 * after the character count of the string.
		 */
		dsr =  ((struct dsr_struct *)
			((char *)hwrpb + hwrpb->dsr_offset));
		return ((char *)dsr + (dsr->sysname_off +
				       sizeof(long)));
	}
}

/*
 * BUFFER is PAGE_SIZE bytes long.
 */
int get_cpuinfo(char *buffer)
{
	extern struct unaligned_stat {
		unsigned long count, va, pc;
	} unaligned[2];

	static char cpu_names[][8] = {
		"EV3", "EV4", "Unknown", "LCA4", "EV5", "EV45", "EV56",
		"EV6", "PCA56", "PCA57"
	};

	struct percpu_struct *cpu;
	unsigned int cpu_index;
	char *cpu_name;
	char *systype_name;
	char *sysvariation_name;
	int len;

	cpu = (struct percpu_struct*)((char*)hwrpb + hwrpb->processor_offset);
	cpu_index = (unsigned) (cpu->type - 1);
	cpu_name = "Unknown";
	if (cpu_index < N(cpu_names))
		cpu_name = cpu_names[cpu_index];

	get_sysnames(hwrpb->sys_type, hwrpb->sys_variation,
		     &systype_name, &sysvariation_name);

	len = sprintf(buffer,
		      "cpu\t\t\t: Alpha\n"
		      "cpu model\t\t: %s\n"
		      "cpu variation\t\t: %ld\n"
		      "cpu revision\t\t: %ld\n"
		      "cpu serial number\t: %s\n"
		      "system type\t\t: %s\n"
		      "system variation\t: %s\n"
		      "system revision\t\t: %ld\n"
		      "system serial number\t: %s\n"
		      "cycle frequency [Hz]\t: %lu %s\n"
		      "timer frequency [Hz]\t: %lu.%02lu\n"
		      "page size [bytes]\t: %ld\n"
		      "phys. address bits\t: %ld\n"
		      "max. addr. space #\t: %ld\n"
		      "BogoMIPS\t\t: %lu.%02lu\n"
		      "kernel unaligned acc\t: %ld (pc=%lx,va=%lx)\n"
		      "user unaligned acc\t: %ld (pc=%lx,va=%lx)\n"
		      "platform string\t\t: %s\n",
		       cpu_name, cpu->variation, cpu->revision,
		       (char*)cpu->serial_no,
		       systype_name, sysvariation_name, hwrpb->sys_revision,
		       (char*)hwrpb->ssn,
		       hwrpb->cycle_freq ? : est_cycle_freq,
		       hwrpb->cycle_freq ? "" : "est.",
		       hwrpb->intr_freq / 4096,
		       (100 * hwrpb->intr_freq / 4096) % 100,
		       hwrpb->pagesize,
		       hwrpb->pa_bits,
		       hwrpb->max_asn,
		       loops_per_sec / 500000, (loops_per_sec / 5000) % 100,
		       unaligned[0].count, unaligned[0].pc, unaligned[0].va,
		       unaligned[1].count, unaligned[1].pc, unaligned[1].va,
		       platform_string());

#ifdef __SMP__
	len += smp_info(buffer+len);
#endif

	return len;
}
