/*
 *  linux/arch/m68k/mac/config.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * Miscellaneous linux stuff
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/interrupt.h>
/* keyb */
#include <linux/random.h>
#include <linux/delay.h>
/* keyb */
#include <linux/init.h>

#define BOOTINFO_COMPAT_1_0
#include <asm/setup.h>
#include <asm/bootinfo.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>

#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/machw.h>

#include <asm/mac_iop.h>
#include <asm/mac_via.h>
#include <asm/mac_oss.h>
#include <asm/mac_psc.h>

/* Offset between Unix time (1970-based) and Mac time (1904-based) */

#define MAC_TIME_OFFSET 2082844800

/*
 * hardware reset vector
 */

static void (*rom_reset)(void);

/* Mac bootinfo struct */

struct mac_booter_data mac_bi_data = {0,};
int mac_bisize = sizeof mac_bi_data;

/* New m68k bootinfo stuff and videobase */

extern int m68k_num_memory;
extern struct mem_info m68k_memory[NUM_MEMINFO];

extern struct mem_info m68k_ramdisk;

extern char m68k_command_line[CL_SIZE];

void *mac_env;		/* Loaded by the boot asm */

/* The phys. video addr. - might be bogus on some machines */
unsigned long mac_orig_videoaddr;

/* Mac specific keyboard functions */
extern int mackbd_init_hw(void);
extern void mackbd_leds(unsigned int leds);

/* Mac specific timer functions */
extern unsigned long mac_gettimeoffset (void);
static void mac_gettod (int *, int *, int *, int *, int *, int *);
static int mac_hwclk (int, struct hwclk_time *);
static int mac_set_clock_mmss (unsigned long);
extern void iop_preinit(void);
extern void iop_init(void);
extern void via_init(void);
extern void via_init_clock(void (*func)(int, void *, struct pt_regs *));
extern void via_flush_cache(void);
extern void oss_init(void);
extern void psc_init(void);

extern void (*kd_mksound)(unsigned int, unsigned int);
extern void mac_mksound(unsigned int, unsigned int);
extern int mac_floppy_init(void);
extern void mac_floppy_setup(char *,int *);

extern void nubus_sweep_video(void);

/* Mac specific debug functions (in debug.c) */
extern void mac_debug_init(void);
extern void mac_debugging_long(int, long);

/* poweroff functions */
extern void via_poweroff(void);
extern void oss_poweroff(void);
extern void adb_poweroff(void);
extern void adb_hwreset(void);

/* pram functions */
extern __u32 via_read_time(void);
extern void via_write_time(__u32);
extern __u32 adb_read_time(void);
extern void adb_write_time(__u32);

#ifdef CONFIG_MAGIC_SYSRQ
static char mac_sysrq_xlate[128] =
	"\000sdfghzxcv\000bqwer"				/* 0x00 - 0x0f */
	"yt123465=97-80)o"					/* 0x10 - 0x1f */
	"u(ip\rlj'k;\\,/nm."					/* 0x20 - 0x2f */
	"\t `\000\033\000\000\000\000\000\000\000\000\000\000\000"	/* 0x30 - 0x3f */
	"\000.\000*\000+\000\000\000\000\000/\r\000-\000"	/* 0x40 - 0x4f */
	"\000\00001234567a89\000\000\000"			/* 0x50 - 0x5f */
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"  /* 0x60 - 0x6f */
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"; /* 0x70 - 0x7f */
#endif

extern void (*kd_mksound)(unsigned int, unsigned int);

static void mac_get_model(char *str);

void mac_bang(int irq, void *vector, struct pt_regs *p)
{
	printk("Resetting ...\n");
	mac_reset();
}

static void mac_sched_init(void (*vector)(int, void *, struct pt_regs *))
{
	via_init_clock(vector);
}

extern int console_loglevel;

/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines were long is 32-bit! (However, as time_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)
 */
