/*
 *  linux/arch/m68k/atari/config.c
 *
 *  Copyright (C) 1994 Bjoern Brauel
 *
 *  5/2/94 Roman Hodek:
 *    Added setting of time_adj to get a better clock.
 *
 *  5/14/94 Roman Hodek:
 *    gettod() for TT 
 *
 *  5/15/94 Roman Hodek:
 *    hard_reset_now() for Atari (and others?)
 *
 *  94/12/30 Andreas Schwab:
 *    atari_sched_init fixed to get precise clock.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * Miscellaneous atari stuff
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/mc146818rtc.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/bootinfo.h>
#include <asm/setup.h>
#include <asm/atarihw.h>
#include <asm/atarihdreg.h>
#include <asm/atariints.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>

#ifdef CONFIG_KGDB
#include <asm/kgdb.h>
#endif

u_long atari_mch_cookie;
struct atari_hw_present atari_hw_present;

extern char m68k_debug_device[];

static void atari_sched_init(void (*)(int, void *, struct pt_regs *));
/* atari specific keyboard functions */
extern int atari_keyb_init(void);
extern int atari_kbdrate (struct kbd_repeat *);
extern void atari_kbd_leds (unsigned int);
/* atari specific irq functions */
extern void atari_init_IRQ (void);
extern int atari_request_irq (unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
                              unsigned long flags, const char *devname, void *dev_id);
extern void atari_free_irq (unsigned int irq, void *dev_id);
extern void atari_enable_irq (unsigned int);
extern void atari_disable_irq (unsigned int);
extern int atari_get_irq_list (char *buf);
static void atari_get_model(char *model);
static int atari_get_hardware_list(char *buffer);
/* atari specific timer functions */
static unsigned long atari_gettimeoffset (void);
static void atari_mste_gettod (int *, int *, int *, int *, int *, int *);
static void atari_gettod (int *, int *, int *, int *, int *, int *);
static int atari_mste_hwclk (int, struct hwclk_time *);
static int atari_hwclk (int, struct hwclk_time *);
static int atari_mste_set_clock_mmss (unsigned long);
static int atari_set_clock_mmss (unsigned long);
extern void atari_mksound( unsigned int count, unsigned int ticks );
static void atari_reset( void );
#ifdef CONFIG_BLK_DEV_FD
extern int atari_floppy_init (void);
extern void atari_floppy_setup(char *, int *);
#endif
extern struct consw fb_con;
extern struct fb_info *atari_fb_init(long *);
static void atari_debug_init(void);
extern void atari_video_setup(char *, int *);

static struct console atari_console_driver;

/* Can be set somewhere, if a SCC master reset has already be done and should
 * not be repeated; used by kgdb */
int atari_SCC_reset_done = 0;


extern void (*kd_mksound)(unsigned int, unsigned int);

/* This function tests for the presence of an address, specially a
 * hardware register address. It is called very early in the kernel
 * initialization process, when the VBR register isn't set up yet. On
 * an Atari, it still points to address 0, which is unmapped. So a bus
 * error would cause another bus error while fetching the exception
 * vector, and the CPU would do nothing at all. So we needed to set up
 * a temporary VBR and a vector table for the duration of the test.
 */

__initfunc(static int hwreg_present( volatile void *regp ))
{
    int	ret = 0;
    long	save_sp, save_vbr;
    long	tmp_vectors[3];

    __asm__ __volatile__
	(	"movec	%/vbr,%2\n\t"
		"movel	#Lberr1,%4@(8)\n\t"
                "movec	%4,%/vbr\n\t"
		"movel	%/sp,%1\n\t"
		"moveq	#0,%0\n\t"
		"tstb	%3@\n\t"  
		"nop\n\t"
		"moveq	#1,%0\n"
                "Lberr1:\n\t"
		"movel	%1,%/sp\n\t"
		"movec	%2,%/vbr"
		: "=&d" (ret), "=&r" (save_sp), "=&r" (save_vbr)
		: "a" (regp), "a" (tmp_vectors)
                );

    return( ret );
}
  
#if 0
__initfunc(static int
hwreg_present_bywrite(volatile void *regp, unsigned char val))
{
    int		ret;
    long	save_sp, save_vbr;
    static long tmp_vectors[3] = { 0, 0, (long)&&after_test };
	
    __asm__ __volatile__
	(	"movec	%/vbr,%2\n\t"	/* save vbr value            */
                "movec	%4,%/vbr\n\t"	/* set up temporary vectors  */
		"movel	%/sp,%1\n\t"	/* save sp                   */
		"moveq	#0,%0\n\t"	/* assume not present        */
		"moveb	%5,%3@\n\t"	/* write the hardware reg    */
		"cmpb	%3@,%5\n\t"	/* compare it                */
		"seq	%0"		/* comes here only if reg    */
                                        /* is present                */
		: "=d&" (ret), "=r&" (save_sp), "=r&" (save_vbr)
		: "a" (regp), "r" (tmp_vectors), "d" (val)
                );
  after_test:
    __asm__ __volatile__
      (	"movel	%0,%/sp\n\t"		/* restore sp                */
        "movec	%1,%/vbr"			/* restore vbr               */
        : : "r" (save_sp), "r" (save_vbr) : "sp"
	);

    return( ret );
}
#endif

/* Basically the same, but writes a value into a word register, protected
 * by a bus error handler */

