#ifndef __LINUX_UHCI_H
#define __LINUX_UHCI_H

#include <linux/list.h>

#include "usb.h"

/*
 * Universal Host Controller Interface data structures and defines
 */

/* Command register */
#define USBCMD		0
#define   USBCMD_RS		0x0001	/* Run/Stop */
#define   USBCMD_HCRESET	0x0002	/* Host reset */
#define   USBCMD_GRESET		0x0004	/* Global reset */
#define   USBCMD_EGSM		0x0008	/* Global Suspend Mode */
#define   USBCMD_FGR		0x0010	/* Force Global Resume */
#define   USBCMD_SWDBG		0x0020	/* SW Debug mode */
#define   USBCMD_CF		0x0040	/* Config Flag (sw only) */
#define   USBCMD_MAXP		0x0080	/* Max Packet (0 = 32, 1 = 64) */

/* Status register */
#define USBSTS		2
#define   USBSTS_USBINT		0x0001	/* Interrupt due to IOC */
#define   USBSTS_ERROR		0x0002	/* Interrupt due to error */
#define   USBSTS_RD		0x0004	/* Resume Detect */
#define   USBSTS_HSE		0x0008	/* Host System Error - basically PCI problems */
#define   USBSTS_HCPE		0x0010	/* Host Controller Process Error - the scripts were buggy */
#define   USBSTS_HCH		0x0020	/* HC Halted */

/* Interrupt enable register */
#define USBINTR		4
#define   USBINTR_TIMEOUT	0x0001	/* Timeout/CRC error enable */
#define   USBINTR_RESUME	0x0002	/* Resume interrupt enable */
#define   USBINTR_IOC		0x0004	/* Interrupt On Complete enable */
#define   USBINTR_SP		0x0008	/* Short packet interrupt enable */

#define USBFRNUM	6
#define USBFLBASEADD	8
#define USBSOF		12

/* USB port status and control registers */
#define USBPORTSC1	16
#define USBPORTSC2	18
#define   USBPORTSC_CCS		0x0001	/* Current Connect Status ("device present") */
#define   USBPORTSC_CSC		0x0002	/* Connect Status Change */
#define   USBPORTSC_PE		0x0004	/* Port Enable */
#define   USBPORTSC_PEC		0x0008	/* Port Enable Change */
#define   USBPORTSC_LS		0x0030	/* Line Status */
#define   USBPORTSC_RD		0x0040	/* Resume Detect */
#define   USBPORTSC_LSDA	0x0100	/* Low Speed Device Attached */
#define   USBPORTSC_PR		0x0200	/* Port Reset */
#define   USBPORTSC_SUSP	0x1000	/* Suspend */

struct uhci_qh {
	unsigned int link;	/* Next queue */
	unsigned int element;	/* Queue element pointer */
	int inuse;		/* Inuse? */
	struct uhci_qh *skel;	/* Skeleton head */
} __attribute__((aligned(16)));

struct uhci_framelist {
	unsigned int frame[1024];
} __attribute__((aligned(4096)));

/*
 * The documentation says "4 words for hardware, 4 words for software".
 *
 * That's silly, the hardware doesn't care. The hardware only cares that
 * the hardware words are 16-byte aligned, and we can have any amount of
 * sw space after the TD entry as far as I can tell.
 *
 * But let's just go with the documentation, at least for 32-bit machines.
 * On 64-bit machines we probably want to take advantage of the fact that
 * hw doesn't really care about the size of the sw-only area.
 *
 * Alas, not anymore, we have more than 4 words of software, woops
 */
struct uhci_td {
	/* Hardware fields */
	__u32 link;
	__u32 status;
	__u32 info;
	__u32 buffer;

	/* Software fields */
	struct list_head irq_list;	/* Active interrupt list.. */
	usb_device_irq completed;	/* Completion handler routine */
	unsigned int *backptr;		/* Where to remove this from.. */
	void *dev_id;
	int inuse;			/* Inuse? (b0) Remove (b1)*/
	struct uhci_qh *qh;
	struct uhci_td *first;
	struct usb_device *dev;		/* the owning device */
} __attribute__((aligned(32)));

struct uhci_iso_td {
	int num;
	char *data;
	int maxsze;

	struct uhci_td *td;

	int frame;
	int endframe;
};

/*
 * Note the alignment requirements of the entries
 *
 * Each UHCI device has pre-allocated QH and TD entries.
 * You can use more than the pre-allocated ones, but I
 * don't see you usually needing to.
 */
struct uhci;

#define UHCI_MAXTD	64

#define UHCI_MAXQH	16

/* The usb device part must be first! */
struct uhci_device {
	struct usb_device	*usb;