static unsigned long mktime(unsigned int year, unsigned int mon,
	unsigned int day, unsigned int hour,
	unsigned int min, unsigned int sec)
{
	if (0 >= (int) (mon -= 2)) {	/* 1..12 -> 11,12,1..10 */
		mon += 12;	/* Puts Feb last since it has leap day */
		year -= 1;
	}
	return (((
	    (unsigned long)(year/4 - year/100 + year/400 + 367*mon/12 + day) +
	      year*365 - 719499
	    )*24 + hour /* now have hours */
	   )*60 + min /* now have minutes */
	  )*60 + sec; /* finally seconds */
}

/*
 * This function translates seconds since 1970 into a proper date.
 *
 * Algorithm cribbed from glibc2.1, __offtime().
 */
#define SECS_PER_MINUTE (60)
#define SECS_PER_HOUR  (SECS_PER_MINUTE * 60)
#define SECS_PER_DAY   (SECS_PER_HOUR * 24)

static void unmktime(unsigned long time, long offset,
		     int *yearp, int *monp, int *dayp,
		     int *hourp, int *minp, int *secp)
{
        /* How many days come before each month (0-12).  */
	static const unsigned short int __mon_yday[2][13] =
	{
		/* Normal years.  */
		{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
		/* Leap years.  */
		{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
	};
	long int days, rem, y, wday, yday;
	const unsigned short int *ip;

	days = time / SECS_PER_DAY;
	rem = time % SECS_PER_DAY;
	rem += offset;
	while (rem < 0) {
		rem += SECS_PER_DAY;
		--days;
	}
	while (rem >= SECS_PER_DAY) {
		rem -= SECS_PER_DAY;
		++days;
	}
	*hourp = rem / SECS_PER_HOUR;
	rem %= SECS_PER_HOUR;
	*minp = rem / SECS_PER_MINUTE;
	*secp = rem % SECS_PER_MINUTE;
	/* January 1, 1970 was a Thursday. */
	wday = (4 + days) % 7; /* Day in the week. Not currently used */
	if (wday < 0) wday += 7;
	y = 1970;

#define DIV(a, b) ((a) / (b) - ((a) % (b) < 0))
#define LEAPS_THRU_END_OF(y) (DIV (y, 4) - DIV (y, 100) + DIV (y, 400))
#define __isleap(year)	\
  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))

	while (days < 0 || days >= (__isleap (y) ? 366 : 365))
	{
		/* Guess a corrected year, assuming 365 days per year.  */
		long int yg = y + days / 365 - (days % 365 < 0);

		/* Adjust DAYS and Y to match the guessed year.  */
		days -= ((yg - y) * 365
			 + LEAPS_THRU_END_OF (yg - 1)
			 - LEAPS_THRU_END_OF (y - 1));
		y = yg;
	}
	*yearp = y - 1900;
	yday = days; /* day in the year.  Not currently used. */
	ip = __mon_yday[__isleap(y)];
	for (y = 11; days < (long int) ip[y]; --y)
		continue;
	days -= ip[y];
	*monp = y;
	*dayp = days + 1; /* day in the month */
	return;
}

/*
 * Return the boot time for use in initializing the kernel clock.
 *
 * I'd like to read the hardware clock here but many machines read
 * the PRAM through ADB, and interrupts aren't initialized when this
 * is called so ADB obviously won't work.
 */

static void mac_gettod(int *yearp, int *monp, int *dayp,
		       int *hourp, int *minp, int *secp)
{
	/* Yes the GMT bias is backwards.  It looks like Penguin is
           screwing up the boottime it gives us... This works for me
           in Canada/Eastern but it might be wrong everywhere else. */
	unmktime(mac_bi_data.boottime, -mac_bi_data.gmtbias * 60,
		yearp, monp, dayp, hourp, minp, secp);
	/* For some reason this is off by one */
	*monp = *monp + 1;
}

/* 
 * Read/write the hardware clock.
 */

