 /*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 *
 * The OHCI HCD layer is a simple but nearly complete implementation of what the
 * USB people would call a HCD  for the OHCI. 
 * (ISO comming soon, Bulk, INT u. CTRL transfers enabled)
 * The layer on top of it, is for interfacing to the alternate-usb device-drivers.
 * 
 * [ This is based on Linus' UHCI code and gregs OHCI fragments (0.03c source tree). ]
 * [ Open Host Controller Interface driver for USB. ]
 * [ (C) Copyright 1999 Linus Torvalds (uhci.c) ]
 * [ (C) Copyright 1999 Gregory P. Smith <greg@electricrain.com> ]
 * [ $Log: ohci.c,v $ ]
 * [ Revision 1.1  1999/04/05 08:32:30  greg ]
 * 
 * v4.0 1999/08/18
 * v2.1 1999/05/09 ep_addr correction, code clean up
 * v2.0 1999/05/04 
 * v1.0 1999/04/27
 * ohci-hcd.h
 */
 
// #define OHCI_DBG    /* printk some debug information */

 
#include <linux/config.h>

// #ifdef CONFIG_USB_OHCI_VROOTHUB
#define VROOTHUB  
// #endif
/* enables virtual root hub 
 * (root hub will be managed by the hub controller 
 *  hub.c of the alternate usb driver)
 *  must be on now
 */
 
 
  
#ifdef OHCI_DBG
#define OHCI_DEBUG(X) X
#else 
#define OHCI_DEBUG(X)
#endif 

/* for readl writel functions */
#include <linux/list.h>
#include <asm/io.h>
struct usb_ohci_ed;
/* for ED and TD structures */

typedef void * __OHCI_BAG;
typedef int (*f_handler )(void * ohci, struct usb_ohci_ed *ed, void *data, int data_len, int status, __OHCI_BAG lw0, __OHCI_BAG lw1);

 
 
/* ED States */

#define ED_NEW 		0x00
#define ED_UNLINK 	0x01
#define ED_OPER		0x02
#define ED_STOP     0x03
#define ED_DEL		0x04
#define ED_RH		0x07 /* marker for RH ED */

#define ED_STATE(ed) 			(((ed)->hwINFO >> 29) & 0x7)
#define ED_setSTATE(ed,state) 	(ed)->hwINFO = ((ed)->hwINFO & ~(0x7 << 29)) | (((state)& 0x7) << 29)
#define ED_TYPE(ed) 			(((ed)->hwINFO >> 27) & 0x3)

struct usb_ohci_ed {
	__u32 hwINFO;       
	__u32 hwTailP;
	__u32 hwHeadP;
	__u32 hwNextED;
	 
	void * buffer_start;
	unsigned int len;
	struct usb_ohci_ed *ed_prev;  
	__u8 int_period;
	__u8 int_branch;
	__u8 int_load; 
	__u8 int_interval;
   
} __attribute((aligned(32)));

struct usb_hcd_ed {
	int endpoint;
	int function;
	int out;
	int type;
	int slow;
	int maxpack;
};

struct ohci_state {
	int len;
	int status;
};


 
/* TD info field */
#define TD_CC       0xf0000000
#define TD_CC_GET(td_p) ((td_p >>28) & 0x0f)
#define TD_CC_SET(td_p, cc) (td_p) = ((td_p) & 0x0fffffff) | (((cc) & 0x0f) << 28)
#define TD_EC       0x0C000000
#define TD_T        0x03000000
#define TD_T_DATA0  0x02000000
#define TD_T_DATA1  0x03000000
#define TD_T_TOGGLE 0x00000000
#define TD_R        0x00040000
#define TD_DI       0x00E00000
#define TD_DI_SET(X) (((X) & 0x07)<< 21)
#define TD_DP       0x00180000
#define TD_DP_SETUP 0x00000000
#define TD_DP_IN    0x00100000
#define TD_DP_OUT   0x00080000

#define TD_ISO		0x00010000
#define TD_DEL      0x00020000

