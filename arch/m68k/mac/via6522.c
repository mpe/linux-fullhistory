/*
 *	6522 Versatile Interface Adapter (VIA)
 *
 *	There are two of these on the Mac II. Some IRQ's are vectored
 *	via them as are assorted bits and bobs - eg rtc, adb.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/delay.h>

#include <asm/adb.h> 
#include <asm/bootinfo.h> 
#include <asm/macintosh.h> 
#include <asm/macints.h> 
#include "via6522.h"
#include <asm/mac_psc.h>

volatile unsigned char *via1=(unsigned char *)VIABASE;
volatile unsigned char *via2=(unsigned char *)VIABASE2;
volatile unsigned char *psc=(unsigned char *)PSCBASE;

volatile long *via_memory_bogon=(long *)&via_memory_bogon;

unsigned char via1_clock, via1_datab;

static int rbv=0;
static int oss=0;

extern void adb_interrupt(int slot, void *via, struct pt_regs *regs);

/*
 * hardware reset vector
 */
static void (*rom_reset)(void);

/*
 * Timer defs.
 */
#define MAC_CLOCK_TICK		(783300/HZ)	/* ticks per HZ */
#define MAC_CLOCK_LOW		(MAC_CLOCK_TICK&0xFF)
#define MAC_CLOCK_HIGH		(MAC_CLOCK_TICK>>8)


void via_configure_base(void)
{

	switch(macintosh_config->via_type)
	{
		/*
		 *      CI, SI, VX, LC 
		 */
		case MAC_VIA_IIci:
			via1=(void *)0x50F00000;
			via2=(void *)0x50F26000;
			rbv=1;
			if (macintosh_config->ident == MAC_MODEL_IIFX) {
				via2=(void *)0x50F1A000;
				oss=1;
			}
			break;
		/*
		 *      Quadra and early MacIIs agree on the VIA locations
		 */
		case MAC_VIA_QUADRA:
		case MAC_VIA_II:
			via1=(void *)0x50F00000;
			via2=(void *)0x50F02000;
			break;
		default:
	}
}


void via_init_clock(void (*func)(int, void *, struct pt_regs *))
{	
	unsigned char c;
	
	via1_clock=via_read(via1, vACR);
	via1_datab=via_read(via1, vBufB);

	/*
	 * Tell what MacOS left us with
	 */

	printk("via_init: boot via1 acr=%X pcr=%X buf_a=%X dir_a=%X buf_b=%X dir_b=%X \n",
		(int)via1_clock, (int)via_read(via1, vPCR), 
		(int)via_read(via1, vBufA), (int)via_read(via1, vDirA),
		(int)via_read(via1, vBufB), (int)via_read(via1, vDirB));

	if (rbv == 0)
		printk("via_init: boot via2 acr=%X pcr=%X buf_a=%X dir_a=%X buf_b=%X dir_b=%X \n",
			(int)via_read(via2, vACR), (int)via_read(via2, vPCR), 
			(int)via_read(via2, vBufA), (int)via_read(via2, vDirA),
			(int)via_read(via2, vBufB), (int)via_read(via2, vDirB));

	/*
	 *	Shut it down
	 */

	via_write(via1,vIER, 0x7F);	 
	
	/*
	 *	Kill the timers
	 */
	 
	via_write(via1,vT1LL,0);
	via_write(via1,vT1LH,0);
	via_write(via1,vT1CL,0);
	via_write(via1,vT1CH,0);
	via_write(via1,vT2CL,0);
	via_write(via1,vT2CH,0);
	
	/*
	 *	Now do via2
	 */

	if(rbv==0) 
	{
		via_write(via2,vT1LL,0);
		via_write(via2,vT1LH,0);
		via_write(via2,vT1CL,0);
		via_write(via2,vT1CH,0);
		via_write(via2,vT2CL,0);
		via_write(via2,vT2CH,0);
		via_write(via2,vIER, 0x7F);	 
	}
	else if (oss==0)
	{
		/*
		 *	Init the RBV chip a bit
		 */
		
		via_write(via2, rIER,0x7F);
	}

	/*
	 *	Disable the timer latches
	 */
	
	c=via_read(via1,vACR);
	via_write(via1,vACR,c&0x3F);

	if(rbv==0) 
	{
		c=via_read(via2,vACR);
		via_write(via2,vACR,c&0x3F);
	}
	  
	/*
	 *	Now start the clock - we want 100Hz
	 */
	 
	via_write(via1,vACR,via_read(via1,vACR)|0x40);
	
	via_write(via1,vT1LL, MAC_CLOCK_LOW);
	via_write(via1,vT1LH, MAC_CLOCK_HIGH);
	via_write(via1,vT1CL, MAC_CLOCK_LOW);
	via_write(via1,vT1CH, MAC_CLOCK_HIGH);
	
	/*
	 *	And enable its interrupt
	 */
	
	request_irq(IRQ_MAC_TIMER_1, func, IRQ_FLG_LOCK, "timer", func);

	/* 
	 * SE/30: disable video int. 
	 * XXX: testing for SE/30 VBL
	 */

	if (macintosh_config->ident == MAC_MODEL_SE30) {
		c = via_read(via1, vBufB);
		via_write(via1, vBufB, c|(0x40));
		c = via_read(via1, vDirB);
		via_write(via1, vDirB, c|(0x40));
	} 

#if 0 /* gone to mac_init_IRQ */
	/*
	 * Set vPCR for SCSI interrupts.
	 *
	 * That is: CA1 negative edge int., CA2 indep., positive edge int.;
	 *	    CB1 negative edge int., CB2 indep., positive edge int..
	 */
        via_write(via2,vPCR, 0x66);
#endif                                                                  

}