static int mac_hwclk(int op, struct hwclk_time *t)
{
	unsigned long now;

	if (!op) { /* read */
		if (macintosh_config->adb_type == MAC_ADB_II) {
			now = via_read_time();
		} else if ((macintosh_config->adb_type == MAC_ADB_IISI) ||
			   (macintosh_config->adb_type == MAC_ADB_CUDA)) {
			now = adb_read_time();
		} else if (macintosh_config->adb_type == MAC_ADB_IOP) {
			now = via_read_time();
		} else {
			now = MAC_TIME_OFFSET;
		}

		now -= MAC_TIME_OFFSET;

		t->wday = 0;
		unmktime(now, 0,
			 &t->year, &t->mon, &t->day,
			 &t->hour, &t->min, &t->sec);
	} else { /* write */
		now = mktime(t->year + 1900, t->mon + 1, t->day,
			     t->hour, t->min, t->sec) + MAC_TIME_OFFSET;

		if (macintosh_config->adb_type == MAC_ADB_II) {
			via_write_time(now);
		} else if ((macintosh_config->adb_type == MAC_ADB_IISI) ||
			   (macintosh_config->adb_type == MAC_ADB_CUDA)) {
			adb_write_time(now);
		} else if (macintosh_config->adb_type == MAC_ADB_IOP) {
			via_write_time(now);
		}
	}
	return 0;
}

/*
 * Set minutes/seconds in the hardware clock
 */

static int mac_set_clock_mmss (unsigned long nowtime)
{
	struct hwclk_time now;

	mac_hwclk(0, &now);
	now.sec = nowtime % 60;
	now.min = (nowtime / 60) % 60;
	mac_hwclk(1, &now);

	return 0;
}

#if 0
void mac_waitbut (void)
{
	;
}
#endif

extern struct consw fb_con;
extern struct fb_info *mac_fb_init(long *);

    /*
     *  Parse a Macintosh-specific record in the bootinfo
     */

int __init mac_parse_bootinfo(const struct bi_record *record)
{
    int unknown = 0;
    const u_long *data = record->data;

    switch (record->tag) {
	case BI_MAC_MODEL:
	    mac_bi_data.id = *data;
	    break;
	case BI_MAC_VADDR:
	    mac_bi_data.videoaddr = *data;
	    break;
	case BI_MAC_VDEPTH:
	    mac_bi_data.videodepth = *data;
	    break;
	case BI_MAC_VROW:
	    mac_bi_data.videorow = *data;
	    break;
	case BI_MAC_VDIM:
	    mac_bi_data.dimensions = *data;
	    break;
	case BI_MAC_VLOGICAL:
	    mac_bi_data.videological = VIDEOMEMBASE + (*data & ~VIDEOMEMMASK);
	    mac_orig_videoaddr = *data;
	    break;
	case BI_MAC_SCCBASE:
	    mac_bi_data.sccbase = *data;
	    break;
	case BI_MAC_BTIME:
	    mac_bi_data.boottime = *data;
	    break;
	case BI_MAC_GMTBIAS:
	    mac_bi_data.gmtbias = *data;
	    break;
	case BI_MAC_MEMSIZE:
	    mac_bi_data.memsize = *data;
	    break;
	case BI_MAC_CPUID:
	    mac_bi_data.cpuid = *data;
	    break;
        case BI_MAC_ROMBASE:
	    mac_bi_data.rombase = *data;
	    break;
	default:
	    unknown = 1;
    }
    return(unknown);
}

/*
 *	Flip into 24bit mode for an instant - flushes the L2 cache card. We
 *	have to disable interrupts for this. Our IRQ handlers will crap 
 *	themselves if they take an IRQ in 24bit mode!
 */

static void mac_cache_card_flush(int writeback)
{
	unsigned long cpu_flags;
	save_flags(cpu_flags);
	cli();
	via_flush_cache();
	restore_flags(cpu_flags);
}

