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
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/config.h>	/* CONFIG_ALPHA_LCA etc */
#include <linux/ioport.h>
#include <linux/mc146818rtc.h>
#include <linux/console.h>

#ifdef CONFIG_RTC
#include <linux/timex.h>
#endif

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/hwrpb.h>
#include <asm/dma.h>
#include <asm/io.h>

extern void setup_smp(void);
extern char *smp_info(void);

#if 1
# define DBG_SRM(args)         printk args
#else
# define DBG_SRM(args)
#endif

struct hae hae = {
	0,
	(unsigned long*) HAE_ADDRESS
};

#ifdef CONFIG_ALPHA_SRM_SETUP
unsigned long srm_hae;
#endif

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
#if defined(CONFIG_ALPHA_BOOK1)
  /* the AlphaBook1 has LCD video fixed at 800x600, 37 rows and 100 cols */
	0, 37,			/* orig-x, orig-y */
	{ 0, 0 },		/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
        100,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
        37,			/* orig-video-lines */
	1,			/* orig-video-isVGA */
	16			/* orig-video-points */
#else
	0, 25,			/* orig-x, orig-y */
	{ 0, 0 },		/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	80,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
	25,			/* orig-video-lines */
	1,			/* orig-video-isVGA */
	16			/* orig-video-points */
#endif
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

#ifdef CONFIG_RTC /* setup interval timer if /dev/rtc is being used */
    outb(0x34, 0x43);  /* binary, mode 2, LSB/MSB, ch 0 */
    outb(LATCH & 0xff, 0x40); /* LSB */
    outb(LATCH >> 8, 0x40); /* MSB */
    request_region(0x40, 0x20, "timer"); /* reserve pit */
#else /* RTC */
#if !defined(CONFIG_ALPHA_RUFFIAN)
    /* Ruffian depends on the system timer established in MILO!! */
    outb(0x36, 0x43);	/* counter 0: system timer */
    outb(0x00, 0x40);
    outb(0x00, 0x40);
#endif /* RUFFIAN */
    request_region(0x70, 0x10, "timer"); /* reserve rtc */
#endif /* RTC */

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

	if ((CMOS_READ(RTC_FREQ_SELECT) & 0x3f) != 0x26) {
	  printk("setup_arch: setting RTC_FREQ to 1024/sec\n");
	  CMOS_WRITE(0x26, RTC_FREQ_SELECT);
	}

	hwrpb = (struct hwrpb_struct*)(IDENT_ADDR + INIT_HWRPB->phys_addr);

#if !defined(CONFIG_ALPHA_TSUNAMI)
#ifdef CONFIG_ALPHA_SRM_SETUP
	srm_hae = *hae.reg; /* save SRM setting for restoration */
	DBG_SRM(("setup_arch: old HAE base: 0x%016lx\n", srm_hae));
#endif /* SRM_SETUP */
	set_hae(hae.cache);	/* sync HAE register w/hae_cache */
#endif /* !TSUNAMI */

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
#elif defined(CONFIG_ALPHA_PYXIS)
	*memory_start_p = pyxis_init(*memory_start_p, *memory_end_p);
#elif defined(CONFIG_ALPHA_T2)
	*memory_start_p = t2_init(*memory_start_p, *memory_end_p);
#elif defined(CONFIG_ALPHA_TSUNAMI)
	*memory_start_p = tsunami_init(*memory_start_p, *memory_end_p);
#elif defined(CONFIG_ALPHA_MCPCIA)
	*memory_start_p = mcpcia_init(*memory_start_p, *memory_end_p);
#endif

#ifdef __SMP__
	setup_smp();
#endif

#ifdef CONFIG_VGA_CONSOLE
	conswitchp = &vga_con;
#endif
#ifdef CONFIG_FB
	/* Frame buffer device based console */
	conswitchp = &fb_con;
#endif
}


#define N(a) (sizeof(a)/sizeof(a[0]))

