/*
 * UHCI-specific debugging code. Invaluable when something
 * goes wrong, but don't get in my face.
 *
 * (C) Copyright 1999 Linus Torvalds
 */

#include <linux/kernel.h>
#include <asm/io.h>

#include "uhci.h"

void show_td(struct uhci_td * td)
{
	printk("%08x ", td->link);
	printk("%se%d %s%s%s%s%s%s%s%s%s%sLength=%x ",
		((td->status >> 29) & 1) ? "SPD " : "",
		((td->status >> 27) & 3),
		((td->status >> 26) & 1) ? "LS " : "",
		((td->status >> 25) & 1) ? "IOS " : "",
		((td->status >> 24) & 1) ? "IOC " : "",
		((td->status >> 23) & 1) ? "Active " : "",
		((td->status >> 22) & 1) ? "Stalled " : "",
		((td->status >> 21) & 1) ? "DataBufErr " : "",
		((td->status >> 20) & 1) ? "Babble " : "",
		((td->status >> 19) & 1) ? "NAK " : "",
		((td->status >> 18) & 1) ? "CRC/Timeo " : "",
		((td->status >> 17) & 1) ? "BitStuff " : "",
		td->status & 0x7ff);
	printk("MaxLen=%x %sEndPt=%x Dev=%x, PID=%x ",
		td->info >> 21,
		 ((td->info >> 19) & 1) ? "DT " : "",
		 (td->info >> 15) & 15,
		 (td->info >> 8) & 127,
		 td->info & 0xff);
	printk("(buf=%08x)\n", td->buffer);
}

static void show_sc(int port, unsigned short status)
{
	printk("  stat%d     =     %04x   %s%s%s%s%s%s%s%s\n",
		port,
		status,
		(status & (1 << 12)) ? " PortSuspend" : "",
		(status & (1 << 9)) ? " PortReset" : "",
		(status & (1 << 8)) ? " LowSpeed" : "",
		(status & 0x40) ? " ResumeDetect" : "",
		(status & 0x08) ? " EnableChange" : "",
		(status & 0x04) ? " PortEnabled" : "",
		(status & 0x02) ? " ConnectChange" : "",
		(status & 0x01) ? " PortConnected" : "");
}

void show_status(struct uhci *uhci)
{
	unsigned int io_addr = uhci->io_addr;
	unsigned short usbcmd, usbstat, usbint, usbfrnum;
	unsigned int flbaseadd;
	unsigned char sof;
	unsigned short portsc1, portsc2;

	usbcmd    = inw(io_addr + 0);
	usbstat   = inw(io_addr + 2);
	usbint    = inw(io_addr + 4);
	usbfrnum  = inw(io_addr + 6);
	flbaseadd = inl(io_addr + 8);
	sof       = inb(io_addr + 12);
	portsc1   = inw(io_addr + 16);
	portsc2   = inw(io_addr + 18);

	printk("  usbcmd    =     %04x   %s%s%s%s%s%s%s%s\n",
		usbcmd,
		(usbcmd & 0x80) ? " Maxp64" : " Maxp32",
		(usbcmd & 0x40) ? " CF" : "",
		(usbcmd & 0x20) ? " SWDBG" : "",
		(usbcmd & 0x10) ? " FGR" : "",
		(usbcmd & 0x08) ? " EGSM" : "",
		(usbcmd & 0x04) ? " GRESET" : "",
		(usbcmd & 0x02) ? " HCRESET" : "",
		(usbcmd & 0x01) ? " RS" : "");

	printk("  usbstat   =     %04x   %s%s%s%s%s%s\n",
		usbstat,
		(usbstat & 0x20) ? " HCHalted" : "",
		(usbstat & 0x10) ? " HostControllerProcessError" : "",
		(usbstat & 0x08) ? " HostSystemError" : "",
		(usbstat & 0x04) ? " ResumeDetect" : "",
		(usbstat & 0x02) ? " USBError" : "",
		(usbstat & 0x01) ? " USBINT" : "");

	printk("  usbint    =     %04x\n", usbint);
	printk("  usbfrnum  =   (%d)%03x\n", (usbfrnum >> 10) & 1, 0xfff & (4*(unsigned int)usbfrnum));
	printk("  flbaseadd = %08x\n", flbaseadd);
	printk("  sof       =       %02x\n", sof);
	show_sc(1, portsc1);
	show_sc(2, portsc2);
}

#define uhci_link_to_qh(x) ((struct uhci_qh *) uhci_link_to_td(x))

struct uhci_td * uhci_link_to_td(unsigned int link)
{
	if (link & 1)
		return NULL;

	return bus_to_virt(link & ~15);
}

void show_queue(struct uhci_qh *qh)
{
	struct uhci_td *td;
	int i = 0;

#if 0
	printk("    link = %p, element = %p\n", qh->link, qh->element);
#endif
	if(!qh->element) {
		printk("    td 0 = NULL\n");
		return;
	}

	for(td = uhci_link_to_td(qh->element); td; 
	    td = uhci_link_to_td(td->link)) {
		printk("    td %d = %p\n", i++, td);
		printk("    ");
		show_td(td);
	}
}

int is_skeleton_qh(struct uhci *uhci, struct uhci_qh *qh)
{
	int j;

	for (j = 0; j < UHCI_MAXQH; j++)
		if (qh == uhci->root_hub->qh + j)
			return 1;

	return 0;
}

static const char *qh_names[] = {"isochronous", "interrupt2", "interrupt4",
				 "interrupt8", "interrupt16", "interrupt32",
				 "interrupt64", "interrupt128", "interrupt256",
				 "control", "bulk0", "bulk1", "bulk2", "bulk3",
				 "unused", "unused"};

void show_queues(struct uhci *uhci)
{
	int i;
	struct uhci_qh *qh;

	for (i = 0; i < UHCI_MAXQH; ++i) {
		printk("  %s:\n", qh_names[i]);
#if 0
		printk("  qh #%d, %p\n", i, virt_to_bus(uhci->root_hub->qh + i));
		show_queue(uhci->root_hub->qh + i);
#endif

		qh = uhci_link_to_qh(uhci->root_hub->qh[i].link);
		for (; qh; qh = uhci_link_to_qh(qh->link)) {
			if (is_skeleton_qh(uhci, qh))
				break;

			show_queue(qh);
		}
	}
}

