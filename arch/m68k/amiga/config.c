/*
 *  linux/amiga/config.c
 *
 *  Copyright (C) 1993 Hamish Macdonald
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */

/*
 * Miscellaneous Amiga stuff
 */

#include <stdarg.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/linkage.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/irq.h>
#include <asm/machdep.h>

u_long amiga_masterclock;
u_long amiga_colorclock;

extern char m68k_debug_device[];

extern void amiga_sched_init(isrfunc handler);
extern int amiga_keyb_init(void);
extern int amiga_kbdrate (struct kbd_repeat *);
extern void amiga_init_INTS (void);
extern int amiga_add_isr (unsigned long, isrfunc, int, void *, char *);
extern int amiga_remove_isr (unsigned long, isrfunc);
extern int amiga_get_irq_list (char *, int);
extern void amiga_enable_irq(unsigned int);
extern void amiga_disable_irq(unsigned int);
extern unsigned long amiga_gettimeoffset (void);
extern void a3000_gettod (int *, int *, int *, int *, int *, int *);
extern void a2000_gettod (int *, int *, int *, int *, int *, int *);
extern int amiga_hwclk (int, struct hwclk_time *);
extern int amiga_set_clock_mmss (unsigned long);
extern void amiga_check_partition (struct gendisk *hd, unsigned int dev);
extern void amiga_mksound( unsigned int count, unsigned int ticks );
#ifdef CONFIG_BLK_DEV_FD
extern int amiga_floppy_init (void);
extern void amiga_floppy_setup(char *, int *);
#endif
extern void amiga_reset (void);
extern void amiga_waitbut(void);
extern struct consw fb_con;
extern struct fb_info *amiga_fb_init(long *);
extern void zorro_init(void);
static void ami_savekmsg_init(void);
static void ami_mem_print(const char *b);
extern void amiga_debug_init(void);
extern void amiga_video_setup(char *, int *);

extern void (*kd_mksound)(unsigned int, unsigned int);

