/* $Id: teles3c.c,v 1.2 1998/02/02 13:27:07 keil Exp $

 * teles3c.c     low level stuff for teles 16.3c
 *
 * Author     Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log: teles3c.c,v $
 * Revision 1.2  1998/02/02 13:27:07  keil
 * New
 *
 *
 *
 */

#define __NO_VERSION__
#include "hisax.h"
#include "hfc_2bds0.h"
#include "isdnl1.h"

extern const char *CardType[];

const char *teles163c_revision = "$Revision: 1.2 $";

static void
t163c_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, stat;
	char tmp[32];

	if (!cs) {
		printk(KERN_WARNING "teles3c: Spurious interrupt!\n");
		return;
	}
	if ((HFCD_ANYINT | HFCD_BUSY_NBUSY) & 
		(stat = cs->BC_Read_Reg(cs, HFCD_DATA, HFCD_STAT))) {
		val = cs->BC_Read_Reg(cs, HFCD_DATA, HFCD_INT_S1);
		if (cs->debug & L1_DEB_ISAC) {
			sprintf(tmp, "teles3c: stat(%02x) s1(%02x)", stat, val);
			debugl1(cs, tmp);
		}
		hfc2bds0_interrupt(cs, val);
	} else {
		if (cs->debug & L1_DEB_ISAC) {
			sprintf(tmp, "teles3c: irq_no_irq stat(%02x)", stat);
			debugl1(cs, tmp);
		}
	}
}

static void
t163c_Timer(struct IsdnCardState *cs)
{
	cs->hw.hfcD.timer.expires = jiffies + 75;
	/* WD RESET */
/*	WriteReg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcD.ctmt | 0x80);
	add_timer(&cs->hw.hfcD.timer);
*/
}

void
release_io_t163c(struct IsdnCardState *cs)
{
	release2bds0(cs);
	del_timer(&cs->hw.hfcD.timer);
	if (cs->hw.hfcD.addr)
		release_region(cs->hw.hfcD.addr, 2);
}

static void
reset_t163c(struct IsdnCardState *cs)
{
	long flags;

	printk(KERN_INFO "teles3c: resetting card\n");
	cs->hw.hfcD.cirm = HFCD_RESET | HFCD_MEM8K;
	cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_CIRM, cs->hw.hfcD.cirm);	/* Reset On */
	save_flags(flags);
	sti();
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(3);
	cs->hw.hfcD.cirm = HFCD_MEM8K;
	cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_CIRM, cs->hw.hfcD.cirm);	/* Reset Off */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(1);
	cs->hw.hfcD.cirm |= HFCD_INTB;
	cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_CIRM, cs->hw.hfcD.cirm);	/* INT B */
	cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_CLKDEL, 0x0e);
	cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_TEST, HFCD_AUTO_AWAKE); /* S/T Auto awake */
	cs->hw.hfcD.ctmt = HFCD_TIM25 | HFCD_AUTO_TIMER;
	cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcD.ctmt);
	cs->hw.hfcD.int_m2 = HFCD_IRQ_ENABLE;
	cs->hw.hfcD.int_m1 = HFCD_INTS_B1TRANS | HFCD_INTS_B2TRANS |
		HFCD_INTS_DTRANS | HFCD_INTS_B1REC | HFCD_INTS_B2REC |
		HFCD_INTS_DREC | HFCD_INTS_L1STATE;
	cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_INT_M1, cs->hw.hfcD.int_m1);
	cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_INT_M2, cs->hw.hfcD.int_m2);
	cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_STATES, HFCD_LOAD_STATE | 2); /* HFC ST 2 */
	udelay(10);
	cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_STATES, 2); /* HFC ST 2 */
	cs->hw.hfcD.mst_m = 0;
	cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_MST_MODE, HFCD_MASTER); /* HFC Master */
	cs->hw.hfcD.sctrl = 0;
	cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_SCTRL, cs->hw.hfcD.sctrl);
	restore_flags(flags);
}

static int
t163c_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	long flags;
	char tmp[32];

	if (cs->debug & L1_DEB_ISAC) {
		
		sprintf(tmp, "teles3c: card_msg %x", mt);
		debugl1(cs, tmp);
	}
	switch (mt) {
		case CARD_RESET:
			reset_t163c(cs);
			return(0);
		case CARD_RELEASE:
			release_io_t163c(cs);
			return(0);
		case CARD_SETIRQ:
			cs->hw.hfcD.timer.expires = jiffies + 75;
			add_timer(&cs->hw.hfcD.timer);
			return(request_irq(cs->irq, &t163c_interrupt,
					I4L_IRQ_FLAG, "HiSax", cs));
		case CARD_INIT:
			init2bds0(cs);
			save_flags(flags);
			sti();
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout((80*HZ)/1000);
			cs->hw.hfcD.ctmt |= HFCD_TIM800;
			cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcD.ctmt); 
			cs->BC_Write_Reg(cs, HFCD_DATA, HFCD_MST_MODE, cs->hw.hfcD.mst_m);
			restore_flags(flags);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}

__initfunc(int
setup_t163c(struct IsdnCard *card))
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, teles163c_revision);
	printk(KERN_INFO "HiSax: Teles 16.3c driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_TELES3C)
		return (0);
	cs->debug = 0xff;
	cs->hw.hfcD.addr = card->para[1] & 0xfffe;
	cs->irq = card->para[0];
	cs->hw.hfcD.cip = 0;
	cs->hw.hfcD.int_s1 = 0;
	cs->hw.hfcD.send = NULL;
	cs->bcs[0].hw.hfc.send = NULL;
	cs->bcs[1].hw.hfc.send = NULL;
	cs->hw.hfcD.bfifosize = 1024 + 512;
	cs->hw.hfcD.dfifosize = 512;
	cs->ph_state = 0;
	cs->hw.hfcD.fifo = 255;
	if (check_region((cs->hw.hfcD.addr), 2)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.hfcD.addr,
		       cs->hw.hfcD.addr + 2);
		return (0);
	} else {
		request_region(cs->hw.hfcD.addr, 2, "teles3c isdn");
	}
	/* Teles 16.3c IO ADR is 0x200 | YY0U (YY Bit 15/14 address) */
	outb(0x00, cs->hw.hfcD.addr);
	outb(0x56, cs->hw.hfcD.addr | 1);
	printk(KERN_INFO
	       "teles3c: defined at 0x%x IRQ %d HZ %d\n",
	       cs->hw.hfcD.addr,
	       cs->irq, HZ);

	set_cs_func(cs);
	cs->hw.hfcD.timer.function = (void *) t163c_Timer;
	cs->hw.hfcD.timer.data = (long) cs;
	init_timer(&cs->hw.hfcD.timer);
	reset_t163c(cs);
	cs->cardmsg = &t163c_card_msg;
	return (1);
}
