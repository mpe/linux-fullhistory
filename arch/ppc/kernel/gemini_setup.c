/*
 *  linux/arch/ppc/kernel/setup.c
 *
 *  Copyright (C) 1995 Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Synergy Microsystems board support by Dan Cox (dan@synergymicro.com)
 *
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h> 
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/blk.h>
#include <linux/console.h>
#include <linux/openpic.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/m48t35.h>
#include <asm/gemini.h>

#include "time.h"
#include "local_irq.h"
#include "open_pic.h"

void gemini_setup_pci_ptrs(void);

static int l2_printed = 0;
static unsigned char gemini_switch_map = 0;
static char *gemini_board_families[] = {
	"VGM", "VSS", "KGM", "VGR", "KSS"
};

static char *gemini_memtypes[] = {
	"EDO DRAM, 60nS", "SDRAM, 15nS, CL=2", "SDRAM, 15nS, CL=2 with ECC"
};

static unsigned int cpu_7xx[16] = {
	0, 15, 14, 0, 0, 13, 5, 9, 6, 11, 8, 10, 16, 12, 7, 0
};
static unsigned int cpu_6xx[16] = {
	0, 0, 14, 0, 0, 13, 5, 9, 6, 11, 8, 10, 0, 12, 7, 0
};


static inline unsigned long _get_HID1(void)
{
	unsigned long val;

	__asm__ __volatile__("mfspr %0,1009" : "=r" (val));
	return val;
}

int
gemini_get_cpuinfo(char *buffer)
{
	int i, len;
	unsigned char reg, rev;
	char *family;
	unsigned int type;

	reg = readb(GEMINI_FEAT);
	family = gemini_board_families[((reg>>4) & 0xf)];
	if (((reg>>4) & 0xf) > 2)
		printk(KERN_ERR "cpuinfo(): unable to determine board family\n");

	reg = readb(GEMINI_BREV);
	type = (reg>>4) & 0xf;
	rev = reg & 0xf;

	reg = readb(GEMINI_BECO);

	len = sprintf( buffer, "machine\t\t: Gemini %s%d, rev %c, eco %d\n", 
		       family, type, (rev + 'A'), (reg & 0xf));

	len += sprintf( buffer+len, "vendor\t\t: %s\n", 
			(_get_PVR() & (1<<15)) ? "IBM" : "Motorola");

	reg = readb(GEMINI_MEMCFG);
	len += sprintf( buffer+len, "memory type\t: %s\n", 
			gemini_memtypes[(reg & 0xc0)>>6]);
	len += sprintf( buffer+len, "switches on\t: ");
	for( i=0; i < 8; i++ ) {
		if ( gemini_switch_map & (1<<i))
			len += sprintf(buffer+len, "%d ", i);
	}
	len += sprintf(buffer+len, "\n");

	return len;
}

static u_char gemini_openpic_initsenses[] = {
	1,
	1,
	1,
	1,
	0,
	0,
	1, /* remainder are level-triggered */
};

#define GEMINI_MPIC_ADDR (0xfcfc0000)
#define GEMINI_MPIC_PCI_CFG (0x80005800)

void __init gemini_openpic_init(void)
{
	grackle_write(GEMINI_MPIC_PCI_CFG + PCI_BASE_ADDRESS_0, 
		      GEMINI_MPIC_ADDR);
	grackle_write(GEMINI_MPIC_PCI_CFG + PCI_COMMAND, PCI_COMMAND_MEMORY);

	OpenPIC = (volatile struct OpenPIC *) GEMINI_MPIC_ADDR;
	OpenPIC_InitSenses = gemini_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof( gemini_openpic_initsenses );

	ioremap( GEMINI_MPIC_ADDR, sizeof( struct OpenPIC ));
}


extern unsigned long loops_per_sec;
extern int root_mountflags;
extern char cmd_line[];


