
/*
 *	6522 Versatile Interface Adapter (VIA)
 *
 *	There are two of these on the Mac II. Some IRQ's are vectored
 *	via them as are assorted bits and bobs - eg rtc, adb.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/macintosh.h> 
#include <asm/macints.h> 
#include "via6522.h"

volatile unsigned char *via1=(unsigned char *)VIABASE;
volatile unsigned char *via2=(unsigned char *)VIABASE2;

unsigned char via1_clock, via1_datab;

static int rbv=0;

/*
 * Debugging the VBL ints
 */

extern int console_loglevel;

/*
 *	VIA1 - hardwired vectors
 */

#if 0 /* gone to macints.[ch] */
extern void via_wtf(int slot, void *via, struct pt_regs *regs);
static void via_do_nubus(int slot, volatile void *via, struct pt_regs *regs);

extern void adb_interrupt(int slot, void *via, struct pt_regs *regs);

static struct via_irq_tab via1_func_tab=
{
	{
		via_wtf,	/* One second interrupt */
		via_wtf,	/* Vblank */
		via_wtf,       	/* ADB data ready */
		via_wtf,	/* ADB data */
		via_wtf,	/* ADB clock */
		via_wtf,
		via_wtf,	/* Slot 6 is replaced by the timer */
		via_wtf
	}
};

static struct via_irq_tab via2_func_tab=
{
	{
		via_wtf,
		via_do_nubus,
		via_wtf,
		via_wtf,
		via_wtf,
		via_wtf,
		via_wtf,
		via_wtf
	}
};

static struct via_irq_tab nubus_func_tab=
{
	{
		via_wtf,
		via_wtf,
		via_wtf,
		via_wtf,
		via_wtf,
		via_wtf,
		via_wtf,
		via_wtf
	}
};
#endif

extern void adb_interrupt(int slot, void *via, struct pt_regs *regs);

#define MAC_CLOCK_TICK		(783300/HZ)	/* ticks per HZ */
#define MAC_CLOCK_LOW		(MAC_CLOCK_TICK&0xFF)
#define MAC_CLOCK_HIGH		(MAC_CLOCK_TICK>>8)


