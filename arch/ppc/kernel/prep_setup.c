/*
 * BK Id: SCCS/s.prep_setup.c 1.44 11/13/01 21:26:07 paulus
 */
/*
 *  linux/arch/ppc/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *
 * Support for PReP (Motorola MTX/MVME)
 * by Troy Benjegerdes (hozer@drgw.net)
 */

/*
 * bootup setup stuff..
 */

#include <linux/config.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/blk.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/timex.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/seq_file.h>

#include <asm/sections.h>
#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/cache.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/mk48t59.h>
#include <asm/prep_nvram.h>
#include <asm/raven.h>
#include <asm/keyboard.h>
#include <asm/vga.h>
#include <asm/time.h>

#include "local_irq.h"
#include "i8259.h"
#include "open_pic.h"

#if defined(CONFIG_SOUND) || defined(CONFIG_SOUND_MODULE)
#include <../drivers/sound/sound_config.h>
#include <../drivers/sound/dev_table.h>
#endif

unsigned char ucSystemType;
unsigned char ucBoardRev;
unsigned char ucBoardRevMaj, ucBoardRevMin;

extern unsigned long mc146818_get_rtc_time(void);
extern int mc146818_set_rtc_time(unsigned long nowtime);
extern unsigned long mk48t59_get_rtc_time(void);
extern int mk48t59_set_rtc_time(unsigned long nowtime);

extern unsigned char prep_nvram_read_val(int addr);
extern void prep_nvram_write_val(int addr,
				 unsigned char val);
extern unsigned char rs_nvram_read_val(int addr);
extern void rs_nvram_write_val(int addr,
				 unsigned char val);
extern void ibm_prep_init(void);

extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
		char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern void pckbd_leds(unsigned char leds);
extern void pckbd_init_hw(void);
extern unsigned char pckbd_sysrq_xlate[];

extern void prep_find_bridges(void);
extern char saved_command_line[];

int _prep_type;

#define cached_21	(((char *)(ppc_cached_irq_mask))[3])
#define cached_A1	(((char *)(ppc_cached_irq_mask))[2])

/* for the mac fs */
kdev_t boot_dev;
/* used in nasty hack for sound - see prep_setup_arch() -- Cort */
long ppc_cs4232_dma, ppc_cs4232_dma2;

extern PTE *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;
extern int probingmem;
extern unsigned long loops_per_jiffy;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

#ifdef CONFIG_SOUND_MODULE
EXPORT_SYMBOL(ppc_cs4232_dma);
EXPORT_SYMBOL(ppc_cs4232_dma2);
#endif

static int __prep
prep_show_cpuinfo(struct seq_file *m)
{
	extern char *Motherboard_map_name;
	int cachew;
#ifdef CONFIG_PREP_RESIDUAL
	int i;
#endif

	seq_printf(m, "machine\t\t: PReP %s\n", Motherboard_map_name);

	switch ( _prep_type ) {
	case _PREP_IBM:
		cachew = inw(0x80c);
		if (cachew & (1<<6))
			seq_printf(m, "Upgrade CPU\n");
		seq_printf(m, "L2\t\t: ");
		if (cachew & (1<<7)) {
			seq_printf(m, "not present\n");
			goto no_l2;
		}
		seq_printf(m, "%sKb,", (cachew & (1 << 10))? "512" : "256");
		seq_printf(m, "%ssync\n", (cachew & (1 << 15))? "" : "a");
		break;
	case _PREP_Motorola:
		cachew = *((unsigned char *)CACHECRBA);
		seq_printf(m, "L2\t\t: ");
		switch (cachew & L2CACHE_MASK) {
		case L2CACHE_512KB:
			seq_printf(m, "512Kb");
			break;
		case L2CACHE_256KB:
			seq_printf(m, "256Kb");
			break;
		case L2CACHE_1MB:
			seq_printf(m, "1MB");
			break;
		case L2CACHE_NONE:
			seq_printf(m, "none\n");
			goto no_l2;
			break;
		default:
			seq_printf(m, "%x\n", cachew);
		}
		
		seq_printf(m, ", parity %s",
			   (cachew & L2CACHE_PARITY)? "enabled" : "disabled");

		seq_printf(m, " SRAM:");
		
		switch ( ((cachew & 0xf0) >> 4) & ~(0x3) ) {
		case 1: seq_printf(m, "synchronous,parity,flow-through\n");
			break;
		case 2: seq_printf(m, "asynchronous,no parity\n");
			break;
		case 3: seq_printf(m, "asynchronous,parity\n");
			break;
		default:seq_printf(m, "synchronous,pipelined,no parity\n");
			break;
		}
		break;
	default:
		break;
	}

no_l2:
#ifdef CONFIG_PREP_RESIDUAL
	if (res->ResidualLength == 0) {
		/* print info about SIMMs */
		seq_printf(m, "simms\t\t: ");
		for (i = 0; (res->ActualNumMemories) && (i < MAX_MEMS); i++) {
			if (res->Memories[i].SIMMSize != 0)
				seq_printf(m, "%d:%ldM ", i,
					(res->Memories[i].SIMMSize > 1024) ? 
					res->Memories[i].SIMMSize>>20 : 
					res->Memories[i].SIMMSize);
		}
		seq_printf(m, "\n");
	}
#endif

	return 0;
}

