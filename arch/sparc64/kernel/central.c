/* $Id: central.c,v 1.11 1998/12/14 12:18:16 davem Exp $
 * central.c: Central FHC driver for Sunfire/Starfire/Wildfire.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include <asm/page.h>
#include <asm/fhc.h>

struct linux_central *central_bus = NULL;
struct linux_fhc *fhc_list = NULL;

#define IS_CENTRAL_FHC(__fhc)	((__fhc) == central_bus->child)

static inline unsigned long long_align(unsigned long addr)
{
	return ((addr + (sizeof(unsigned long) - 1)) &
		~(sizeof(unsigned long) - 1));
}

extern void prom_central_ranges_init(int cnode, struct linux_central *central);
extern void prom_fhc_ranges_init(int fnode, struct linux_fhc *fhc);

static unsigned long probe_other_fhcs(unsigned long memory_start)
{
	struct linux_prom64_registers fpregs[6];
	char namebuf[128];
	int node;

	node = prom_getchild(prom_root_node);
	node = prom_searchsiblings(node, "fhc");
	if (node == 0) {
		prom_printf("FHC: Cannot find any toplevel firehose controllers.\n");
		prom_halt();
	}
	while(node) {
		struct linux_fhc *fhc;
		int board;
		u32 tmp;

		fhc = (struct linux_fhc *)memory_start;
		memory_start += sizeof(struct linux_fhc);
		memory_start = long_align(memory_start);

		/* Link it into the FHC chain. */
		fhc->next = fhc_list;
		fhc_list = fhc;

		/* Toplevel FHCs have no parent. */
		fhc->parent = NULL;
		
		fhc->prom_node = node;
		prom_getstring(node, "name", namebuf, sizeof(namebuf));
		strcpy(fhc->prom_name, namebuf);
		prom_fhc_ranges_init(node, fhc);

		/* Non-central FHC's have 64-bit OBP format registers. */
		if(prom_getproperty(node, "reg",
				    (char *)&fpregs[0], sizeof(fpregs)) == -1) {
			prom_printf("FHC: Fatal error, cannot get fhc regs.\n");
			prom_halt();
		}

		/* Only central FHC needs special ranges applied. */
		fhc->fhc_regs.pregs = (struct fhc_internal_regs *)
			__va(fpregs[0].phys_addr);
		fhc->fhc_regs.ireg = (struct fhc_ign_reg *)
			__va(fpregs[1].phys_addr);
		fhc->fhc_regs.ffregs = (struct fhc_fanfail_regs *)
			__va(fpregs[2].phys_addr);
		fhc->fhc_regs.sregs = (struct fhc_system_regs *)
			__va(fpregs[3].phys_addr);
		fhc->fhc_regs.uregs = (struct fhc_uart_regs *)
			__va(fpregs[4].phys_addr);
		fhc->fhc_regs.tregs = (struct fhc_tod_regs *)
			__va(fpregs[5].phys_addr);

		board = prom_getintdefault(node, "board#", -1);
		fhc->board = board;

		tmp = fhc->fhc_regs.pregs->fhc_jtag_ctrl;
		if((tmp & FHC_JTAG_CTRL_MENAB) != 0)
			fhc->jtag_master = 1;
		else
			fhc->jtag_master = 0;

		tmp = fhc->fhc_regs.pregs->fhc_id;
		printk("FHC(board %d): Version[%x] PartID[%x] Manuf[%x] %s\n",
		       board,
		       (tmp & FHC_ID_VERS) >> 28,
		       (tmp & FHC_ID_PARTID) >> 12,
		       (tmp & FHC_ID_MANUF) >> 1,
		       (fhc->jtag_master ? "(JTAG Master)" : ""));
		
		/* This bit must be set in all non-central FHC's in
		 * the system.  When it is clear, this identifies
		 * the central board.
		 */
		fhc->fhc_regs.pregs->fhc_control |= FHC_CONTROL_IXIST;

		/* Look for the next FHC. */
		node = prom_getsibling(node);
		if(node == 0)
			break;
		node = prom_searchsiblings(node, "fhc");
		if(node == 0)
			break;
	}

	return memory_start;
}