void __init gemini_setup_arch(unsigned long *memstart, unsigned long *memend)
{
	unsigned int cpu;
	extern char cmd_line[];


	loops_per_sec = 50000000;

#ifdef CONFIG_BLK_DEV_INITRD
	/* bootable off CDROM */
	if (initrd_start)
		ROOT_DEV = MKDEV(SCSI_CDROM_MAJOR, 0);
	else
#endif
		ROOT_DEV = to_kdev_t(0x0801);

	/* nothing but serial consoles... */  
	sprintf(cmd_line, "%s console=ttyS0", cmd_line);


	/* The user switches on the front panel can be used as follows:

	   Switch 0 - adds "debug" to the command line for verbose boot info,
	   Switch 7 - boots in single-user mode 

	*/

	gemini_switch_map = readb( GEMINI_USWITCH );
  
	if ( gemini_switch_map & (1<<GEMINI_SWITCH_VERBOSE))
		sprintf(cmd_line, "%s debug", cmd_line);

	if ( gemini_switch_map & (1<<GEMINI_SWITCH_SINGLE_USER))
		sprintf(cmd_line, "%s single", cmd_line);

	printk("Boot arguments: %s\n", cmd_line);

	/* mutter some kind words about who made the CPU */
	cpu = _get_PVR();
	printk("CPU manufacturer: %s [rev=%04x]\n", (cpu & (1<<15)) ? "IBM" :
	       "Motorola", (cpu & 0xffff));

	/* take special pains to map the MPIC, since it isn't mapped yet */
	gemini_openpic_init();

	/* start the L2 */
	gemini_init_l2();

}


int
gemini_get_clock_speed(void)
{
	unsigned long hid1;
	int clock;
	unsigned char reg;
  
	hid1 = _get_HID1();
	if ((_get_PVR()>>16) == 8)
		hid1 = cpu_7xx[hid1];
	else
		hid1 = cpu_6xx[hid1];

	reg = readb(GEMINI_BSTAT) & 0xc0;

	switch( reg >> 2 ) {

	case 0:
	default:
		clock = (hid1*100)/3;
		break;
  
	case 1:
		clock = (hid1*125)/3;
		break;
  
	case 2:
		clock = (hid1*50)/3;
		break;
	}

	return clock;
}


#define L2CR_PIPE_LATEWR   (0x01800000)   /* late-write SRAM */
#define L2CR_L2CTL         (0x00100000)   /* RAM control */
#define L2CR_INST_DISABLE  (0x00400000)   /* disable for insn's */
#define L2CR_L2I           (0x00200000)   /* global invalidate */
#define L2CR_L2E           (0x80000000)   /* enable */
#define L2CR_L2WT          (0x00080000)   /* write-through */

void __init gemini_init_l2(void)
{
	unsigned char reg;
	unsigned long cache;
	int speed;

	reg = readb(GEMINI_L2CFG);

	/* 750's L2 initializes differently from a 604's.  Also note that a Grackle
	   bug will hang a dual-604 board, so make sure that doesn't happen by not
	   turning on the L2 */
	if ( _get_PVR() >> 16 != 8 ) {
     
		/* check for dual cpus and cry sadly about the loss of an L2... */
		if ((( readb(GEMINI_CPUSTAT) & 0x0c ) >> 2) != 1) 
			printk("Sorry. Your dual-604 does not allow the L2 to be enabled due "
			       "to a Grackle bug.\n");
		else if ( reg & GEMINI_L2_SIZE_MASK ) {
			printk("Enabling 604 L2 cache: %dKb\n", 
			       (128<<((reg & GEMINI_L2_SIZE_MASK)>>6)));
			writeb( 1, GEMINI_L2CFG );
		}
	}

	/* do a 750 */
	else {
		/* Synergy's first round of 750 boards had the L2 size stuff into the
		   board register above.  If it's there, it's used; if not, the
		   standard default is 1Mb.  The L2 type, I'm told, is "most likely
		   probably always going to be late-write".  --Dan */

		if (reg & 0xc0) {
			if (!l2_printed) {
				printk("Enabling 750 L2 cache: %dKb\n", 
				       (128 << ((reg & 0xc0)>>6)));
				l2_printed=1;
			}
    
			/* take the size given */
			cache = (((reg>>6) & 0x3)<<28);
		}
		else
			/* default of 1Mb */
			cache = 0x3<<28;
 
		reg &= 0x3;

		/* a cache ratio of 1:1 and CPU clock speeds in excess of 300Mhz are bad
		   things.  If found, tune it down to 1:1.5.  -- Dan */
		if (!reg) {

			speed = gemini_get_clock_speed();
   
			if (speed >= 300) {
				printk("Warning:  L2 ratio is 1:1 on a %dMhz processor.  Dropping to 1:1.5.\n",
				       speed );
				printk("Contact Synergy Microsystems for an ECO to fix this problem\n");
				reg = 0x1;
			}
		}

		/* standard stuff */
		cache |= ((1<<reg)<<25);
#ifdef __SMP__
		/* A couple errata for the 750's (both IBM and Motorola silicon)
		   note that you can get missed cache lines on MP implementations.
		   The workaround - if you call it that - is to make the L2
		   write-through.  This is fixed in IBM's 3.1 rev (I'm told), but
		   for now, always make 2.x versions use L2 write-through. --Dan */
		if (((_get_PVR()>>8) & 0xf) <= 2)
			cache |= L2CR_L2WT;
#endif
		cache |= L2CR_PIPE_LATEWR|L2CR_L2CTL|L2CR_INST_DISABLE;
		_set_L2CR(0);
		_set_L2CR(cache|L2CR_L2I|L2CR_L2E);
	}
}