__initfunc(static int hwreg_write( volatile void *regp, unsigned short val ))
{
	int		ret;
	long	save_sp, save_vbr;
	long	tmp_vectors[3];

	__asm__ __volatile__
	(	"movec	%/vbr,%2\n\t"
		"movel	#Lberr2,%4@(8)\n\t"
		"movec	%4,%/vbr\n\t"
		"movel	%/sp,%1\n\t"
		"moveq	#0,%0\n\t"
		"movew	%5,%3@\n\t"  
		"nop	\n\t"	/* If this nop isn't present, 'ret' may already be
				 * loaded with 1 at the time the bus error
				 * happens! */
		"moveq	#1,%0\n"
	"Lberr2:\n\t"
		"movel	%1,%/sp\n\t"
		"movec	%2,%/vbr"
		: "=&d" (ret), "=&r" (save_sp), "=&r" (save_vbr)
		: "a" (regp), "a" (tmp_vectors), "g" (val)
	);

	return( ret );
}

/* ++roman: This is a more elaborate test for an SCC chip, since the plain
 * Medusa board generates DTACK at the SCC's standard addresses, but a SCC
 * board in the Medusa is possible. Also, the addresses where the ST_ESCC
 * resides generate DTACK without the chip, too.
 * The method is to write values into the interrupt vector register, that
 * should be readable without trouble (from channel A!).
 */

__initfunc(static int scc_test( volatile char *ctla ))
{
	if (!hwreg_present( ctla ))
		return( 0 );
	MFPDELAY();

	*ctla = 2; MFPDELAY();
	*ctla = 0x40; MFPDELAY();
	
	*ctla = 2; MFPDELAY();
	if (*ctla != 0x40) return( 0 );
	MFPDELAY();

	*ctla = 2; MFPDELAY();
	*ctla = 0x60; MFPDELAY();
	
	*ctla = 2; MFPDELAY();
	if (*ctla != 0x60) return( 0 );

	return( 1 );
}


    /*
     *  Parse an Atari-specific record in the bootinfo
     */

__initfunc(int atari_parse_bootinfo(const struct bi_record *record))
{
    int unknown = 0;
    const u_long *data = record->data;

    switch (record->tag) {
	case BI_ATARI_MCH_COOKIE:
	    atari_mch_cookie = *data;
	    break;
	default:
	    unknown = 1;
    }
    return(unknown);
}

    /*
     *  Setup the Atari configuration info
     */

