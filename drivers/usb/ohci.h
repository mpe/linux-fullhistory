#ifndef __LINUX_OHCI_H
#define __LINUX_OHCI_H

/*
 * Open Host Controller Interface data structures and defines.
 *
 * (C) Copyright 1999 Gregory P. Smith <greg@electricrain.com>
 *
 * $Id: ohci.h,v 1.24 1999/05/16 10:18:26 greg Exp $
 */

#include <linux/list.h>
#include <asm/io.h>

#include "usb.h"

struct ohci_ed;

/*
 * Each TD must be aligned on a 16-byte boundary.  From the OHCI v1.0 spec
 * it does not state that TDs must be contiguious in memory (due to the
 * use of the next_td field).  This gives us extra room at the end of a
 * TD for our own driver specific data.
 *
 * This structure's size must be a multiple of 16 bytes. ?? no way, I
 * don't see why.  Alignment should be all that matters.
 */
struct ohci_td {
    	/* OHCI Hardware fields */
    	__u32 info;		/* TD status & type flags */
	__u32 cur_buf;		/* Current Buffer Pointer (bus address) */
	__u32 next_td;		/* Next TD Pointer (bus address) */
	__u32 buf_end;		/* Memory Buffer End Pointer (bus address) */

	/* Driver specific fields */
	struct ohci_ed *ed;		/* address of the ED this TD is on */
	struct ohci_td *next_dl_td;	/* used during donelist processing */
	void *data;			/* virt. address of the the buffer */
	usb_device_irq completed;	/* Completion handler routine */
	int hcd_flags;			/* Flags for the HCD: */
		/* bit0 = boolean: Is this TD allocated? */
		/* bit1 = boolean: Is this a dummy (end of list) TD? */

	/* User or Device class driver specific fields */
	void *dev_id;	/* user defined pointer passed to irq handler */
} __attribute((aligned(16)));

#define OHCI_TD_ROUND	(1 << 18)	/* buffer rounding bit */
#define OHCI_TD_D	(3 << 19)	/* direction of xfer: */
#define OHCI_TD_D_IN	(2 << 19)
#define OHCI_TD_D_OUT	(1 << 19)
#define OHCI_TD_D_SETUP (0 << 19)
#define td_set_dir_in(d)	((d) ? OHCI_TD_D_IN : OHCI_TD_D_OUT )
#define td_set_dir_out(d)	((d) ? OHCI_TD_D_OUT : OHCI_TD_D_IN )
#define OHCI_TD_IOC_DELAY (7 << 21)	/* frame delay allowed before int. */
#define OHCI_TD_IOC_OFF	(OHCI_TD_IOC_DELAY)	/* no interrupt on complete */
#define OHCI_TD_DT	(3 << 24)	/* data toggle bits */
#define TOGGLE_AUTO	(0 << 24)	/* automatic (from the ED) */
#define TOGGLE_DATA0	(2 << 24)	/* force Data0 */
#define TOGGLE_DATA1	(3 << 24)	/* force Data1 */
#define td_force_toggle(b)	(((b) | 2) << 24)
#define OHCI_TD_ERRCNT	(3 << 26)	/* error count */
#define td_errorcount(td)	(((td).info >> 26) & 3)
#define clear_td_errorcount(td)	((td)->info &= ~(__u32)OHCI_TD_ERRCNT)
#define OHCI_TD_CC	(0xf << 28)	/* condition code */
#define OHCI_TD_CC_GET(td_i) (((td_i) >> 28) & 0xf)
#define OHCI_TD_CC_NEW	(OHCI_TD_CC)	/* set this on all unaccessed TDs! */
#define td_cc_notaccessed(td)	(((td).info >> 29) == 7)
#define td_cc_accessed(td)	(((td).info >> 29) != 7)
#define td_cc_noerror(td)	((((td).info) & OHCI_TD_CC) == 0)
#define td_active(td)	(!td_cc_noerror((td)) && (td_errorcount((td)) < 3))
#define td_done(td)	(td_cc_noerror((td)) || (td_errorcount((td)) == 3))

/*
 * Macros to use the td->hcd_flags field.
 */
#define td_allocated(td)	((td).hcd_flags & 1)
#define allocate_td(td)		((td)->hcd_flags |= 1)
#define ohci_free_td(td)	((td)->hcd_flags &= ~(__u32)1)