void __init config_mac(void)
{

    if (!MACH_IS_MAC) {
      printk("ERROR: no Mac, but config_mac() called!! \n");
    }
    
    mach_sched_init      = mac_sched_init;
    mach_keyb_init       = mackbd_init_hw;
    mach_kbd_leds        = mackbd_leds;
    mach_init_IRQ        = mac_init_IRQ;
    mach_request_irq     = mac_request_irq;
    mach_free_irq        = mac_free_irq;
    enable_irq           = mac_enable_irq;
    disable_irq          = mac_disable_irq;
    mach_get_model	 = mac_get_model;
    mach_gettimeoffset   = mac_gettimeoffset;
    mach_gettod          = mac_gettod;
    mach_hwclk           = mac_hwclk;
    mach_set_clock_mmss	 = mac_set_clock_mmss;
#if 0
    mach_mksound         = mac_mksound;
#endif
    mach_reset           = mac_reset;
    mach_halt            = mac_poweroff;
    mach_power_off       = mac_poweroff;
    conswitchp	         = &dummy_con;
    mach_max_dma_address = 0xffffffff;
#if 0
    mach_debug_init	 = mac_debug_init;
#endif
    kd_mksound		 = mac_mksound;
#ifdef CONFIG_MAGIC_SYSRQ
    mach_sysrq_key = 114;         /* HELP */
    mach_sysrq_shift_state = 8;   /* Alt */
    mach_sysrq_shift_mask = 0xff; /* all modifiers except CapsLock */
    mach_sysrq_xlate = mac_sysrq_xlate;
#endif
#ifdef CONFIG_HEARTBEAT
#if 0
    mach_heartbeat = mac_heartbeat;
    mach_heartbeat_irq = IRQ_MAC_TIMER;
#endif
#endif

    /*
     * Determine hardware present
     */
     
    mac_identify();
    mac_report_hardware();
    
    /* AFAIK only the IIci takes a cache card.  The IIfx has onboard
       cache ... someone needs to figure out how to tell if it's on or
       not. */
    if (macintosh_config->ident == MAC_MODEL_IICI
	|| macintosh_config->ident == MAC_MODEL_IIFX) {
    	mach_l2_flush = mac_cache_card_flush;
    }
#ifdef MAC_DEBUG_SOUND
    /* goes on forever if timers broken */
    mac_mksound(1000,10);
#endif

    /*
     * Check for machine specific fixups.
     */

#ifdef OLD_NUBUS_CODE
    nubus_sweep_video();
#endif
}	


/*
 *	Macintosh Table: hardcoded model configuration data. 
 *
 *	Much of this was defined by Alan, based on who knows what docs. 
 *	I've added a lot more, and some of that was pure guesswork based 
 *	on hardware pages present on the Mac web site. Possibly wildly 
 *	inaccurate, so look here if a new Mac model won't run. Example: if
 *	a Mac crashes immediately after the VIA1 registers have been dumped
 *	to the screen, it probably died attempting to read DirB on a RBV. 
 *	Meaning it should have MAC_VIA_IIci here :-)
 */
 
struct mac_model *macintosh_config;

static struct mac_model mac_data_table[]=
{
	/*
	 *	The default machine, in case we get an unsupported one
	 *	We'll pretend to be a Macintosh II, that's pretty safe.
	 */

	{	MAC_MODEL_II,	"Unknown",	MAC_ADB_II,	MAC_VIA_II,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},

	/*
	 *	Original MacII hardware
	 *	
	 */
	 