void config_amiga(void)
{
  char *type = NULL;

  switch(boot_info.bi_amiga.model) {
  case AMI_500:
    type = "A500";
    break;
  case AMI_500PLUS:
    type = "A500+";
    break;
  case AMI_600:
    type = "A600";
    break;
  case AMI_1000:
    type = "A1000";
    break;
  case AMI_1200:
    type = "A1200";
    break;
  case AMI_2000:
    type = "A2000";
    break;
  case AMI_2500:
    type = "A2500";
    break;
  case AMI_3000:
    type = "A3000";
    break;
  case AMI_3000T:
    type = "A3000T";
    break;
  case AMI_3000PLUS:
    type = "A3000+";
    break;
  case AMI_4000:
    type = "A4000";
    break;
  case AMI_4000T:
    type = "A4000T";
    break;
  case AMI_CDTV:
    type = "CDTV";
    break;
  case AMI_CD32:
    type = "CD32";
    break;
  case AMI_DRACO:
    type = "Draco";
    break;
  }
  printk("Amiga hardware found: ");
  if (type)
    printk("[%s] ", type);
  switch(boot_info.bi_amiga.model) {
  case AMI_UNKNOWN:
    goto Generic;

  case AMI_500:
  case AMI_500PLUS:
  case AMI_1000:
    AMIGAHW_SET(A2000_CLK);             /* Is this correct? */
    printk("A2000_CLK ");
    goto Generic;

  case AMI_600:
  case AMI_1200:
    AMIGAHW_SET(A1200_IDE);
    printk("A1200_IDE ");
    AMIGAHW_SET(A2000_CLK);             /* Is this correct? */
    printk("A2000_CLK ");
    goto Generic;

  case AMI_2000:
  case AMI_2500:
    AMIGAHW_SET(A2000_CLK);
    printk("A2000_CLK ");
    goto Generic;

  case AMI_3000:
  case AMI_3000T:
    AMIGAHW_SET(AMBER_FF);
    printk("AMBER_FF ");
    AMIGAHW_SET(MAGIC_REKICK);
    printk("MAGIC_REKICK ");
    /* fall through */
  case AMI_3000PLUS:
    AMIGAHW_SET(A3000_SCSI);
    printk("A3000_SCSI ");
    AMIGAHW_SET(A3000_CLK);
    printk("A3000_CLK ");
    goto Generic;

  case AMI_4000T:
    AMIGAHW_SET(A4000_SCSI);
    printk("A4000_SCSI ");
    /* fall through */
  case AMI_4000:
    AMIGAHW_SET(A4000_IDE);
    printk("A4000_IDE ");
    AMIGAHW_SET(A3000_CLK);
    printk("A3000_CLK ");
    goto Generic;

  case AMI_CDTV:
  case AMI_CD32:
    AMIGAHW_SET(CD_ROM);
    printk("CD_ROM ");
    AMIGAHW_SET(A2000_CLK);             /* Is this correct? */
    printk("A2000_CLK ");
    goto Generic;

  Generic:
    AMIGAHW_SET(AMI_VIDEO);
    AMIGAHW_SET(AMI_BLITTER);
    AMIGAHW_SET(AMI_AUDIO);
    AMIGAHW_SET(AMI_FLOPPY);
    AMIGAHW_SET(AMI_KEYBOARD);
    AMIGAHW_SET(AMI_MOUSE);
    AMIGAHW_SET(AMI_SERIAL);
    AMIGAHW_SET(AMI_PARALLEL);
    AMIGAHW_SET(CHIP_RAM);
    AMIGAHW_SET(PAULA);
    printk("VIDEO BLITTER AUDIO FLOPPY KEYBOARD MOUSE SERIAL PARALLEL "
	   "CHIP_RAM PAULA ");

    switch(boot_info.bi_amiga.chipset) {
    case CS_OCS:
    case CS_ECS:
    case CS_AGA:
      switch (custom.deniseid & 0xf) {
      case 0x0c:
	AMIGAHW_SET(DENISE_HR);
	printk("DENISE_HR");
	break;
      case 0x08:
	AMIGAHW_SET(LISA);
	printk("LISA ");
	break;
      }
      break;
    default:
      AMIGAHW_SET(DENISE);
      printk("DENISE ");
      break;
    }
    switch ((custom.vposr>>8) & 0x7f) {
    case 0x00:
      AMIGAHW_SET(AGNUS_PAL);
      printk("AGNUS_PAL ");
      break;
    case 0x10:
      AMIGAHW_SET(AGNUS_NTSC);
      printk("AGNUS_NTSC ");
      break;
    case 0x20:
    case 0x21:
      AMIGAHW_SET(AGNUS_HR_PAL);
      printk("AGNUS_HR_PAL ");
      break;
    case 0x30:
    case 0x31:
      AMIGAHW_SET(AGNUS_HR_NTSC);
      printk("AGNUS_HR_NTSC ");
      break;
    case 0x22:
    case 0x23:
      AMIGAHW_SET(ALICE_PAL);
      printk("ALICE_PAL ");
      break;
    case 0x32:
    case 0x33:
      AMIGAHW_SET(ALICE_NTSC);
      printk("ALICE_NTSC ");
      break;
    }
    AMIGAHW_SET(ZORRO);
    printk("ZORRO ");
    break;

  case AMI_DRACO:
    panic("No support for Draco yet");
 
  default:
    panic("Unknown Amiga Model");
  }
  printk("\n");
 
  mach_sched_init      = amiga_sched_init;
  mach_keyb_init       = amiga_keyb_init;
  mach_kbdrate         = amiga_kbdrate;
  mach_init_INTS       = amiga_init_INTS;
  mach_add_isr         = amiga_add_isr;
#if 0 /* ++1.3++ */
  mach_remove_isr      = amiga_remove_isr;
  mach_enable_irq      = amiga_enable_irq;
  mach_disable_irq     = amiga_disable_irq;
#endif
  mach_get_irq_list    = amiga_get_irq_list;
  mach_gettimeoffset   = amiga_gettimeoffset;
  if (AMIGAHW_PRESENT(A3000_CLK)){
    mach_gettod  = a3000_gettod;
    mach_max_dma_address = 0xffffffff; /*
					* default MAX_DMA 0xffffffff
					* on Z3 machines - we should
					* consider adding something
					* like a dma_mask in kmalloc
					* later on, so people using Z2
					* boards in Z3 machines won't
					* get into trouble - Jes
					*/
  }
  else{ /* if (AMIGAHW_PRESENT(A2000_CLK)) */
    mach_gettod  = a2000_gettod;
    mach_max_dma_address = 0x00ffffff; /*
					* default MAX_DMA 0x00ffffff
					* on Z2 machines.
					*/
  }
  mach_hwclk           = amiga_hwclk;
  mach_set_clock_mmss  = amiga_set_clock_mmss;
  mach_check_partition = amiga_check_partition;
  mach_mksound         = amiga_mksound;
#ifdef CONFIG_BLK_DEV_FD
  mach_floppy_init     = amiga_floppy_init;
  mach_floppy_setup    = amiga_floppy_setup;
#endif
  mach_reset           = amiga_reset;
  waitbut              = amiga_waitbut;
  conswitchp           = &fb_con;
  mach_fb_init         = amiga_fb_init;
  mach_debug_init      = amiga_debug_init;
  mach_video_setup     = amiga_video_setup;
  kd_mksound           = amiga_mksound;

  /* Fill in the clock values (based on the 700 kHz E-Clock) */
  amiga_masterclock = 40*amiga_eclock;	/* 28 MHz */
  amiga_colorclock = 5*amiga_eclock;		/* 3.5 MHz */

  /* clear all DMA bits */
  custom.dmacon = DMAF_ALL;
  /* ensure that the DMA master bit is set */
  custom.dmacon = DMAF_SETCLR | DMAF_MASTER;

  /* initialize chipram allocator */
  amiga_chip_init ();

  /* initialize only once here, not every time the debug level is raised */
  if (!strcmp( m68k_debug_device, "mem" ))
    ami_savekmsg_init();

  /*
   * if it is an A3000, set the magic bit that forces
   * a hard rekick
   */
  if (AMIGAHW_PRESENT(MAGIC_REKICK))
    *(u_char *)ZTWO_VADDR(0xde0002) |= 0x80;

  zorro_init();
#ifdef CONFIG_ZORRO
  /*
   * Identify all known AutoConfig Expansion Devices
   */
  zorro_identify();
#endif /* CONFIG_ZORRO */
}