#define td_dummy(td)		((td).hcd_flags & 2)
#define make_dumb_td(td)	((td)->hcd_flags |= 2)
#define clear_dumb_td(td)	((td)->hcd_flags &= ~(__u32)2)


/*
 * The endpoint descriptors also requires 16-byte alignment
 */
struct ohci_ed {
	/* OHCI hardware fields */
	__u32 status;
	__u32 tail_td;	/* TD Queue tail pointer */
	__u32 _head_td;	/* TD Queue head pointer, toggle carry & halted bits */
	__u32 next_ed;	/* Next ED */
} __attribute((aligned(16)));

/* get the head_td */
#define ed_head_td(ed)	((ed)->_head_td & 0xfffffff0)

/* save the carry & halted flag while setting the head_td */
#define set_ed_head_td(ed, td)	((ed)->_head_td = (td) | ((ed)->_head_td & 3))

/* Control the ED's halted flag */
#define ohci_halt_ed(ed)	((ed)->_head_td |= 1)
#define ohci_unhalt_ed(ed)	((ed)->_head_td &= ~(__u32)1)
#define ohci_ed_halted(ed)	((ed)->_head_td & 1)

#define OHCI_ED_SKIP	(1 << 14)
#define OHCI_ED_MPS	(0x7ff << 16)
/* FIXME: should cap at the USB max packet size [0x4ff] */
#define ed_set_maxpacket(s)	(((s) << 16) & OHCI_ED_MPS)
#define OHCI_ED_F_NORM	(0)
#define OHCI_ED_F_ISOC	(1 << 15)
#define ed_set_type_isoc(i)	((i) ? OHCI_ED_F_ISOC : OHCI_ED_F_NORM)
#define OHCI_ED_S_LOW	(1 << 13)
#define OHCI_ED_S_HIGH	(0)
#define ed_set_speed(s)		((s) ? OHCI_ED_S_LOW : OHCI_ED_S_HIGH)
#define OHCI_ED_D	(3 << 11)
#define OHCI_ED_D_IN	(2 << 11)
#define OHCI_ED_D_OUT	(1 << 11)
#define ed_set_dir_in(d)	((d) ? OHCI_ED_D_IN : OHCI_ED_D_OUT)
#define ed_set_dir_out(d)	((d) ? OHCI_ED_D_OUT : OHCI_ED_D_IN)
#define OHCI_ED_EN	(0xf << 7)
#define OHCI_ED_FA	(0x7f)


/* NOTE: bits 27-31 of the status dword are reserved for the HCD */
/*
 * We'll use this status flag for to mark if an ED is in use by the
 * driver or not.  If the bit is set, it is being used.
 */
#define ED_ALLOCATED	(1 << 31)
#define ed_allocated(ed)	((ed).status & ED_ALLOCATED)
#define allocate_ed(ed)		((ed)->status |= ED_ALLOCATED)

/*
 * The HCCA (Host Controller Communications Area) is a 256 byte
 * structure defined in the OHCI spec. that the host controller is
 * told the base address of.  It must be 256-byte aligned.
 */
#define NUM_INTS 32	/* part of the OHCI standard */
struct ohci_hcca {
    	__u32	int_table[NUM_INTS];	/* Interrupt ED table */
	__u16	frame_no;		/* current frame number */
	__u16	pad1;			/* set to 0 on each frame_no change */
	__u32	donehead;		/* info returned for an interrupt */
	u8	reserved_for_hc[116];
} __attribute((aligned(256)));

/*
 * The TD entries here are pre-allocated as Linus did with his simple
 * UHCI implementation.  With the current state of this driver that
 * shouldn't be a problem.  However if someone ever connects 127
 * supported devices to this driver and tries to use them all at once:
 * 	a)  They're insane!
 * 	b)  They should code in dynamic allocation
 */
struct ohci;

/*
 *  Warning: These constants must not be so large as to cause the
 *  ohci_device structure to exceed one 4096 byte page.  Or "weird
 *  things will happen" as the alloc_ohci() function assumes that
 *  its less than one page.  (FIXME)
 */
#define NUM_TDS	32		/* num of preallocated transfer descriptors */
#define DATA_BUF_LEN 16		/* num of unsigned long's for the data buf */

