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

/* Legacy support register */
#define USBLEGSUP 0xc0
#define USBLEGSUP_DEFAULT 0x2000 /* only PIRQ enable set */

#define UHCI_NULL_DATA_SIZE	0x7ff	/* for UHCI controller TD */

#define UHCI_PTR_BITS		0x000F
#define UHCI_PTR_TERM		0x0001
#define UHCI_PTR_QH		0x0002
#define UHCI_PTR_DEPTH		0x0004

#define UHCI_NUMFRAMES		1024	/* in the frame list [array] */
#define UHCI_MAX_SOF_NUMBER	2047	/* in an SOF packet */
#define CAN_SCHEDULE_FRAMES	1000	/* how far future frames can be scheduled */

struct uhci_td;

struct uhci_qh {
	/* Hardware fields */
	__u32 link;				/* Next queue */
	__u32 element;				/* Queue element pointer */

	/* Software fields */
	atomic_t refcnt;			/* Reference counting */
	struct uhci_device *dev;		/* The owning device */
	struct uhci_qh *skel;			/* Skeleton head */

	struct uhci_td *first;			/* First TD in the chain */

	wait_queue_head_t wakeup;
} __attribute__((aligned(16)));

struct uhci_framelist {
	__u32 frame[UHCI_NUMFRAMES];
} __attribute__((aligned(4096)));

/*
 * for TD <status>:
 */
#define TD_CTRL_SPD		(1 << 29)	/* Short Packet Detect */
#define TD_CTRL_C_ERR_MASK	(3 << 27)	/* Error Counter bits */
#define TD_CTRL_LS		(1 << 26)	/* Low Speed Device */
#define TD_CTRL_IOS		(1 << 25)	/* Isochronous Select */
#define TD_CTRL_IOC		(1 << 24)	/* Interrupt on Complete */
#define TD_CTRL_ACTIVE		(1 << 23)	/* TD Active */
#define TD_CTRL_STALLED		(1 << 22)	/* TD Stalled */
#define TD_CTRL_DBUFERR		(1 << 21)	/* Data Buffer Error */
#define TD_CTRL_BABBLE		(1 << 20)	/* Babble Detected */
#define TD_CTRL_NAK		(1 << 19)	/* NAK Received */
#define TD_CTRL_CRCTIMEO	(1 << 18)	/* CRC/Time Out Error */
#define TD_CTRL_BITSTUFF	(1 << 17)	/* Bit Stuff Error */
#define TD_CTRL_ACTLEN_MASK	0x7ff		/* actual length, encoded as n - 1 */

#define TD_CTRL_ANY_ERROR	(TD_CTRL_STALLED | TD_CTRL_DBUFERR | \
				 TD_CTRL_BABBLE | TD_CTRL_CRCTIME | TD_CTRL_BITSTUFF)

#define uhci_status_bits(ctrl_sts)	(ctrl_sts & 0xFE0000)
#define uhci_actual_length(ctrl_sts)	((ctrl_sts + 1) & TD_CTRL_ACTLEN_MASK) /* 1-based */

#define uhci_ptr_to_virt(x)	bus_to_virt(x & ~UHCI_PTR_BITS)

/*
 * for TD <flags>:
 */
#define UHCI_TD_REMOVE		0x0001		/* Remove when done */

/*
 * for TD <info>: (a.k.a. Token)
 */
#define TD_TOKEN_TOGGLE		19

#define uhci_maxlen(token)	((token) >> 21)
#define uhci_toggle(token)	(((token) >> TD_TOKEN_TOGGLE) & 1)
#define uhci_endpoint(token)	(((token) >> 15) & 0xf)
#define uhci_devaddr(token)	(((token) >> 8) & 0x7f)
#define uhci_devep(token)	(((token) >> 8) & 0x7ff)
#define uhci_packetid(token)	((token) & 0xff)
#define uhci_packetout(token)	(uhci_packetid(token) != USB_PID_IN)
#define uhci_packetin(token)	(uhci_packetid(token) == USB_PID_IN)


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
 * Alas, not anymore, we have more than 4 words for software, woops
 */
struct uhci_td {
	/* Hardware fields */
	__u32 link;
	__u32 status;
	__u32 info;
	__u32 buffer;

	/* Software fields */
	unsigned int *backptr;		/* Where to remove this from.. */
	struct list_head irq_list;	/* Active interrupt list.. */

	usb_device_irq completed;	/* Completion handler routine */
	void *dev_id;

	atomic_t refcnt;		/* Reference counting */
	struct uhci_device *dev;	/* The owning device */
	struct uhci_qh *qh;		/* QH this TD is a part of (ignored for Isochronous) */
	int flags;			/* Remove, etc */
	int isoc_td_number;		/* 0-relative number within a usb_isoc_desc. */
} __attribute__((aligned(16)));