extern long time_finetune;	/* from kernel/sched.c */

static unsigned short jiffy_ticks;

#if 1 /* ++1.3++ */
static void timer_wrapper( int irq, struct pt_regs *fp, void *otimerf )
 {
   unsigned short flags, old_flags;

   ciab.icr = 0x01;

   save_flags(flags);
   old_flags = (flags & ~0x0700) | (fp->sr & 0x0700);
   
   restore_flags(old_flags);

   (*(isrfunc)otimerf)( irq, fp, NULL );

   restore_flags(flags);
   ciab.icr = 0x81;
}
#endif

void amiga_sched_init (isrfunc timer_routine)
{

#if 0 /* XXX */ /* I think finetune was removed by the 1.3.29 patch */
    double finetune;
#endif

    jiffy_ticks = (amiga_eclock+50)/100;
#if 0 /* XXX */
    finetune = (jiffy_ticks-amiga_eclock/HZ)/amiga_eclock*1000000*(1<<24);
    time_finetune = finetune+0.5;
#endif

    ciab.cra &= 0xC0;	 /* turn off timer A, continuous mode, from Eclk */
    ciab.talo = jiffy_ticks % 256;
    ciab.tahi = jiffy_ticks / 256;
    /* CIA interrupts when counter underflows, so adjust ticks by 1 */
    jiffy_ticks -= 1;

    /* install interrupt service routine for CIAB Timer A */
    /*
     * Please don't change this to use ciaa, as it interferes with the
     * SCSI code. We'll have to take a look at this later
     */
#if 0
    add_isr (IRQ_AMIGA_CIAB_TA, timer_routine, 0, NULL, "timer");
#else
    add_isr (IRQ_AMIGA_CIAB_TA, timer_wrapper, 0, timer_routine, "timer");
#endif
    /* start timer */
    ciab.cra |= 0x01;
}

#define TICK_SIZE 10000

/* This is always executed with interrupts disabled.  */
unsigned long amiga_gettimeoffset (void)
{
    unsigned short hi, lo, hi2;
    unsigned long ticks, offset = 0;

    /* read CIA A timer A current value */
    hi	= ciab.tahi;
    lo	= ciab.talo;
    hi2 = ciab.tahi;

    if (hi != hi2) {
	lo = ciab.talo;
	hi = hi2;
    }

    ticks = hi << 8 | lo;

#if 0 /* XXX */
/* reading the ICR clears all interrupts.  bad idea! */
      if (ticks > jiffy_ticks - jiffy_ticks / 100)
	/* check for pending interrupt */
	if (ciab.icr & CIA_ICR_TA)
	  offset = 10000;
#endif

    ticks = (jiffy_ticks-1) - ticks;
    ticks = (10000 * ticks) / jiffy_ticks;

    return ticks + offset;
}