__initfunc(void config_atari(void))
{
    memset(&atari_hw_present, 0, sizeof(atari_hw_present));

    atari_debug_init();

    mach_sched_init      = atari_sched_init;
    mach_keyb_init       = atari_keyb_init;
    mach_kbdrate         = atari_kbdrate;
    mach_kbd_leds        = atari_kbd_leds;
    mach_init_IRQ        = atari_init_IRQ;
    mach_request_irq     = atari_request_irq;
    mach_free_irq        = atari_free_irq;
    enable_irq           = atari_enable_irq;
    disable_irq          = atari_disable_irq;
    mach_get_model	 = atari_get_model;
    mach_get_hardware_list = atari_get_hardware_list;
    mach_get_irq_list	 = atari_get_irq_list;
    mach_gettimeoffset   = atari_gettimeoffset;
    mach_reset           = atari_reset;
#ifdef CONFIG_BLK_DEV_FD
    mach_floppy_init	 = atari_floppy_init;
    mach_floppy_setup	 = atari_floppy_setup;
#endif
    conswitchp	         = &fb_con;
    mach_fb_init         = atari_fb_init;
    mach_max_dma_address = 0xffffff;
    mach_video_setup	 = atari_video_setup;
    kd_mksound		 = atari_mksound;

    /* ++bjoern: 
     * Determine hardware present
     */

    printk( "Atari hardware found: " );
    if (is_medusa || is_hades) {
        /* There's no Atari video hardware on the Medusa, but all the
         * addresses below generate a DTACK so no bus error occurs! */
    }
    else if (hwreg_present( f030_xreg )) {
	ATARIHW_SET(VIDEL_SHIFTER);
        printk( "VIDEL " );
        /* This is a temporary hack: If there is Falcon video
         * hardware, we assume that the ST-DMA serves SCSI instead of
         * ACSI. In the future, there should be a better method for
         * this...
         */
	ATARIHW_SET(ST_SCSI);
        printk( "STDMA-SCSI " );
    }
    else if (hwreg_present( tt_palette )) {
	ATARIHW_SET(TT_SHIFTER);
        printk( "TT_SHIFTER " );
    }
    else if (hwreg_present( &shifter.bas_hi )) {
        if (hwreg_present( &shifter.bas_lo ) &&
	    (shifter.bas_lo = 0x0aau, shifter.bas_lo == 0x0aau)) {
	    ATARIHW_SET(EXTD_SHIFTER);
            printk( "EXTD_SHIFTER " );
        }
        else {
	    ATARIHW_SET(STND_SHIFTER);
            printk( "STND_SHIFTER " );
        }
    }
    if (hwreg_present( &mfp.par_dt_reg )) {
	ATARIHW_SET(ST_MFP);
        printk( "ST_MFP " );
    }
    if (hwreg_present( &tt_mfp.par_dt_reg )) {
	ATARIHW_SET(TT_MFP);
        printk( "TT_MFP " );
    }
    if (hwreg_present( &tt_scsi_dma.dma_addr_hi )) {
	ATARIHW_SET(SCSI_DMA);
        printk( "TT_SCSI_DMA " );
    }
    if (!is_hades && hwreg_present( &st_dma.dma_hi )) {
	ATARIHW_SET(STND_DMA);
        printk( "STND_DMA " );
    }
    if (is_medusa || /* The ST-DMA address registers aren't readable
                      * on all Medusas, so the test below may fail */
        (hwreg_present( &st_dma.dma_vhi ) &&
         (st_dma.dma_vhi = 0x55) && (st_dma.dma_hi = 0xaa) &&
         st_dma.dma_vhi == 0x55 && st_dma.dma_hi == 0xaa &&
         (st_dma.dma_vhi = 0xaa) && (st_dma.dma_hi = 0x55) &&
         st_dma.dma_vhi == 0xaa && st_dma.dma_hi == 0x55)) {
	ATARIHW_SET(EXTD_DMA);
        printk( "EXTD_DMA " );
    }
    if (hwreg_present( &tt_scsi.scsi_data )) {
	ATARIHW_SET(TT_SCSI);
        printk( "TT_SCSI " );
    }
    if (hwreg_present( &sound_ym.rd_data_reg_sel )) {
	ATARIHW_SET(YM_2149);
        printk( "YM2149 " );
    }
    if (!is_medusa && !is_hades && hwreg_present( &tt_dmasnd.ctrl )) {
	ATARIHW_SET(PCM_8BIT);
        printk( "PCM " );
    }
    if (!is_hades && hwreg_present( &codec.unused5 )) {
	ATARIHW_SET(CODEC);
        printk( "CODEC " );
    }
    if (hwreg_present( &dsp56k_host_interface.icr )) {
	ATARIHW_SET(DSP56K);
        printk( "DSP56K " );
    }
    if (hwreg_present( &tt_scc_dma.dma_ctrl ) &&
#if 0
	/* This test sucks! Who knows some better? */
	(tt_scc_dma.dma_ctrl = 0x01, (tt_scc_dma.dma_ctrl & 1) == 1) &&
	(tt_scc_dma.dma_ctrl = 0x00, (tt_scc_dma.dma_ctrl & 1) == 0)
#else
	!is_medusa && !is_hades
#endif
	) {
	ATARIHW_SET(SCC_DMA);
        printk( "SCC_DMA " );
    }
    if (scc_test( &scc.cha_a_ctrl )) {
	ATARIHW_SET(SCC);
        printk( "SCC " );
    }
    if (scc_test( &st_escc.cha_b_ctrl )) {
	ATARIHW_SET( ST_ESCC );
	printk( "ST_ESCC " );
    }
    if (is_hades)
    {
        ATARIHW_SET( VME );
        printk( "VME " );
    }
    else if (hwreg_present( &tt_scu.sys_mask )) {
	ATARIHW_SET(SCU);
	/* Assume a VME bus if there's a SCU */
	ATARIHW_SET( VME );
        printk( "VME SCU " );
    }
    if (hwreg_present( (void *)(0xffff9210) )) {
	ATARIHW_SET(ANALOG_JOY);
        printk( "ANALOG_JOY " );
    }
    if (!is_hades && hwreg_present( blitter.halftone )) {
	ATARIHW_SET(BLITTER);
        printk( "BLITTER " );
    }
    if (hwreg_present( (void *)(ATA_HD_BASE+ATA_HD_CMD) )) {
	ATARIHW_SET(IDE);
        printk( "IDE " );
    }
#if 1 /* This maybe wrong */
    if (!is_medusa && !is_hades &&
	hwreg_present( &tt_microwire.data ) &&
	hwreg_present( &tt_microwire.mask ) &&
	(tt_microwire.mask = 0x7ff,
	 tt_microwire.data = MW_LM1992_PSG_HIGH | MW_LM1992_ADDR,
	 tt_microwire.data != 0)) {
	ATARIHW_SET(MICROWIRE);
	while (tt_microwire.mask != 0x7ff) ;
        printk( "MICROWIRE " );
    }
#endif
    if (hwreg_present( &tt_rtc.regsel )) {
	ATARIHW_SET(TT_CLK);
        printk( "TT_CLK " );
        mach_gettod = atari_gettod;
        mach_hwclk = atari_hwclk;
        mach_set_clock_mmss = atari_set_clock_mmss;
    }
    if (!is_hades && hwreg_present( &mste_rtc.sec_ones)) {
	ATARIHW_SET(MSTE_CLK);
        printk( "MSTE_CLK ");
        mach_gettod = atari_mste_gettod;
        mach_hwclk = atari_mste_hwclk;
        mach_set_clock_mmss = atari_mste_set_clock_mmss;
    }
    if (!is_medusa && !is_hades &&
	hwreg_present( &dma_wd.fdc_speed ) &&
	hwreg_write( &dma_wd.fdc_speed, 0 )) {
	    ATARIHW_SET(FDCSPEED);
	    printk( "FDC_SPEED ");
    }
    if (!is_hades && !ATARIHW_PRESENT(ST_SCSI)) {
	ATARIHW_SET(ACSI);
        printk( "ACSI " );
    }
    printk("\n");

    if (CPU_IS_040_OR_060)
        /* Now it seems to be safe to turn of the tt0 transparent
         * translation (the one that must not be turned off in
         * head.S...)
         */
        __asm__ volatile ("moveq #0,%/d0\n\t"
                          ".chip 68040\n\t"
			  "movec %%d0,%%itt0\n\t"
			  "movec %%d0,%%dtt0\n\t"
			  ".chip 68k"
						  : /* no outputs */
						  : /* no inputs */
						  : "d0");
	
    /* allocator for memory that must reside in st-ram */
    atari_stram_init ();

    /* Set up a mapping for the VMEbus address region:
     *
     * VME is either at phys. 0xfexxxxxx (TT) or 0xa00000..0xdfffff
     * (MegaSTE) In both cases, the whole 16 MB chunk is mapped at
     * 0xfe000000 virt., because this can be done with a single
     * transparent translation. On the 68040, lots of often unused
     * page tables would be needed otherwise. On a MegaSTE or similar,
     * the highest byte is stripped off by hardware due to the 24 bit
     * design of the bus.
     */

    if (CPU_IS_020_OR_030) {
        unsigned long	tt1_val;
        tt1_val = 0xfe008543;	/* Translate 0xfexxxxxx, enable, cache
                                 * inhibit, read and write, FDC mask = 3,
                                 * FDC val = 4 -> Supervisor only */
        __asm__ __volatile__ ( ".chip 68030\n\t"
				"pmove	%0@,%/tt1\n\t"
				".chip 68k"
				: : "a" (&tt1_val) );
    }
    else {
        __asm__ __volatile__
            ( "movel %0,%/d0\n\t"
	      ".chip 68040\n\t"
	      "movec %%d0,%%itt1\n\t"
	      "movec %%d0,%%dtt1\n\t"
	      ".chip 68k"
              :
              : "g" (0xfe00a040)	/* Translate 0xfexxxxxx, enable,
                                         * supervisor only, non-cacheable/
                                         * serialized, writable */
              : "d0" );

    }
}

