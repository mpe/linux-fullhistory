/*
 * $Id: uhci-debug.c,v 1.12 1999/12/13 15:24:42 fliegl Exp $
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <asm/io.h>

#define DEBUG

#include "usb.h"
#include "uhci.h"

void dump_urb (purb_t purb)
{
	dbg("urb                   :%p", purb);
	dbg("next                  :%p", purb->next);
	dbg("dev                   :%p", purb->dev);
	dbg("pipe                  :%08X", purb->pipe);
	dbg("status                :%d", purb->status);
	dbg("transfer_flags        :%08X", purb->transfer_flags);
	dbg("transfer_buffer       :%p", purb->transfer_buffer);
	dbg("transfer_buffer_length:%d", purb->transfer_buffer_length);
	dbg("actual_length         :%d", purb->actual_length);
	dbg("setup_packet          :%p", purb->setup_packet);
	dbg("start_frame           :%d", purb->start_frame);
	dbg("number_of_packets     :%d", purb->number_of_packets);
	dbg("interval              :%d", purb->interval);
	dbg("error_count           :%d", purb->error_count);
	dbg("context               :%p", purb->context);
	dbg("complete              :%p", purb->complete);
}

void beep (long freq)
{
	long v;
	char low, high;

	if (!freq)
		outb (inb (0x61) & 252, 0x61);
	else {
		outb (inb (0x61) | 0x3, 0x61);

		v = 1193180L / freq;

		low = (char) (v & 255);
		high = (char) ((v >> 8) & 255);

		outb (182, 0x43);
		outb (low, 0x42);
		outb (high, 0x42);
	}
}

void uhci_show_qh (puhci_desc_t qh)
{
	if (qh->type != QH_TYPE) {
		dbg("qh has not QH_TYPE");
		return;
	}
	dbg("uhci_show_qh %p (%08lX):", qh, virt_to_bus (qh));

	if (qh->hw.qh.head & UHCI_PTR_TERM)
		dbg("Head Terminate");
	else {
		if (qh->hw.qh.head & UHCI_PTR_QH)
			dbg("Head points to QH");
		else
			dbg("Head points to TD");

		dbg("head: %08X", qh->hw.qh.head & ~UHCI_PTR_BITS);
	}
	if (qh->hw.qh.element & UHCI_PTR_TERM)
		dbg("Element Terminate");
	else {

		if (qh->hw.qh.element & UHCI_PTR_QH)
			dbg("Element points to QH");
		else
			dbg("Element points to TD");
		dbg("element: %08X", qh->hw.qh.element & ~UHCI_PTR_BITS);
	}
}

void uhci_show_td (puhci_desc_t td)
{
	char *spid;

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

	dbg("uhci_show_td %p (%08lX) MaxLen=%02x DT%d EndPt=%x Dev=%x, PID=%x(%s) (buf=%08x)",
	     td,
	     virt_to_bus(td),
	     td->hw.td.info >> 21,
	     ((td->hw.td.info >> 19) & 1),
	     (td->hw.td.info >> 15) & 15,
	     (td->hw.td.info >> 8) & 127,
	     (td->hw.td.info & 0xff),
	     spid,
	     td->hw.td.buffer);

	dbg("Len=%02x e%d %s%s%s%s%s%s%s%s%s%s",
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

	if (td->hw.td.link & UHCI_PTR_TERM)
		dbg("Link Terminate");
	else {
		if (td->hw.td.link & UHCI_PTR_QH)
			dbg("%s, link points to QH @ %08x",
			     (td->hw.td.link & UHCI_PTR_DEPTH ? "Depth first" : " Breadth first"),
			     td->hw.td.link & ~UHCI_PTR_BITS);
		else
			dbg("%s, link points to TD @ %08x",
			     (td->hw.td.link & UHCI_PTR_DEPTH ? "Depth first" : " Breadth first"),
			     td->hw.td.link & ~UHCI_PTR_BITS);
	}
}

void uhci_show_td_queue (puhci_desc_t td)
{
	dbg("uhci_show_td_queue %p (%08lX):", td, virt_to_bus (td));
	while (1) {
		uhci_show_td (td);
		if (td->hw.td.link & UHCI_PTR_TERM)
			break;
		//if(!(td->hw.td.link&UHCI_PTR_DEPTH))
		//      break;
		if (td != bus_to_virt (td->hw.td.link & ~UHCI_PTR_BITS))
			td = bus_to_virt (td->hw.td.link & ~UHCI_PTR_BITS);
		else {
			dbg("td points to itself!");
			break;
		}
//              schedule();
	}
}

void uhci_show_queue (puhci_desc_t qh)
{
	dbg("uhci_show_queue %p:", qh);
	while (1) {
		uhci_show_qh (qh);

		if (qh->hw.qh.element & UHCI_PTR_QH)
			dbg("Warning: qh->element points to qh!");
		else if (!(qh->hw.qh.element & UHCI_PTR_TERM))
			uhci_show_td_queue (bus_to_virt (qh->hw.qh.element & ~UHCI_PTR_BITS));

		if (qh->hw.qh.head & UHCI_PTR_TERM)
			break;

		if (qh != bus_to_virt (qh->hw.qh.head & ~UHCI_PTR_BITS))
			qh = bus_to_virt (qh->hw.qh.head & ~UHCI_PTR_BITS);
		else {
			dbg("qh points to itself!");
			break;
		}
	}
}

static void uhci_show_sc (int port, unsigned short status)
{
	dbg("  stat%d     =     %04x   %s%s%s%s%s%s%s%s",
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

	dbg("  usbcmd    =     %04x   %s%s%s%s%s%s%s%s",
	     usbcmd,
	     (usbcmd & USBCMD_MAXP) ? "Maxp64 " : "Maxp32 ",
	     (usbcmd & USBCMD_CF) ? "CF " : "",
	     (usbcmd & USBCMD_SWDBG) ? "SWDBG " : "",
	     (usbcmd & USBCMD_FGR) ? "FGR " : "",
	     (usbcmd & USBCMD_EGSM) ? "EGSM " : "",
	     (usbcmd & USBCMD_GRESET) ? "GRESET " : "",
	     (usbcmd & USBCMD_HCRESET) ? "HCRESET " : "",
	     (usbcmd & USBCMD_RS) ? "RS " : "");

	dbg("  usbstat   =     %04x   %s%s%s%s%s%s",
	     usbstat,
	     (usbstat & USBSTS_HCH) ? "HCHalted " : "",
	     (usbstat & USBSTS_HCPE) ? "HostControllerProcessError " : "",
	     (usbstat & USBSTS_HSE) ? "HostSystemError " : "",
	     (usbstat & USBSTS_RD) ? "ResumeDetect " : "",
	     (usbstat & USBSTS_ERROR) ? "USBError " : "",
	     (usbstat & USBSTS_USBINT) ? "USBINT " : "");

	dbg("  usbint    =     %04x", usbint);
	dbg("  usbfrnum  =   (%d)%03x", (usbfrnum >> 10) & 1,
	     0xfff & (4 * (unsigned int) usbfrnum));
	dbg("  flbaseadd = %08x", flbaseadd);
	dbg("  sof       =       %02x", sof);
	uhci_show_sc (1, portsc1);
	uhci_show_sc (2, portsc2);
}
