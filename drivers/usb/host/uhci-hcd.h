#ifndef __LINUX_UHCI_HCD_H
#define __LINUX_UHCI_HCD_H

#include <linux/list.h>
#include <linux/usb.h>

#define usb_packetid(pipe)	(usb_pipein(pipe) ? USB_PID_IN : USB_PID_OUT)
#define PIPE_DEVEP_MASK		0x0007ff00

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
#define   USBPORTSC_OC		0x0400	/* Over Current condition */
#define   USBPORTSC_SUSP	0x1000	/* Suspend */

/* Legacy support register */
#define USBLEGSUP		0xc0
#define   USBLEGSUP_DEFAULT	0x2000	/* only PIRQ enable set */

#define UHCI_NULL_DATA_SIZE	0x7FF	/* for UHCI controller TD */

#define UHCI_PTR_BITS		cpu_to_le32(0x000F)
#define UHCI_PTR_TERM		cpu_to_le32(0x0001)
#define UHCI_PTR_QH		cpu_to_le32(0x0002)
#define UHCI_PTR_DEPTH		cpu_to_le32(0x0004)
#define UHCI_PTR_BREADTH	cpu_to_le32(0x0000)

#define UHCI_NUMFRAMES		1024	/* in the frame list [array] */
#define UHCI_MAX_SOF_NUMBER	2047	/* in an SOF packet */
#define CAN_SCHEDULE_FRAMES	1000	/* how far future frames can be scheduled */

struct uhci_frame_list {
	__u32 frame[UHCI_NUMFRAMES];

	void *frame_cpu[UHCI_NUMFRAMES];

	dma_addr_t dma_handle;
};

struct urb_priv;

/*
 * One role of a QH is to hold a queue of TDs for some endpoint.  Each QH is
 * used with one URB, and qh->element (updated by the HC) is either:
 *   - the next unprocessed TD for the URB, or
 *   - UHCI_PTR_TERM (when there's no more traffic for this endpoint), or
 *   - the QH for the next URB queued to the same endpoint.
 *
 * The other role of a QH is to serve as a "skeleton" framelist entry, so we
 * can easily splice a QH for some endpoint into the schedule at the right
 * place.  Then qh->element is UHCI_PTR_TERM.
 *
 * In the frame list, qh->link maintains a list of QHs seen by the HC:
 *     skel1 --> ep1-qh --> ep2-qh --> ... --> skel2 --> ...
 */
struct uhci_qh {
	/* Hardware fields */
	__u32 link;			/* Next queue */
	__u32 element;			/* Queue element pointer */

	/* Software fields */
	dma_addr_t dma_handle;

	struct usb_device *dev;
	struct urb_priv *urbp;

	struct list_head list;		/* P: uhci->frame_list_lock */
	struct list_head remove_list;	/* P: uhci->remove_list_lock */
} __attribute__((aligned(16)));

/*
 * for TD <status>:
 */
#define td_status(td)		le32_to_cpu((td)->status)
#define TD_CTRL_SPD		(1 << 29)	/* Short Packet Detect */
#define TD_CTRL_C_ERR_MASK	(3 << 27)	/* Error Counter bits */
#define TD_CTRL_C_ERR_SHIFT	27
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
#define TD_CTRL_ACTLEN_MASK	0x7FF	/* actual length, encoded as n - 1 */

#define TD_CTRL_ANY_ERROR	(TD_CTRL_STALLED | TD_CTRL_DBUFERR | \
				 TD_CTRL_BABBLE | TD_CTRL_CRCTIME | TD_CTRL_BITSTUFF)

#define uhci_maxerr(err)		((err) << TD_CTRL_C_ERR_SHIFT)
#define uhci_status_bits(ctrl_sts)	((ctrl_sts) & 0xFE0000)
#define uhci_actual_length(ctrl_sts)	(((ctrl_sts) + 1) & TD_CTRL_ACTLEN_MASK) /* 1-based */

/*
 * for TD <info>: (a.k.a. Token)
 */
#define td_token(td)		le32_to_cpu((td)->token)
#define TD_TOKEN_DEVADDR_SHIFT	8
#define TD_TOKEN_TOGGLE_SHIFT	19
#define TD_TOKEN_TOGGLE		(1 << 19)
#define TD_TOKEN_EXPLEN_SHIFT	21
#define TD_TOKEN_EXPLEN_MASK	0x7FF		/* expected length, encoded as n - 1 */
#define TD_TOKEN_PID_MASK	0xFF

#define uhci_explen(len)	((len) << TD_TOKEN_EXPLEN_SHIFT)