static void probe_clock_board(struct linux_central *central,
			      struct linux_fhc *fhc,
			      int cnode, int fnode)
{
	struct linux_prom_registers cregs[3];
	int clknode, nslots, tmp, nregs;

	clknode = prom_searchsiblings(prom_getchild(fnode), "clock-board");
	if(clknode == 0 || clknode == -1) {
		prom_printf("Critical error, central lacks clock-board.\n");
		prom_halt();
	}
	nregs = prom_getproperty(clknode, "reg", (char *)&cregs[0], sizeof(cregs));
	if (nregs == -1) {
		prom_printf("CENTRAL: Fatal error, cannot map clock-board regs.\n");
		prom_halt();
	}
	nregs /= sizeof(struct linux_prom_registers);
	prom_apply_fhc_ranges(fhc, &cregs[0], nregs);
	prom_apply_central_ranges(central, &cregs[0], nregs);
	central->cfreg = (volatile u8 *)
		__va((((unsigned long)cregs[0].which_io) << 32) |
		     (((unsigned long)cregs[0].phys_addr)+0x02));
	central->clkregs = (struct clock_board_regs *)
		__va((((unsigned long)cregs[1].which_io) << 32) |
		     (((unsigned long)cregs[1].phys_addr)));
	if(nregs == 2)
		central->clkver = NULL;
	else
		central->clkver = (volatile u8 *)
			__va((((unsigned long)cregs[2].which_io) << 32) |
			     (((unsigned long)cregs[2].phys_addr)));

	tmp = central->clkregs->stat1;
	tmp &= 0xc0;
	switch(tmp) {
	case 0x40:
		nslots = 16;
		break;
	case 0xc0:
		nslots = 8;
		break;
	case 0x80:
		if(central->clkver != NULL &&
		   *(central->clkver) != 0) {
			if((*(central->clkver) & 0x80) != 0)
				nslots = 4;
			else
				nslots = 5;
			break;
		}
	default:
		nslots = 4;
		break;
	};
	central->slots = nslots;
	printk("CENTRAL: Detected %d slot Enterprise system. cfreg[%02x] cver[%02x]\n",
	       central->slots, *(central->cfreg),
	       (central->clkver ? *(central->clkver) : 0x00));
}

unsigned long central_probe(unsigned long memory_start)
{
	struct linux_prom_registers fpregs[6];
	struct linux_fhc *fhc;
	char namebuf[128];
	int cnode, fnode, err;

	cnode = prom_finddevice("/central");
	if(cnode == 0 || cnode == -1) {
		extern void starfire_check(void);

		starfire_check();
		return memory_start;
	}

	/* Ok we got one, grab some memory for software state. */
	memory_start = long_align(memory_start);
	central_bus = (struct linux_central *) (memory_start);

	memory_start += sizeof(struct linux_central);
	memory_start = long_align(memory_start);
	fhc = (struct linux_fhc *)(memory_start);
	memory_start += sizeof(struct linux_fhc);
	memory_start = long_align(memory_start);

	/* First init central. */
	central_bus->child = fhc;
	central_bus->prom_node = cnode;

	prom_getstring(cnode, "name", namebuf, sizeof(namebuf));
	strcpy(central_bus->prom_name, namebuf);

	prom_central_ranges_init(cnode, central_bus);

	/* And then central's FHC. */
	fhc->next = fhc_list;
	fhc_list = fhc;

	fhc->parent = central_bus;
	fnode = prom_searchsiblings(prom_getchild(cnode), "fhc");
	if(fnode == 0 || fnode == -1) {
		prom_printf("Critical error, central board lacks fhc.\n");
		prom_halt();
	}
	fhc->prom_node = fnode;
	prom_getstring(fnode, "name", namebuf, sizeof(namebuf));
	strcpy(fhc->prom_name, namebuf);

	prom_fhc_ranges_init(fnode, fhc);

	/* Now, map in FHC register set. */
	if (prom_getproperty(fnode, "reg", (char *)&fpregs[0], sizeof(fpregs)) == -1) {
		prom_printf("CENTRAL: Fatal error, cannot get fhc regs.\n");
		prom_halt();
	}
	prom_apply_central_ranges(central_bus, &fpregs[0], 6);
	
	fhc->fhc_regs.pregs = (struct fhc_internal_regs *)
		__va((((unsigned long)fpregs[0].which_io)<<32) |
		     (((unsigned long)fpregs[0].phys_addr)));
	fhc->fhc_regs.ireg = (struct fhc_ign_reg *)
		__va((((unsigned long)fpregs[1].which_io)<<32) |
		     (((unsigned long)fpregs[1].phys_addr)));
	fhc->fhc_regs.ffregs = (struct fhc_fanfail_regs *)
		__va((((unsigned long)fpregs[2].which_io)<<32) |
		     (((unsigned long)fpregs[2].phys_addr)));
	fhc->fhc_regs.sregs = (struct fhc_system_regs *)
		__va((((unsigned long)fpregs[3].which_io)<<32) |
		     (((unsigned long)fpregs[3].phys_addr)));
	fhc->fhc_regs.uregs = (struct fhc_uart_regs *)
		__va((((unsigned long)fpregs[4].which_io)<<32) |
		     (((unsigned long)fpregs[4].phys_addr)));
	fhc->fhc_regs.tregs = (struct fhc_tod_regs *)
		__va((((unsigned long)fpregs[5].which_io)<<32) |
		     (((unsigned long)fpregs[5].phys_addr)));

	/* Obtain board number from board status register, Central's
	 * FHC lacks "board#" property.
	 */
	err = fhc->fhc_regs.pregs->fhc_bsr;
	fhc->board = (((err >> 16) & 0x01) |
		      ((err >> 12) & 0x0e));

	fhc->jtag_master = 0;

	/* Attach the clock board registers for CENTRAL. */
	probe_clock_board(central_bus, fhc, cnode, fnode);

	err = fhc->fhc_regs.pregs->fhc_id;
	printk("FHC(board %d): Version[%x] PartID[%x] Manuf[%x] (CENTRAL)\n",
	       fhc->board,
	       ((err & FHC_ID_VERS) >> 28),
	       ((err & FHC_ID_PARTID) >> 12),
	       ((err & FHC_ID_MANUF) >> 1));

	return probe_other_fhcs(memory_start);
}

