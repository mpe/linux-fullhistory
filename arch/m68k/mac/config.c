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

#include "via6522.h"

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
extern int mac_keyb_init(void);
extern int mac_kbdrate(struct kbd_repeat *k);
extern void mac_kbd_leds(unsigned int leds);

/* Mac specific irq functions */
extern void mac_init_IRQ (void);
extern void (*mac_handlers[]) (int, void *, struct pt_regs *);
extern int mac_request_irq (unsigned int irq,
			    void (*handler)(int, void *, struct pt_regs *),
                            unsigned long flags, const char *devname,
			    void *dev_id);
extern void mac_free_irq (unsigned int irq, void *dev_id);
extern void mac_enable_irq (unsigned int);
extern void mac_disable_irq (unsigned int);
static void mac_get_model(char *model);
/*static int mac_get_hardware_list(char *buffer);*/
extern int mac_get_irq_list (char *);

/* Mac specific timer functions */
extern unsigned long mac_gettimeoffset (void);
static void mac_gettod (int *, int *, int *, int *, int *, int *);
static int mac_hwclk (int, struct hwclk_time *);
static int mac_set_clock_mmss (unsigned long);
extern void via_init_clock(void (*func)(int, void *, struct pt_regs *));

extern void (*kd_mksound)(unsigned int, unsigned int);
extern void mac_mksound(unsigned int, unsigned int);
extern int mac_floppy_init(void);
extern void mac_floppy_setup(char *,int *);

extern void nubus_sweep_video(void);

/* Mac specific debug functions (in debug.c) */
extern void mac_debug_init(void);
extern void mac_debugging_long(int, long);

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

/*
 * This function translates the boot timeval into a proper date, to initialize
 * the system time.
 */

static void mac_gettod (int *yearp, int *monp, int *dayp,
		 int *hourp, int *minp, int *secp)
{
	unsigned long time;
	int leap, oldleap, isleap;
	int mon_days[14] = { -1, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, -1 };

	time = mac_bi_data.boottime - 60*mac_bi_data.gmtbias; /* seconds */

	*minp = time / 60;
	*secp = time - (*minp * 60);
	time  = *minp;				/* minutes now */

	*hourp = time / 60;
	*minp  = time - (*hourp * 60);
	time   = *hourp;			/* hours now */

	*dayp  = time / 24;
	*hourp = time - (*dayp * 24);
	time   = *dayp;				/* days now ... */

	/* for leap day calculation */
	*yearp = (time / 365) + 1970;		/* approx. year */

	/* leap year calculation - there's an easier way, I bet. And it's broken :-( */
	/* calculate leap days up to previous year */
	oldleap =  (*yearp-1)/4 - (*yearp-1)/100 + (*yearp-1)/400;
	/* calculate leap days incl. this year */
	leap    =  *yearp/4 - *yearp/100 + *yearp/400;
	/* this year a leap year ?? */
	isleap  = (leap != oldleap);

	/* adjust days: days, excluding past leap days since epoch */
	time  -= oldleap - (1970/4 - 1970/100 + 1970/400);

	/* precise year, and day in year */
	*yearp = (time / 365);			/* #years since epoch */
	*dayp  = time - (*yearp * 365) + 1;	/* #days this year (0: Jan 1) */
	*yearp += 70;				/* add epoch :-) */
	time = *dayp;
	
	if (isleap)				/* add leap day ?? */
		mon_days[2] += 1;

	/* count the months */
	for (*monp = 1; time > mon_days[*monp]; (*monp)++) 
		time -= mon_days[*monp];

	*dayp = time;

	return;
}

/* 
 * TBI: read and write hwclock
 */

static int mac_hwclk( int op, struct hwclk_time *t )
{
    return 0;
}

/*
 * TBI: set minutes/seconds in hwclock
 */

static int mac_set_clock_mmss (unsigned long nowtime)
{
    short real_seconds = nowtime % 60, real_minutes = (nowtime / 60) % 60;

    return 0;
}

void mac_waitbut (void)
{
	;
}

extern struct consw fb_con;
extern struct fb_info *mac_fb_init(long *);
extern void mac_video_setup(char *, int *);

void (*mac_handlers[8])(int, void *, struct pt_regs *)=
{
	mac_default_handler,
	mac_default_handler,
	mac_default_handler,
	mac_default_handler,
	mac_default_handler,
	mac_default_handler,
	mac_default_handler,
	mac_default_handler
};

    /*
     *  Parse a Macintosh-specific record in the bootinfo
     */