__initfunc(static void
atari_sched_init(void (*timer_routine)(int, void *, struct pt_regs *)))
{
    /* set Timer C data Register */
    mfp.tim_dt_c = INT_TICKS;
    /* start timer C, div = 1:100 */
    mfp.tim_ct_cd = (mfp.tim_ct_cd & 15) | 0x60; 
    /* install interrupt service routine for MFP Timer C */
    request_irq(IRQ_MFP_TIMC, timer_routine, IRQ_TYPE_SLOW,
                "timer", timer_routine);
}

/* ++andreas: gettimeoffset fixed to check for pending interrupt */

#define TICK_SIZE 10000
  
/* This is always executed with interrupts disabled.  */
static unsigned long atari_gettimeoffset (void)
{
  unsigned long ticks, offset = 0;

  /* read MFP timer C current value */
  ticks = mfp.tim_dt_c;
  /* The probability of underflow is less than 2% */
  if (ticks > INT_TICKS - INT_TICKS / 50)
    /* Check for pending timer interrupt */
    if (mfp.int_pn_b & (1 << 5))
      offset = TICK_SIZE;

  ticks = INT_TICKS - ticks;
  ticks = ticks * 10000L / INT_TICKS;

  return ticks + offset;
}


static void
mste_read(struct MSTE_RTC *val)
{
#define COPY(v) val->v=(mste_rtc.v & 0xf)
	do {
		COPY(sec_ones) ; COPY(sec_tens) ; COPY(min_ones) ; 
		COPY(min_tens) ; COPY(hr_ones) ; COPY(hr_tens) ; 
		COPY(weekday) ; COPY(day_ones) ; COPY(day_tens) ; 
		COPY(mon_ones) ; COPY(mon_tens) ; COPY(year_ones) ;
		COPY(year_tens) ;
	/* prevent from reading the clock while it changed */
	} while (val->sec_ones != (mste_rtc.sec_ones & 0xf));
#undef COPY
}

static void
mste_write(struct MSTE_RTC *val)
{
#define COPY(v) mste_rtc.v=val->v
	do {
		COPY(sec_ones) ; COPY(sec_tens) ; COPY(min_ones) ; 
		COPY(min_tens) ; COPY(hr_ones) ; COPY(hr_tens) ; 
		COPY(weekday) ; COPY(day_ones) ; COPY(day_tens) ; 
		COPY(mon_ones) ; COPY(mon_tens) ; COPY(year_ones) ;
		COPY(year_tens) ;
	/* prevent from writing the clock while it changed */
	} while (val->sec_ones != (mste_rtc.sec_ones & 0xf));
#undef COPY
}

#define	RTC_READ(reg)				\
    ({	unsigned char	__val;			\
		outb(reg,&tt_rtc.regsel);	\
		__val = tt_rtc.data;		\
		__val;				\
	})

#define	RTC_WRITE(reg,val)			\
    do {					\
		outb(reg,&tt_rtc.regsel);	\
		tt_rtc.data = (val);		\
	} while(0)


static void atari_mste_gettod (int *yearp, int *monp, int *dayp,
			       int *hourp, int *minp, int *secp)
{
    int hr24=0, hour;
    struct MSTE_RTC val;

    mste_rtc.mode=(mste_rtc.mode | 1);
    hr24=mste_rtc.mon_tens & 1;
    mste_rtc.mode=(mste_rtc.mode & ~1);

    mste_read(&val);
    *secp = val.sec_ones + val.sec_tens * 10;
    *minp = val.min_ones + val.min_tens * 10;
    hour = val.hr_ones + val.hr_tens * 10;
    if (!hr24) {
        if (hour == 12 || hour == 12 + 20)
	    hour -= 12;
	if (hour >= 20)
	    hour += 12 - 20;
    }
    *hourp = hour;
    *dayp = val.day_ones + val.day_tens * 10;
    *monp = val.mon_ones + val.mon_tens * 10;
    *yearp = val.year_ones + val.year_tens * 10 + 80;	
}

  
static void atari_gettod (int *yearp, int *monp, int *dayp,
			  int *hourp, int *minp, int *secp)
{
    unsigned char	ctrl;
    unsigned short tos_version;
    int hour, pm;

    while (!(RTC_READ(RTC_FREQ_SELECT) & RTC_UIP)) ;
    while (RTC_READ(RTC_FREQ_SELECT) & RTC_UIP) ;

    *secp  = RTC_READ(RTC_SECONDS);
    *minp  = RTC_READ(RTC_MINUTES);
    hour = RTC_READ(RTC_HOURS);
    *dayp  = RTC_READ(RTC_DAY_OF_MONTH);
    *monp  = RTC_READ(RTC_MONTH);
    *yearp = RTC_READ(RTC_YEAR);
    pm = hour & 0x80;
    hour &= ~0x80;

    ctrl = RTC_READ(RTC_CONTROL); 

    if (!(ctrl & RTC_DM_BINARY)) {
        BCD_TO_BIN(*secp);
        BCD_TO_BIN(*minp);
        BCD_TO_BIN(hour);
        BCD_TO_BIN(*dayp);
        BCD_TO_BIN(*monp);
        BCD_TO_BIN(*yearp);
    }
    if (!(ctrl & RTC_24H)) {
	if (!pm && hour == 12)
	    hour = 0;
	else if (pm && hour != 12)
            hour += 12;
    }
    *hourp = hour;

    /* Adjust values (let the setup valid) */

    /* Fetch tos version at Physical 2 */
    /* We my not be able to access this address if the kernel is
       loaded to st ram, since the first page is unmapped.  On the
       Medusa this is always the case and there is nothing we can do
       about this, so we just assume the smaller offset.  For the TT
       we use the fact that in head.S we have set up a mapping
       0xFFxxxxxx -> 0x00xxxxxx, so that the first 16MB is accessible
       in the last 16MB of the address space. */
    tos_version = (is_medusa || is_hades) ? 0xfff : *(unsigned short *)0xFF000002;
    *yearp += (tos_version < 0x306) ? 70 : 68;
}

