#ifdef DEBUG

static void uhci_show_qh (puhci_desc_t qh)
{
	if (qh->type != QH_TYPE) {
		printk (KERN_DEBUG MODSTR "qh has not QH_TYPE\n");
		return;
	}
	printk (KERN_DEBUG MODSTR "uhci_show_qh %p (%08lX):\n", qh, virt_to_bus (qh));

	if (qh->hw.qh.head & UHCI_PTR_TERM)
		printk (KERN_DEBUG MODSTR "Head Terminate\n");
	else {
		if (qh->hw.qh.head & UHCI_PTR_QH)
			printk (KERN_DEBUG MODSTR "Head points to QH\n");
		else
			printk (KERN_DEBUG MODSTR "Head points to TD\n");

		printk (KERN_DEBUG MODSTR "head: %08X\n", qh->hw.qh.head & ~UHCI_PTR_BITS);
	}
	if (qh->hw.qh.element & UHCI_PTR_TERM)
		printk (KERN_DEBUG MODSTR "Element Terminate\n");
	else {

		if (qh->hw.qh.element & UHCI_PTR_QH)
			printk (KERN_DEBUG MODSTR "Element points to QH\n");
		else
			printk (KERN_DEBUG MODSTR "Element points to TD\n");
		printk (KERN_DEBUG MODSTR "element: %08X\n", qh->hw.qh.element & ~UHCI_PTR_BITS);
	}
}
#endif

static void uhci_show_td (puhci_desc_t td)
{
	char *spid;
	printk (KERN_DEBUG MODSTR "uhci_show_td %p (%08lX) ", td, virt_to_bus (td));

	switch (td->hw.td.info & 0xff) {
	case USB_PID_SETUP:
		spid = "SETUP";
		break;
	case USB_PID_OUT:
		spid = " OUT ";
		break;
	case USB_PID_IN:
		spid = " IN  ";
		break;
	default:
		spid = "  ?  ";
		break;
	}

	printk ("MaxLen=%02x DT%d EndPt=%x Dev=%x, PID=%x(%s) (buf=%08x)\n",
	     td->hw.td.info >> 21,
	     ((td->hw.td.info >> 19) & 1),
	     (td->hw.td.info >> 15) & 15,
	     (td->hw.td.info >> 8) & 127,
	     (td->hw.td.info & 0xff),
	     spid,
	     td->hw.td.buffer);

	printk (KERN_DEBUG MODSTR "Len=%02x e%d %s%s%s%s%s%s%s%s%s%s\n",
	     td->hw.td.status & 0x7ff,
	     ((td->hw.td.status >> 27) & 3),
	     (td->hw.td.status & TD_CTRL_SPD) ? "SPD " : "",
	     (td->hw.td.status & TD_CTRL_LS) ? "LS " : "",
	     (td->hw.td.status & TD_CTRL_IOC) ? "IOC " : "",
	     (td->hw.td.status & TD_CTRL_ACTIVE) ? "Active " : "",
	     (td->hw.td.status & TD_CTRL_STALLED) ? "Stalled " : "",
	     (td->hw.td.status & TD_CTRL_DBUFERR) ? "DataBufErr " : "",
	     (td->hw.td.status & TD_CTRL_BABBLE) ? "Babble " : "",
	     (td->hw.td.status & TD_CTRL_NAK) ? "NAK " : "",
	     (td->hw.td.status & TD_CTRL_CRCTIMEO) ? "CRC/Timeo " : "",
	     (td->hw.td.status & TD_CTRL_BITSTUFF) ? "BitStuff " : ""
		);
#if 1
	if (td->hw.td.link & UHCI_PTR_TERM)
		printk (KERN_DEBUG MODSTR "Link Terminate\n");
	else {
		if (td->hw.td.link & UHCI_PTR_QH)
			printk (KERN_DEBUG MODSTR "%s, link points to QH @ %08x\n",
			     (td->hw.td.link & UHCI_PTR_DEPTH ? "Depth first" : " Breadth first"),
			     td->hw.td.link & ~UHCI_PTR_BITS);
		else
			printk (KERN_DEBUG MODSTR "%s, link points to TD @ %08x \n",
			     (td->hw.td.link & UHCI_PTR_DEPTH ? "Depth first" : " Breadth first"),
			     td->hw.td.link & ~UHCI_PTR_BITS);
	}
#endif
}
#ifdef DEBUG
static void uhci_show_td_queue (puhci_desc_t td)
{
	printk (KERN_DEBUG MODSTR "uhci_show_td_queue %p (%08lX):\n", td, virt_to_bus (td));
	while (1) {
		uhci_show_td (td);
		if (td->hw.td.link & UHCI_PTR_TERM)
			break;
		//if(!(td->hw.td.link&UHCI_PTR_DEPTH))
		//      break;
		if (td != bus_to_virt (td->hw.td.link & ~UHCI_PTR_BITS))
			td = bus_to_virt (td->hw.td.link & ~UHCI_PTR_BITS);
		else {
			printk (KERN_DEBUG MODSTR "td points to itself!\n");
			break;
		}
//              schedule();
	}
}