void
gemini_restart(char *cmd)
{
	__cli();
	/* make a clean restart, not via the MPIC */
	_gemini_reboot();
	for(;;);
}

void
gemini_power_off(void)
{
	for(;;);
}

void
gemini_halt(void)
{
	gemini_restart(NULL);
}

void __init gemini_init_IRQ(void)
{
	int i;

	/* gemini has no 8259 */
	open_pic.irq_offset = 0;
	for( i=0; i < 16; i++ ) 
		irq_desc[i].ctl = &open_pic;
	openpic_init(1);
}

#define gemini_rtc_read(x)       (readb(GEMINI_RTC+(x)))
#define gemini_rtc_write(val,x)  (writeb((val),(GEMINI_RTC+(x))))

/* ensure that the RTC is up and running */
void __init gemini_time_init(void)
{
	unsigned char reg;

	reg = gemini_rtc_read(M48T35_RTC_CONTROL);

	if ( reg & M48T35_RTC_STOPPED ) {
		printk(KERN_INFO "M48T35 real-time-clock was stopped. Now starting...\n");
		gemini_rtc_write((reg & ~(M48T35_RTC_STOPPED)), M48T35_RTC_CONTROL);
		gemini_rtc_write((reg | M48T35_RTC_SET), M48T35_RTC_CONTROL);
	}
}

#undef DEBUG_RTC

unsigned long
gemini_get_rtc_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	unsigned char reg;

	reg = gemini_rtc_read(M48T35_RTC_CONTROL);
	gemini_rtc_write((reg|M48T35_RTC_READ), M48T35_RTC_CONTROL);
#ifdef DEBUG_RTC
	printk("get rtc: reg = %x\n", reg);
#endif
  
	do {
		sec = gemini_rtc_read(M48T35_RTC_SECONDS);
		min = gemini_rtc_read(M48T35_RTC_MINUTES);
		hour = gemini_rtc_read(M48T35_RTC_HOURS);
		day = gemini_rtc_read(M48T35_RTC_DOM);
		mon = gemini_rtc_read(M48T35_RTC_MONTH);
		year = gemini_rtc_read(M48T35_RTC_YEAR);
	} while( sec != gemini_rtc_read(M48T35_RTC_SECONDS));
#ifdef DEBUG_RTC
	printk("get rtc: sec=%x, min=%x, hour=%x, day=%x, mon=%x, year=%x\n", 
	       sec, min, hour, day, mon, year);
#endif

	gemini_rtc_write(reg, M48T35_RTC_CONTROL);

	BCD_TO_BIN(sec);
	BCD_TO_BIN(min);
	BCD_TO_BIN(hour);
	BCD_TO_BIN(day);
	BCD_TO_BIN(mon);
	BCD_TO_BIN(year);

	if ((year += 1900) < 1970)
		year += 100;
#ifdef DEBUG_RTC
	printk("get rtc: sec=%x, min=%x, hour=%x, day=%x, mon=%x, year=%x\n", 
	       sec, min, hour, day, mon, year);
#endif

	return mktime( year, mon, day, hour, min, sec );
}


