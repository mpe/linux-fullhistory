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

	regs.revision = readl(&ohci->regs->revision);
	regs.control = readl(&ohci->regs->control);
	regs.cmdstatus = readl(&ohci->regs->cmdstatus);
	regs.intrstatus = readl(&ohci->regs->intrstatus);
	regs.intrenable = readl(&ohci->regs->intrenable);
	regs.hcca = readl(&ohci->regs->hcca);
	regs.ed_periodcurrent = readl(&ohci->regs->ed_periodcurrent);
	regs.ed_controlhead = readl(&ohci->regs->ed_controlhead);
	regs.ed_controlcurrent = readl(&ohci->regs->ed_controlcurrent);
	regs.ed_bulkhead = readl(&ohci->regs->ed_bulkhead);
	regs.ed_bulkcurrent = readl(&ohci->regs->ed_bulkcurrent);
	regs.current_donehead = readl(&ohci->regs->current_donehead);
	regs.fminterval = readl(&ohci->regs->fminterval);
	regs.fmremaining = readl(&ohci->regs->fmremaining);
	regs.fmnumber = readl(&ohci->regs->fmnumber);
	regs.periodicstart = readl(&ohci->regs->periodicstart);
	regs.lsthresh = readl(&ohci->regs->lsthresh);
	regs.roothub.a = readl(&ohci->regs->roothub.a);
	regs.roothub.b = readl(&ohci->regs->roothub.b);
	regs.roothub.status = readl(&ohci->regs->roothub.status);
	for (i=0; i<MAX_ROOT_PORTS; ++i)
		regs.roothub.portstatus[i] = readl(&ohci->regs->roothub.portstatus[i]);

	printk(KERN_DEBUG "  ohci revision    =  %x\n", regs.revision);
	printk(KERN_DEBUG "  ohci control     =  %x\n", regs.control);
	printk(KERN_DEBUG "  ohci cmdstatus   =  %x\n", regs.cmdstatus);
	printk(KERN_DEBUG "  ohci intrstatus  =  %x\n", regs.intrstatus);
	printk(KERN_DEBUG "  ohci intrenable  =  %x\n", regs.intrenable);

	printk(KERN_DEBUG "  ohci hcca        =  %x\n", regs.hcca);
	printk(KERN_DEBUG "  ohci ed_pdcur    =  %x\n", regs.ed_periodcurrent);
	printk(KERN_DEBUG "  ohci ed_ctrlhead =  %x\n", regs.ed_controlhead);
	printk(KERN_DEBUG "  ohci ed_ctrlcur  =  %x\n", regs.ed_controlcurrent);
	printk(KERN_DEBUG "  ohci ed_bulkhead =  %x\n", regs.ed_bulkhead);
	printk(KERN_DEBUG "  ohci ed_bulkcur  =  %x\n", regs.ed_bulkcurrent);
	printk(KERN_DEBUG "  ohci curdonehead =  %x\n", regs.current_donehead);

	printk(KERN_DEBUG "  ohci fminterval  =  %x\n", regs.fminterval);
	printk(KERN_DEBUG "  ohci fmremaining =  %x\n", regs.fmremaining);
	printk(KERN_DEBUG "  ohci fmnumber    =  %x\n", regs.fmnumber);
	printk(KERN_DEBUG "  ohci pdstart     =  %x\n", regs.periodicstart);
	printk(KERN_DEBUG "  ohci lsthresh    =  %x\n", regs.lsthresh);

	printk(KERN_DEBUG "  ohci roothub.a   =  %x\n", regs.roothub.a);
	printk(KERN_DEBUG "  ohci roothub.b   =  %x\n", regs.roothub.b);
	printk(KERN_DEBUG "  ohci root status =  %x\n", regs.roothub.status);
	printk(KERN_DEBUG "    roothub.port0  =  %x\n", regs.roothub.portstatus[0]);
	printk(KERN_DEBUG "    roothub.port1  =  %x\n", regs.roothub.portstatus[1]);
} /* show_ohci_status() */


void show_ohci_ed(struct ohci_ed *ed)
{
	int stat = le32_to_cpup(&ed->status);
	int skip = (stat & OHCI_ED_SKIP);
	int mps = (stat & OHCI_ED_MPS) >> 16;
	int isoc = (stat & OHCI_ED_F_ISOC);
	int low_speed = (stat & OHCI_ED_S_LOW);
	int dir = (stat & OHCI_ED_D);
	int endnum = (stat & OHCI_ED_EN) >> 7;
	int funcaddr = (stat & OHCI_ED_FA);
	int halted = (le32_to_cpup(&ed->_head_td) & 1);
	int toggle = (le32_to_cpup(&ed->_head_td) & 2) >> 1;

	printk(KERN_DEBUG "   ohci ED:\n");
	printk(KERN_DEBUG "     status     =  0x%x\n", stat);
	printk(KERN_DEBUG "       %sMPS %d%s%s%s%s tc%d e%d fa%d [HCD_%d%s]\n",
			skip ? "Skip " : "",
			mps,
			isoc ? " Isoc." : "",
			low_speed ? " LowSpd" : "",
			(dir == OHCI_ED_D_IN) ? " Input" :
			(dir == OHCI_ED_D_OUT) ? " Output" : "",
			halted ? " Halted" : "",
			toggle,
			endnum,
			funcaddr,
			ohci_ed_hcdtype(ed) >> 27,
			(stat & ED_ALLOCATED) ? ", Allocated" : "");
	printk(KERN_DEBUG "     tail_td    =  0x%x\n", ed_tail_td(ed));
	printk(KERN_DEBUG "     head_td    =  0x%x\n", ed_head_td(ed));
	printk(KERN_DEBUG "     next_ed    =  0x%x\n", le32_to_cpup(&ed->next_ed));
} /* show_ohci_ed() */