void a3000_gettod (int *yearp, int *monp, int *dayp,
		   int *hourp, int *minp, int *secp)
{
	volatile struct tod3000 *tod = TOD_3000;

	tod->cntrl1 = TOD3000_CNTRL1_HOLD;

	*secp  = tod->second1 * 10 + tod->second2;
	*minp  = tod->minute1 * 10 + tod->minute2;
	*hourp = tod->hour1   * 10 + tod->hour2;
	*dayp  = tod->day1    * 10 + tod->day2;
	*monp  = tod->month1  * 10 + tod->month2;
	*yearp = tod->year1   * 10 + tod->year2;

	tod->cntrl1 = TOD3000_CNTRL1_FREE;
}

void a2000_gettod (int *yearp, int *monp, int *dayp,
		   int *hourp, int *minp, int *secp)
{
	volatile struct tod2000 *tod = TOD_2000;

	tod->cntrl1 = TOD2000_CNTRL1_HOLD;

	while (tod->cntrl1 & TOD2000_CNTRL1_BUSY)
		;

	*secp  = tod->second1     * 10 + tod->second2;
	*minp  = tod->minute1     * 10 + tod->minute2;
	*hourp = (tod->hour1 & 3) * 10 + tod->hour2;
	*dayp  = tod->day1        * 10 + tod->day2;
	*monp  = tod->month1      * 10 + tod->month2;
	*yearp = tod->year1       * 10 + tod->year2;

	if (!(tod->cntrl3 & TOD2000_CNTRL3_24HMODE))
		if ((!tod->hour1 & TOD2000_HOUR1_PM) && *hourp == 12)
			*hourp = 0;
		else if ((tod->hour1 & TOD2000_HOUR1_PM) && *hourp != 12)
			*hourp += 12;

	tod->cntrl1 &= ~TOD2000_CNTRL1_HOLD;
}

int amiga_hwclk(int op, struct hwclk_time *t)
{
	if (AMIGAHW_PRESENT(A3000_CLK)) {
		volatile struct tod3000 *tod = TOD_3000;

		tod->cntrl1 = TOD3000_CNTRL1_HOLD;

		if (!op) { /* read */
			t->sec  = tod->second1 * 10 + tod->second2;
			t->min  = tod->minute1 * 10 + tod->minute2;
			t->hour = tod->hour1   * 10 + tod->hour2;
			t->day  = tod->day1    * 10 + tod->day2;
			t->wday = tod->weekday;
			t->mon  = tod->month1  * 10 + tod->month2 - 1;
			t->year = tod->year1   * 10 + tod->year2;
		} else {
			tod->second1 = t->sec / 10;
			tod->second2 = t->sec % 10;
			tod->minute1 = t->min / 10;
			tod->minute2 = t->min % 10;
			tod->hour1   = t->hour / 10;
			tod->hour2   = t->hour % 10;
			tod->day1    = t->day / 10;
			tod->day2    = t->day % 10;
			if (t->wday != -1)
				tod->weekday = t->wday;
			tod->month1  = (t->mon + 1) / 10;
			tod->month2  = (t->mon + 1) % 10;
			tod->year1   = t->year / 10;
			tod->year2   = t->year % 10;
		}

		tod->cntrl1 = TOD3000_CNTRL1_FREE;
	} else /* if (AMIGAHW_PRESENT(A2000_CLK)) */ {
		volatile struct tod2000 *tod = TOD_2000;

		tod->cntrl1 = TOD2000_CNTRL1_HOLD;
	    
		while (tod->cntrl1 & TOD2000_CNTRL1_BUSY)
			;

		if (!op) { /* read */
			t->sec  = tod->second1     * 10 + tod->second2;
			t->min  = tod->minute1     * 10 + tod->minute2;
			t->hour = (tod->hour1 & 3) * 10 + tod->hour2;
			t->day  = tod->day1        * 10 + tod->day2;
			t->wday = tod->weekday;
			t->mon  = tod->month1      * 10 + tod->month2 - 1;
			t->year = tod->year1       * 10 + tod->year2;

			if (!(tod->cntrl3 & TOD2000_CNTRL3_24HMODE))
				if ((!tod->hour1 & TOD2000_HOUR1_PM) && t->hour == 12)
					t->hour = 0;
				else if ((tod->hour1 & TOD2000_HOUR1_PM) && t->hour != 12)
					t->hour += 12;
		} else {
			tod->second1 = t->sec / 10;
			tod->second2 = t->sec % 10;
			tod->minute1 = t->min / 10;
			tod->minute2 = t->min % 10;
			if (tod->cntrl3 & TOD2000_CNTRL3_24HMODE)
				tod->hour1 = t->hour / 10;
			else if (t->hour >= 12)
				tod->hour1 = TOD2000_HOUR1_PM +
					(t->hour - 12) / 10;
			else
				tod->hour1 = t->hour / 10;
			tod->hour2   = t->hour % 10;
			tod->day1    = t->day / 10;
			tod->day2    = t->day % 10;
			if (t->wday != -1)
				tod->weekday = t->wday;
			tod->month1  = (t->mon + 1) / 10;
			tod->month2  = (t->mon + 1) % 10;
			tod->year1   = t->year / 10;
			tod->year2   = t->year % 10;
		}

		tod->cntrl1 &= ~TOD2000_CNTRL1_HOLD;
	}

	return 0;
}