#define HWCLK_POLL_INTERVAL	5

static int atari_mste_hwclk( int op, struct hwclk_time *t )
{
    int hour, year;
    int hr24=0;
    struct MSTE_RTC val;
    
    mste_rtc.mode=(mste_rtc.mode | 1);
    hr24=mste_rtc.mon_tens & 1;
    mste_rtc.mode=(mste_rtc.mode & ~1);

    if (op) {
        /* write: prepare values */
        
        val.sec_ones = t->sec % 10;
        val.sec_tens = t->sec / 10;
        val.min_ones = t->min % 10;
        val.min_tens = t->min / 10;
        hour = t->hour;
        if (!hr24) {
	    if (hour > 11)
		hour += 20 - 12;
	    if (hour == 0 || hour == 20)
		hour += 12;
        }
        val.hr_ones = hour % 10;
        val.hr_tens = hour / 10;
        val.day_ones = t->day % 10;
        val.day_tens = t->day / 10;
        val.mon_ones = (t->mon+1) % 10;
        val.mon_tens = (t->mon+1) / 10;
        year = t->year - 80;
        val.year_ones = year % 10;
        val.year_tens = year / 10;
        val.weekday = t->wday;
        mste_write(&val);
        mste_rtc.mode=(mste_rtc.mode | 1);
        val.year_ones = (year % 4);	/* leap year register */
        mste_rtc.mode=(mste_rtc.mode & ~1);
    }
    else {
        mste_read(&val);
        t->sec = val.sec_ones + val.sec_tens * 10;
        t->min = val.min_ones + val.min_tens * 10;
        hour = val.hr_ones + val.hr_tens * 10;
	if (!hr24) {
	    if (hour == 12 || hour == 12 + 20)
		hour -= 12;
	    if (hour >= 20)
                hour += 12 - 20;
        }
	t->hour = hour;
	t->day = val.day_ones + val.day_tens * 10;
        t->mon = val.mon_ones + val.mon_tens * 10 - 1;
        t->year = val.year_ones + val.year_tens * 10 + 80;
        t->wday = val.weekday;
    }
    return 0;
}

static int atari_hwclk( int op, struct hwclk_time *t )
{
    int sec=0, min=0, hour=0, day=0, mon=0, year=0, wday=0; 
    unsigned long 	flags;
    unsigned short	tos_version;
    unsigned char	ctrl;
    int pm = 0;

    /* Tos version at Physical 2.  See above for explanation why we
       cannot use PTOV(2).  */
    tos_version = (is_medusa || is_hades) ? 0xfff : *(unsigned short *)0xff000002;

    ctrl = RTC_READ(RTC_CONTROL); /* control registers are
                                   * independent from the UIP */

    if (op) {
        /* write: prepare values */
        
        sec  = t->sec;
        min  = t->min;
        hour = t->hour;
        day  = t->day;
        mon  = t->mon + 1;
        year = t->year - ((tos_version < 0x306) ? 70 : 68);
        wday = t->wday + (t->wday >= 0);
        
        if (!(ctrl & RTC_24H)) {
	    if (hour > 11) {
		pm = 0x80;
		if (hour != 12)
		    hour -= 12;
	    }
	    else if (hour == 0)
		hour = 12;
        }
        
        if (!(ctrl & RTC_DM_BINARY)) {
            BIN_TO_BCD(sec);
            BIN_TO_BCD(min);
            BIN_TO_BCD(hour);
            BIN_TO_BCD(day);
            BIN_TO_BCD(mon);
            BIN_TO_BCD(year);
            if (wday >= 0) BIN_TO_BCD(wday);
        }
    }
    
    /* Reading/writing the clock registers is a bit critical due to
     * the regular update cycle of the RTC. While an update is in
     * progress, registers 0..9 shouldn't be touched.
     * The problem is solved like that: If an update is currently in
     * progress (the UIP bit is set), the process sleeps for a while
     * (50ms). This really should be enough, since the update cycle
     * normally needs 2 ms.
     * If the UIP bit reads as 0, we have at least 244 usecs until the
     * update starts. This should be enough... But to be sure,
     * additionally the RTC_SET bit is set to prevent an update cycle.
     */

    while( RTC_READ(RTC_FREQ_SELECT) & RTC_UIP ) {
        current->state = TASK_INTERRUPTIBLE;
        current->timeout = jiffies + HWCLK_POLL_INTERVAL;
        schedule();
    }

    save_flags(flags);
    cli();
    RTC_WRITE( RTC_CONTROL, ctrl | RTC_SET );
    if (!op) {
        sec  = RTC_READ( RTC_SECONDS );
        min  = RTC_READ( RTC_MINUTES );
        hour = RTC_READ( RTC_HOURS );
        day  = RTC_READ( RTC_DAY_OF_MONTH );
        mon  = RTC_READ( RTC_MONTH );
        year = RTC_READ( RTC_YEAR );
        wday = RTC_READ( RTC_DAY_OF_WEEK );
    }
    else {
        RTC_WRITE( RTC_SECONDS, sec );
        RTC_WRITE( RTC_MINUTES, min );
        RTC_WRITE( RTC_HOURS, hour + pm);
        RTC_WRITE( RTC_DAY_OF_MONTH, day );
        RTC_WRITE( RTC_MONTH, mon );
        RTC_WRITE( RTC_YEAR, year );
        if (wday >= 0) RTC_WRITE( RTC_DAY_OF_WEEK, wday );
    }
    RTC_WRITE( RTC_CONTROL, ctrl & ~RTC_SET );
    restore_flags(flags);

    if (!op) {
        /* read: adjust values */
        
        if (hour & 0x80) {
	    hour &= ~0x80;
	    pm = 1;
	}

	if (!(ctrl & RTC_DM_BINARY)) {
            BCD_TO_BIN(sec);
            BCD_TO_BIN(min);
            BCD_TO_BIN(hour);
            BCD_TO_BIN(day);
            BCD_TO_BIN(mon);
            BCD_TO_BIN(year);
            BCD_TO_BIN(wday);
        }

        if (!(ctrl & RTC_24H)) {
	    if (!pm && hour == 12)
		hour = 0;
	    else if (pm && hour != 12)
		hour += 12;
        }

        t->sec  = sec;
        t->min  = min;
        t->hour = hour;
        t->day  = day;
        t->mon  = mon - 1;
        t->year = year + ((tos_version < 0x306) ? 70 : 68);
        t->wday = wday - 1;
    }

    return( 0 );
}