#define uhci_expected_length(token) ((((token) >> 21) + 1) & TD_TOKEN_EXPLEN_MASK)
#define uhci_toggle(token)	(((token) >> TD_TOKEN_TOGGLE_SHIFT) & 1)
#define uhci_endpoint(token)	(((token) >> 15) & 0xf)
#define uhci_devaddr(token)	(((token) >> TD_TOKEN_DEVADDR_SHIFT) & 0x7f)
#define uhci_devep(token)	(((token) >> TD_TOKEN_DEVADDR_SHIFT) & 0x7ff)
#define uhci_packetid(token)	((token) & TD_TOKEN_PID_MASK)
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
 * Alas, not anymore, we have more than 4 words for software, woops.
 * Everything still works tho, surprise! -jerdfelt
 *
 * td->link points to either another TD (not necessarily for the same urb or
 * even the same endpoint), or nothing (PTR_TERM), or a QH (for queued urbs)
 */
struct uhci_td {
	/* Hardware fields */
	__u32 link;
	__u32 status;
	__u32 token;
	__u32 buffer;

	/* Software fields */
	dma_addr_t dma_handle;

	struct usb_device *dev;
	struct urb *urb;

	struct list_head list;		/* P: urb->lock */
	struct list_head remove_list;	/* P: uhci->td_remove_list_lock */

	int frame;			/* for iso: what frame? */
	struct list_head fl_list;	/* P: uhci->frame_list_lock */
} __attribute__((aligned(16)));

/*
 * The UHCI driver places Interrupt, Control and Bulk into QH's both
 * to group together TD's for one transfer, and also to faciliate queuing
 * of URB's. To make it easy to insert entries into the schedule, we have
 * a skeleton of QH's for each predefined Interrupt latency, low speed
 * control, high speed control and terminating QH (see explanation for
 * the terminating QH below).
 *
 * When we want to add a new QH, we add it to the end of the list for the
 * skeleton QH.
 *
 * For instance, the queue can look like this:
 *
 * skel int128 QH
 * dev 1 interrupt QH
 * dev 5 interrupt QH
 * skel int64 QH
 * skel int32 QH
 * ...
 * skel int1 QH
 * skel low speed control QH
 * dev 5 control QH
 * skel high speed control QH
 * skel bulk QH
 * dev 1 bulk QH
 * dev 2 bulk QH
 * skel terminating QH
 *
 * The terminating QH is used for 2 reasons:
 * - To place a terminating TD which is used to workaround a PIIX bug
 *   (see Intel errata for explanation)
 * - To loop back to the high speed control queue for full speed bandwidth
 *   reclamation
 *
 * Isochronous transfers are stored before the start of the skeleton
 * schedule and don't use QH's. While the UHCI spec doesn't forbid the
 * use of QH's for Isochronous, it doesn't use them either. Since we don't
 * need to use them either, we follow the spec diagrams in hope that it'll
 * be more compatible with future UHCI implementations.
 */

#define UHCI_NUM_SKELQH		12
#define skel_int128_qh		skelqh[0]
#define skel_int64_qh		skelqh[1]
#define skel_int32_qh		skelqh[2]
#define skel_int16_qh		skelqh[3]
#define skel_int8_qh		skelqh[4]
#define skel_int4_qh		skelqh[5]
#define skel_int2_qh		skelqh[6]
#define skel_int1_qh		skelqh[7]
#define skel_ls_control_qh	skelqh[8]
#define skel_hs_control_qh	skelqh[9]
#define skel_bulk_qh		skelqh[10]
#define skel_term_qh		skelqh[11]

/*
 * Search tree for determining where <interval> fits in the skelqh[]
 * skeleton.
 *
 * An interrupt request should be placed into the slowest skelqh[]
 * which meets the interval/period/frequency requirement.
 * An interrupt request is allowed to be faster than <interval> but not slower.
 *
 * For a given <interval>, this function returns the appropriate/matching
 * skelqh[] index value.
 */
static inline int __interval_to_skel(int interval)
{
	if (interval < 16) {
		if (interval < 4) {
			if (interval < 2)
				return 7;	/* int1 for 0-1 ms */
			return 6;		/* int2 for 2-3 ms */
		}
		if (interval < 8)
			return 5;		/* int4 for 4-7 ms */
		return 4;			/* int8 for 8-15 ms */
	}
	if (interval < 64) {
		if (interval < 32)
			return 3;		/* int16 for 16-31 ms */
		return 2;			/* int32 for 32-63 ms */
	}
	if (interval < 128)
		return 1;			/* int64 for 64-127 ms */
	return 0;				/* int128 for 128-255 ms (Max.) */
}