static int __prep
prep_show_percpuinfo(struct seq_file *m, int i)
{
	int len = 0;

	/* PREP's without residual data will give incorrect values here */
	seq_printf(m, "clock\t\t: ");
#ifdef CONFIG_PREP_RESIDUAL	
	if (res->ResidualLength)
		seq_printf(m, "%ldMHz\n",
			   (res->VitalProductData.ProcessorHz > 1024) ?
			   res->VitalProductData.ProcessorHz>>20 :
			   res->VitalProductData.ProcessorHz);
	else
#endif /* CONFIG_PREP_RESIDUAL */
		seq_printf(m, "???\n");

	return 0;
}

static void __init
prep_setup_arch(void)
{
	unsigned char reg;
#if 0 /* unused?? */
	unsigned char ucMothMemType;
	unsigned char ucEquipPres1;
#endif

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;
	
	/* Lookup PCI host bridges */
	prep_find_bridges();
	
	/* Set up floppy in PS/2 mode */
	outb(0x09, SIO_CONFIG_RA);
	reg = inb(SIO_CONFIG_RD);
	reg = (reg & 0x3F) | 0x40;
	outb(reg, SIO_CONFIG_RD);
	outb(reg, SIO_CONFIG_RD);	/* Have to write twice to change! */

	/*
	 * We need to set up the NvRAM access routines early as prep_init
	 * has yet to be called
	 */
	ppc_md.nvram_read_val = prep_nvram_read_val;
	ppc_md.nvram_write_val = prep_nvram_write_val;

	/* we should determine this according to what we find! -- Cort */
	switch ( _prep_type )
	{
	case _PREP_IBM:
		/* Enable L2.  Assume we don't need to flush -- Cort*/
		*(unsigned char *)(0x8000081c) |= 3;
		ROOT_DEV = to_kdev_t(0x0301); /* hda1 */
		break;
	case _PREP_Motorola:
		/* Enable L2.  Assume we don't need to flush -- Cort*/
		*(unsigned char *)(0x8000081c) |= 3;
#ifdef CONFIG_BLK_DEV_INITRD
		if (initrd_start)
			ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0); /* /dev/ram */
		else
#endif
#ifdef CONFIG_ROOT_NFS
			ROOT_DEV = to_kdev_t(0x00ff); /* /dev/nfs */
#else
			ROOT_DEV = to_kdev_t(0x0802); /* /dev/sda2 */
#endif
		break;
	}

	/* Read in NVRAM data */ 
	init_prep_nvram();

	/* if no bootargs, look in NVRAM */
	if ( cmd_line[0] == '\0' ) {
		char *bootargs;
		 bootargs = prep_nvram_get_var("bootargs");
		 if (bootargs != NULL) {
			 strcpy(cmd_line, bootargs);
			 /* again.. */
			 strcpy(saved_command_line, cmd_line);
		}
	}