int
gemini_set_rtc_time( unsigned long now )
{
	unsigned char reg;
	struct rtc_time tm;

	to_tm( now, &tm );

	reg = gemini_rtc_read(M48T35_RTC_CONTROL);
#if DEBUG_RTC
	printk("set rtc: reg = %x\n", reg);
#endif
  
	gemini_rtc_write((reg|M48T35_RTC_SET), M48T35_RTC_CONTROL);
#if DEBUG_RTC
	printk("set rtc: tm vals - sec=%x, min=%x, hour=%x, mon=%x, mday=%x, year=%x\n",
	       tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mon, tm.tm_mday, tm.tm_year);
#endif
  
	tm.tm_year -= 1900;
	BIN_TO_BCD(tm.tm_sec);
	BIN_TO_BCD(tm.tm_min);
	BIN_TO_BCD(tm.tm_hour);
	BIN_TO_BCD(tm.tm_mon);
	BIN_TO_BCD(tm.tm_mday);
	BIN_TO_BCD(tm.tm_year);
#ifdef DEBUG_RTC
	printk("set rtc: tm vals - sec=%x, min=%x, hour=%x, mon=%x, mday=%x, year=%x\n",
	       tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mon, tm.tm_mday, tm.tm_year);
#endif

	gemini_rtc_write(tm.tm_sec, M48T35_RTC_SECONDS);
	gemini_rtc_write(tm.tm_min, M48T35_RTC_MINUTES);
	gemini_rtc_write(tm.tm_hour, M48T35_RTC_HOURS);
	gemini_rtc_write(tm.tm_mday, M48T35_RTC_DOM);
	gemini_rtc_write(tm.tm_mon, M48T35_RTC_MONTH);
	gemini_rtc_write(tm.tm_year, M48T35_RTC_YEAR);

	/* done writing */
	gemini_rtc_write(reg, M48T35_RTC_CONTROL);

	if ((time_state == TIME_ERROR) || (time_state == TIME_BAD))
		time_state = TIME_OK;
  
	return 0;
}

/*  use the RTC to determine the decrementer count */
void __init gemini_calibrate_decr(void)
{
	int freq, divisor;
	unsigned char reg;

	/* determine processor bus speed */
	reg = readb(GEMINI_BSTAT);

	switch(((reg & 0x0c)>>2)&0x3) {
	case 0:
	default:
		freq = 66;
		break;
	case 1:
		freq = 83;
		break;
	case 2:
		freq = 100;
		break;
	}

	freq *= 1000000;
	divisor = 4;
	decrementer_count = freq / HZ / divisor;
	count_period_num = divisor;
	count_period_den = freq / 1000000;
}


void __init gemini_init(unsigned long r3, unsigned long r4, unsigned long r5,
			unsigned long r6, unsigned long r7)
{
	void chrp_do_IRQ(struct pt_regs *, int, int);
	void layout_bus( struct pci_bus * );
 
	gemini_setup_pci_ptrs();

	ISA_DMA_THRESHOLD = 0;
	DMA_MODE_READ = 0;
	DMA_MODE_WRITE = 0;

#ifdef CONFIG_BLK_DEV_INITRD
	if ( r4 )
	{
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif

	ppc_md.setup_arch = gemini_setup_arch;
	ppc_md.setup_residual = NULL;
	ppc_md.get_cpuinfo = gemini_get_cpuinfo;
	ppc_md.irq_cannonicalize = NULL;
	ppc_md.init_IRQ = gemini_init_IRQ;
	ppc_md.do_IRQ = chrp_do_IRQ;
	ppc_md.init = NULL;

	ppc_md.restart = gemini_restart;
	ppc_md.power_off = gemini_power_off;
	ppc_md.halt = gemini_halt;

	ppc_md.time_init = gemini_time_init;
	ppc_md.set_rtc_time = gemini_set_rtc_time;
	ppc_md.get_rtc_time = gemini_get_rtc_time;
	ppc_md.calibrate_decr = gemini_calibrate_decr;

	/* no keyboard/mouse/video stuff yet.. */
	ppc_md.kbd_setkeycode = NULL;
	ppc_md.kbd_getkeycode = NULL;
	ppc_md.kbd_translate = NULL;
	ppc_md.kbd_unexpected_up = NULL;
	ppc_md.kbd_leds = NULL;
	ppc_md.kbd_init_hw = NULL;
#ifdef CONFIG_MAGIC_SYSRQ
	ppc_md.ppc_kbd_sysrq_xlate = NULL;
#endif
	ppc_md.pcibios_fixup_bus = layout_bus;
}
