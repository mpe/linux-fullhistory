/*
 * $Id: mbx_setup.c,v 1.10 1999/05/14 07:24:19 davem Exp $
 *
 *  linux/arch/ppc/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified for MBX using prep/chrp/pmac functions by Dan (dmalek@jlc.net)
 */

/*
 * bootup setup stuff..
 */

#include <linux/config.h>
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
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/blk.h>
#include <linux/ioport.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/ide.h>
#include <asm/mbx.h>
#include <asm/machdep.h>

#include "time.h"
#include "local_irq.h"

static int mbx_set_rtc_time(unsigned long time);
unsigned long mbx_get_rtc_time(void);
void mbx_calibrate_decr(void);

extern int mackbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int mackbd_getkeycode(unsigned int scancode);
extern int mackbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char mackbd_unexpected_up(unsigned char keycode);
extern void mackbd_leds(unsigned char leds);
extern void mackbd_init_hw(void);

extern unsigned long loops_per_sec;

unsigned long empty_zero_page[1024];

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

extern char saved_command_line[256];

extern unsigned long find_available_memory(void);
extern void m8xx_cpm_reset(uint);

void __init adbdev_init(void)
{
}

__initfunc(void
mbx_setup_arch(unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	int	cpm_page;
	extern char cmd_line[];
	
	cpm_page = *memory_start_p;
	*memory_start_p += PAGE_SIZE;
	
	sprintf(cmd_line,
"%s root=/dev/nfs nfsroot=/sys/mbxroot",
		cmd_line);
	printk("Boot arguments: %s\n", cmd_line);

	/* Reset the Communication Processor Module.
	*/
	m8xx_cpm_reset(cpm_page);

#ifdef notdef
	ROOT_DEV = to_kdev_t(0x0301); /* hda1 */
#endif
	
#ifdef CONFIG_BLK_DEV_INITRD
#if 0
	ROOT_DEV = to_kdev_t(0x0200); /* floppy */  
	rd_prompt = 1;
	rd_doload = 1;
	rd_image_start = 0;
#endif
	/* initrd_start and size are setup by boot/head.S and kernel/head.S */
	if ( initrd_start )
	{
		if (initrd_end > *memory_end_p)
		{
			printk("initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       initrd_end,*memory_end_p);
			initrd_start = 0;
		}
	}
#endif

#ifdef notdef
	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");
#endif
}

void
abort(void)
{
#ifdef CONFIG_XMON
	extern void xmon(void *);
	xmon(0);
#endif
	machine_restart(NULL);
}

/* The decrementer counts at the system (internal) clock frequency divided by
 * sixteen, or external oscillator divided by four.  Currently, we only
 * support the MBX, which is system clock divided by sixteen.
 */
__initfunc(void mbx_calibrate_decr(void))
{
	bd_t	*binfo = (bd_t *)&res;
	int freq, fp, divisor;

	if ((((immap_t *)MBX_IMAP_ADDR)->im_clkrst.car_sccr & 0x02000000) == 0)
		printk("WARNING: Wrong decrementer source clock.\n");

	/* The manual says the frequency is in Hz, but it is really
	 * as MHz.  The value 'fp' is the number of decrementer ticks
	 * per second.
	 */
	fp = (binfo->bi_intfreq * 1000000) / 16;
	freq = fp*60;	/* try to make freq/1e6 an integer */
        divisor = 60;
        printk("time_init: decrementer frequency = %d/%d\n", freq, divisor);
        decrementer_count = freq / HZ / divisor;
        count_period_num = divisor;
        count_period_den = freq / 1000000;
}

/* A place holder for time base interrupts, if they are ever enabled.
*/
void timebase_interrupt(int irq, void * dev, struct pt_regs * regs)
{
	printk("timebase_interrupt()\n");
}

/* The RTC on the MPC8xx is an internal register.
 * We want to protect this during power down, so we need to unlock,
 * modify, and re-lock.
 */
static int
mbx_set_rtc_time(unsigned long time)
{
	((immap_t *)MBX_IMAP_ADDR)->im_sitk.sitk_rtck = KAPWR_KEY;
	((immap_t *)MBX_IMAP_ADDR)->im_sit.sit_rtc = time;
	((immap_t *)MBX_IMAP_ADDR)->im_sitk.sitk_rtck = ~KAPWR_KEY;
	return(0);
}

initfunc(unsigned long
mbx_get_rtc_time(void)
{
	/* First, unlock all of the registers we are going to modify.
	 * To protect them from corruption during power down, registers
	 * that are maintained by keep alive power are "locked".  To
	 * modify these registers we have to write the key value to
	 * the key location associated with the register.
	 */
	((immap_t *)MBX_IMAP_ADDR)->im_sitk.sitk_tbscrk = KAPWR_KEY;
	((immap_t *)MBX_IMAP_ADDR)->im_sitk.sitk_rtcsck = KAPWR_KEY;


	/* Disable the RTC one second and alarm interrupts.
	*/
	((immap_t *)MBX_IMAP_ADDR)->im_sit.sit_rtcsc &=
						~(RTCSC_SIE | RTCSC_ALE);

	/* Enabling the decrementer also enables the timebase interrupts
	 * (or from the other point of view, to get decrementer interrupts
	 * we have to enable the timebase).  The decrementer interrupt
	 * is wired into the vector table, nothing to do here for that.
	 */
	((immap_t *)MBX_IMAP_ADDR)->im_sit.sit_tbscr =
				((mk_int_int_mask(DEC_INTERRUPT) << 8) |
					 (TBSCR_TBF | TBSCR_TBE));
	if (request_irq(DEC_INTERRUPT, timebase_interrupt, 0, "tbint", NULL) != 0)
		panic("Could not allocate timer IRQ!");

	/* Get time from the RTC.
	*/
	return ((immap_t *)MBX_IMAP_ADDR)->im_sit.sit_rtc;
}

void
mbx_restart(char *cmd)
{
	extern void MBX_gorom(void);

	MBX_gorom();
}

void
mbx_power_off(void)
{
   mbx_restart(NULL);
}

void
mbx_halt(void)
{
   mbx_restart(NULL)
}


int mbx_setup_residual(char *buffer)
{
        int     len = 0;
	bd_t	*bp;
	extern	RESIDUAL *res;
			
	bp = (bd_t *)res;
			
	len += sprintf(len+buffer,"clock\t\t: %dMHz\n"
		       "bus clock\t: %dMHz\n",
		       bp->bi_intfreq /*/ 1000000*/,
		       bp->bi_busfreq /*/ 1000000*/);

	return len;
}

void
mbx_do_IRQ(struct pt_regs *regs,
	   int            cpu,
           int            isfake)
{
	int irq;
        unsigned long bits = 0;

        /* For MPC8xx, read the SIVEC register and shift the bits down
         * to get the irq number.         */
        bits = ((immap_t *)IMAP_ADDR)->im_siu_conf.sc_sivec;
        irq = bits >> 26;
        irq += ppc8xx_pic.irq_offset;
        bits = 1UL << irq;

	if (irq < 0) {
		printk(KERN_DEBUG "Bogus interrupt %d from PC = %lx\n",
		       irq, regs->nip);
		spurious_interrupts++;
	}
	else {
                ppc_irq_dispatch_handler( regs, irq );
	}

}

static void mbx_i8259_action(int cpl, void *dev_id, struct pt_regs *regs)
{
	int bits, irq;

	/* A bug in the QSpan chip causes it to give us 0xff always
	 * when doing a character read.  So read 32 bits and shift.
	 * This doesn't seem to return useful values anyway, but
	 * read it to make sure things are acked.
	 * -- Cort
	 */
	irq = (inl(0x508) >> 24)&0xff;
	if ( irq != 0xff ) printk("iack %d\n", irq);
	
	outb(0x0C, 0x20);
	irq = inb(0x20) & 7;
	if (irq == 2)
	{
		outb(0x0C, 0xA0);
		irq = inb(0xA0);
		irq = (irq&7) + 8;
	}
	bits = 1UL << irq;
	irq += i8259_pic.irq_offset;
	ppc_irq_dispatch_handler( regs, irq );
}


/* On MBX8xx, the interrupt control (SIEL) was set by EPPC-bug.  External
 * interrupts can be either edge or level triggered, but there is no
 * reason for us to change the EPPC-bug values (it would not work if we did).
 */
__initfunc(void
mbx_init_IRQ(void))
{
	int i;

        ppc8xx_pic.irq_offset = 16;
        for ( i = 16 ; i < 32 ; i++ )
                irq_desc[i].ctl = &ppc8xx_pic;
        unmask_irq(CPM_INTERRUPT);

        for ( i = 0 ; i < 16 ; i++ )
                irq_desc[i].ctl = &i8259_pic;
        i8259_init();
        request_irq(ISA_BRIDGE_INT, mbx_i8259_action, 0, "8259 cascade", NULL);
        enable_irq(ISA_BRIDGE_INT);
}

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/*
 * IDE stuff.
 */
void
mbx_ide_insw(ide_ioreg_t port, void *buf, int ns)
{
	ide_insw(port+_IO_BASE), buf, ns);
}

void
mbx_ide_outsw(ide_ioreg_t port, void *buf, int ns)
{
	ide_outsw(port+_IO_BASE, buf, ns);
}

int
mbx_ide_default_irq(ide_ioreg_t base)
{
        return 14;
}

ide_ioreg_t
mbx_ide_default_io_base(int index)
{
        return index;
}

int
mbx_ide_check_region(ide_ioreg_t from, unsigned int extent)
{
        return 0
}

void
mbx_ide_request_region(ide_ioreg_t from,
			unsigned int extent,
			const char *name)
{
}

void
mbx_ide_release_region(ide_ioreg_t from,
			unsigned int extent)
{
}

void
mbx_ide_fix_driveid(struct hd_driveid *id)
{
        ppc_generic_ide_fix_driveid(id);
}

void
mbx_ide_init_hwif_ports(hw_regs_t *hw, ide_ioreg_t data_port, ide_ioreg_t ctrl_port, int *irq)
{
	ide_ioreg_t reg = data_port;
	int i;

	*irq = 0;

	if (data_port != 0)	/* Only map the first ATA flash drive */
		return;

#ifdef ATA_FLASH

	reg = (ide_ioreg_t) ioremap(PCMCIA_MEM_ADDR, 0x200);

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}

	/* Does not matter */

	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] = reg;
	}
	if (irq)
		hw->irq = 13;
