/* $Id: sportster.c,v 1.14.6.2 2001/09/23 22:24:51 kai Exp $
 *
 * low level stuff for USR Sportster internal TA
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to Christian "naddy" Weisgerber (3Com, US Robotics) for documentation
 *
 *
 */
#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"

extern const char *CardType[];
const char *sportster_revision = "$Revision: 1.14.6.2 $";

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define	 SPORTSTER_ISAC		0xC000
#define	 SPORTSTER_HSCXA	0x0000
#define	 SPORTSTER_HSCXB	0x4000
#define	 SPORTSTER_RES_IRQ	0x8000
#define	 SPORTSTER_RESET	0x80
#define	 SPORTSTER_INTE		0x40

static inline int
calc_off(unsigned int base, unsigned int off)
{
	return(base + ((off & 0xfc)<<8) + ((off & 3)<<1));
}

static inline void
read_fifo(unsigned int adr, u8 * data, int size)
{
	insb(adr, data, size);
}

static void
write_fifo(unsigned int adr, u8 * data, int size)
{
	outsb(adr, data, size);
}

static u8
isac_read(struct IsdnCardState *cs, u8 offset)
{
	return bytein(calc_off(cs->hw.spt.isac, offset));
}

static void
isac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	byteout(calc_off(cs->hw.spt.isac, offset), value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	read_fifo(cs->hw.spt.isac, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	write_fifo(cs->hw.spt.isac, data, size);
}

static struct dc_hw_ops isac_ops = {
	.read_reg   = isac_read,
	.write_reg  = isac_write,
	.read_fifo  = isac_read_fifo,
	.write_fifo = isac_write_fifo,
};

static u8
hscx_read(struct IsdnCardState *cs, int hscx, u8 offset)
{
	return bytein(calc_off(cs->hw.spt.hscx[hscx], offset));
}

static void
hscx_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)
{
	byteout(calc_off(cs->hw.spt.hscx[hscx], offset), value);
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	read_fifo(cs->hw.spt.hscx[hscx], data, size);
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	write_fifo(cs->hw.spt.hscx[hscx], data, size);
}

static struct bc_hw_ops hscx_ops = {
	.read_reg   = hscx_read,
	.write_reg  = hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static void
sportster_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;

	hscxisac_irq(intno, dev_id, regs);
	bytein(cs->hw.spt.cfg_reg + SPORTSTER_RES_IRQ +1);
}

static void
sportster_release(struct IsdnCardState *cs)
{
	int i, adr;

	byteout(cs->hw.spt.cfg_reg + SPORTSTER_RES_IRQ, 0);
	for (i=0; i<64; i++) {
		adr = cs->hw.spt.cfg_reg + i *1024;
		release_region(adr, 8);
	}
}

static int
sportster_reset(struct IsdnCardState *cs)
{
	cs->hw.spt.res_irq |= SPORTSTER_RESET; /* Reset On */
	byteout(cs->hw.spt.cfg_reg + SPORTSTER_RES_IRQ, cs->hw.spt.res_irq);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	cs->hw.spt.res_irq &= ~SPORTSTER_RESET; /* Reset Off */
	byteout(cs->hw.spt.cfg_reg + SPORTSTER_RES_IRQ, cs->hw.spt.res_irq);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((10*HZ)/1000);
	return 0;
}

static int
Sportster_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	return(0);
}

static void
sportster_init(struct IsdnCardState *cs)
{
	inithscxisac(cs);
	cs->hw.spt.res_irq |= SPORTSTER_INTE; /* IRQ On */
	byteout(cs->hw.spt.cfg_reg + SPORTSTER_RES_IRQ, cs->hw.spt.res_irq);
}

static struct card_ops sportster_ops = {
	.init     = sportster_init,
	.reset    = sportster_reset,
	.release  = sportster_release,
	.irq_func = sportster_interrupt,
};

static int __init
get_io_range(struct IsdnCardState *cs)
{
	int i, j, adr;
	
	for (i=0;i<64;i++) {
		adr = cs->hw.spt.cfg_reg + i *1024;
		if (!request_region(adr, 8, "sportster")) {
			printk(KERN_WARNING
				"HiSax: %s config port %x-%x already in use\n",
				CardType[cs->typ], adr, adr + 8);
			break;
		}
	}
	if (i==64)
		return(1);
	else {
		for (j=0; j<i; j++) {
			adr = cs->hw.spt.cfg_reg + j *1024;
			release_region(adr, 8);
		}
		return(0);
	}
}

int __init
setup_sportster(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, sportster_revision);
	printk(KERN_INFO "HiSax: USR Sportster driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_SPORTSTER)
		return (0);

	cs->hw.spt.cfg_reg = card->para[1];
	cs->irq = card->para[0];
	if (!get_io_range(cs))
		return (0);
	cs->hw.spt.isac = cs->hw.spt.cfg_reg + SPORTSTER_ISAC;
	cs->hw.spt.hscx[0] = cs->hw.spt.cfg_reg + SPORTSTER_HSCXA;
	cs->hw.spt.hscx[1] = cs->hw.spt.cfg_reg + SPORTSTER_HSCXB;
	
	switch(cs->irq) {
		case 5:	cs->hw.spt.res_irq = 1;
			break;
		case 7:	cs->hw.spt.res_irq = 2;
			break;
		case 10:cs->hw.spt.res_irq = 3;
			break;
		case 11:cs->hw.spt.res_irq = 4;
			break;
		case 12:cs->hw.spt.res_irq = 5;
			break;
		case 14:cs->hw.spt.res_irq = 6;
			break;
		case 15:cs->hw.spt.res_irq = 7;
			break;
		default:sportster_release(cs);
			printk(KERN_WARNING "Sportster: wrong IRQ\n");
			return(0);
	}
	sportster_reset(cs);
	printk(KERN_INFO
	       "HiSax: %s config irq:%d cfg:0x%X\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.spt.cfg_reg);

	cs->dc_hw_ops = &isac_ops;
	cs->bc_hw_ops = &hscx_ops;
	cs->cardmsg = &Sportster_card_msg;
	cs->card_ops = &sportster_ops;
	ISACVersion(cs, "Sportster:");
	if (HscxVersion(cs, "Sportster:")) {
		printk(KERN_WARNING
		       "Sportster: wrong HSCX versions check IO address\n");
		sportster_release(cs);
		return (0);
	}
	return (1);
}