#ifdef CONFIG_SOUND_CS4232
	/*
	 * setup proper values for the cs4232 driver so we don't have
	 * to recompile for the motorola or ibm workstations sound systems.
	 * This is a really nasty hack, but unless we change the driver
	 * it's the only way to support both addrs from one binary.
	 * -- Cort
	 */
	if ( _machine == _MACH_prep )
	{
		extern struct card_info snd_installed_cards[];
		struct card_info *snd_ptr;

		for ( snd_ptr = snd_installed_cards; 
			snd_ptr < &snd_installed_cards[num_sound_cards];
			snd_ptr++ )
		{
			if ( snd_ptr->card_type == SNDCARD_CS4232 )
			{
				if ( _prep_type == _PREP_Motorola )
				{
					snd_ptr->config.io_base = 0x830;
					snd_ptr->config.irq = 10;
					snd_ptr->config.dma = ppc_cs4232_dma = 6;
					snd_ptr->config.dma2 = ppc_cs4232_dma2 = 7;
				}
				if ( _prep_type == _PREP_IBM )
				{
					snd_ptr->config.io_base = 0x530;
					snd_ptr->config.irq = 5;
					snd_ptr->config.dma = ppc_cs4232_dma = 1;
					/* this is wrong - but leave it for now */
					snd_ptr->config.dma2 = ppc_cs4232_dma2 = 7;
				}
			}
		}
	}
#endif /* CONFIG_SOUND_CS4232 */	

	/*print_residual_device_info();*/

	switch (_prep_type) {
	case _PREP_Motorola:
		raven_init();
		break;
	case _PREP_IBM:
		ibm_prep_init();
		break;
	}

#ifdef CONFIG_VGA_CONSOLE
	/* remap the VGA memory */
	vgacon_remap_base = 0xf0000000;
	/*vgacon_remap_base = ioremap(0xc0000000, 0xba000);*/
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
}

/*
 * Determine the decrementer frequency from the residual data
 * This allows for a faster boot as we do not need to calibrate the
 * decrementer against another clock. This is important for embedded systems.
 */
static int __init
prep_res_calibrate_decr(void)
{
#ifdef CONFIG_PREP_RESIDUAL
	unsigned long freq, divisor = 4;

	if ( res->VitalProductData.ProcessorBusHz ) {
		freq = res->VitalProductData.ProcessorBusHz;
		printk("time_init: decrementer frequency = %lu.%.6lu MHz\n",
				(freq/divisor)/1000000,
				(freq/divisor)%1000000);
		tb_to_us = mulhwu_scale_factor(freq/divisor, 1000000);
		tb_ticks_per_jiffy = freq / HZ / divisor;
		return 0;
	} else
#endif	
		return 1;
}

/*
 * Uses the on-board timer to calibrate the on-chip decrementer register
 * for prep systems.  On the pmac the OF tells us what the frequency is
 * but on prep we have to figure it out.
 * -- Cort
 */
/* Done with 3 interrupts: the first one primes the cache and the
 * 2 following ones measure the interval. The precision of the method
 * is still doubtful due to the short interval sampled.
 */
static volatile int calibrate_steps __initdata = 3;
static unsigned tbstamp __initdata = 0;

static void __init
prep_calibrate_decr_handler(int irq, void *dev, struct pt_regs *regs)
{
	unsigned long t, freq;
	int step=--calibrate_steps;

	t = get_tbl();
	if (step > 0) {
		tbstamp = t;
	} else {
		freq = (t - tbstamp)*HZ;
		printk("time_init: decrementer frequency = %lu.%.6lu MHz\n",
			 freq/1000000, freq%1000000);
		tb_ticks_per_jiffy = freq / HZ;
		tb_to_us = mulhwu_scale_factor(freq, 1000000);
	}
}