int amiga_set_clock_mmss (unsigned long nowtime)
{
	short real_seconds = nowtime % 60, real_minutes = (nowtime / 60) % 60;

	if (AMIGAHW_PRESENT(A3000_CLK)) {
		volatile struct tod3000 *tod = TOD_3000;

		tod->cntrl1 = TOD3000_CNTRL1_HOLD;

		tod->second1 = real_seconds / 10;
		tod->second2 = real_seconds % 10;
		tod->minute1 = real_minutes / 10;
		tod->minute2 = real_minutes % 10;
		
		tod->cntrl1 = TOD3000_CNTRL1_FREE;
	} else /* if (AMIGAHW_PRESENT(A2000_CLK)) */ {
		volatile struct tod2000 *tod = TOD_2000;

		tod->cntrl1 = TOD2000_CNTRL1_HOLD;
	    
		while (tod->cntrl1 & TOD2000_CNTRL1_BUSY)
			;

		tod->second1 = real_seconds / 10;
		tod->second2 = real_seconds % 10;
		tod->minute1 = real_minutes / 10;
		tod->minute2 = real_minutes % 10;

		tod->cntrl1 &= ~TOD2000_CNTRL1_HOLD;
	}

	return 0;
}

void amiga_waitbut (void)
{
    int i;

    while (1) {
	while (ciaa.pra & 0x40);

	/* debounce */
	for (i = 0; i < 1000; i++);

	if (!(ciaa.pra & 0x40))
	    break;
    }

    /* wait for button up */
    while (1) {
	while (!(ciaa.pra & 0x40));

	/* debounce */
	for (i = 0; i < 1000; i++);

	if (ciaa.pra & 0x40)
	    break;
    }
}

void ami_serial_print (const char *str)
{
    while (*str) {
        if (*str == '\n') {
            custom.serdat = (unsigned char)'\r' | 0x100;
            while (!(custom.serdatr & 0x2000))
                ;
        }
        custom.serdat = (unsigned char)*str++ | 0x100;
        while (!(custom.serdatr & 0x2000))
            ;
    }
}

void amiga_debug_init (void)
{
    extern void (*debug_print_proc)(const char *);

    if (!strcmp( m68k_debug_device, "ser" )) {
        /* no initialization required (?) */
        debug_print_proc = ami_serial_print;
    } else if (!strcmp( m68k_debug_device, "mem" )) {
        /* already initialized by config_amiga() (needed only once) */
        debug_print_proc = ami_mem_print;
    }
}

void dbprintf(const char *fmt , ...)
{
	static char buf[1024];
	va_list args;
	extern void console_print (const char *str);
	extern int vsprintf(char * buf, const char * fmt, va_list args);

	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);

	console_print (buf);
}

NORET_TYPE void amiga_reset( void )
    ATTRIB_NORET;