__initfunc(int mac_parse_bootinfo(const struct bi_record *record))
{
    int unknown = 0;
    const u_long *data = record->data;

    switch (record->tag) {
	case BI_MAC_MODEL:
	    mac_bi_data.id = *data;
	    break;
	case BI_MAC_VADDR:
	    mac_bi_data.videoaddr = VIDEOMEMBASE + (*data & ~VIDEOMEMMASK);
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
	    mac_bi_data.videological = *data;
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
	unsigned long flags;
	save_flags(flags);
	cli();
	via_write(via2, vBufB, via_read(via2,vBufB)&~VIA2B_vMode32);
	via_write(via2, vBufB, via_read(via2,vBufB)|VIA2B_vMode32);
	restore_flags(flags);
}

__initfunc(void config_mac(void))
{

    if (!MACH_IS_MAC) {
      printk("ERROR: no Mac, but config_mac() called!! \n");
    }
    
    mac_debug_init();
        
    mach_sched_init      = mac_sched_init;
    mach_keyb_init       = mac_keyb_init;
    mach_kbdrate         = mac_kbdrate;
    mach_kbd_leds        = mac_kbd_leds;
    mach_init_IRQ        = mac_init_IRQ;
    mach_request_irq     = mac_request_irq;
    mach_free_irq        = mac_free_irq;
    enable_irq           = mac_enable_irq;
    disable_irq          = mac_disable_irq;
#if 1
    mach_default_handler = &mac_handlers;
#endif
    mach_get_model	 = mac_get_model;
    mach_get_irq_list	 = mac_get_irq_list;
    mach_gettimeoffset   = mac_gettimeoffset;
    mach_gettod          = mac_gettod;
    mach_hwclk           = mac_hwclk;
    mach_set_clock_mmss	 = mac_set_clock_mmss;
#if 0
    mach_mksound         = mac_mksound;
#endif
    mach_reset           = mac_reset;
    conswitchp	         = &dummy_con;
    mach_max_dma_address = 0xffffffff;
#if 0
    mach_debug_init	 = mac_debug_init;
    mach_video_setup	 = mac_video_setup;
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
    
    if(
    	/* Cache cards */
        macintosh_config->ident == MAC_MODEL_IICI||
    	macintosh_config->ident == MAC_MODEL_IISI||
    	macintosh_config->ident == MAC_MODEL_IICX||
    	/* On board L2 cache */
    	macintosh_config->ident == MAC_MODEL_IIFX)
    {
    	mach_l2_flush = mac_cache_card_flush;
    }
    /* goes on forever if timers broken */
#ifdef MAC_DEBUG_SOUND
    mac_mksound(1000,10);
#endif

    /*
     * Check for machine specific fixups.
     */

    nubus_sweep_video();
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
	 */

	{	MAC_MODEL_IICI,	"IIci",	MAC_ADB_II,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_IIFX,	"IIfx",	MAC_ADB_II,	MAC_VIA_IIci,	MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
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
	{	MAC_MODEL_Q700, "Quadra 700",   MAC_ADB_II,  MAC_VIA_QUADRA, MAC_SCSI_QUADRA2, MAC_IDE_NONE,   MAC_SCC_QUADRA2,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_Q800, "Quadra 800", MAC_ADB_II,   MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE,   MAC_SCC_QUADRA,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_Q840, "Quadra 840AV", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA3, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_MACE,		MAC_NUBUS},
	/* These might have IOP problems */
	{	MAC_MODEL_Q900, "Quadra 900", MAC_ADB_IISI, MAC_VIA_QUADRA, MAC_SCSI_QUADRA2, MAC_IDE_NONE,   MAC_SCC_IOP,	MAC_ETHER_SONIC,	MAC_NUBUS},
	{	MAC_MODEL_Q950, "Quadra 950", MAC_ADB_IISI, MAC_VIA_QUADRA, MAC_SCSI_QUADRA2, MAC_IDE_NONE,   MAC_SCC_IOP,	MAC_ETHER_SONIC,	MAC_NUBUS},

	/* 
	 *	Performa - more LC type machines
	 */

	{	MAC_MODEL_P460,  "Performa 460", MAC_ADB_IISI, MAC_VIA_IIci,   MAC_SCSI_OLD,    MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_P475,  "Performa 475", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE, MAC_NUBUS},
	{	MAC_MODEL_P475F, "Performa 475", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE, MAC_NUBUS},
	{	MAC_MODEL_P520,  "Performa 520", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_P550,  "Performa 550", MAC_ADB_CUDA, MAC_VIA_IIci,   MAC_SCSI_OLD,    MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_P575,  "Performa 575", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_P588,  "Performa 588", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_TV,    "TV",           MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_OLD,	MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_P600,  "Performa 600", MAC_ADB_IISI, MAC_VIA_IIci,   MAC_SCSI_OLD,	MAC_IDE_NONE,	MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
#if 0	/* other sources seem to suggest the P630/Q630/LC630 is more like LCIII */
	{	MAC_MODEL_P630,  "Performa 630", MAC_ADB_IISI, MAC_VIA_IIci,   MAC_SCSI_OLD, MAC_IDE_NONE, MAC_SCC_II,	MAC_ETHER_NONE,	MAC_NUBUS},
#endif
	/*
	 *	Centris - just guessing again; maybe like Quadra
	 */

	{	MAC_MODEL_C610, "Centris 610",   MAC_ADB_II,   MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_SONIC, MAC_NUBUS},
	{	MAC_MODEL_C650, "Centris 650",   MAC_ADB_II,   MAC_VIA_QUADRA, MAC_SCSI_QUADRA,  MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_SONIC, MAC_NUBUS},
	{	MAC_MODEL_C660, "Centris 660AV", MAC_ADB_CUDA, MAC_VIA_QUADRA, MAC_SCSI_QUADRA3, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},

	/*
	 *      Power books - seem similar to early Quadras ? (most have 030 though)
	 */

	{	MAC_MODEL_PB140,  "PowerBook 140",   MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB145,  "PowerBook 145",   MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	/*	The PB150 has IDE, and IIci style VIA */
	{	MAC_MODEL_PB150,  "PowerBook 150",   MAC_ADB_PB1, MAC_VIA_IIci,   MAC_SCSI_QUADRA, MAC_IDE_PB,	 MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB160,  "PowerBook 160",   MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB165,  "PowerBook 165",   MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB165C, "PowerBook 165c",  MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB170,  "PowerBook 170",   MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_OLD,    MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB180,  "PowerBook 180",   MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB180C, "PowerBook 180c",  MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB190,  "PowerBook 190",   MAC_ADB_PB1, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_PB,   MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB520,  "PowerBook 520",   MAC_ADB_PB2, MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},

	/*
	 *      Power book Duos - similar to Power books, I hope
	 */

	{	MAC_MODEL_PB210,  "PowerBook Duo 210",  MAC_ADB_PB2,  MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB230,  "PowerBook Duo 230",  MAC_ADB_PB2,  MAC_VIA_IIci, MAC_SCSI_OLD,    MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB250,  "PowerBook Duo 250",  MAC_ADB_PB2,  MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB270C, "PowerBook Duo 270c", MAC_ADB_PB2,  MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB280,  "PowerBook Duo 280",  MAC_ADB_PB2,  MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},
	{	MAC_MODEL_PB280C, "PowerBook Duo 280c", MAC_ADB_PB2,  MAC_VIA_QUADRA, MAC_SCSI_QUADRA, MAC_IDE_NONE, MAC_SCC_QUADRA,	MAC_ETHER_NONE,	MAC_NUBUS},

	/*
	 *	Other stuff ??
	 */
	{	-1, NULL, 0,0,0,}
};

void mac_identify(void)
{
	struct mac_model *m=&mac_data_table[0];

	/* Penguin data useful? */	
	int model = mac_bi_data.id;
	if (!model) {
		/* no bootinfo model id -> NetBSD booter was used! */
		/* XXX FIXME: breaks for model > 31 */
		model=(mac_bi_data.cpuid>>2)&63;
		printk ("No bootinfo model ID, using cpuid instead (hey, use Penguin!)\n");
	}

	printk ("Detected Macintosh model: %d \n", model);
	
	while(m->ident != -1)
	{
		if(m->ident == model)
			break;
		m++;
	}
	if(m->ident==-1)
	{
		printk("\nUnknown macintosh model %d, probably unsupported.\n", 
			model);
		model = MAC_MODEL_Q800;
		printk("Defaulting to: Quadra800, model id %d\n", model);
		printk("Please report this case to linux-mac68k@wave.lm.com\n");
		m=&mac_data_table[0];
		while(m->ident != -1)
		{
			if(m->ident == model)
				break;
			m++;
		}
		if(m->ident==-1)
			panic("mac model config data corrupt!\n");
	}

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
	 *	Save the pointer
	 */

	macintosh_config=m;

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
	via_configure_base();
}

void mac_report_hardware(void)
{
	printk("Apple Macintosh %s\n", macintosh_config->name);
}

static void mac_get_model(char *str)
{
	strcpy(str,"Macintosh ");
	strcat(str, macintosh_config->name);
	if(mach_l2_flush && !(via_read(via2, vBufB)&VIA2B_vCDis))
		strcat(str, "(+L2 cache)");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  tab-width: 8
 * End:
 */
