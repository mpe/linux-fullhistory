 /*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 *
 * The OHCI HCD layer is a simple but nearly complete implementation of what the
 * USB people would call a HCD  for the OHCI. 
 * (ISO comming soon, Bulk disabled, INT u. CTRL transfers enabled)
 * The layer on top of it, is for interfacing to the alternate-usb device-drivers.
 * 
 * [ This is based on Linus' UHCI code and gregs OHCI fragments (0.03c source tree). ]
 * [ Open Host Controller Interface driver for USB. ]
 * [ (C) Copyright 1999 Linus Torvalds (uhci.c) ]
 * [ (C) Copyright 1999 Gregory P. Smith <greg@electricrain.com> ]
 * [ $Log: ohci.c,v $ ]
 * [ Revision 1.1  1999/04/05 08:32:30  greg ]
 * 
 * 
 * v2.1 1999/05/09 ep_addr correction, code clean up
 * v2.0 1999/05/04 
 * v1.0 1999/04/27
 * ohci-hcd.h
 */

#include <linux/config.h>

#ifdef CONFIG_USB_OHCI_VROOTHUB
#define VROOTHUB  
#endif
/* enables virtual root hub 
 * (root hub will be managed by the hub controller 
 *  hub.c of the alternate usb driver)
 *  last time I did more testing without virtual root hub 
 *  -> the virtual root hub could be more unstable now */
 
 
  
#ifdef OHCI_DBG
#define OHCI_DEBUG(X) X
#else 
#define OHCI_DEBUG(X)
#endif 

/* for readl writel functions */
#include <linux/list.h>
#include <asm/io.h>

/* for ED and TD structures */

typedef void * __OHCI_BAG;
typedef int (*f_handler )(void * ohci, unsigned int ep_addr, int cmd_len, void *cmd, void *data, int data_len, int status, __OHCI_BAG lw0, __OHCI_BAG lw1);



struct ep_address {
  __u8 ep;  /* bit 7: IN/-OUT, 6,5: type 10..CTRL 00..ISO  11..BULK 10..INT, 3..0: ep nr */ 
  __u8 fa;    /* function address */
  __u8 hc;
  __u8 host;
};

union ep_addr_ {
  unsigned int iep;
  struct ep_address bep;
};

/*
 * ED and TD descriptors has to be 16-byte aligned
 */
struct ohci_hw_ed {
  __u32 info;       
  __u32 tail_td;	/* TD Queue tail pointer */
  __u32 head_td;	/* TD Queue head pointer */
  __u32 next_ed;	/* Next ED */
} __attribute((aligned(16)));


struct usb_ohci_ed {
  struct ohci_hw_ed hw;
  /*  struct ohci * ohci; */
  f_handler handler;
  union ep_addr_ ep_addr;
  struct usb_ohci_ed *ed_list;
  struct usb_ohci_ed *ed_prev;
} __attribute((aligned(32)));

 /* OHCI Hardware fields */
struct ohci_hw_td {     
  __u32 info;
  __u32 cur_buf;		/* Current Buffer Pointer */
  __u32 next_td;		/* Next TD Pointer */
  __u32 buf_end;		/* Memory Buffer End Pointer */
} __attribute((aligned(16)));

/* TD info field */
#define TD_CC       0xf0000000
#define TD_CC_GET(td_p) ((td_p >>28) & 0x04)
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



struct usb_ohci_td {
  struct ohci_hw_td hw;
  void *  buffer_start;
  f_handler handler; 
  struct usb_ohci_td *prev_td;
  struct usb_ohci_ed *ep;
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

struct ohci_rep_td {
  int cmd_len;
  void * cmd;
  void * data;
  int data_len;
  f_handler handler;
  struct ohci_rep_td *next_td;
  int ep_addr;
  __OHCI_BAG lw0;
  __OHCI_BAG lw1;
  __u32 status;
} __attribute((aligned(32))); 

#define OHCI_ED_SKIP	(1 << 14)
#define OHCI_ED_MPS	(0x7ff << 16)
#define OHCI_ED_F_NORM	(0)
#define OHCI_ED_F_ISOC	(1 << 15)
#define OHCI_ED_S_LOW	(1 << 13)
#define OHCI_ED_S_HIGH	(0)
#define OHCI_ED_D	(3 << 11)
#define OHCI_ED_D_IN	(2 << 11)
#define OHCI_ED_D_OUT	(1 << 11)
#define OHCI_ED_EN	(0xf << 7)
#define OHCI_ED_FA	(0x7f)

 
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

  

#define ED_INT_1	1
#define ED_INT_2	2
#define ED_INT_4	4
#define ED_INT_8	8
#define ED_INT_16	16
#define ED_INT_32	32
#define ED_CONTROL	64
#define ED_BULK		65
#define ED_ISO		0	/* same as 1ms interrupt queue */
 

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
#define OHCI_USB_OPER		(2 << 6)
#define OHCI_USB_SUSPEND	(3 << 6)

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
        int root_hub_funct_addr;                /* Address of Root Hub endpoint */       
        int ohci_int_load[32];                       /* load of the 32 Interrupt Chains (for load ballancing)*/     
        struct usb_ohci_ed * ed_rm_list;        /* list of all endpoints to be removed */
        struct usb_ohci_ed * ed_bulktail;       /* last endpoint of bulk list */
        struct usb_ohci_ed * ed_controltail;    /* last endpoint of control list */
        struct usb_ohci_ed * ed_isotail;        /* last endpoint of iso list */
        struct usb_ohci_ed ed_rh_ep0;
        struct usb_ohci_ed ed_rh_epi;
        struct ohci_rep_td *td_rh_epi;
        int intrstatus;
        struct usb_ohci_ed *ed_func_ep0[128];   /* "hash"-table for ep to ed mapping */
        struct ohci_rep_td *repl_queue;			/* for internal requests */
        int rh_int_interval;
        int rh_int_timer;   
        struct usb_bus *bus;
       
       
};