/*
 * For this "simple" driver we only support a single ED for each
 * polling rate.
 *
 * Later on this driver should be extended to use a full tree of EDs
 * so that there can be 32 different 32ms polling frames, etc.
 * Individual devices shouldn't need as many as the root hub in that
 * case; complicating how things are done at the moment.
 *
 * Bulk and Control transfers hang off of their own ED lists.
 */
#define NUM_EDS 16		/* num of preallocated endpoint descriptors */

#define ohci_to_usb(ohci)	((ohci)->usb)
#define usb_to_ohci(usb)	((struct ohci_device *)(usb)->hcpriv)

/* The usb_device must be first! */
struct ohci_device {
    	struct usb_device	*usb;

	struct ohci		*ohci;
	struct ohci_hcca 	*hcca;		/* OHCI mem. mapped IO area */

	struct ohci_ed		ed[NUM_EDS];	/* Endpoint Descriptors */
	struct ohci_td		td[NUM_TDS];	/* Transfer Descriptors */

	unsigned long		data[DATA_BUF_LEN];
};

/* .... */

/*
 * These are the index of the placeholder EDs for the root hub to
 * build the interrupt transfer ED tree out of.
 */
#define ED_INT_1	0
#define ED_INT_2	1
#define ED_INT_4	2
#define ED_INT_8	3
#define ED_INT_16	4
#define ED_INT_32	5
#define ED_ISO		ED_INT_1	/* same as 1ms interrupt queue */

/*
 * Given a period p in ms, convert it to the closest endpoint
 * interrupt frequency; rounding down.  This is a gross macro.
 * Feel free to toss it for actual code. (gasp!)
 */
#define ms_to_ed_int(p) \
	((p >= 32) ? ED_INT_32 : \
	 ((p & 16) ? ED_INT_16 : \
	  ((p & 8) ? ED_INT_8 : \
	   ((p & 4) ? ED_INT_4 : \
	    ((p & 2) ? ED_INT_2 : \
	     ED_INT_1)))))  /* hmm.. scheme or lisp anyone? */

/*
 * This is the maximum number of root hub ports.  I don't think we'll
 * ever see more than two as that's the space available on an ATX
 * motherboard's case, but it could happen.  The OHCI spec allows for
 * up to 15... (which is insane given that they each need to supply up
 * to 500ma; that would be 7.5 amps!).  I have seen a PCI card with 4
 * downstream ports on it.
 * 
 * Although I suppose several "ports" could be connected directly to
 * internal laptop devices such as a keyboard, mouse, camera and
 * serial/parallel ports.  hmm...  That'd be neat.
 */
#define MAX_ROOT_PORTS	15	/* maximum OHCI root hub ports */

/*
 * This is the structure of the OHCI controller's memory mapped I/O
 * region.  This is Memory Mapped I/O.  You must use the readl() and
 * writel() macros defined in asm/io.h to access these!!
 */
struct ohci_regs {
	/* control and status registers */
	__u32	revision;
	__u32	control;
	__u32	cmdstatus;
	__u32	intrstatus;
	__u32	intrenable;
	__u32	intrdisable;
	/* memory pointers */
	__u32	hcca;
	__u32	ed_periodcurrent;
	__u32	ed_controlhead;
	__u32	ed_controlcurrent;
	__u32	ed_bulkhead;
	__u32	ed_bulkcurrent;
	__u32	current_donehead; /* The driver should get this from the HCCA */
	/* frame counters */
	__u32	fminterval;
	__u32	fmremaining;
	__u32	fmnumber;
	__u32	periodicstart;
	__u32	lsthresh;
	/* Root hub ports */
	struct	ohci_roothub_regs {
		__u32	a;
		__u32	b;
		__u32	status;
		__u32	portstatus[MAX_ROOT_PORTS];
	} roothub;
} __attribute((aligned(32)));

/*
 * These are used by internal ED managing functions as a
 * parameter to state the type of ED to deal with (when it matters).
 */
#define HCD_ED_ISOC     (0)
#define HCD_ED_INT      (1)
#define HCD_ED_CONTROL  (2)
#define HCD_ED_BULK     (3)

/* 
 * Read a MMIO register and re-write it after ANDing with (m)
 */
#define writel_mask(m, a) writel( (readl((unsigned long)(a))) & (__u32)(m), (unsigned long)(a) )