static __inline__ void fhc_ledblink(struct linux_fhc *fhc, int on)
{
	volatile u32 *ctrl = (volatile u32 *)
		&fhc->fhc_regs.pregs->fhc_control;
	u32 tmp;

	tmp = *ctrl;

	/* NOTE: reverse logic on this bit */
	if (on)
		tmp &= ~(FHC_CONTROL_RLED);
	else
		tmp |= FHC_CONTROL_RLED;
	tmp &= ~(FHC_CONTROL_AOFF | FHC_CONTROL_BOFF | FHC_CONTROL_SLINE);

	*ctrl = tmp;
	tmp = *ctrl;
}

static __inline__ void central_ledblink(struct linux_central *central, int on)
{
	volatile u8 *ctrl = (volatile u8 *) &central->clkregs->control;
	int tmp;

	tmp = *ctrl;

	/* NOTE: reverse logic on this bit */
	if(on)
		tmp &= ~(CLOCK_CTRL_RLED);
	else
		tmp |= CLOCK_CTRL_RLED;

	*ctrl = tmp;
	tmp = *ctrl;
}

static struct timer_list sftimer;
static int led_state;

static void sunfire_timer(unsigned long __ignored)
{
	struct linux_fhc *fhc;

	central_ledblink(central_bus, led_state);
	for(fhc = fhc_list; fhc != NULL; fhc = fhc->next)
		if(! IS_CENTRAL_FHC(fhc))
			fhc_ledblink(fhc, led_state);
	led_state = ! led_state;
	sftimer.expires = jiffies + (HZ >> 1);
	add_timer(&sftimer);
}

/* After PCI/SBUS busses have been probed, this is called to perform
 * final initialization of all FireHose Controllers in the system.
 */
void firetruck_init(void)
{
	struct linux_central *central = central_bus;
	struct linux_fhc *fhc;

	/* No central bus, nothing to do. */
	if (central == NULL)
		return;

	for(fhc = fhc_list; fhc != NULL; fhc = fhc->next) {
		volatile u32 *ctrl = (volatile u32 *)
			&fhc->fhc_regs.pregs->fhc_control;
		u32 tmp;

		/* Clear all of the interrupt mapping registers
		 * just in case OBP left them in a foul state.
		 */
#define ZAP(REG1, REG2) \
do {	volatile u32 *__iclr = (volatile u32 *)(&(REG1)); \
	volatile u32 *__imap = (volatile u32 *)(&(REG2)); \
	*(__iclr) = 0; \
	(void) *(__iclr); \
	*(__imap) &= ~(0x80000000); \
	(void) *(__imap); \
} while(0)

		ZAP(fhc->fhc_regs.ffregs->fhc_ff_iclr,
		    fhc->fhc_regs.ffregs->fhc_ff_imap);
		ZAP(fhc->fhc_regs.sregs->fhc_sys_iclr,
		    fhc->fhc_regs.sregs->fhc_sys_imap);
		ZAP(fhc->fhc_regs.uregs->fhc_uart_iclr,
		    fhc->fhc_regs.uregs->fhc_uart_imap);
		ZAP(fhc->fhc_regs.tregs->fhc_tod_iclr,
		    fhc->fhc_regs.tregs->fhc_tod_imap);

#undef ZAP

		/* Setup FHC control register. */
		tmp = *ctrl;

		/* All non-central boards have this bit set. */
		if(! IS_CENTRAL_FHC(fhc))
			tmp |= FHC_CONTROL_IXIST;

		/* For all FHCs, clear the firmware synchronization
		 * line and both low power mode enables.
		 */
		tmp &= ~(FHC_CONTROL_AOFF | FHC_CONTROL_BOFF | FHC_CONTROL_SLINE);
		*ctrl = tmp;
		tmp = *ctrl; /* Ensure completion */
	}

	/* OBP leaves it on, turn it off so clock board timer LED
	 * is in sync with FHC ones.
	 */
	central->clkregs->control &= ~(CLOCK_CTRL_RLED);

	led_state = 0;
	init_timer(&sftimer);
	sftimer.data = 0;
	sftimer.function = &sunfire_timer;
	sftimer.expires = jiffies + (HZ >> 1);
	add_timer(&sftimer);
}