/*
 *  Warning: This constant must not be so large as to cause the
 *  ohci_device structure to exceed one 4096 byte page.  Or "weird
 *  things will happen" as the alloc_ohci() function assumes that
 *  its less than one page at the moment.  (FIXME)
 */
#define NUM_TDS	4		/* num of preallocated transfer descriptors */
#define NUM_EDS 80		/* num of preallocated endpoint descriptors */

struct ohci_hc_area {

	struct ohci_hcca 	hcca;		/* OHCI mem. mapped IO area 256 Bytes*/

	struct ohci_hw_ed		ed[NUM_EDS];	/* Endpoint Descriptors 80 * 16  : 1280 Bytes */
	struct ohci_hw_td		td[NUM_TDS];	/* Transfer Descriptors 2 * 32   : 64 Bytes */
        struct ohci             ohci;
        
};
struct ohci_device {
	struct usb_device	*usb;
	struct ohci			*ohci;
	unsigned long		data[16];
};

#define ohci_to_usb(uhci)	((ohci)->usb)
#define usb_to_ohci(usb)	((struct ohci_device *)(usb)->hcpriv)

/* Debugging code */
/*void show_ed(struct ohci_ed *ed);
void show_td(struct ohci_td *td);
void show_status(struct ohci *ohci); */

/* hcd */
int ohci_trans_req(struct ohci * ohci, unsigned int ep_addr, int cmd_len, void  *cmd, void * data, int data_len, __OHCI_BAG lw0, __OHCI_BAG lw1); 
struct usb_ohci_ed *usb_ohci_add_ep(struct ohci * ohci, unsigned int ep_addr, int interval, int load, f_handler handler, int ep_size, int speed);
int usb_ohci_rm_function(struct ohci * ohci, unsigned int ep_addr); 
int usb_ohci_rm_ep(struct ohci * ohci, struct  usb_ohci_ed *ed);
struct usb_ohci_ed * ohci_find_ep(struct ohci *ohci, unsigned int ep_addr_in);

/* roothub */
int ohci_del_rh_int_timer(struct ohci * ohci);
int	ohci_init_rh_int_timer(struct ohci * ohci, int interval);  	
int root_hub_int_req(struct ohci * ohci, int cmd_len, void * ctrl, void *  data, int data_len, __OHCI_BAG lw0, __OHCI_BAG lw1, f_handler handler);
int root_hub_send_irq(struct ohci * ohci, void * data, int data_len );
int root_hub_control_msg(struct ohci *ohci, int cmd_len, void *rh_cmd, void *rh_data, int len, __OHCI_BAG lw0, __OHCI_BAG lw1, f_handler handler);
int queue_reply(struct ohci * ohci, unsigned int ep_addr, int cmd_len,void * cmd, void * data,int  len, __OHCI_BAG lw0, __OHCI_BAG lw1, f_handler handler);
int send_replies(struct ohci * ohci);

  
 

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
#define OHCI_FREE(x) kfree(x); printk("OHCI FREE: %d\n", -- __ohci_free_cnt)
#define OHCI_ALLOC(x,size) (x) = kmalloc(size, GFP_KERNEL); printk("OHCI ALLO: %d\n", ++ __ohci_free_cnt)
#define USB_FREE(x) kfree(x); printk("USB FREE: %d\n", -- __ohci_free1_cnt)
#define USB_ALLOC(x,size) (x) = kmalloc(size, GFP_KERNEL); printk("USB ALLO: %d\n", ++ __ohci_free1_cnt)
static int __ohci_free_cnt = 0;
static int __ohci_free1_cnt = 0;
#else
#define OHCI_FREE(x) kfree(x) 
#define OHCI_ALLOC(x,size) (x) = kmalloc(size, GFP_KERNEL) 
#define USB_FREE(x) kfree(x) 
#define USB_ALLOC(x,size) (x) = kmalloc(size, GFP_KERNEL) 
#endif
 