/*
 * Read a MMIO register and re-write it after ORing with (b)
 */
#define writel_set(b, a) writel( (readl((unsigned long)(a))) | (__u32)(b), (unsigned long)(a) )


#define PORT_CCS	(1)		/* port current connect status */
#define PORT_PES	(1 << 1)	/* port enable status */
#define PORT_PSS	(1 << 2)	/* port suspend status */
#define PORT_POCI	(1 << 3)	/* port overcurrent indicator */
#define PORT_PRS	(1 << 4)	/* port reset status */
#define PORT_PPS	(1 << 8)	/* port power status */
#define PORT_LSDA	(1 << 9)	/* port low speed dev. attached */
#define PORT_CSC	(1 << 16)	/* port connect status change */
#define PORT_PESC	(1 << 17)	/* port enable status change */
#define PORT_PSSC	(1 << 18)	/* port suspend status change */
#define PORT_OCIC	(1 << 19)	/* port over current indicator chg */
#define PORT_PRSC	(1 << 20)	/* port reset status change */

/*
 * Root Hub status register masks
 */
#define OHCI_ROOT_LPS	(1)		/* turn off root hub ports power */
#define OHCI_ROOT_OCI	(1 << 1)	/* Overcurrent Indicator */
#define OHCI_ROOT_DRWE	(1 << 15)	/* Device remote wakeup enable */
#define OHCI_ROOT_LPSC	(1 << 16)	/* turn on root hub ports power */
#define OHCI_ROOT_OCIC	(1 << 17)	/* Overcurrent indicator change */
#define OHCI_ROOT_CRWE	(1 << 31)	/* Clear RemoteWakeupEnable */

/*
 * Root hub A register masks
 */
#define OHCI_ROOT_A_NPS	(1 << 9)
#define OHCI_ROOT_A_PSM	(1 << 8)

/*
 * Root hub B register masks
 */

/*
 * Interrupt register masks
 */
#define OHCI_INTR_SO	(1)
#define OHCI_INTR_WDH	(1 << 1)
#define OHCI_INTR_SF	(1 << 2)
#define OHCI_INTR_RD	(1 << 3)
#define OHCI_INTR_UE	(1 << 4)
#define OHCI_INTR_FNO	(1 << 5)
#define OHCI_INTR_RHSC	(1 << 6)
#define OHCI_INTR_OC	(1 << 30)
#define OHCI_INTR_MIE	(1 << 31)

/*
 * Control register masks
 */
#define OHCI_USB_OPER		(2 << 6)
#define OHCI_USB_SUSPEND	(3 << 6)
#define OHCI_USB_PLE		(1 << 2)  /* Periodic (interrupt) list enable */
#define OHCI_USB_IE		(1 << 3)  /* Isochronous list enable */
#define OHCI_USB_CLE		(1 << 4)  /* Control list enable */
#define OHCI_USB_BLE		(1 << 5)  /* Bulk list enable */

/*
 * Command status register masks
 */
#define OHCI_CMDSTAT_HCR	(1)
#define OHCI_CMDSTAT_CLF	(1 << 1)
#define OHCI_CMDSTAT_BLF	(1 << 2)
#define OHCI_CMDSTAT_OCR	(1 << 3)
#define OHCI_CMDSTAT_SOC	(3 << 16)

/*
 * This is the full ohci controller description
 *
 * Note how the "proper" USB information is just
 * a subset of what the full implementation needs. (Linus)
 */
struct ohci {
    	int irq;
	struct ohci_regs *regs;			/* OHCI controller's memory */
	struct usb_bus *bus;
	struct list_head interrupt_list;	/* List of interrupt active TDs for this OHCI */
};

#define OHCI_TIMER		/* enable the OHCI timer */
#define OHCI_TIMER_FREQ	(234)	/* ms between each root hub status check */

#undef OHCI_RHSC_INT		/* Don't use root hub status interrupts! */

/* Debugging code [ohci-debug.c] */
void show_ohci_ed(struct ohci_ed *ed);
void show_ohci_td(struct ohci_td *td);
void show_ohci_status(struct ohci *ohci);
void show_ohci_device(struct ohci_device *dev);
void show_ohci_hcca(struct ohci_hcca *hcca);

#endif
/* vim:sw=8
 */