void via_init_clock(void (*func)(int, void *, struct pt_regs *))
{
	unsigned char c;

/*	mac_debugging_penguin(6);*/
	
	switch(macintosh_config->via_type)
	{
		/*
		 *      CI, SI, VX, LC 
		 */
		case MAC_VIA_IIci:
			via1=(void *)0x50F00000;
			via2=(void *)0x50F26000;
			rbv=1;
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
	else
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

/*	mac_debugging_penguin(7);*/

	/* 
	 * SE/30: disable video int. 
	 * XXX: testing for SE/30 VBL
	 */

	if (macintosh_config->ident == MAC_MODEL_SE30 
	    && console_loglevel != 10) {
		c = via_read(via1, vBufB);
		via_write(via1, vBufB, c|(0x40));
		c = via_read(via1, vDirB);
		via_write(via1, vDirB, c|(0x40));
	} 

	/*
	 * XXX: use positive edge
	 */

	if (console_loglevel == 10) {
		c = via_read(via1, vPCR);
		via_write(via1, vPCR, c|(0x1));
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

#if 0 /* moved to macints.c */

static void via_irq(volatile unsigned char *via, struct via_irq_tab *irqtab,
	struct pt_regs *regs)
{
	unsigned char events=(via_read(via, vIFR)&via_read(via,vIER))&0x7F;
	int i;
	int ct=0;	

	/*
	 *	Shouldnt happen
	 */
	 
	if(events==0)
	{
		printk("via_irq: nothing pending!\n");
		return;
	}

	do {
		/*
		 *	Clear the pending flag
		 */

		/* HACK HACK - FIXME !!! - just testing some keyboard ideas */
	
		/* events&=~(1<<4); */		 
		via_write(via, vIFR, events);
	 
		/*
		 *	Now see what bits are raised
		 */
	 
		for(i=0;i<7;i++)
		{
			if(events&(1<<i))
				(irqtab->vector[i])(i, via, regs);
		}
	
		/*
		 *	And done..
		 */
		events=(via_read(via, vIFR)&via_read(via,vIER))&0x7F;
		ct++;
		if(events && ct>8)
		{
		        printk("via: stuck events %x\n",events);
		        break;
		}
	}
	while(events);

	scsi_mac_polled();
}

/*
 *
 *	The RBV is different. RBV appears to stand for randomly broken
 *	VIA.
 */

static void rbv_irq(volatile unsigned char *via, struct via_irq_tab *irqtab,
	struct pt_regs *regs)
{
	unsigned char events=(via_read(via, rIFR)&via_read(via,rIER))&0x7F;
	int i;
	int ct=0;	

	/*
	 *	Shouldnt happen
	 */
	 
	if(events==0)
	{
		printk("rbv_irq: nothing pending!\n");
		return;
	}

	do {
		/*
		 *	Clear the pending flag
		 */

		/* HACK HACK - FIXME !!! - just testing some keyboard ideas */
	
		/* events&=~(1<<4); */		 
		via_write(via, rIFR, events);
	 
		/*
		 *	Now see what bits are raised
		 */
	 
		for(i=0;i<7;i++)
		{
			if(events&(1<<i))
				(irqtab->vector[i])(i, via, regs);
		}
	
		/*
		 *	And done..
		 */
		events=(via_read(via, rIFR)&via_read(via,rIER))&0x7F;
		ct++;
		if(events && ct>8)
		{
		        printk("rbv: stuck events %x\n",events);
			for(i=0;i<7;i++)
			{
				if(events&(1<<i))
				{
					printk("rbv - bashing source %d\n",
						i);
					via_write(via, rIER, i);
					via_write(via, rIFR, i);
				}
			}
		        break;
		}
	}
	while(events);
}

/*
 *	System interrupts
 */

void via1_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	via_irq(via1, &via1_func_tab, regs);
}

/*
 *	Nubus interrupts
 */
 
void via2_irq(int irq, void *dev_id, struct pt_regs *regs)
{
/*	printk("via2 interrupt\n");*/
	if(rbv)
		rbv_irq(via2, &via2_func_tab, regs);
	else
		via_irq(via2, &via2_func_tab, regs);
}

/*
 *	Unexpected via interrupt
 */
 
void via_wtf(int slot, volatile void *via, struct pt_regs *regs)
{
	printk("Unexpected event %d on via %p\n",slot,via);
}

void nubus_wtf(int slot, volatile void *via, struct pt_regs *regs)
{
	printk("Unexpected interrupt on nubus slot %d\n",slot);
}

#endif

/*
 *	The power switch - yes its software!
 */
 
void mac_reset(void)
{
	if(rbv)	{
		via_write(via2, rBufB, via_read(via2, rBufB)&~0x04);
	} else {
		/* Direction of vDirB is output */
		via_write(via2,vDirB,via_read(via2,vDirB)|0x04);
		/* Send a value of 0 on that line */
		via_write(via2,vBufB,via_read(via2,vBufB)&~0x04);
	}
	/* We never make it this far... */
	/* XXX - delay do we need to spin here ? */
	while(1);	/* Just in case .. */
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

#if 0 /* moved to macints.c */


/*
 *    Set up the SCSI
 */

void via_scsi_disable(void)
{
	if (rbv)
		via_write(via2, rIER, (1<<3)|(1<<0));
	else
		via_write(via2, vIER, (1<<3)|(1<<0));
}

void via_scsi_enable(void)
{
	if (rbv)
		via_write(via2, rIER, (1<<3)|(1<<0)|0x80);
	else
		via_write(via2, vIER, (1<<3)|(1<<0)|0x80);
}

void via_scsi_clear(void)
{
        if (rbv) 
		via_write(via2, rIFR, (1<<3)|(1<<0)|0x80);
	volatile unsigned char deep_magic=via_read(via2, vBufB);
	via_scsi_enable();
}
 
void via_setup_scsi(void (*handler)(int,volatile void *,struct pt_regs *))
{
	via2_func_tab.vector[0]=handler;        /* SCSI DRQ */
	via2_func_tab.vector[3]=handler;        /* SCSI IRQ */
	via_write(via2, vPCR, 0x66);		/* Edge direction! */
	via_scsi_enable();
}

/*
 *	Nubus handling
 */
 
static int nubus_active=0;
 
int nubus_request_irq(int slot, void (*handler)(int,void *,struct pt_regs *))
{
	slot-=9;
/*	printk("Nubus request irq for slot %d\n",slot);*/
	if(nubus_func_tab.vector[slot]==nubus_wtf)
		return -EBUSY;
	nubus_func_tab.vector[slot]=handler;
	nubus_active|=1<<slot;
/*	printk("program slot %d\n",slot);*/
/*	printk("via2=%p\n",via2);*/
#if 0
	via_write(via2, vDirA, 
		via_read(via2, vDirA)|(1<<slot));
	via_write(via2, vBufA, 0);
#endif		
	if (!rbv) {
	/* Make sure the bit is an input */
	via_write(via2, vDirA, 
		via_read(via2, vDirA)&~(1<<slot));
	}
/*	printk("nubus irq on\n");*/
	return 0;
}

int nubus_free_irq(int slot)
{
	slot-=9;
	nubus_active&=~(1<<slot);
	nubus_func_tab.vector[slot]=nubus_wtf;
	if (rbv) {
		via_write(via2, rBufA, 1<<slot);
	} else {
		via_write(via2, vDirA, 
			via_read(via2, vDirA)|(1<<slot));
		via_write(via2, vBufA, 1<<slot);
		via_write(via2, vDirA, 
			via_read(via2, vDirA)&~(1<<slot));
	}
	return 0;
}

static void via_do_nubus(int slot, volatile void *via, struct pt_regs *regs)
{
	unsigned char map;
	int i;
	int ct=0;

/*	printk("nubus interrupt\n");*/
		
	if (rbv) {
		via_write(via2, rIFR, 0x82);	/* lock the nubus interrupt */
	} else {
		via_write(via2, vIFR, 0x82);	/* lock the nubus interrupt */
		
	while(1)
	{
		if(rbv)
			map=~via_read(via2, rBufA);
		else
			map=~via_read(via2, vBufA);
		if((map=(map&nubus_active))==0)
			break;
		if(ct++>2)
		{
			printk("nubus stuck events - %d/%d\n", map, nubus_active);
			return;
		}
		for(i=0;i<7;i++)
		{
			if(map&(1<<i))
			{
				(nubus_func_tab.vector[i])(i+9, via, regs);
			}
		}
		if (rbv)
			via_write(via2, rIFR, 0x02);	/* clear it */
		else
			via_write(via2, vIFR, 0x02);	/* clear it */
	}
	
	/* And done */
}
#endif

void nubus_init_via(void)
{
	if (rbv) {
		via_write(via2, rBufB, via_read(via2, rBufB)|0x02);
		via_write(via2, rIER, 0x82);	/* Interrupts on */
	} else {
		/* Assert the nubus active */
		via_write(via2, vDirB, via_read(via2, vDirB)|0x02);
		via_write(via2, vBufB, via_read(via2, vBufB)|0x02);
		/* Make the nubus interrupt source register all output (disable) */
		/* via_write(via2, vDirA, 0xFF); */
		via_write(via2, vIER, 0x82);	/* Interrupts on */
	}
	printk("BTW  boot via1 acr=%X datab=%X pcr=%X\n",
		(int)via1_clock, (int)via1_datab, (int)via_read(via1, vPCR));
}