/* CC Codes */
#define TD_CC_NOERROR      0x00
#define TD_CC_CRC          0x01
#define TD_CC_BITSTUFFING  0x02
#define TD_CC_DATATOGGLEM  0x03
#define TD_CC_STALL        0x04
#define TD_DEVNOTRESP      0x05
#define TD_PIDCHECKFAIL    0x06
#define TD_UNEXPECTEDPID   0x07
#define TD_DATAOVERRUN     0x08
#define TD_DATAUNDERRUN    0x09
#define TD_BUFFEROVERRUN   0x0C
#define TD_BUFFERUNDERRUN  0x0D
#define TD_NOTACCESSED     0x0F


#define MAXPSW 2

struct usb_ohci_td { 
	__u32 hwINFO;
  	__u32 hwCBP;		/* Current Buffer Pointer */
  	__u32 hwNextTD;		/* Next TD Pointer */
  	__u32 hwBE;		/* Memory Buffer End Pointer */
  	__u16 hwPSW[MAXPSW];

  	__u32 type;
  	void *  buffer_start;
  	f_handler handler;
  	struct usb_ohci_ed *ed;
  	struct usb_ohci_td *next_dl_td;
  	__OHCI_BAG lw0;
  	__OHCI_BAG lw1;
} __attribute((aligned(32)));


/* TD types */
#define BULK		0x03
#define INT			0x01
#define CTRL		0x02
#define ISO			0x00
/* TD types with direction */
#define BULK_IN		0x07
#define BULK_OUT	0x03
#define INT_IN		0x05
#define INT_OUT		0x01
#define CTRL_IN		0x06
#define CTRL_OUT	0x02
#define ISO_IN		0x04
#define ISO_OUT		0x00

#define CTRL_SETUP  	0x102
#define CTRL_DATA_IN	0x206
#define CTRL_DATA_OUT	0x202
#define CTRL_STATUS_IN	0x306
#define CTRL_STATUS_OUT	0x302

 
#define SEND            0x00001000
#define ST_ADDR         0x00002000
#define ADD_LEN         0x00004000
#define DEL             0x00008000
#define DEL_ED          0x00040000

#define OHCI_ED_SKIP	(1 << 14)
 

 
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
	__u32	done_head;		/* info returned for an interrupt */
	u8		reserved_for_hc[116];
} __attribute((aligned(256)));

  
/*
 * This is the maximum number of root hub ports.  I don't think we'll
 * ever see more than two as that's the space available on an ATX
 * motherboard's case, but it could happen.  The OHCI spec allows for
 * up to 15... (which is insane!)
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
	__u32	donehead;
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
 * Read a MMIO register and re-write it after ANDing with (m)
 */
#define writel_mask(m, a) writel( (readl((__u32)(a))) & (__u32)(m), (__u32)(a) )

/*
 * Read a MMIO register and re-write it after ORing with (b)
 */
#define writel_set(b, a) writel( (readl((__u32)(a))) | (__u32)(b), (__u32)(a) )

/*
 * cmdstatus register */
#define OHCI_CLF  0x02
#define OHCI_BLF  0x04

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
#define OHCI_USB_RESET		0
#define OHCI_USB_OPER		(2 << 6)
#define OHCI_USB_SUSPEND	(3 << 6)

struct virt_root_hub {
	int devnum; /* Address of Root Hub endpoint */ 
	usb_device_irq handler;
	void * dev_id;
	void * int_addr;
	int send;
	int interval;
	struct timer_list rh_int_timer;
};
/*
 * This is the full ohci controller description
 *
 * Note how the "proper" USB information is just
 * a subset of what the full implementation needs. (Linus)
 */


struct ohci {
	int irq;
	struct ohci_regs *regs;					/* OHCI controller's memory */	
	struct ohci_hc_area *hc_area;			/* hcca, int ed-tree, ohci itself .. */                
                             
	int ohci_int_load[32];                  /* load of the 32 Interrupt Chains (for load ballancing)*/     
	struct usb_ohci_ed * ed_rm_list;        /* list of all endpoints to be removed */
	struct usb_ohci_ed * ed_bulktail;       /* last endpoint of bulk list */
	struct usb_ohci_ed * ed_controltail;    /* last endpoint of control list */
 	struct usb_ohci_ed * ed_isotail;        /* last endpoint of iso list */
	int intrstatus;
	struct ohci_rep_td *repl_queue;			/* for internal requests */
	int rh_int_interval;
	int rh_int_timer;   
	struct usb_bus *bus;    
	struct virt_root_hub rh;
};