void amiga_reset (void)
{
  unsigned long jmp_addr040 = VTOP(&&jmp_addr_label040);
  unsigned long jmp_addr = VTOP(&&jmp_addr_label);

  cli();
  if (m68k_is040or060)
    /* Setup transparent translation registers for mapping
     * of 16 MB kernel segment before disabling translation
     */
    __asm__ __volatile__
      ("movel    %0,%/d0\n\t"
       "andl     #0xff000000,%/d0\n\t"
       "orw      #0xe020,%/d0\n\t"   /* map 16 MB, enable, cacheable */
       ".long    0x4e7b0004\n\t"   /* movec d0,itt0 */
       ".long    0x4e7b0006\n\t"   /* movec d0,dtt0 */
       "jmp      %0@\n\t"
       : /* no outputs */
       : "a" (jmp_addr040));
  else
    /* for 680[23]0, just disable translation and jump to the physical
     * address of the label
     */
    __asm__ __volatile__
      ("pmove  %/tc,%@\n\t"
       "bclr   #7,%@\n\t"
       "pmove  %@,%/tc\n\t"
       "jmp    %0@\n\t"
       : /* no outputs */
       : "a" (jmp_addr));
 jmp_addr_label040:
  /* disable translation on '040 now */
  __asm__ __volatile__    
    ("moveq #0,%/d0\n\t"
     ".long 0x4e7b0003\n\t"         /* movec d0,tc; disable MMU */
     : /* no outputs */
     : /* no inputs */
     : "d0");

 jmp_addr_label:
  /* pickup reset address from AmigaOS ROM, reset devices and jump
   * to reset address
   */
  __asm__ __volatile__
    ("movew #0x2700,%/sr\n\t"
     "leal  0x01000000,%/a0\n\t"
     "subl  %/a0@(-0x14),%/a0\n\t"
     "movel %/a0@(4),%/a0\n\t"
     "subql #2,%/a0\n\t"
     "bra   1f\n\t"
     /* align on a longword boundary */
     __ALIGN_STR "\n"
     "1:\n\t"
     "reset\n\t"
     "jmp   %/a0@" : /* Just that gcc scans it for % escapes */ );
  
  for (;;);

}

extern void *amiga_chip_alloc(long size);


#define SAVEKMSG_MAXMEM		128*1024


#define SAVEKMSG_MAGIC1		0x53415645	/* 'SAVE' */
#define SAVEKMSG_MAGIC2		0x4B4D5347	/* 'KMSG' */

struct savekmsg {
    u_long magic1;		/* SAVEKMSG_MAGIC1 */
    u_long magic2;		/* SAVEKMSG_MAGIC2 */
    u_long magicptr;		/* address of magic1 */
    u_long size;
    char data[0];
};

static struct savekmsg *savekmsg = NULL;


static void ami_savekmsg_init(void)
{
    savekmsg = (struct savekmsg *)amiga_chip_alloc(SAVEKMSG_MAXMEM);
    savekmsg->magic1 = SAVEKMSG_MAGIC1;
    savekmsg->magic2 = SAVEKMSG_MAGIC2;
    savekmsg->magicptr = VTOP(savekmsg);
    savekmsg->size = 0;
}


static void ami_mem_print(const char *b)
{
    int len;

    for (len = 0; b[len]; len++);
    if (savekmsg->size+len <= SAVEKMSG_MAXMEM) {
        memcpy(savekmsg->data+savekmsg->size, b, len);
        savekmsg->size += len;
    }
}


void amiga_get_model(char *model)
{
    strcpy(model, "Amiga ");
    switch (boot_info.bi_amiga.model) {
	case AMI_500:
	    strcat(model, "500");
	    break;
	case AMI_500PLUS:
	    strcat(model, "500+");
	    break;
	case AMI_600:
	    strcat(model, "600");
	    break;
	case AMI_1000:
	    strcat(model, "1000");
	    break;
	case AMI_1200:
	    strcat(model, "1200");
	    break;
	case AMI_2000:
	    strcat(model, "2000");
	    break;
	case AMI_2500:
	    strcat(model, "2500");
	    break;
	case AMI_3000:
	    strcat(model, "3000");
	    break;
	case AMI_3000T:
	    strcat(model, "3000T");
	    break;
	case AMI_3000PLUS:
	    strcat(model, "3000+");
	    break;
	case AMI_4000:
	    strcat(model, "4000");
	    break;
	case AMI_4000T:
	    strcat(model, "4000T");
	    break;
	case AMI_CDTV:
	    strcat(model, "CDTV");
	    break;
	case AMI_CD32:
	    strcat(model, "CD32");
	    break;
	case AMI_DRACO:
	    strcpy(model, "DRACO");
	    break;
    }
}