static void uhci_show_queue (puhci_desc_t qh)
{
	printk (KERN_DEBUG MODSTR "uhci_show_queue %p:\n", qh);
	while (1) {
		uhci_show_qh (qh);

		if (qh->hw.qh.element & UHCI_PTR_QH)
			printk (KERN_DEBUG MODSTR "Warning: qh->element points to qh!\n");
		else if (!(qh->hw.qh.element & UHCI_PTR_TERM))
			uhci_show_td_queue (bus_to_virt (qh->hw.qh.element & ~UHCI_PTR_BITS));

		if (qh->hw.qh.head & UHCI_PTR_TERM)
			break;

		if (qh != bus_to_virt (qh->hw.qh.head & ~UHCI_PTR_BITS))
			qh = bus_to_virt (qh->hw.qh.head & ~UHCI_PTR_BITS);
		else {
			printk (KERN_DEBUG MODSTR "qh points to itself!\n");
			break;
		}
	}
}

static void uhci_show_sc (int port, unsigned short status)
{
	printk ("  stat%d     =     %04x   %s%s%s%s%s%s%s%s\n",
	     port,
	     status,
	     (status & USBPORTSC_SUSP) ? "PortSuspend " : "",
	     (status & USBPORTSC_PR) ? "PortReset " : "",
	     (status & USBPORTSC_LSDA) ? "LowSpeed " : "",
	     (status & USBPORTSC_RD) ? "ResumeDetect " : "",
	     (status & USBPORTSC_PEC) ? "EnableChange " : "",
	     (status & USBPORTSC_PE) ? "PortEnabled " : "",
	     (status & USBPORTSC_CSC) ? "ConnectChange " : "",
	     (status & USBPORTSC_CCS) ? "PortConnected " : "");
}

void uhci_show_status (puhci_t s)
{
	unsigned int io_addr = s->io_addr;
	unsigned short usbcmd, usbstat, usbint, usbfrnum;
	unsigned int flbaseadd;
	unsigned char sof;
	unsigned short portsc1, portsc2;

	usbcmd = inw (io_addr + 0);
	usbstat = inw (io_addr + 2);
	usbint = inw (io_addr + 4);
	usbfrnum = inw (io_addr + 6);
	flbaseadd = inl (io_addr + 8);
	sof = inb (io_addr + 12);
	portsc1 = inw (io_addr + 16);
	portsc2 = inw (io_addr + 18);

	printk ("  usbcmd    =     %04x   %s%s%s%s%s%s%s%s\n",
	     usbcmd,
	     (usbcmd & USBCMD_MAXP) ? "Maxp64 " : "Maxp32 ",
	     (usbcmd & USBCMD_CF) ? "CF " : "",
	     (usbcmd & USBCMD_SWDBG) ? "SWDBG " : "",
	     (usbcmd & USBCMD_FGR) ? "FGR " : "",
	     (usbcmd & USBCMD_EGSM) ? "EGSM " : "",
	     (usbcmd & USBCMD_GRESET) ? "GRESET " : "",
	     (usbcmd & USBCMD_HCRESET) ? "HCRESET " : "",
	     (usbcmd & USBCMD_RS) ? "RS " : "");

	printk ("  usbstat   =     %04x   %s%s%s%s%s%s\n",
	     usbstat,
	     (usbstat & USBSTS_HCH) ? "HCHalted " : "",
	     (usbstat & USBSTS_HCPE) ? "HostControllerProcessError " : "",
	     (usbstat & USBSTS_HSE) ? "HostSystemError " : "",
	     (usbstat & USBSTS_RD) ? "ResumeDetect " : "",
	     (usbstat & USBSTS_ERROR) ? "USBError " : "",
	     (usbstat & USBSTS_USBINT) ? "USBINT " : "");

	printk ("  usbint    =     %04x\n", usbint);
	printk ("  usbfrnum  =   (%d)%03x\n", (usbfrnum >> 10) & 1,
	     0xfff & (4 * (unsigned int) usbfrnum));
	printk ("  flbaseadd = %08x\n", flbaseadd);
	printk ("  sof       =       %02x\n", sof);
	uhci_show_sc (1, portsc1);
	uhci_show_sc (2, portsc2);
}
#endif