/*
 * Note the alignment requirements of the entries
 *
 * Each UHCI device has pre-allocated QH and TD entries.
 * You can use more than the pre-allocated ones, but I
 * don't see you usually needing to.
 */
struct uhci;

#if 0
#define UHCI_MAXTD	64

#define UHCI_MAXQH	16
#endif

/* The usb device part must be first! Not anymore -jerdfelt */
struct uhci_device {
	struct usb_device	*usb;

	atomic_t		refcnt;

	struct uhci		*uhci;
#if 0
	struct uhci_qh		qh[UHCI_MAXQH];		/* These are the "common" qh's for each device */
	struct uhci_td		td[UHCI_MAXTD];
#endif

	unsigned long		data[16];
};

#define uhci_to_usb(uhci)	((uhci)->usb)
#define usb_to_uhci(usb)	((struct uhci_device *)(usb)->hcpriv)

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
 *  generic-  ->  dev1-  ->  generic-  ->  dev1-  ->  control-  ->  bulk- -> ...
 *   iso-QH      iso-QH       irq-QH      irq-QH        QH           QH
 *      |           |            |           |           |            |
 *     End     dev1-iso-TD1     End     dev1-irq-TD1    ...          ... 
 *                  |
 *             dev1-iso-TD2
 *                  |
 *                ....
 *
 * This may vary a bit (the UHCI docs don't explicitly say you can put iso
 * transfers in QH's and all of their pictures don't have that either) but
 * other than that, that is what we're doing now
 *
 * And now we don't put Iso transfers in QH's, so we don't waste one on it
 *
 * To keep with Linus' nomenclature, this is called the QH skeleton. These
 * labels (below) are only signficant to the root hub's QH's
 */
#define UHCI_NUM_SKELQH		11

#define skel_int1_qh		skelqh[0]
#define skel_int2_qh		skelqh[1]
#define skel_int4_qh		skelqh[2]
#define skel_int8_qh		skelqh[3]
#define skel_int16_qh		skelqh[4]
#define skel_int32_qh		skelqh[5]
#define skel_int64_qh		skelqh[6]
#define skel_int128_qh		skelqh[7]
#define skel_int256_qh		skelqh[8]

#define skel_control_qh		skelqh[9]

#define skel_bulk_qh		skelqh[10]

/*
 * Search tree for determining where <interval> fits in the
 * skelqh[] skeleton.
 *
 * An interrupt request should be placed into the slowest skelqh[]
 * which meets the interval/period/frequency requirement.
 * An interrupt request is allowed to be faster than <interval> but not slower.
 *
 * For a given <interval>, this function returns the appropriate/matching
 * skelqh[] index value.
 *
 * NOTE: For UHCI, we don't really need int256_qh since the maximum interval
 * is 255 ms.  However, we do need an int1_qh since 1 is a valid interval
 * and we should meet that frequency when requested to do so.
 * This will require some change(s) to the UHCI skeleton.
 */
static inline int __interval_to_skel(interval)
{
	if (interval < 16) {
		if (interval < 4) {
			if (interval < 2) {
				return 0;	/* int1 for 0-1 ms */
			}
			return 1;		/* int2 for 2-3 ms */
		}
		if (interval < 8) {
			return 2;		/* int4 for 4-7 ms */
		}
		return 3;			/* int 8 for 8-15 ms */
	}
	if (interval < 64) {
		if (interval < 32) {
			return 4;		/* int16 for 16-31 ms */
		}
		return 5;			/* int32 for 32-63 ms */
	}
	if (interval < 128)
		return 6;			/* int64 for 64-127 ms */
	return 7;				/* int128 for 128-255 ms (Max.) */
}

/*
 * This describes the full uhci information.
 *
 * Note how the "proper" USB information is just
 * a subset of what the full implementation needs.
 */
struct uhci {
	int irq;
	unsigned int io_addr;
	unsigned int io_size;

	int control_pid;
	int control_running;
	int control_continue;

	struct list_head uhci_list;

	struct usb_bus *bus;

	struct uhci_qh skelqh[UHCI_NUM_SKELQH];	/* Skeleton QH's */

	struct uhci_framelist *fl;		/* Frame list */
	struct list_head interrupt_list;	/* List of interrupt-active TD's for this uhci */

	struct uhci_td *ticktd;
};

/* needed for the debugging code */
struct uhci_td *uhci_link_to_td(unsigned int element);

/* Debugging code */
void uhci_show_td(struct uhci_td *td);
void uhci_show_status(struct uhci *uhci);
void uhci_show_queue(struct uhci_qh *qh);
void uhci_show_queues(struct uhci *uhci);

#endif