int amiga_get_hardware_list(char *buffer)
{
    int len = 0;

    if (AMIGAHW_PRESENT(CHIP_RAM))
	len += sprintf(buffer+len, "Chip RAM:\t%ldK\n",
		       boot_info.bi_amiga.chip_size>>10);
    len += sprintf(buffer+len, "PS Freq:\t%dHz\nEClock Freq:\t%ldHz\n",
		   boot_info.bi_amiga.psfreq, amiga_eclock);
    if (AMIGAHW_PRESENT(AMI_VIDEO)) {
	char *type;
	switch(boot_info.bi_amiga.chipset) {
	    case CS_OCS:
		type = "OCS";
		break;
	    case CS_ECS:
		type = "ECS";
		break;
	    case CS_AGA:
		type = "AGA";
		break;
	    default:
		type = "Old or Unknown";
		break;
	}
	len += sprintf(buffer+len, "Graphics:\t%s\n", type);
    }

#define AMIGAHW_ANNOUNCE(name, str)				\
    if (AMIGAHW_PRESENT(name))				\
	len += sprintf (buffer+len, "\t%s\n", str)

    len += sprintf (buffer + len, "Detected hardware:\n");

    AMIGAHW_ANNOUNCE(AMI_VIDEO, "Amiga Video");
    AMIGAHW_ANNOUNCE(AMI_BLITTER, "Blitter");
    AMIGAHW_ANNOUNCE(AMBER_FF, "Amber Flicker Fixer");
    AMIGAHW_ANNOUNCE(AMI_AUDIO, "Amiga Audio");
    AMIGAHW_ANNOUNCE(AMI_FLOPPY, "Floppy Controller");
    AMIGAHW_ANNOUNCE(A3000_SCSI, "SCSI Controller WD33C93 (A3000 style)");
    AMIGAHW_ANNOUNCE(A4000_SCSI, "SCSI Controller NCR53C710 (A4000T style)");
    AMIGAHW_ANNOUNCE(A1200_IDE, "IDE Interface (A1200 style)");
    AMIGAHW_ANNOUNCE(A4000_IDE, "IDE Interface (A4000 style)");
    AMIGAHW_ANNOUNCE(CD_ROM, "Internal CD ROM drive");
    AMIGAHW_ANNOUNCE(AMI_KEYBOARD, "Keyboard");
    AMIGAHW_ANNOUNCE(AMI_MOUSE, "Mouse Port");
    AMIGAHW_ANNOUNCE(AMI_SERIAL, "Serial Port");
    AMIGAHW_ANNOUNCE(AMI_PARALLEL, "Parallel Port");
    AMIGAHW_ANNOUNCE(A2000_CLK, "Hardware Clock (A2000 style)");
    AMIGAHW_ANNOUNCE(A3000_CLK, "Hardware Clock (A3000 style)");
    AMIGAHW_ANNOUNCE(CHIP_RAM, "Chip RAM");
    AMIGAHW_ANNOUNCE(PAULA, "Paula 8364");
    AMIGAHW_ANNOUNCE(DENISE, "Denise 8362");
    AMIGAHW_ANNOUNCE(DENISE_HR, "Denise 8373");
    AMIGAHW_ANNOUNCE(LISA, "Lisa 8375");
    AMIGAHW_ANNOUNCE(AGNUS_PAL, "Normal/Fat PAL Agnus 8367/8371");
    AMIGAHW_ANNOUNCE(AGNUS_NTSC, "Normal/Fat NTSC Agnus 8361/8370");
    AMIGAHW_ANNOUNCE(AGNUS_HR_PAL, "Fat Hires PAL Agnus 8372");
    AMIGAHW_ANNOUNCE(AGNUS_HR_NTSC, "Fat Hires NTSC Agnus 8372");
    AMIGAHW_ANNOUNCE(ALICE_PAL, "PAL Alice 8374");
    AMIGAHW_ANNOUNCE(ALICE_NTSC, "NTSC Alice 8374");
    AMIGAHW_ANNOUNCE(MAGIC_REKICK, "Magic Hard Rekick");
    if (AMIGAHW_PRESENT(ZORRO))
	len += sprintf(buffer+len, "\tZorro AutoConfig: %d Expansion Device%s\n",
		       boot_info.bi_amiga.num_autocon,
		       boot_info.bi_amiga.num_autocon == 1 ? "" : "s");

    return(len);
}