static int atari_mste_set_clock_mmss (unsigned long nowtime)
{
    short real_seconds = nowtime % 60, real_minutes = (nowtime / 60) % 60;
    struct MSTE_RTC val;
    unsigned char rtc_minutes;

    mste_read(&val);  
    rtc_minutes= val.min_ones + val.min_tens * 10;
    if ((rtc_minutes < real_minutes
         ? real_minutes - rtc_minutes
         : rtc_minutes - real_minutes) < 30)
    {
        val.sec_ones = real_seconds % 10;
        val.sec_tens = real_seconds / 10;
        val.min_ones = real_minutes % 10;
        val.min_tens = real_minutes / 10;
        mste_write(&val);
    }
    else
        return -1;
    return 0;
}

static int atari_set_clock_mmss (unsigned long nowtime)
{
    int retval = 0;
    short real_seconds = nowtime % 60, real_minutes = (nowtime / 60) % 60;
    unsigned char save_control, save_freq_select, rtc_minutes;

    save_control = RTC_READ (RTC_CONTROL); /* tell the clock it's being set */
    RTC_WRITE (RTC_CONTROL, save_control | RTC_SET);

    save_freq_select = RTC_READ (RTC_FREQ_SELECT); /* stop and reset prescaler */
    RTC_WRITE (RTC_FREQ_SELECT, save_freq_select | RTC_DIV_RESET2);

    rtc_minutes = RTC_READ (RTC_MINUTES);
    if (!(save_control & RTC_DM_BINARY))
        BCD_TO_BIN (rtc_minutes);

    /* Since we're only adjusting minutes and seconds, don't interfere
       with hour overflow.  This avoids messing with unknown time zones
       but requires your RTC not to be off by more than 30 minutes.  */
    if ((rtc_minutes < real_minutes
         ? real_minutes - rtc_minutes
         : rtc_minutes - real_minutes) < 30)
        {
            if (!(save_control & RTC_DM_BINARY))
                {
                    BIN_TO_BCD (real_seconds);
                    BIN_TO_BCD (real_minutes);
                }
            RTC_WRITE (RTC_SECONDS, real_seconds);
            RTC_WRITE (RTC_MINUTES, real_minutes);
        }
    else
        retval = -1;

    RTC_WRITE (RTC_FREQ_SELECT, save_freq_select);
    RTC_WRITE (RTC_CONTROL, save_control);
    return retval;
}

static inline void ata_mfp_out (char c)
{
    while (!(mfp.trn_stat & 0x80)) /* wait for tx buf empty */
	barrier ();
    mfp.usart_dta = c;
}

static void atari_mfp_console_write (const char *str, unsigned int count)
{
    while (count--) {
	if (*str == '\n')
	    ata_mfp_out( '\r' );
	ata_mfp_out( *str++ );
    }
}

static inline void ata_scc_out (char c)
{
    do {
	MFPDELAY();
    } while (!(scc.cha_b_ctrl & 0x04)); /* wait for tx buf empty */
    MFPDELAY();
    scc.cha_b_data = c;
}

static void atari_scc_console_write (const char *str, unsigned int count)
{
    while (count--) {
	if (*str == '\n')
	    ata_scc_out( '\r' );
	ata_scc_out( *str++ );
    }
}

static int ata_par_out (char c)
{
    extern unsigned long loops_per_sec;
    unsigned char tmp;
    /* This a some-seconds timeout in case no printer is connected */
    unsigned long i = loops_per_sec > 1 ? loops_per_sec : 10000000;

    while( (mfp.par_dt_reg & 1) && --i ) /* wait for BUSY == L */
	;
    if (!i) return( 0 );
    
    sound_ym.rd_data_reg_sel = 15;  /* select port B */
    sound_ym.wd_data = c;           /* put char onto port */
    sound_ym.rd_data_reg_sel = 14;  /* select port A */
    tmp = sound_ym.rd_data_reg_sel;
    sound_ym.wd_data = tmp & ~0x20; /* set strobe L */
    MFPDELAY();                     /* wait a bit */
    sound_ym.wd_data = tmp | 0x20;  /* set strobe H */
    return( 1 );
}