static void __init
prep_calibrate_decr(void)
{
	int res;

	/* Try and get this from the residual data. */
	res = prep_res_calibrate_decr();

	/* If we didn't get it from the residual data, try this. */
	if ( res ) {
		unsigned long flags;

		save_flags(flags);

#define TIMER0_COUNT 0x40
#define TIMER_CONTROL 0x43
		/* set timer to periodic mode */
		outb_p(0x34,TIMER_CONTROL);/* binary, mode 2, LSB/MSB, ch 0 */
		/* set the clock to ~100 Hz */
		outb_p(LATCH & 0xff , TIMER0_COUNT);	/* LSB */
		outb(LATCH >> 8 , TIMER0_COUNT);	/* MSB */

		if (request_irq(0, prep_calibrate_decr_handler, 0, "timer", NULL) != 0)
			panic("Could not allocate timer IRQ!");
		__sti();
		/* wait for calibrate */
		while ( calibrate_steps )
			;
		restore_flags(flags);
		free_irq( 0, NULL);
	}
}

static long __init
mk48t59_init(void) {
	unsigned char tmp;

	tmp = ppc_md.nvram_read_val(MK48T59_RTC_CONTROLB);
	if (tmp & MK48T59_RTC_CB_STOP) {
		printk("Warning: RTC was stopped, date will be wrong.\n");
		ppc_md.nvram_write_val(MK48T59_RTC_CONTROLB, 
					 tmp & ~MK48T59_RTC_CB_STOP);
		/* Low frequency crystal oscillators may take a very long
		 * time to startup and stabilize. For now just ignore the
		 * the issue, but attempting to calibrate the decrementer
		 * from the RTC just after this wakeup is likely to be very 
		 * inaccurate. Firmware should not allow to load
		 * the OS with the clock stopped anyway...
		 */
	}
	/* Ensure that the clock registers are updated */
	tmp = ppc_md.nvram_read_val(MK48T59_RTC_CONTROLA);
	tmp &= ~(MK48T59_RTC_CA_READ | MK48T59_RTC_CA_WRITE);
	ppc_md.nvram_write_val(MK48T59_RTC_CONTROLA, tmp);
	return 0;
}

/* We use the NVRAM RTC to time a second to calibrate the decrementer,
 * the RTC registers have just been set up in the right state by the
 * preceding routine.
 */
static void __init
mk48t59_calibrate_decr(void)
{
	unsigned long freq;
	unsigned long t1;
	unsigned char save_control;
	long i;
	unsigned char sec;
 
		
	/* Make sure the time is not stopped. */
	save_control = ppc_md.nvram_read_val(MK48T59_RTC_CONTROLB);
	
	ppc_md.nvram_write_val(MK48T59_RTC_CONTROLA,
			(save_control & (~MK48T59_RTC_CB_STOP)));

	/* Now make sure the read bit is off so the value will change. */
	save_control = ppc_md.nvram_read_val(MK48T59_RTC_CONTROLA);
	save_control &= ~MK48T59_RTC_CA_READ;
	ppc_md.nvram_write_val(MK48T59_RTC_CONTROLA, save_control);


	/* Read the seconds value to see when it changes. */
	sec = ppc_md.nvram_read_val(MK48T59_RTC_SECONDS);
	/* Actually this is bad for precision, we should have a loop in
	 * which we only read the seconds counter. nvram_read_val writes
	 * the address bytes on every call and this takes a lot of time.
	 * Perhaps an nvram_wait_change method returning a time
	 * stamp with a loop count as parameter would be the  solution.
	 */
	for (i = 0 ; i < 1000000 ; i++)	{ /* may take up to 1 second... */
		t1 = get_tbl();
		if (ppc_md.nvram_read_val(MK48T59_RTC_SECONDS) != sec) {
			break;
		}
	}

	sec = ppc_md.nvram_read_val(MK48T59_RTC_SECONDS);
	for (i = 0 ; i < 1000000 ; i++)	{ /* Should take up 1 second... */
		freq = get_tbl()-t1;
		if (ppc_md.nvram_read_val(MK48T59_RTC_SECONDS) != sec)
			break;
	}

	printk("time_init: decrementer frequency = %lu.%.6lu MHz\n",
		 freq/1000000, freq%1000000);
	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);
}