#endif
}
#endif

__initfunc(void
mbx_init(unsigned long r3, unsigned long r4, unsigned long r5,
	 unsigned long r6, unsigned long r7))
{

	if ( r3 )
		memcpy( (void *)&res,(void *)(r3+KERNELBASE), sizeof(bd_t) );
	
#ifdef CONFIG_PCI
	mbx_setup_pci_ptrs();
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	/* take care of initrd if we have one */
	if ( r4 )
	{
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */
	/* take care of cmd line */
	if ( r6 )
	{
		
		*(char *)(r7+KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6+KERNELBASE));
	}

	ppc_md.setup_arch     = mbx_setup_arch;
	ppc_md.setup_residual = mbx_setup_residual;
	ppc_md.get_cpuinfo    = NULL;
	ppc_md.irq_cannonicalize = NULL;
	ppc_md.init_IRQ       = mbx_init_IRQ;
	ppc_md.do_IRQ         = mbx_do_IRQ;
	ppc_md.init           = NULL;

	ppc_md.restart        = mbx_restart;
	ppc_md.power_off      = mbx_power_off;
	ppc_md.halt           = mbx_halt;

	ppc_md.time_init      = NULL;
	ppc_md.set_rtc_time   = mbx_set_rtc_time;
	ppc_md.get_rtc_time   = mbx_get_rtc_time;
	ppc_md.calibrate_decr = mbx_calibrate_decr;

	ppc_md.kbd_setkeycode    = pckbd_setkeycode;
	ppc_md.kbd_getkeycode    = pckbd_getkeycode;
	ppc_md.kbd_translate     = pckbd_translate;
	ppc_md.kbd_unexpected_up = pckbd_unexpected_up;
	ppc_md.kbd_leds          = pckbd_leds;
	ppc_md.kbd_init_hw       = pckbd_init_hw;
#ifdef CONFIG_MAGIC_SYSRQ
	ppc_md.kbd_sysrq_xlate	 = pckbd_sysrq_xlate;
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
        ppc_ide_md.insw = mbx_ide_insw;
        ppc_ide_md.outsw = mbx_ide_outsw;
        ppc_ide_md.default_irq = mbx_ide_default_irq;
        ppc_ide_md.default_io_base = mbx_ide_default_io_base;
        ppc_ide_md.check_region = mbx_ide_check_region;
        ppc_ide_md.request_region = mbx_ide_request_region;
        ppc_ide_md.release_region = mbx_ide_release_region;
        ppc_ide_md.fix_driveid = mbx_ide_fix_driveid;
        ppc_ide_md.ide_init_hwif = mbx_ide_init_hwif_ports;

        ppc_ide_md.io_base = _IO_BASE;
#endif		
}