static void atari_par_console_write (const char *str, unsigned int count)
{
    static int printer_present = 1;

    if (!printer_present)
	return;

    while (count--) {
	if (*str == '\n')
	    if (!ata_par_out( '\r' )) {
		printer_present = 0;
		return;
	    }
	if (!ata_par_out( *str++ )) {
	    printer_present = 0;
	    return;
	}
    }
}


__initfunc(static void atari_debug_init(void))
{
#ifdef CONFIG_KGDB
	/* if the m68k_debug_device is used by the GDB stub, do nothing here */
	if (kgdb_initialized)
		return(NULL);
#endif

    if (!strcmp( m68k_debug_device, "ser" )) {
	/* defaults to ser2 for a Falcon and ser1 otherwise */
	strcpy( m68k_debug_device, 
		((atari_mch_cookie >> 16) == ATARI_MCH_FALCON) ?
		"ser2" : "ser1" );

    }

    if (!strcmp( m68k_debug_device, "ser1" )) {
	/* ST-MFP Modem1 serial port */
	mfp.trn_stat  &= ~0x01; /* disable TX */
	mfp.usart_ctr  = 0x88;  /* clk 1:16, 8N1 */
	mfp.tim_ct_cd &= 0x70;  /* stop timer D */
	mfp.tim_dt_d   = 2;     /* 9600 bps */
	mfp.tim_ct_cd |= 0x01;  /* start timer D, 1:4 */
	mfp.trn_stat  |= 0x01;  /* enable TX */
	atari_console_driver.write = atari_mfp_console_write;
    }
    else if (!strcmp( m68k_debug_device, "ser2" )) {
	/* SCC Modem2 serial port */
	static unsigned char *p, scc_table[] = {
	    9, 12,		/* Reset */
	    4, 0x44,		/* x16, 1 stopbit, no parity */
	    3, 0xc0,		/* receiver: 8 bpc */
	    5, 0xe2,		/* transmitter: 8 bpc, assert dtr/rts */
	    9, 0,		/* no interrupts */
	    10, 0,		/* NRZ */
	    11, 0x50,		/* use baud rate generator */
	    12, 24, 13, 0,	/* 9600 baud */
	    14, 2, 14, 3,	/* use master clock for BRG, enable */
	    3, 0xc1,		/* enable receiver */
	    5, 0xea,		/* enable transmitter */
	    0
	};
	    
	(void)scc.cha_b_ctrl; /* reset reg pointer */
	for( p = scc_table; *p != 0; ) {
	    scc.cha_b_ctrl = *p++;
	    MFPDELAY();
	    scc.cha_b_ctrl = *p++;
	    MFPDELAY();
	}
	atari_console_driver.write = atari_scc_console_write;
    }
    else if (!strcmp( m68k_debug_device, "par" )) {
	/* parallel printer */
	atari_turnoff_irq( IRQ_MFP_BUSY ); /* avoid ints */
	sound_ym.rd_data_reg_sel = 7;  /* select mixer control */
	sound_ym.wd_data = 0xff;       /* sound off, ports are output */
	sound_ym.rd_data_reg_sel = 15; /* select port B */
	sound_ym.wd_data = 0;          /* no char */
	sound_ym.rd_data_reg_sel = 14; /* select port A */
	sound_ym.wd_data = sound_ym.rd_data_reg_sel | 0x20; /* strobe H */
	atari_console_driver.write = atari_par_console_write;
    }
    if (atari_console_driver.write)
	register_console(&atari_console_driver);
}

/* ++roman:
 *
 * This function does a reset on machines that lack the ability to
 * assert the processor's _RESET signal somehow via hardware. It is
 * based on the fact that you can find the initial SP and PC values
 * after a reset at physical addresses 0 and 4. This works pretty well
 * for Atari machines, since the lowest 8 bytes of physical memory are
 * really ROM (mapped by hardware). For other 680x0 machines: don't
 * know if it works...
 *
 * To get the values at addresses 0 and 4, the MMU better is turned
 * off first. After that, we have to jump into physical address space
 * (the PC before the pmove statement points to the virtual address of
 * the code). Getting that physical address is not hard, but the code
 * becomes a bit complex since I've tried to ensure that the jump
 * statement after the pmove is in the cache already (otherwise the
 * processor can't fetch it!). For that, the code first jumps to the
 * jump statement with the (virtual) address of the pmove section in
 * an address register . The jump statement is surely in the cache
 * now. After that, that physical address of the reset code is loaded
 * into the same address register, pmove is done and the same jump
 * statements goes to the reset code. Since there are not many
 * statements between the two jumps, I hope it stays in the cache.
 *
 * The C code makes heavy use of the GCC features that you can get the
 * address of a C label. No hope to compile this with another compiler
 * than GCC!
 */
  
/* ++andreas: no need for complicated code, just depend on prefetch */