static void __prep
prep_restart(char *cmd)
{
	unsigned long i = 10000;

	__cli();

	/* set exception prefix high - to the prom */
	_nmask_and_or_msr(0, MSR_IP);

	/* make sure bit 0 (reset) is a 0 */
	outb( inb(0x92) & ~1L , 0x92 );
	/* signal a reset to system control port A - soft reset */
	outb( inb(0x92) | 1 , 0x92 );

	while ( i != 0 ) i++;
	panic("restart failed\n");
}

static void __prep
prep_halt(void)
{
	unsigned long flags;
	__cli();
	/* set exception prefix high - to the prom */
	save_flags( flags );
	restore_flags( flags|MSR_IP );

	/* make sure bit 0 (reset) is a 0 */
	outb( inb(0x92) & ~1L , 0x92 );
	/* signal a reset to system control port A - soft reset */
	outb( inb(0x92) | 1 , 0x92 );

	while ( 1 ) ;
	/*
	 * Not reached
	 */
}

/*
 * On IBM PReP's, power management is handled by a Signetics 87c750 behind the
 * Utah component on the ISA bus. To access the 750 you must write a series of
 * nibbles to port 0x82a (decoded by the Utah). This is described somewhat in
 * the IBM Carolina Technical Specification.
 * -Hollis
 */
static void __prep
utah_sig87c750_setbit(unsigned int bytenum, unsigned int bitnum, int value)
{
	/*
	 * byte1: 0 0 0 1 0  d  a5 a4
	 * byte2: 0 0 0 1 a3 a2 a1 a0
	 *
	 * d = the bit's value, enabled or disabled
	 * (a5 a4 a3) = the byte number, minus 20
	 * (a2 a1 a0) = the bit number
	 *
	 * example: set the 5th bit of byte 21 (21.5)
	 *     a5 a4 a3 = 001 (byte 1)
	 *     a2 a1 a0 = 101 (bit 5)
	 *
	 *     byte1 = 0001 0100 (0x14)
	 *     byte2 = 0001 1101 (0x1d)
	 */
	unsigned char byte1=0x10, byte2=0x10;
	const unsigned int pm_reg_1=0x82a; /* ISA address */

	/* the 750's '20.0' is accessed as '0.0' through Utah (which adds 20) */
	bytenum -= 20;

	byte1 |= (!!value) << 2;		/* set d */
	byte1 |= (bytenum >> 1) & 0x3;	/* set a5, a4 */

	byte2 |= (bytenum & 0x1) << 3;	/* set a3 */
	byte2 |= bitnum & 0x7;			/* set a2, a1, a0 */

	outb(byte1, pm_reg_1);		/* first nibble */
	mb();
	udelay(100);				/* important: let controller recover */

	outb(byte2, pm_reg_1);		/* second nibble */
	mb();
	udelay(100);				/* important: let controller recover */
}

static void __prep
prep_power_off(void)
{
	if ( _prep_type == _PREP_IBM) {
		/* tested on:
		 * 		Carolina's: 7248-43P, 6070 (PowerSeries 850)
		 * should work on:
		 * 		Carolina: 6050 (PowerSeries 830)
		 * 		7043-140 (Tiger 1)
		 */
		unsigned long flags;
		__cli();
		/* set exception prefix high - to the prom */
		save_flags( flags );
		restore_flags( flags|MSR_IP );

		utah_sig87c750_setbit(21, 5, 1); /* set bit 21.5, "PMEXEC_OFF" */

		while ( 1 ) ;
		/* not reached */
	} else {
		prep_halt();
	}
}

static unsigned int __prep
prep_irq_cannonicalize(u_int irq)
{
	if (irq == 2)
	{
		return 9;
	}
	else
	{
		return irq;
	}
}

static int __prep
prep_get_irq(struct pt_regs *regs)
{
	return i8259_irq(smp_processor_id());
}		