/* A change was made to the HWRPB via an ECO and the following code tracks
 * a part of the ECO.  In HWRPB versions less than 5, the ECO was not
 * implemented in the console firmware.  If it's revision 5 or greater we can
 * get the name of the platform as an ASCII string from the HWRPB.  That's what
 * this function does.  It checks the revision level and if the string is in
 * the HWRPB it returns the address of the string--a pointer to the name of the
 * platform.
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

static void
get_sysnames(long type, long variation,
	     char **type_name, char **variation_name)
{
	static char *sys_unknown = "Unknown";
	static char *systype_names[] = {
		"0",
		"ADU", "Cobra", "Ruby", "Flamingo", "Mannequin", "Jensen",
		"Pelican", "Morgan", "Sable", "Medulla", "Noname",
		"Turbolaser", "Avanti", "Mustang", "Alcor", "Tradewind",
		"Mikasa", "EB64", "EB66", "EB64+", "AlphaBook1",
		"Rawhide", "K2", "Lynx", "XL", "EB164", "Noritake",
		"Cortex", "29", "Miata", "XXM", "Takara", "Yukon",
		"Tsunami", "Wildfire", "CUSCO"
	};

	static char *unofficial_names[] = {"100", "Ruffian"};

	static char * eb164_names[] = {"EB164", "PC164", "LX164", "SX164"};
	static int eb164_indices[] = {0,0,0,1,1,1,1,1,2,2,2,2,3,3,3,3};

	static char * alcor_names[] = {"Alcor", "Maverick", "Bret"};
	static int alcor_indices[] = {0,0,0,1,1,1,0,0,0,0,0,0,2,2,2,2,2,2};

	static char * eb64p_names[] = {"EB64+", "Cabriolet", "AlphaPCI64"};
	static int eb64p_indices[] = {0,0,1.2};

	static char * eb66_names[] = {"EB66", "EB66+"};
	static int eb66_indices[] = {0,0,1};

	static char * rawhide_names[] = {"Dodge", "Wrangler", "Durango",
					 "Tincup", "DaVinci"};
	static int rawhide_indices[] = {0,0,0,1,1,2,2,3,3,4,4};

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
	} /* end family switch */
}

/*
 * BUFFER is PAGE_SIZE bytes long.
 */
int get_cpuinfo(char *buffer)
{
	static char *cpu_names[] = {
		"EV3", "EV4", "Unknown", "LCA4", "EV5", "EV45", "EV56",
		"EV6", "PCA56", "PCA57"
	};

	extern struct unaligned_stat {
		unsigned long count, va, pc;
	} unaligned[2];

	struct percpu_struct *cpu;
	unsigned int cpu_index;
	char *cpu_name;
	char *systype_name;
	char *sysvariation_name;

	cpu = (struct percpu_struct*)((char*)hwrpb + hwrpb->processor_offset);
	cpu_index = (unsigned) (cpu->type - 1);
	cpu_name = "Unknown";
	if (cpu_index < N(cpu_names))
		cpu_name = cpu_names[cpu_index];

	get_sysnames(hwrpb->sys_type, hwrpb->sys_variation,
		     &systype_name, &sysvariation_name);

	return sprintf(buffer,
		       "cpu\t\t\t: Alpha\n"
		       "cpu model\t\t: %s\n"
		       "cpu variation\t\t: %ld\n"
		       "cpu revision\t\t: %ld\n"
		       "cpu serial number\t: %s\n"
		       "system type\t\t: %s\n"
		       "system variation\t: %s\n"
		       "system revision\t\t: %ld\n"
		       "system serial number\t: %s\n"
		       "cycle frequency [Hz]\t: %lu\n"
		       "timer frequency [Hz]\t: %lu.%02lu\n"
		       "page size [bytes]\t: %ld\n"
		       "phys. address bits\t: %ld\n"
		       "max. addr. space #\t: %ld\n"
		       "BogoMIPS\t\t: %lu.%02lu\n"
		       "kernel unaligned acc\t: %ld (pc=%lx,va=%lx)\n"
		       "user unaligned acc\t: %ld (pc=%lx,va=%lx)\n"
		       "platform string\t\t: %s\n"
#ifdef __SMP__
		       "%s"
#endif
		       ,

		       cpu_name, cpu->variation, cpu->revision,
		       (char*)cpu->serial_no,
		       systype_name, sysvariation_name, hwrpb->sys_revision,
		       (char*)hwrpb->ssn,
		       hwrpb->cycle_freq,
		       hwrpb->intr_freq / 4096,
		       (100 * hwrpb->intr_freq / 4096) % 100,
		       hwrpb->pagesize,
		       hwrpb->pa_bits,
		       hwrpb->max_asn,
		       loops_per_sec / 500000, (loops_per_sec / 5000) % 100,
		       unaligned[0].count, unaligned[0].pc, unaligned[0].va,
		       unaligned[1].count, unaligned[1].pc, unaligned[1].va,
		       platform_string()
#ifdef __SMP__
		       , smp_info()
#endif
		       );
}
