/* $Id: avm_a1.c,v 2.7 1998/02/02 13:29:37 keil Exp $

 * avm_a1.c     low level stuff for AVM A1 (Fritz) isdn cards
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log: avm_a1.c,v $
 * Revision 2.7  1998/02/02 13:29:37  keil
 * fast io
 *
 * Revision 2.6  1998/01/13 23:09:46  keil
 * really disable timer
 *
 * Revision 2.5  1998/01/02 06:50:29  calle
 * Perodic timer of A1 now disabled, no need for linux driver.
 *
 * Revision 2.4  1997/11/08 21:35:42  keil
 * new l1 init
 *
 * Revision 2.3  1997/11/06 17:13:32  keil
 * New 2.1 init code
 *
 * Revision 2.2  1997/10/29 18:55:48  keil
 * changes for 2.1.60 (irq2dev_map)
 *
 * Revision 2.1  1997/07/27 21:47:13  keil
 * new interface structures
 *
 * Revision 2.0  1997/06/26 11:02:48  keil
 * New Layer and card interface
 *
 * Revision 1.6  1997/04/13 19:54:07  keil
 * Change in IRQ check delay for SMP
 *
 * Revision 1.5  1997/04/06 22:54:10  keil
 * Using SKB's
 *
 * Revision 1.4  1997/01/27 15:50:21  keil
 * SMP proof,cosmetics
 *
 * Revision 1.3  1997/01/21 22:14:20  keil
 * cleanups
 *
 * Revision 1.2  1996/10/27 22:07:31  keil
 * cosmetic changes
 *
 * Revision 1.1  1996/10/13 20:04:49  keil
 * Initial revision
 *
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"

extern const char *CardType[];
const char *avm_revision = "$Revision: 2.7 $";

#define	 AVM_A1_STAT_ISAC	0x01
#define	 AVM_A1_STAT_HSCX	0x02
#define	 AVM_A1_STAT_TIMER	0x04

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

static inline u_char
readreg(unsigned int adr, u_char off)
{
	return (bytein(adr + off));
}

static inline void
writereg(unsigned int adr, u_char off, u_char data)
{
	byteout(adr + off, data);
}


static inline void
read_fifo(unsigned int adr, u_char * data, int size)
{
	insb(adr, data, size);
}

static void
write_fifo(unsigned int adr, u_char * data, int size)
{
	outsb(adr, data, size);
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return (readreg(cs->hw.avm.isac, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writereg(cs->hw.avm.isac, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	read_fifo(cs->hw.avm.isacfifo, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	write_fifo(cs->hw.avm.isacfifo, data, size);
}

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readreg(cs->hw.avm.hscx[hscx], offset));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writereg(cs->hw.avm.hscx[hscx], offset, value);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readreg(cs->hw.avm.hscx[nr], reg)
#define WRITEHSCX(cs, nr, reg, data) writereg(cs->hw.avm.hscx[nr], reg, data)
#define READHSCXFIFO(cs, nr, ptr, cnt) read_fifo(cs->hw.avm.hscxfifo[nr], ptr, cnt)
#define WRITEHSCXFIFO(cs, nr, ptr, cnt) write_fifo(cs->hw.avm.hscxfifo[nr], ptr, cnt)

#include "hscx_irq.c"

static void
avm_a1_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, sval, stat = 0;
	char tmp[32];

	if (!cs) {
		printk(KERN_WARNING "AVM A1: Spurious interrupt!\n");
		return;
	}
	while (((sval = bytein(cs->hw.avm.cfg_reg)) & 0xf) != 0x7) {
		if (!(sval & AVM_A1_STAT_TIMER)) {
			byteout(cs->hw.avm.cfg_reg, 0x1E);
			sval = bytein(cs->hw.avm.cfg_reg);
		} else if (cs->debug & L1_DEB_INTSTAT) {
			sprintf(tmp, "avm IntStatus %x", sval);
			debugl1(cs, tmp);
		}
		if (!(sval & AVM_A1_STAT_HSCX)) {
			val = readreg(cs->hw.avm.hscx[1], HSCX_ISTA);
			if (val) {
				hscx_int_main(cs, val);
				stat |= 1;
			}
		}
		if (!(sval & AVM_A1_STAT_ISAC)) {
			val = readreg(cs->hw.avm.isac, ISAC_ISTA);
			if (val) {
				isac_interrupt(cs, val);
				stat |= 2;
			}
		}
	}
	if (stat & 1) {
		writereg(cs->hw.avm.hscx[0], HSCX_MASK, 0xFF);
		writereg(cs->hw.avm.hscx[1], HSCX_MASK, 0xFF);
		writereg(cs->hw.avm.hscx[0], HSCX_MASK, 0x0);
		writereg(cs->hw.avm.hscx[1], HSCX_MASK, 0x0);
	}
	if (stat & 2) {
		writereg(cs->hw.avm.isac, ISAC_MASK, 0xFF);
		writereg(cs->hw.avm.isac, ISAC_MASK, 0x0);
	}
}

inline static void
release_ioregs(struct IsdnCardState *cs, int mask)
{
	release_region(cs->hw.avm.cfg_reg, 8);
	if (mask & 1)
		release_region(cs->hw.avm.isac + 32, 32);
	if (mask & 2)
		release_region(cs->hw.avm.isacfifo, 1);
	if (mask & 4)
		release_region(cs->hw.avm.hscx[0] + 32, 32);
	if (mask & 8)
		release_region(cs->hw.avm.hscxfifo[0], 1);
	if (mask & 0x10)
		release_region(cs->hw.avm.hscx[1] + 32, 32);
	if (mask & 0x20)
		release_region(cs->hw.avm.hscxfifo[1], 1);
}

static int
AVM_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			return(0);
		case CARD_RELEASE:
			release_ioregs(cs, 0x3f);
			return(0);
		case CARD_SETIRQ:
			return(request_irq(cs->irq, &avm_a1_interrupt,
					I4L_IRQ_FLAG, "HiSax", cs));
		case CARD_INIT:
			clear_pending_isac_ints(cs);
			clear_pending_hscx_ints(cs);
			initisac(cs);
			inithscx(cs);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}

__initfunc(int
setup_avm_a1(struct IsdnCard *card))
{
	u_char val;
	struct IsdnCardState *cs = card->cs;
	long flags;
	char tmp[64];

	strcpy(tmp, avm_revision);
	printk(KERN_INFO "HiSax: AVM driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_A1)
		return (0);

	cs->hw.avm.cfg_reg = card->para[1] + 0x1800;
	cs->hw.avm.isac = card->para[1] + 0x1400 - 0x20;
	cs->hw.avm.hscx[0] = card->para[1] + 0x400 - 0x20;
	cs->hw.avm.hscx[1] = card->para[1] + 0xc00 - 0x20;
	cs->hw.avm.isacfifo = card->para[1] + 0x1000;
	cs->hw.avm.hscxfifo[0] = card->para[1];
	cs->hw.avm.hscxfifo[1] = card->para[1] + 0x800;
	cs->irq = card->para[0];
	if (check_region((cs->hw.avm.cfg_reg), 8)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.avm.cfg_reg,
		       cs->hw.avm.cfg_reg + 8);
		return (0);
	} else {
		request_region(cs->hw.avm.cfg_reg, 8, "avm cfg");
	}
	if (check_region((cs->hw.avm.isac + 32), 32)) {
		printk(KERN_WARNING
		       "HiSax: %s isac ports %x-%x already in use\n",
		       CardType[cs->typ],
		       cs->hw.avm.isac + 32,
		       cs->hw.avm.isac + 64);
		release_ioregs(cs, 0);
		return (0);
	} else {
		request_region(cs->hw.avm.isac + 32, 32, "HiSax isac");
	}
	if (check_region((cs->hw.avm.isacfifo), 1)) {
		printk(KERN_WARNING
		       "HiSax: %s isac fifo port %x already in use\n",
		       CardType[cs->typ],
		       cs->hw.avm.isacfifo);
		release_ioregs(cs, 1);
		return (0);
	} else {
		request_region(cs->hw.avm.isacfifo, 1, "HiSax isac fifo");
	}
	if (check_region((cs->hw.avm.hscx[0]) + 32, 32)) {
		printk(KERN_WARNING
		       "HiSax: %s hscx A ports %x-%x already in use\n",
		       CardType[cs->typ],
		       cs->hw.avm.hscx[0] + 32,
		       cs->hw.avm.hscx[0] + 64);
		release_ioregs(cs, 3);
		return (0);
	} else {
		request_region(cs->hw.avm.hscx[0] + 32, 32, "HiSax hscx A");
	}
	if (check_region(cs->hw.avm.hscxfifo[0], 1)) {
		printk(KERN_WARNING
		       "HiSax: %s hscx A fifo port %x already in use\n",
		       CardType[cs->typ],
		       cs->hw.avm.hscxfifo[0]);
		release_ioregs(cs, 7);
		return (0);
	} else {
		request_region(cs->hw.avm.hscxfifo[0], 1, "HiSax hscx A fifo");
	}
	if (check_region(cs->hw.avm.hscx[1] + 32, 32)) {
		printk(KERN_WARNING
		       "HiSax: %s hscx B ports %x-%x already in use\n",
		       CardType[cs->typ],
		       cs->hw.avm.hscx[1] + 32,
		       cs->hw.avm.hscx[1] + 64);
		release_ioregs(cs, 0xf);
		return (0);
	} else {
		request_region(cs->hw.avm.hscx[1] + 32, 32, "HiSax hscx B");
	}
	if (check_region(cs->hw.avm.hscxfifo[1], 1)) {
		printk(KERN_WARNING
		       "HiSax: %s hscx B fifo port %x already in use\n",
		       CardType[cs->typ],
		       cs->hw.avm.hscxfifo[1]);
		release_ioregs(cs, 0x1f);
		return (0);
	} else {
		request_region(cs->hw.avm.hscxfifo[1], 1, "HiSax hscx B fifo");
	}
	save_flags(flags);
	byteout(cs->hw.avm.cfg_reg, 0x0);
	sti();
	HZDELAY(HZ / 5 + 1);
	byteout(cs->hw.avm.cfg_reg, 0x1);
	HZDELAY(HZ / 5 + 1);
	byteout(cs->hw.avm.cfg_reg, 0x0);
	HZDELAY(HZ / 5 + 1);
	val = cs->irq;
	if (val == 9)
		val = 2;
	byteout(cs->hw.avm.cfg_reg + 1, val);
	HZDELAY(HZ / 5 + 1);
	byteout(cs->hw.avm.cfg_reg, 0x0);
	HZDELAY(HZ / 5 + 1);
	restore_flags(flags);

	val = bytein(cs->hw.avm.cfg_reg);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       cs->hw.avm.cfg_reg, val);
	val = bytein(cs->hw.avm.cfg_reg + 3);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       cs->hw.avm.cfg_reg + 3, val);
	val = bytein(cs->hw.avm.cfg_reg + 2);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       cs->hw.avm.cfg_reg + 2, val);
	byteout(cs->hw.avm.cfg_reg, 0x1E);
	val = bytein(cs->hw.avm.cfg_reg);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       cs->hw.avm.cfg_reg, val);

	printk(KERN_INFO
	       "HiSax: %s config irq:%d cfg:0x%X\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.avm.cfg_reg);
	printk(KERN_INFO
	       "HiSax: isac:0x%X/0x%X\n",
	       cs->hw.avm.isac + 32, cs->hw.avm.isacfifo);
	printk(KERN_INFO
	       "HiSax: hscx A:0x%X/0x%X  hscx B:0x%X/0x%X\n",
	       cs->hw.avm.hscx[0] + 32, cs->hw.avm.hscxfifo[0],
	       cs->hw.avm.hscx[1] + 32, cs->hw.avm.hscxfifo[1]);

	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &AVM_card_msg;
	ISACVersion(cs, "AVM A1:");
	if (HscxVersion(cs, "AVM A1:")) {
		printk(KERN_WARNING
		       "AVM A1: wrong HSCX versions check IO address\n");
		release_ioregs(cs, 0x3f);
		return (0);
	}
	return (1);
}