/*
 * TBI: get time offset between scheduling timer ticks
 */
#define TICK_SIZE 10000
  
/* This is always executed with interrupts disabled.  */

unsigned long mac_gettimeoffset (void)
{
	unsigned long ticks, offset = 0;

	/* read VIA1 timer 2 current value */
	ticks = via_read(via1, vT1CL) + (via_read(via1, vT1CH)<<8);
	/* The probability of underflow is less than 2% */
	if (ticks > MAC_CLOCK_TICK - MAC_CLOCK_TICK / 50)
		/* Check for pending timer interrupt in VIA1 IFR */
		if (via_read(via1, vIFR) & 0x40)
			offset = TICK_SIZE;

	ticks = MAC_CLOCK_TICK - ticks;
	ticks = ticks * 10000L / MAC_CLOCK_TICK;

	return ticks + offset;
}

/*
 *	PSC (AV Macs; level 3-6): initialize interrupt enable registers
 */

void psc_init(void)
{
	via_write(psc, pIER3, 0x01);
	via_write(psc, pIER4, 0x09);
	via_write(psc, pIER4, 0x86);
	via_write(psc, pIER5, 0x03);
	via_write(psc, pIER6, 0x07);
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

	if (macintosh_config->adb_type == MAC_ADB_II)
	{
		if(rbv)	{
			via_write(via2, rBufB, via_read(via2, rBufB)&~0x04);
		} else {
			/* Direction of vDirB is output */
			via_write(via2,vDirB,via_read(via2,vDirB)|0x04);
			/* Send a value of 0 on that line */
			via_write(via2,vBufB,via_read(via2,vBufB)&~0x04);
			/* Otherwise it prints "It is now.." then shuts off */
			mdelay(1000);
		}

		/* We should never make it this far... */
		printk ("It is now safe to switch off your machine.\n");

		/* XXX - delay do we need to spin here ? */
		while(1);	/* Just in case .. */
	}

	/*
	 * Initially discovered this technique in the Mach kernel of MkLinux in
	 * osfmk/src/mach_kernel/ppc/POWERMAC/cuda_power.c.  Found equivalent LinuxPPC
	 * code in arch/ppc/kernel/setup.c, which also has a PMU technique for PowerBooks!
	 * --David Kilzer
	 */

	else if (macintosh_config->adb_type == MAC_ADB_IISI
	      || macintosh_config->adb_type == MAC_ADB_CUDA)
	{
		struct adb_request req;

		/*
		 * Print our "safe" message before we send the request
		 * just in case the request never returns.
		 */

		printk ("It is now safe to switch off your machine.\n");

		adb_request (&req, NULL, 2, CUDA_PACKET, CUDA_POWERDOWN);

		printk ("ADB powerdown request sent.\n");
		for (;;)
		{
			adb_poll();
		}
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

	if (macintosh_config->adb_type == MAC_ADB_II)
	{
		unsigned long flags;
		unsigned long *reset_hook;

		/* need ROMBASE in booter */
		/* indeed, plus need to MAP THE ROM !! */

		if (mac_bi_data.rombase == 0)
			mac_bi_data.rombase = 0x40800000;

		/* works on some */
		rom_reset = (void *) (mac_bi_data.rombase + 0xa);

#if 0
		/* testing, doesn't work on SE/30 either */
		reset_hook = (unsigned long *) (mac_bi_data.rombase + 0x4);
		printk("ROM reset hook: %p\n", *reset_hook);
		rom_reset = *reset_hook;
#endif
		if (macintosh_config->ident == MAC_MODEL_SE30) {
			/*
			 * MSch: Machines known to crash on ROM reset ...
			 */
			printk("System halted.\n");
			while(1);
		} else {
			save_flags(flags);
			cli();

			rom_reset();

			restore_flags(flags);
		}

		/* We never make it this far... it usually panics above. */
		printk ("Restart failed.  Please restart manually.\n");

		/* XXX - delay do we need to spin here ? */
		while(1);	/* Just in case .. */
	}

	/*
	 * Initially discovered this technique in the Mach kernel of MkLinux in
	 * osfmk/src/mach_kernel/ppc/POWERMAC/cuda_power.c.  Found equivalent LinuxPPC
	 * code in arch/ppc/kernel/setup.c, which also has a PMU technique!
	 * --David Kilzer
	 * 
	 * I suspect the MAC_ADB_CUDA code might work with other ADB types of machines
	 * but have no way to test this myself.  --DDK
	 */

	else if (macintosh_config->adb_type == MAC_ADB_IISI
	      || macintosh_config->adb_type == MAC_ADB_CUDA)
	{
		struct adb_request req;

		adb_request (&req, NULL, 2, CUDA_PACKET, CUDA_RESET_SYSTEM);

		printk ("Restart failed.  Please restart manually.\n");
		for (;;)
		{
			adb_poll();
		}
	}
}

/*
 *	Set up the keyboard
 */
 
void via_setup_keyboard(void)
{
#if 0 /* moved to adb */
	via1_func_tab.vector[2]=adb_interrupt;
#else
	request_irq(IRQ_MAC_ADB, adb_interrupt, IRQ_FLG_LOCK, "adb interrupt",
		    adb_interrupt);
#endif
}

/*
 *	Floppy hook
 */

void via1_set_head(int head)
{
	if(head==0)
		via_write(via1, vBufA, via_read(via1, vBufA)&~0x20);
	else
		via_write(via1, vBufA, via_read(via1, vBufA)|0x20);
}

void nubus_init_via(void)
{
	if (rbv) {
		if (oss==0) {
			via_write(via2, rBufB, via_read(via2, rBufB)|0x02);
			via_write(via2, rIER, 0x82);	/* Interrupts on */
		}
	} else {
		/* Assert the nubus active */
		via_write(via2, vDirB, via_read(via2, vDirB)|0x02);
		via_write(via2, vBufB, via_read(via2, vBufB)|0x02);
		/* Make the nubus interrupt source register all output (disable) */
		/* via_write(via2, vDirA, 0xFF); */
		via_write(via2, vIER, 0x82);	/* Interrupts on */
	}

	printk("nubus_init_via: via1 acr=%X datab=%X pcr=%X\n",
		(int)via_read(via1, vACR), (int)via_read(via1, vBufB), 
		(int)via_read(via1, vPCR));

	if (rbv==0)
		printk("nubus_init_via: via2 acr=%X datab=%X pcr=%X\n",
			(int)via_read(via2, vACR), (int)via_read(via2, vBufB), 
			(int)via_read(via2, vPCR));
}