static void __init
prep_init_IRQ(void)
{
	int i;

	if (OpenPIC_Addr != NULL)
		openpic_init(1, NUM_8259_INTERRUPTS, 0, -1);
	for ( i = 0 ; i < NUM_8259_INTERRUPTS ; i++ )
		irq_desc[i].handler = &i8259_pic;
	i8259_init();	
}

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/*
 * IDE stuff.
 */
static int __prep
prep_ide_default_irq(ide_ioreg_t base)
{
	switch (base) {
		case 0x1f0: return 13;
		case 0x170: return 13;
		case 0x1e8: return 11;
		case 0x168: return 10;
		case 0xfff0: return 14;		/* MCP(N)750 ide0 */
		case 0xffe0: return 15;		/* MCP(N)750 ide1 */
		default: return 0;
	}
}

static ide_ioreg_t __prep
prep_ide_default_io_base(int index)
{
	switch (index) {
		case 0: return 0x1f0;
		case 1: return 0x170;
		case 2: return 0x1e8;
		case 3: return 0x168;
		default:
			return 0;
	}
}

static int __prep
prep_ide_check_region(ide_ioreg_t from, unsigned int extent)
{
	return check_region(from, extent);
}

static void __prep
prep_ide_request_region(ide_ioreg_t from,
			unsigned int extent,
			const char *name)
{
	request_region(from, extent, name);
}

static void __prep
prep_ide_release_region(ide_ioreg_t from,
			unsigned int extent)
{
	release_region(from, extent);
}

static void __init
prep_ide_init_hwif_ports (hw_regs_t *hw, ide_ioreg_t data_port, ide_ioreg_t ctrl_port, int *irq)
{
	ide_ioreg_t reg = data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] = hw->io_ports[IDE_DATA_OFFSET] + 0x206;
	}
	if (irq != NULL)
		*irq = 0;
}
#endif

#ifdef CONFIG_SMP
/* PReP (MTX) support */
static int __init
smp_prep_probe(void)
{
	extern int mot_multi;

	if (mot_multi) {
		openpic_request_IPIs();
		smp_hw_index[1] = 1;
		return 2;
	}

	return 1;
}

static void __init
smp_prep_kick_cpu(int nr)
{
	*(unsigned long *)KERNELBASE = nr;
	asm volatile("dcbf 0,%0"::"r"(KERNELBASE):"memory");
	printk("CPU1 reset, waiting\n");
}

static void __init
smp_prep_setup_cpu(int cpu_nr)
{
	if (OpenPIC_Addr)
		do_openpic_setup_cpu();
}

static struct smp_ops_t prep_smp_ops __prepdata = {
	smp_openpic_message_pass,
	smp_prep_probe,
	smp_prep_kick_cpu,
	smp_prep_setup_cpu,
};
#endif /* CONFIG_SMP */

/*
 * This finds the amount of physical ram and does necessary
 * setup for prep.  This is pretty architecture specific so
 * this will likely stay separate from the pmac.
 * -- Cort
 */
static unsigned long __init
prep_find_end_of_memory(void)
{
	unsigned long total = 0;
	extern unsigned int boot_mem_size;

#ifdef CONFIG_PREP_RESIDUAL	
	total = res->TotalMemory;
#endif	

	if (total == 0 && boot_mem_size != 0)
		total = boot_mem_size;
	else if (total == 0) {
		/*
		 * I need a way to probe the amount of memory if the residual
		 * data doesn't contain it. -- Cort
		 */
		total = 0x02000000;
		printk(KERN_INFO "Ramsize from residual data was 0"
			 " -- defaulting to %ldM\n", total>>20);
	}

	return (total);
}

/*
 * Setup the bat mappings we're going to load that cover
 * the io areas.  RAM was mapped by mapin_ram().
 * -- Cort
 */
static void __init
prep_map_io(void)
{
	io_block_mapping(0x80000000, PREP_ISA_IO_BASE, 0x10000000, _PAGE_IO);
	io_block_mapping(0xf0000000, PREP_ISA_MEM_BASE, 0x08000000, _PAGE_IO);
}

static void __init
prep_init2(void)
{
#ifdef CONFIG_NVRAM
	request_region(PREP_NVRAM_AS0, 0x8, "nvram");
#endif
	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");
}