	{	MAC_MODEL_II,	"II",		MAC_ADB_II,	MAC_VIA_II,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_IIX,	"IIx",		MAC_ADB_II,	MAC_VIA_II,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_IICX,	"IIcx",		MAC_ADB_II,	MAC_VIA_II,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_SE30, "SE/30",	MAC_ADB_II,	MAC_VIA_II,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	
	/*
	 *	Weirdified MacII hardware - all subtley different. Gee thanks
	 *	Apple. All these boxes seem to have VIA2 in a different place to
	 *	the MacII (+1A000 rather than +4000)
	 *
	 *	The IIfx apparently has different ADB hardware, and stuff
	 *	so zany nobody knows how to drive it.
	 *	Even so, with Marten's help we'll try to deal with it :-)
	 * CSA: see http://developer.apple.com/technotes/hw/hw_09.html
	 */

	{	MAC_MODEL_IICI,	"IIci",	MAC_ADB_II,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_IIFX,	"IIfx",	MAC_ADB_IOP,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_IOP,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_IISI, "IIsi",	MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_IIVI,	"IIvi",	MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_IIVX,	"IIvx",	MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	
	/*
	 *	Classic models (guessing: similar to SE/30 ?? Nope, similar to LC ...)
	 */

	{	MAC_MODEL_CLII, "Classic II",		MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,     MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_CCL,  "Color Classic",	MAC_ADB_CUDA,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,     MAC_ETHER_NONE,	MAC_NUBUS},

	/*
	 *	Some Mac LC machines. Basically the same as the IIci, ADB like IIsi
	 */
	
	{	MAC_MODEL_LC,	"LC",	  MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_LCII,	"LC II",  MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_LCIII,"LC III", MAC_ADB_IISI,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},

	/*
	 *	Quadra. Video is at 0xF9000000, via is like a MacII. We label it differently 
	 *	as some of the stuff connected to VIA2 seems different. Better SCSI chip and 
	 *	onboard ethernet using a NatSemi SONIC except the 660AV and 840AV which use an 
	 *	AMD 79C940 (MACE).
	 *	The 700, 900 and 950 have some I/O chips in the wrong place to
	 *	confuse us. The 840AV has a SCSI location of its own (same as
	 *	the 660AV).
	 */	 
	 
	{	MAC_MODEL_Q605, "Quadra 605", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE,   MAC_SCC_QUADRA,	MAC_ETHER_NONE,		MAC_NUBUS},
	{	MAC_MODEL_Q610, "Quadra 610", MAC_ADB_II,   MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE,   MAC_SCC_QUADRA,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_Q630, "Quadra 630", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_QUADRA, MAC_SCC_QUADRA,	MAC_ETHER_SONIC,	MAC_NUBUS},
 	{	MAC_MODEL_Q650, "Quadra 650", MAC_ADB_II,   MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE,   MAC_SCC_QUADRA,	MAC_ETHER_SONIC,	MAC_NUBUS},
	/*	The Q700 does have a NS Sonic */
	{	MAC_MODEL_Q700, "Quadra 700", MAC_ADB_II,   MAC_VIA_QUADRA, MAC_SCSI_QUADRA2, MAC_IDE_NONE,   MAC_SCC_QUADRA2,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_Q800, "Quadra 800", MAC_ADB_II,   MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE,   MAC_SCC_QUADRA,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_Q840, "Quadra 840AV", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA3, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_MACE,		MAC_NUBUS},
	{	MAC_MODEL_Q900, "Quadra 900", MAC_ADB_IOP, MAC_VIA_QUADRA, MAC_SCSI_QUADRA2, MAC_IDE_NONE,   MAC_SCC_IOP,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_Q950, "Quadra 950", MAC_ADB_IOP, MAC_VIA_QUADRA, MAC_SCSI_QUADRA2, MAC_IDE_NONE,   MAC_SCC_IOP,	MAC_ETHER_SONIC,	MAC_NUBUS},

	/* 
	 *	Performa - more LC type machines
	 */

	{	MAC_MODEL_P460,  "Performa 460", MAC_ADB_IISI, MAC_VIA_IIci,   MAC_SCSI_OLD, 	MAC_IDE_NONE,   MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_P475,  "Performa 475", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE,   MAC_SCC_II,	MAC_ETHER_NONE, MAC_NUBUS},
	{	MAC_MODEL_P475F, "Performa 475", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE,   MAC_SCC_II,	MAC_ETHER_NONE, MAC_NUBUS},
	{	MAC_MODEL_P520,  "Performa 520", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE,   MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},

	{	MAC_MODEL_P550,  "Performa 550", MAC_ADB_CUDA, MAC_VIA_IIci,   MAC_SCSI_OLD,    MAC_IDE_NONE,   MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_P575,  "Performa 575", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE,   MAC_SCC_II,	MAC_ETHER_NONE, MAC_NUBUS},
	/* These have the comm slot, and therefore the possibility of SONIC ethernet */
	{	MAC_MODEL_P588,  "Performa 588", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_QUADRA, MAC_SCC_II,	MAC_ETHER_SONIC, MAC_NUBUS},
	{	MAC_MODEL_TV,    "TV",           MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_OLD,	MAC_IDE_NONE,   MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_P600,  "Performa 600", MAC_ADB_IISI, MAC_VIA_IIci,   MAC_SCSI_OLD,	MAC_IDE_NONE,   MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},

	/*
	 *	Centris - just guessing again; maybe like Quadra
	 */

	/* The C610 may or may not have SONIC.  We probe to make sure */
	{	MAC_MODEL_C610, "Centris 610",   MAC_ADB_II,   MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_C650, "Centris 650",   MAC_ADB_II,   MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_C660, "Centris 660AV", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA3, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_MACE,		MAC_NUBUS},

	/*
	 *      Power books - seem similar to early Quadras ? (most have 030 though)
	 */

	{	MAC_MODEL_PB140,  "PowerBook 140",   MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_OLD,  MAC_IDE_NONE,	MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB145,  "PowerBook 145",   MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_NONE, MAC_IDE_NONE,	MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	/*	The PB150 has IDE, and IIci style VIA */
	{	MAC_MODEL_PB150,  "PowerBook 150",   MAC_ADB_PB1, MAC_VIA_IIci,   MAC_SCSI_NONE, MAC_IDE_PB,	MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB160,  "PowerBook 160",   MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_NONE, MAC_IDE_NONE,	MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB165,  "PowerBook 165",   MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_NONE, MAC_IDE_NONE,	MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB165C, "PowerBook 165c",  MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_NONE, MAC_IDE_NONE,	MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB170,  "PowerBook 170",   MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_OLD,  MAC_IDE_NONE,	MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB180,  "PowerBook 180",   MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_NONE, MAC_IDE_NONE,	MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB180C, "PowerBook 180c",  MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_NONE, MAC_IDE_NONE,	MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB190,  "PowerBook 190cs", MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_NONE, MAC_IDE_PB,	MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	/* These have onboard SONIC */
	{	MAC_MODEL_PB520,  "PowerBook 520",   MAC_ADB_PB2, MAC_VIA_QUADRA, MAC_SCSI_NONE, MAC_IDE_NONE,	MAC_SCC_QUADRA,	MAC_ETHER_SONIC, MAC_NUBUS},

	/*
	 *      Power book Duos - similar to Power books, I hope
	 */

	/* All of these might have onboard SONIC in the Dock but I'm not quite sure */
	{	MAC_MODEL_PB210,  "PowerBook Duo 210",  MAC_ADB_PB2,  MAC_VIA_IIci, MAC_SCSI_OLD, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB230,  "PowerBook Duo 230",  MAC_ADB_PB2,  MAC_VIA_IIci, MAC_SCSI_OLD, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB250,  "PowerBook Duo 250",  MAC_ADB_PB2,  MAC_VIA_IIci, MAC_SCSI_OLD, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB270C, "PowerBook Duo 270c", MAC_ADB_PB2,  MAC_VIA_IIci, MAC_SCSI_OLD, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB280,  "PowerBook Duo 280",  MAC_ADB_PB2,  MAC_VIA_IIci, MAC_SCSI_OLD, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB280C, "PowerBook Duo 280c", MAC_ADB_PB2,  MAC_VIA_IIci, MAC_SCSI_OLD, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},

	/*
	 *	Other stuff ??
	 */
	{	-1, NULL, 0,0,0,}
};

void mac_identify(void)
{
	struct mac_model *m;

	/* Penguin data useful? */	
	int model = mac_bi_data.id;
	if (!model) {
		/* no bootinfo model id -> NetBSD booter was used! */
		/* XXX FIXME: breaks for model > 31 */
		model=(mac_bi_data.cpuid>>2)&63;
		printk ("No bootinfo model ID, using cpuid instead (hey, use Penguin!)\n");
	}

	macintosh_config = mac_data_table; 
	for (m = macintosh_config ; m->ident != -1 ; m++) {
		if (m->ident == model) {
			macintosh_config = m;
			break;
		}
	}

	/* We need to pre-init the IOPs, if any. Otherwise */
	/* the serial console won't work if the user had   */
	/* the serial ports set to "Faster" mode in MacOS. */

	iop_preinit();
	mac_debug_init();

	printk ("Detected Macintosh model: %d \n", model);

	/*
	 * Report booter data:
	 */
	printk (" Penguin bootinfo data:\n");
	printk (" Video: addr 0x%lx row 0x%lx depth %lx dimensions %ld x %ld\n", 
		mac_bi_data.videoaddr, mac_bi_data.videorow, 
		mac_bi_data.videodepth, mac_bi_data.dimensions & 0xFFFF, 
		mac_bi_data.dimensions >> 16); 
	printk (" Videological 0x%lx phys. 0x%lx, SCC at 0x%lx \n",
		mac_bi_data.videological, mac_orig_videoaddr, 
		mac_bi_data.sccbase); 
	printk (" Boottime: 0x%lx GMTBias: 0x%lx \n",
		mac_bi_data.boottime, mac_bi_data.gmtbias); 
	printk (" Machine ID: %ld CPUid: 0x%lx memory size: 0x%lx \n",
		mac_bi_data.id, mac_bi_data.cpuid, mac_bi_data.memsize); 
#if 0
	printk ("Ramdisk: addr 0x%lx size 0x%lx\n", 
		m68k_ramdisk.addr, m68k_ramdisk.size);
#endif

	/*
	 * TODO: set the various fields in macintosh_config->hw_present here!
	 */
	switch (macintosh_config->scsi_type) {
	case MAC_SCSI_OLD:
	  MACHW_SET(MAC_SCSI_80);
	  break;
	case MAC_SCSI_QUADRA:
	case MAC_SCSI_QUADRA2:
	case MAC_SCSI_QUADRA3:
	  MACHW_SET(MAC_SCSI_96);
	  if ((macintosh_config->ident == MAC_MODEL_Q900) ||
	      (macintosh_config->ident == MAC_MODEL_Q950))
	    MACHW_SET(MAC_SCSI_96_2);
	  break;
	default:
	  printk("config.c: wtf: unknown scsi, using 53c80\n");
	  MACHW_SET(MAC_SCSI_80);
	  break;

	}
	iop_init();
	via_init();
	oss_init();
	psc_init();
}

void mac_report_hardware(void)
{
	printk("Apple Macintosh %s\n", macintosh_config->name);
}

static void mac_get_model(char *str)
{
	strcpy(str,"Macintosh ");
	strcat(str, macintosh_config->name);
}

/*
 *	The power switch - yes it's software!
 */

void mac_poweroff(void)
{
	/*
	 * MAC_ADB_IISI may need to be moved up here if it doesn't actually
	 * work using the ADB packet method.  --David Kilzer
	 */

	if (oss_present) {
		oss_poweroff();
	} else if (macintosh_config->adb_type == MAC_ADB_II) {
		via_poweroff();
	} else {
		adb_poweroff();
	}
}

/* 
 * Not all Macs support software power down; for the rest, just 
 * try the ROM reset vector ...
 */
void mac_reset(void)
{
	/*
	 * MAC_ADB_IISI may need to be moved up here if it doesn't actually
	 * work using the ADB packet method.  --David Kilzer
	 */

	if (macintosh_config->adb_type == MAC_ADB_II) {
		unsigned long cpu_flags;

		/* need ROMBASE in booter */
		/* indeed, plus need to MAP THE ROM !! */

		if (mac_bi_data.rombase == 0)
			mac_bi_data.rombase = 0x40800000;

		/* works on some */
		rom_reset = (void *) (mac_bi_data.rombase + 0xa);

		if (macintosh_config->ident == MAC_MODEL_SE30) {
			/*
			 * MSch: Machines known to crash on ROM reset ...
			 */
			printk("System halted.\n");
			while(1);
		} else {
			save_flags(cpu_flags);
			cli();

			rom_reset();

			restore_flags(cpu_flags);
		}

		/* We never make it this far... it usually panics above. */
		printk ("Restart failed.  Please restart manually.\n");

		/* XXX - delay do we need to spin here ? */
		while(1);       /* Just in case .. */
	} else if (macintosh_config->adb_type == MAC_ADB_IISI
		|| macintosh_config->adb_type == MAC_ADB_CUDA) {
		adb_hwreset();
	} else if (CPU_IS_030) {

		/* 030-specific reset routine.  The idea is general, but the
		 * specific registers to reset are '030-specific.  Until I
		 * have a non-030 machine, I can't test anything else.
		 *  -- C. Scott Ananian <cananian@alumni.princeton.edu>
		 */

		unsigned long rombase = 0x40000000;

		/* make a 1-to-1 mapping, using the transparent tran. reg. */
		unsigned long virt = (unsigned long) mac_reset;
		unsigned long phys = virt_to_phys(mac_reset);
		unsigned long offset = phys-virt;
		cli(); /* lets not screw this up, ok? */
		__asm__ __volatile__(".chip 68030\n\t"
				     "pmove %0,%/tt0\n\t"
				     ".chip 68k"
				     : : "m" ((phys&0xFF000000)|0x8777));
		/* Now jump to physical address so we can disable MMU */
		__asm__ __volatile__(
                    ".chip 68030\n\t"
		    "lea %/pc@(1f),%/a0\n\t"
		    "addl %0,%/a0\n\t"/* fixup target address and stack ptr */
		    "addl %0,%/sp\n\t" 
		    "pflusha\n\t"
		    "jmp %/a0@\n\t" /* jump into physical memory */
		    "0:.long 0\n\t" /* a constant zero. */
		    /* OK.  Now reset everything and jump to reset vector. */
		    "1:\n\t"
		    "lea %/pc@(0b),%/a0\n\t"
		    "pmove %/a0@, %/tc\n\t" /* disable mmu */
		    "pmove %/a0@, %/tt0\n\t" /* disable tt0 */
		    "pmove %/a0@, %/tt1\n\t" /* disable tt1 */
		    "movel #0, %/a0\n\t"
		    "movec %/a0, %/vbr\n\t" /* clear vector base register */
		    "movec %/a0, %/cacr\n\t" /* disable caches */
		    "movel #0x0808,%/a0\n\t"
		    "movec %/a0, %/cacr\n\t" /* flush i&d caches */
		    "movew #0x2700,%/sr\n\t" /* set up status register */
		    "movel %1@(0x0),%/a0\n\t"/* load interrupt stack pointer */
		    "movec %/a0, %/isp\n\t" 
		    "movel %1@(0x4),%/a0\n\t" /* load reset vector */
		    "reset\n\t" /* reset external devices */
		    "jmp %/a0@\n\t" /* jump to the reset vector */
		    ".chip 68k"
		    : : "r" (offset), "a" (rombase) : "a0");

		/* should never get here */
		sti(); /* sure, why not */
		printk ("030 Restart failed.  Please restart manually.\n");
		while(1);
	} else {
		/* We never make it here... The above shoule handle all cases. */
		printk ("Restart failed.  Please restart manually.\n");

		/* XXX - delay do we need to spin here ? */
		while(1);       /* Just in case .. */
	}
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  tab-width: 8
 * End:
 */