void show_ohci_td(struct ohci_td *td)
{
	int info = le32_to_cpup(&td->info);
	int td_round = info & OHCI_TD_ROUND;
	int td_dir = info & OHCI_TD_D;
	int td_int_delay = (info & OHCI_TD_IOC_DELAY) >> 21;
	int td_toggle = (info & OHCI_TD_DT) >> 24;
	int td_errcnt = td_errorcount(*td);
	int td_cc = OHCI_TD_CC_GET(info);

	printk(KERN_DEBUG "   ohci TD hardware fields:\n");
	printk(KERN_DEBUG "      info     =  0x%x\n", info);
	printk(KERN_DEBUG "        %s%s%s%d %s %s%d\n",
		td_round ? "Rounding " : "",
		(td_dir == OHCI_TD_D_IN) ? "Input " :
		(td_dir == OHCI_TD_D_OUT) ? "Output " :
		(td_dir == OHCI_TD_D_SETUP) ? "Setup " : "",
		"IntDelay ", td_int_delay,
		(td_toggle < 2) ? " " :
		(td_toggle & 1) ? "Data1" : "Data0",
		"ErrorCnt ", td_errcnt);
	printk(KERN_DEBUG "        ComplCode 0x%x, %sAccessed\n",
		td_cc,
		td_cc_accessed(*td) ? "" : "Not ");

	printk(KERN_DEBUG "        %s%s\n",
		td_allocated(*td) ? "Allocated" : "Free",
		td_dummy(*td) ? " DUMMY" : "");

	printk(KERN_DEBUG "      cur_buf  =  0x%x\n", le32_to_cpup(&td->cur_buf));
	printk(KERN_DEBUG "      next_td  =  0x%x\n", le32_to_cpup(&td->next_td));
	printk(KERN_DEBUG "      buf_end  =  0x%x\n", le32_to_cpup(&td->buf_end));
	printk(KERN_DEBUG "   ohci TD driver fields:\n");
	printk(KERN_DEBUG "      flags    =  %x {", td->hcd_flags);
	if (td_allocated(*td))
		printk(" alloc");
	if (td_dummy(*td))
		printk(" dummy");
	if (td_endofchain(*td))
		printk(" endofchain");
	if (!can_auto_free(*td))
		printk(" noautofree");
	printk("}\n");
	printk(KERN_DEBUG "      data     =  %p\n", td->data);
	printk(KERN_DEBUG "      cmpltd   =  %p\n", td->completed);
	printk(KERN_DEBUG "      dev_id   =  %p\n", td->dev_id);
	printk(KERN_DEBUG "      ed       =  %p\n", td->ed);
	if (td->data != NULL) {
	unsigned char *d = td->data;
	printk(KERN_DEBUG "   DATA: %02x %02x %02x %02x %02x %02x %02x %02x\n",
			d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7] );
	}
} /* show_ohci_td() */


void show_ohci_td_chain(struct ohci_td *td)
{
	struct ohci_td *cur_td;
	if (td == NULL) return;

	printk(KERN_DEBUG "+++ OHCI TD Chain %lx: +++\n", virt_to_bus(td));

	cur_td = td;
	for (;;) {
		show_ohci_td(cur_td);
		if (!cur_td->next_td) break;
		cur_td = bus_to_virt(le32_to_cpup(&cur_td->next_td));
		/* we can't trust -anything- we find inside of a dummy TD */
		if (td_dummy(*cur_td)) break;
	}

	printk(KERN_DEBUG "--- End  TD Chain %lx. ---\n", virt_to_bus(td));
} /* show_ohci_td_chain () */


void show_ohci_device(struct ohci_device *dev)
{
	int idx;
	printk(KERN_DEBUG "  ohci_device usb       =  %p\n", dev->usb);
	printk(KERN_DEBUG "  ohci_device ohci      =  %p\n", dev->ohci);
	printk(KERN_DEBUG "  ohci_device ohci_hcca =  %p\n", dev->hcca);
	for (idx=0; idx<3 /*NUM_EDS*/; ++idx) {
		printk(KERN_DEBUG "   [ed num %d] ", idx);
		show_ohci_ed(&dev->ed[idx]);
	}
	for (idx=0; idx<3 /*NUM_TDS*/; ++idx) {
		printk(KERN_DEBUG "   [td num %d] ", idx);
		show_ohci_td(&dev->td[idx]);
	}
	printk(KERN_DEBUG "  ohci_device data\n    ");
	for (idx=0; idx<4; ++idx) {
		printk(KERN_DEBUG " %08lx", dev->data[idx]);
	}
	printk(KERN_DEBUG "\n");
} /* show_ohci_device() */


void show_ohci_hcca(struct ohci_hcca *hcca)
{
	int idx;

	printk(KERN_DEBUG "  ohci_hcca\n");

	for (idx=0; idx<NUM_INTS; idx++) {
		printk(KERN_DEBUG "    int_table[%2d]  == %x\n", idx,
		       le32_to_cpup(hcca->int_table + idx));
	}

	printk(KERN_DEBUG "    frame_no          == %d\n",
	       le16_to_cpup(&hcca->frame_no));
	printk(KERN_DEBUG "    donehead          == 0x%08x\n",
	       le32_to_cpup(&hcca->donehead));
} /* show_ohci_hcca() */


/* vim:sw=8
 */