/*
 * Device states for the host controller.
 *
 * To prevent "bouncing" in the presence of electrical noise,
 * we insist on a 1-second "grace" period, before switching to
 * the RUNNING or SUSPENDED states, during which the state is
 * not allowed to change.
 *
 * The resume process is divided into substates in order to avoid
 * potentially length delays during the timer handler.
 *
 * States in which the host controller is halted must have values <= 0.
 */
enum uhci_state {
	UHCI_RESET,
	UHCI_RUNNING_GRACE,		/* Before RUNNING */
	UHCI_RUNNING,			/* The normal state */
	UHCI_SUSPENDING_GRACE,		/* Before SUSPENDED */
	UHCI_SUSPENDED = -10,		/* When no devices are attached */
	UHCI_RESUMING_1,
	UHCI_RESUMING_2
};

#define hcd_to_uhci(hcd_ptr) container_of(hcd_ptr, struct uhci_hcd, hcd)

/*
 * This describes the full uhci information.
 *
 * Note how the "proper" USB information is just
 * a subset of what the full implementation needs.
 */
struct uhci_hcd {
	struct usb_hcd hcd;

#ifdef CONFIG_PROC_FS
	/* procfs */
	struct proc_dir_entry *proc_entry;
#endif

	/* Grabbed from PCI */
	unsigned long io_addr;

	struct pci_pool *qh_pool;
	struct pci_pool *td_pool;

	struct usb_bus *bus;

	struct uhci_td *term_td;	/* Terminating TD, see UHCI bug */
	struct uhci_qh *skelqh[UHCI_NUM_SKELQH];	/* Skeleton QH's */

	spinlock_t frame_list_lock;
	struct uhci_frame_list *fl;		/* P: uhci->frame_list_lock */
	int fsbr;				/* Full speed bandwidth reclamation */
	unsigned long fsbrtimeout;		/* FSBR delay */

	enum uhci_state state;			/* FIXME: needs a spinlock */
	unsigned long state_end;		/* Time of next transition */
	int resume_detect;			/* Need a Global Resume */

	/* Main list of URB's currently controlled by this HC */
	spinlock_t urb_list_lock;
	struct list_head urb_list;		/* P: uhci->urb_list_lock */

	/* List of QH's that are done, but waiting to be unlinked (race) */
	spinlock_t qh_remove_list_lock;
	struct list_head qh_remove_list;	/* P: uhci->qh_remove_list_lock */

	/* List of TD's that are done, but waiting to be freed (race) */
	spinlock_t td_remove_list_lock;
	struct list_head td_remove_list;	/* P: uhci->td_remove_list_lock */

	/* List of asynchronously unlinked URB's */
	spinlock_t urb_remove_list_lock;
	struct list_head urb_remove_list;	/* P: uhci->urb_remove_list_lock */

	/* List of URB's awaiting completion callback */
	spinlock_t complete_list_lock;
	struct list_head complete_list;		/* P: uhci->complete_list_lock */

	int rh_numports;

	struct timer_list stall_timer;
};

struct urb_priv {
	struct list_head urb_list;

	struct urb *urb;
	struct usb_device *dev;

	struct uhci_qh *qh;		/* QH for this URB */
	struct list_head td_list;	/* P: urb->lock */

	int fsbr : 1;			/* URB turned on FSBR */
	int fsbr_timeout : 1;		/* URB timed out on FSBR */
	int queued : 1;			/* QH was queued (not linked in) */
	int short_control_packet : 1;	/* If we get a short packet during */
					/*  a control transfer, retrigger */
					/*  the status phase */

	int status;			/* Final status */

	unsigned long inserttime;	/* In jiffies */
	unsigned long fsbrtime;		/* In jiffies */

	struct list_head queue_list;	/* P: uhci->frame_list_lock */
	struct list_head complete_list;	/* P: uhci->complete_list_lock */
};

/*
 * Locking in uhci.c
 *
 * spinlocks are used extensively to protect the many lists and data
 * structures we have. It's not that pretty, but it's necessary. We
 * need to be done with all of the locks (except complete_list_lock) when
 * we call urb->complete. I've tried to make it simple enough so I don't
 * have to spend hours racking my brain trying to figure out if the
 * locking is safe.
 *
 * Here's the safe locking order to prevent deadlocks:
 *
 * #1 uhci->urb_list_lock
 * #2 urb->lock
 * #3 uhci->urb_remove_list_lock, uhci->frame_list_lock, 
 *   uhci->qh_remove_list_lock
 * #4 uhci->complete_list_lock
 *
 * If you're going to grab 2 or more locks at once, ALWAYS grab the lock
 * at the lowest level FIRST and NEVER grab locks at the same level at the
 * same time.
 * 
 * So, if you need uhci->urb_list_lock, grab it before you grab urb->lock
 */

#endif