#define NUM_TDS	0		/* num of preallocated transfer descriptors */
#define NUM_EDS 32		/* num of preallocated endpoint descriptors */

struct ohci_hc_area {
	struct ohci_hcca 	hcca;		/* OHCI mem. mapped IO area 256 Bytes*/
 	struct ohci         ohci;
        
};
struct ohci_device {
	struct usb_device	*usb;
	struct ohci			*ohci;
	struct usb_ohci_ed	ed[NUM_EDS];
	unsigned long		data[16];
};

#define ohci_to_usb(ohci)	((ohci)->usb)
#define usb_to_ohci(usb)	((struct ohci_device *)(usb)->hcpriv)

/* hcd */
struct usb_ohci_td * ohci_trans_req(struct ohci * ohci, struct usb_ohci_ed * ed, int cmd_len, void  *cmd, void * data, int data_len, __OHCI_BAG lw0, __OHCI_BAG lw1, unsigned int type, f_handler handler); 
struct usb_ohci_ed *usb_ohci_add_ep(struct usb_device * usb_dev, struct usb_hcd_ed * hcd_ed, int interval, int load);
int usb_ohci_rm_function(struct usb_device * usb_dev, f_handler handler, __OHCI_BAG lw0, __OHCI_BAG lw1); 
int usb_ohci_rm_ep(struct usb_device * usb_dev, struct  usb_ohci_ed *ed, f_handler handler, __OHCI_BAG lw0, __OHCI_BAG lw1, int send);
struct usb_ohci_ed * ohci_find_ep(struct usb_device * usb_dev, struct usb_hcd_ed *hcd_ed);
 
/* roothub */

int root_hub_control_msg(struct usb_device *usb_dev, unsigned int pipe, devrequest *cmd, void *data, int len);
int root_hub_release_irq(struct usb_device *usb_dev, void * ed);  
void * root_hub_request_irq(struct usb_device *usb_dev, unsigned int pipe, usb_device_irq handler, int period, void *dev_id);

/* Root-Hub Register info */

#define RH_PS_CCS            0x00000001   
#define RH_PS_PES            0x00000002   
#define RH_PS_PSS            0x00000004   
#define RH_PS_POCI           0x00000008   
#define RH_PS_PRS            0x00000010  
#define RH_PS_PPS            0x00000100   
#define RH_PS_LSDA           0x00000200    
#define RH_PS_CSC            0x00010000 
#define RH_PS_PESC           0x00020000   
#define RH_PS_PSSC           0x00040000    
#define RH_PS_OCIC           0x00080000    
#define RH_PS_PRSC           0x00100000   


#ifdef OHCI_DBG
#define OHCI_FREE(x) kfree(x); printk("OHCI FREE: %d: %4x\n", -- __ohci_free_cnt, (unsigned int) x)
#define OHCI_ALLOC(x,size) (x) = kmalloc(size, GFP_KERNEL); printk("OHCI ALLO: %d: %4x\n", ++ __ohci_free_cnt,(unsigned int) x)
#define USB_FREE(x) kfree(x); printk("USB FREE: %d: %4x\n", -- __ohci_free1_cnt, (unsigned int) x)
#define USB_ALLOC(x,size) (x) = kmalloc(size, GFP_KERNEL); printk("USB ALLO: %d: %4x\n", ++ __ohci_free1_cnt, (unsigned int) x)
static int __ohci_free_cnt = 0;
static int __ohci_free1_cnt = 0;
#else
#define OHCI_FREE(x) kfree(x) 
#define OHCI_ALLOC(x,size) (x) = kmalloc(size, GFP_KERNEL) 
#define USB_FREE(x) kfree(x) 
#define USB_ALLOC(x,size) (x) = kmalloc(size, GFP_KERNEL) 
#endif
 
