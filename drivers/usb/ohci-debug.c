/*
 * OHCI debugging code.  It's gross.
 *
 * (C) Copyright 1999 Gregory P. Smith
 */

#include <linux/kernel.h>
#include <asm/io.h>

#include "ohci.h"

void show_ohci_status(struct ohci *ohci)
{
	struct ohci_regs regs;
	int i;

	regs.revision = readl(ohci->regs->revision);
	regs.control = readl(ohci->regs->control);
	regs.cmdstatus = readl(ohci->regs->cmdstatus);
	regs.intrstatus = readl(ohci->regs->intrstatus);
	regs.intrenable = readl(ohci->regs->intrenable);
	regs.intrdisable = readl(ohci->regs->intrdisable);
	regs.hcca = readl(ohci->regs->hcca);
	regs.ed_periodcurrent = readl(ohci->regs->ed_periodcurrent);
	regs.ed_controlhead = readl(ohci->regs->ed_controlhead);
	regs.ed_bulkhead = readl(ohci->regs->ed_bulkhead);
	regs.ed_bulkcurrent = readl(ohci->regs->ed_bulkcurrent);
	regs.current_donehead = readl(ohci->regs->current_donehead);
	regs.fminterval = readl(ohci->regs->fminterval);
	regs.fmremaining = readl(ohci->regs->fmremaining);
	regs.fmnumber = readl(ohci->regs->fmnumber);
	regs.periodicstart = readl(ohci->regs->periodicstart);
	regs.lsthresh = readl(ohci->regs->lsthresh);
	regs.roothub.a = readl(ohci->regs->roothub.a);
	regs.roothub.b = readl(ohci->regs->roothub.b);
	regs.roothub.status = readl(ohci->regs->roothub.status);
	for (i=0; i<MAX_ROOT_PORTS; ++i)
		regs.roothub.portstatus[i] = readl(ohci->regs->roothub.portstatus[i]);

	printk("  ohci revision    =  0x%x\n", regs.revision);
	printk("  ohci control     =  0x%x\n", regs.control);
	printk("  ohci cmdstatus   =  0x%x\n", regs.cmdstatus);
	printk("  ohci intrstatus  =  0x%x\n", regs.intrstatus);
	printk("  ohci roothub.a   =  0x%x\n", regs.roothub.a);
	printk("  ohci roothub.b   =  0x%x\n", regs.roothub.b);
	printk("  ohci root status =  0x%x\n", regs.roothub.status);
} /* show_ohci_status() */


static void show_ohci_ed(struct ohci_ed *ed)
{
	int stat = ed->status;
	int skip = (stat & OHCI_ED_SKIP);
	int mps = (stat & OHCI_ED_MPS) >> 16;
	int isoc = (stat & OHCI_ED_F_ISOC);
	int low_speed = (stat & OHCI_ED_S_LOW);
	int dir = (stat & OHCI_ED_D);
	int endnum = (stat & OHCI_ED_EN) >> 7;
	int funcaddr = (stat & OHCI_ED_FA);
	int halted = (ed->head_td & 1);
	int toggle = (ed->head_td & 2) >> 1;

	printk("   ohci ED:\n");
	printk("     status     =  0x%x\n", stat);
	printk("       %sMPS %d%s%s%s%s tc%d e%d fa%d\n",
			skip ? "Skip " : "",
			mps,
			isoc ? "Isoc. " : "",
			low_speed ? "LowSpd " : "",
			(dir == OHCI_ED_D_IN) ? "Input " :
			(dir == OHCI_ED_D_OUT) ? "Output " : "",
			halted ? "Halted " : "",
			toggle,
			endnum,
			funcaddr);
	printk("     tail_td    =  0x%x\n", ed->tail_td);
	printk("     head_td    =  0x%x\n", ed->head_td);
	printk("     next_ed    =  0x%x\n", ed->next_ed);
} /* show_ohci_ed() */


static void show_ohci_td(struct ohci_td *td)
{
	int td_round = td->info & OHCI_TD_ROUND;
	int td_dir = td->info & OHCI_TD_D;
	int td_int_delay = td->info & OHCI_TD_IOC_DELAY;
	int td_toggle = td->info & OHCI_TD_DT;
	int td_errcnt = td_errorcount(td->info);
	int td_cc = td->info & OHCI_TD_CC;

	printk("   ohci TD hardware fields:\n");
	printk("      info     =  0x%x\n", td->info);
	printk("        %s%s%s%d%s%s%d%s%d\n",
		td_round ? "Rounding " : "",
		(td_dir == OHCI_TD_D_IN) ? "Input " :
		(td_dir == OHCI_TD_D_OUT) ? "Output " :
		(td_dir == OHCI_TD_D_SETUP) ? "Setup " : "",
		"IntDelay ", td_int_delay >> 21,
		td_toggle ? "Data1 " : "Data0 ",
		"ErrorCnt ", td_errcnt,
		"ComplCode ", td_cc);
	printk("       %sAccessed, %sActive\n",
			td_cc_accessed(td->info) ? "" : "Not ",
			td_active(td->info) ? "" : "Not ");

	printk("      cur_buf  =  0x%x\n", td->cur_buf);
	printk("      next_td  =  0x%x\n", td->next_td);
	printk("      buf_end  =  0x%x\n", td->buf_end);
	printk("   ohci TD driver fields:\n");
	printk("      data     =  %p\n", td->data);
	printk("      dev_id   =  %p\n", td->dev_id);
	printk("      ed_bus   =  %x\n", td->ed_bus);
} /* show_ohci_td() */


void show_ohci_device(struct ohci_device *dev)
{
	int idx;
	printk("  ohci_device usb       =  %p\n", dev->usb);
	printk("  ohci_device ohci      =  %p\n", dev->ohci);
	printk("  ohci_device ohci_hcca =  %p\n", dev->hcca);
	for (idx=0; idx<8 /*NUM_EDS*/; ++idx) {
		printk("   [ed num %d] ", idx);
		show_ohci_ed(&dev->ed[idx]);
	}
	for (idx=0; idx<8 /*NUM_TDS*/; ++idx) {
		printk("   [td num %d] ", idx);
		show_ohci_td(&dev->td[idx]);
	}
	printk("  ohci_device data\n    ");
	for (idx=0; idx<4; ++idx) {
		printk(" %08lx", dev->data[idx]);
	}
	printk("\n");
} /* show_ohci_device() */


/* vim:sw=8
 */