	struct uhci		*uhci;
	struct uhci_qh		qh[UHCI_MAXQH];		/* These are the "common" qh's for each device */
	struct uhci_td		td[UHCI_MAXTD];

	unsigned long		data[16];
};

#define uhci_to_usb(uhci)	((uhci)->usb)
#define usb_to_uhci(usb)	((struct uhci_device *)(usb)->hcpriv)

/*
 * The root hub pre-allocated QH's and TD's have
 * some special global uses..
 */
#if 0
#define control_td	td		/* Td's 0-30 */
/* This is only for the root hub's TD list */
#define tick_td		td[31]
#endif

/*
 * There are various standard queues. We set up several different
 * queues for each of the three basic queue types: interrupt,
 * control, and bulk.
 *
 *  - There are various different interrupt latencies: ranging from
 *    every other USB frame (2 ms apart) to every 256 USB frames (ie
 *    256 ms apart). Make your choice according to how obnoxious you
 *    want to be on the wire, vs how critical latency is for you.
 *  - The control list is done every frame.
 *  - There are 4 bulk lists, so that up to four devices can have a
 *    bulk list of their own and when run concurrently all four lists
 *    will be be serviced.
 *
 * This is a bit misleading, there are various interrupt latencies, but they
 * vary a bit, interrupt2 isn't exactly 2ms, it can vary up to 4ms since the
 * other queues can "override" it. interrupt4 can vary up to 8ms, etc. Minor
 * problem
 *
 * In the case of the root hub, these QH's are just head's of qh's. Don't
 * be scared, it kinda makes sense. Look at this wonderful picture care of
 * Linus:
 *
 *  generic-iso-QH  ->  dev1-iso-QH  ->  generic-irq-QH  ->  dev1-irq-QH  -> ...
 *       |                       |                  |                   |
 *      End          dev1-iso-TD1          End            dev1-irq-TD1
 *                       |
 *                   dev1-iso-TD2
 *                       |
 *                      ....
 *
 * This may vary a bit (the UHCI docs don't explicitly say you can put iso
 * transfers in QH's and all of their pictures don't have that either) but
 * other than that, that is what we're doing now
 *
 * To keep with Linus' nomenclature, this is called the qh skeleton. These
 * labels (below) are only signficant to the root hub's qh's
 */
#define skel_iso_qh		qh[0]

#define skel_int2_qh		qh[1]
#define skel_int4_qh		qh[2]
#define skel_int8_qh		qh[3]
#define skel_int16_qh		qh[4]
#define skel_int32_qh		qh[5]
#define skel_int64_qh		qh[6]
#define skel_int128_qh		qh[7]
#define skel_int256_qh		qh[8]

#define skel_control_qh		qh[9]

#define skel_bulk0_qh		qh[10]
#define skel_bulk1_qh		qh[11]
#define skel_bulk2_qh		qh[12]
#define skel_bulk3_qh		qh[13]

/*
 * These are significant to the devices allocation of QH's
 */
#if 0
#define iso_qh			qh[0]
#define int_qh			qh[1]	/* We have 2 "common" interrupt QH's */
#define control_qh		qh[3]
#define bulk_qh			qh[4]	/* We have 4 "common" bulk QH's */
#define extra_qh		qh[8]	/* The rest, anything goes */
#endif

/*
 * This describes the full uhci information.
 *
 * Note how the "proper" USB information is just
 * a subset of what the full implementation needs.
 */
struct uhci {
	int irq;
	unsigned int io_addr;

	struct usb_bus *bus;

#if 0
	/* These are "standard" QH's for the entire bus */
	struct uhci_qh qh[UHCI_MAXQH];
#endif
	struct uhci_framelist *fl;		/* Frame list */
	struct list_head interrupt_list;	/* List of interrupt-active TD's for this uhci */
};

/* needed for the debugging code */
struct uhci_td *uhci_link_to_td(unsigned int element);

/* Debugging code */
void show_td(struct uhci_td * td);
void show_status(struct uhci *uhci);
void show_queues(struct uhci *uhci);

int uhci_compress_isochronous(struct usb_device *usb_dev, void *_isodesc);
int uhci_unsched_isochronous(struct usb_device *usb_dev, void *_isodesc);
int uhci_sched_isochronous(struct usb_device *usb_dev, void *_isodesc, void *_pisodesc);
void *uhci_alloc_isochronous(struct usb_device *usb_dev, unsigned int pipe, void *data, int len, int maxsze, usb_device_irq completed, void *dev_id);
void uhci_delete_isochronous(struct usb_device *dev, void *_isodesc);

#endif