static void atari_reset (void)
{
    long tc_val = 0;
    long reset_addr;

    /* On the Medusa, phys. 0x4 may contain garbage because it's no
       ROM.  See above for explanation why we cannot use PTOV(4). */
    reset_addr = is_hades ? 0x7fe00030 :
                 (is_medusa ? 0xe00030 : *(unsigned long *) 0xff000004);

    acia.key_ctrl = ACIA_RESET;             /* reset ACIA for switch off OverScan, if it's active */

    /* processor independent: turn off interrupts and reset the VBR;
     * the caches must be left enabled, else prefetching the final jump
     * instruction doesn't work. */
    cli();
    __asm__ __volatile__
	("moveq	#0,%/d0\n\t"
	 "movec	%/d0,%/vbr"
	 : : : "d0" );
    
    if (CPU_IS_040_OR_060) {
        unsigned long jmp_addr040 = VTOP(&&jmp_addr_label040);
	if (CPU_IS_060) {
	    /* 68060: clear PCR to turn off superscalar operation */
	    __asm__ __volatile__
		("moveq	#0,%/d0\n\t"
		 ".chip 68060\n\t"
		 "movec %%d0,%%pcr\n\t"
		 ".chip 68k"
		 : : : "d0" );
	}
	    
        __asm__ __volatile__
            ("movel    %0,%/d0\n\t"
             "andl     #0xff000000,%/d0\n\t"
             "orw      #0xe020,%/d0\n\t"   /* map 16 MB, enable, cacheable */
             ".chip 68040\n\t"
	     "movec    %%d0,%%itt0\n\t"
             "movec    %%d0,%%dtt0\n\t"
	     ".chip 68k\n\t"
             "jmp   %0@\n\t"
             : /* no outputs */
             : "a" (jmp_addr040)
             : "d0" );
      jmp_addr_label040:
        __asm__ __volatile__
          ("moveq #0,%/d0\n\t"
	   "nop\n\t"
	   ".chip 68040\n\t"
	   "cinva %%bc\n\t"
	   "pflusha\n\t"
	   "movec %%d0,%%tc\n\t"
	   ".chip 68k\n\t"
           "jmp %0@"
           : /* no outputs */
           : "a" (reset_addr)
           : "d0");
    }
    else
        __asm__ __volatile__
            ("pmove %0@,%/tc\n\t"
             "jmp %1@"
             : /* no outputs */
             : "a" (&tc_val), "a" (reset_addr));
}


static void atari_get_model(char *model)
{
    strcpy(model, "Atari ");
    switch (atari_mch_cookie >> 16) {
	case ATARI_MCH_ST:
	    if (ATARIHW_PRESENT(MSTE_CLK))
		strcat (model, "Mega ST");
	    else
		strcat (model, "ST");
	    break;
	case ATARI_MCH_STE:
	    if ((atari_mch_cookie & 0xffff) == 0x10)
		strcat (model, "Mega STE");
	    else
		strcat (model, "STE");
	    break;
	case ATARI_MCH_TT:
	    if (is_medusa)
		/* Medusa has TT _MCH cookie */
		strcat (model, "Medusa");
	    else if (is_hades)
		strcat(model, "Hades");
	    else
		strcat (model, "TT");
	    break;
	case ATARI_MCH_FALCON:
	    strcat (model, "Falcon");
	    break;
	default:
	    sprintf (model + strlen (model), "(unknown mach cookie 0x%lx)",
		     atari_mch_cookie);
	    break;
    }
}


static int atari_get_hardware_list(char *buffer)
{
    int len = 0, i;

    for (i = 0; i < m68k_num_memory; i++)
	len += sprintf (buffer+len, "\t%3ld MB at 0x%08lx (%s)\n",
			m68k_memory[i].size >> 20, m68k_memory[i].addr,
			(m68k_memory[i].addr & 0xff000000 ?
			 "alternate RAM" : "ST-RAM"));

#define ATARIHW_ANNOUNCE(name,str)				\
    if (ATARIHW_PRESENT(name))			\
	len += sprintf (buffer + len, "\t%s\n", str)

    len += sprintf (buffer + len, "Detected hardware:\n");
    ATARIHW_ANNOUNCE(STND_SHIFTER, "ST Shifter");
    ATARIHW_ANNOUNCE(EXTD_SHIFTER, "STe Shifter");
    ATARIHW_ANNOUNCE(TT_SHIFTER, "TT Shifter");
    ATARIHW_ANNOUNCE(VIDEL_SHIFTER, "Falcon Shifter");
    ATARIHW_ANNOUNCE(YM_2149, "Programmable Sound Generator");
    ATARIHW_ANNOUNCE(PCM_8BIT, "PCM 8 Bit Sound");
    ATARIHW_ANNOUNCE(CODEC, "CODEC Sound");
    ATARIHW_ANNOUNCE(TT_SCSI, "SCSI Controller NCR5380 (TT style)");
    ATARIHW_ANNOUNCE(ST_SCSI, "SCSI Controller NCR5380 (Falcon style)");
    ATARIHW_ANNOUNCE(ACSI, "ACSI Interface");
    ATARIHW_ANNOUNCE(IDE, "IDE Interface");
    ATARIHW_ANNOUNCE(FDCSPEED, "8/16 Mhz Switch for FDC");
    ATARIHW_ANNOUNCE(ST_MFP, "Multi Function Peripheral MFP 68901");
    ATARIHW_ANNOUNCE(TT_MFP, "Second Multi Function Peripheral MFP 68901");
    ATARIHW_ANNOUNCE(SCC, "Serial Communications Controller SCC 8530");
    ATARIHW_ANNOUNCE(ST_ESCC, "Extended Serial Communications Controller SCC 85230");
    ATARIHW_ANNOUNCE(ANALOG_JOY, "Paddle Interface");
    ATARIHW_ANNOUNCE(MICROWIRE, "MICROWIRE(tm) Interface");
    ATARIHW_ANNOUNCE(STND_DMA, "DMA Controller (24 bit)");
    ATARIHW_ANNOUNCE(EXTD_DMA, "DMA Controller (32 bit)");
    ATARIHW_ANNOUNCE(SCSI_DMA, "DMA Controller for NCR5380");
    ATARIHW_ANNOUNCE(SCC_DMA, "DMA Controller for SCC");
    ATARIHW_ANNOUNCE(TT_CLK, "Clock Chip MC146818A");
    ATARIHW_ANNOUNCE(MSTE_CLK, "Clock Chip RP5C15");
    ATARIHW_ANNOUNCE(SCU, "System Control Unit");
    ATARIHW_ANNOUNCE(BLITTER, "Blitter");
    ATARIHW_ANNOUNCE(VME, "VME Bus");
    ATARIHW_ANNOUNCE(DSP56K, "DSP56001 processor");

    return(len);
}