void __init
prep_init(unsigned long r3, unsigned long r4, unsigned long r5,
		unsigned long r6, unsigned long r7)
{
#ifdef CONFIG_PREP_RESIDUAL	
	/* make a copy of residual data */
	if ( r3 ) {
		memcpy((void *)res,(void *)(r3+KERNELBASE),
			 sizeof(RESIDUAL));
	}
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	if ( r4 )
	{
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	/* Copy cmd_line parameters */
	if ( r6 )
	{
		*(char *)(r7 + KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6 + KERNELBASE));
	}
	
	isa_io_base = PREP_ISA_IO_BASE;
	isa_mem_base = PREP_ISA_MEM_BASE;
	pci_dram_offset = PREP_PCI_DRAM_OFFSET;
	ISA_DMA_THRESHOLD = 0x00ffffff;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;

	/* figure out what kind of prep workstation we are */
#ifdef CONFIG_PREP_RESIDUAL	
	if ( res->ResidualLength != 0 )
	{
		if ( !strncmp(res->VitalProductData.PrintableModel,"IBM",3) )
			_prep_type = _PREP_IBM;
		else
			_prep_type = _PREP_Motorola;
	}
	else /* assume motorola if no residual (netboot?) */
#endif
	{
		_prep_type = _PREP_Motorola;
	}

	ppc_md.setup_arch     = prep_setup_arch;
	ppc_md.show_percpuinfo = prep_show_percpuinfo;
	ppc_md.show_cpuinfo   = prep_show_cpuinfo;
	ppc_md.irq_cannonicalize = prep_irq_cannonicalize;
	ppc_md.init_IRQ       = prep_init_IRQ;
	/* this gets changed later on if we have an OpenPIC -- Cort */
	ppc_md.get_irq        = prep_get_irq;
	ppc_md.init           = prep_init2;

	ppc_md.restart        = prep_restart;
	ppc_md.power_off      = prep_power_off;
	ppc_md.halt           = prep_halt;

	ppc_md.time_init      = NULL;
	if (_prep_type == _PREP_IBM) {
		ppc_md.set_rtc_time   = mc146818_set_rtc_time;
		ppc_md.get_rtc_time   = mc146818_get_rtc_time;
		ppc_md.calibrate_decr = prep_calibrate_decr;
	} else {
		ppc_md.set_rtc_time   = mk48t59_set_rtc_time;
		ppc_md.get_rtc_time   = mk48t59_get_rtc_time;
		ppc_md.calibrate_decr = mk48t59_calibrate_decr;
		ppc_md.time_init      = mk48t59_init;
	}

	ppc_md.find_end_of_memory = prep_find_end_of_memory;
	ppc_md.setup_io_mappings = prep_map_io;

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
	ppc_ide_md.default_irq = prep_ide_default_irq;
	ppc_ide_md.default_io_base = prep_ide_default_io_base;
	ppc_ide_md.ide_check_region = prep_ide_check_region;
	ppc_ide_md.ide_request_region = prep_ide_request_region;
	ppc_ide_md.ide_release_region = prep_ide_release_region;
	ppc_ide_md.ide_init_hwif = prep_ide_init_hwif_ports;
#endif

#ifdef CONFIG_VT
	ppc_md.kbd_setkeycode    = pckbd_setkeycode;
	ppc_md.kbd_getkeycode    = pckbd_getkeycode;
	ppc_md.kbd_translate     = pckbd_translate;
	ppc_md.kbd_unexpected_up = pckbd_unexpected_up;
	ppc_md.kbd_leds          = pckbd_leds;
	ppc_md.kbd_init_hw       = pckbd_init_hw;
#ifdef CONFIG_MAGIC_SYSRQ
	ppc_md.ppc_kbd_sysrq_xlate	 = pckbd_sysrq_xlate;
	SYSRQ_KEY = 0x54;
#endif
#endif

#ifdef CONFIG_SMP
	ppc_md.smp_ops		 = &prep_smp_ops;
#endif /* CONFIG_SMP */
}
