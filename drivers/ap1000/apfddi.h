  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
#define BUFFER_MEM	0x40000
#define CSR0		0x60000
#define CSR1		0x60004
#define PLC		0x60080
#define FORMAC		0x60200
#define FIFO		0x68000

/* Size of buffer memory */
#define BUFFER_SIZE	32768		/* words; 128kB */

/* Bits in CSR0 */
#define CS0_INT_REQ	0x8000		/* board interrupt request asserted */
#define CS0_MAC_IRQ	0x4000		/* FORMAC is requesting interrupt */
#define CS0_PHY_IRQ	0x2000		/* PLC is requesting interrupt */
#define CS0_LED2	0x1000		/* turn on led 2 */
#define CS0_DO_IRQ	0x0200		/* request interrupt */
#define CS0_INT_ENABLE	0x0100		/* enable interrupt requests */
#define CS0_DMA_ENABLE	0x0080		/* enable DMA requests */
#define CS0_DMA_RECV	0x0040		/* DMA requests are in receive dirn. */
#define CS0_LED1	0x0010		/* turn on led 1 */
#define CS0_LED0	0x0008		/* turn on led 0 (red) */
#define CS0_HREQ	0x0007		/* host request to FORMAC */
#define CS0_HREQ_WSPEC	0x0002		/* write special frames */
#define CS0_HREQ_RECV	0x0003		/* read receive queue */
#define CS0_HREQ_WS	0x0004		/* write synchronous queue */
#define CS0_HREQ_WA0	0x0005		/* write async queue 0 */
#define CS0_HREQ_WA1	0x0006		/* write async queue 1 */
#define CS0_HREQ_WA2	0x0007		/* write async queue 2 */

/* Bits in CSR1 */
#define CS1_THIS_QAF	0x0800		/* this queue almost full */
#define CS1_FIFO_TAG	0x0400		/* tag of word at head of fifo */
#define CS1_BUF_RD_TAG	0x0200		/* tag of last word read from buffer */
#define CS1_BUF_WR_TAG	0x0100		/* tag to write to buffer */
#define CS1_TAGMODE	0x0080		/* enable tag mode */
#define CS1_RESET_MAC	0x0040		/* reset FORMAC and PLC */
#define CS1_RESET_FIFO	0x0020		/* reset FIFO */
#define CS1_CLEAR_QAF	0x0010		/* clear queue-almost-full bits */
#define CS1_FIFO_LEVEL	0x0007		/* # words in FIFO (0 - 4) */

/*
 * FDDI Frame Control values.
 */
#define FDDI_SMT		0x41
#define FDDI_SMT_NSA		0x4f
#define FDDI_FC_LLC		0x50
#define FDDI_FC_LLC_MASK	0xf0

/*
 * Unnumbered LLC format commands
 */
#define LLC_UI		0x3
#define LLC_UI_P	0x13
#define LLC_DISC	0x43
#define	LLC_DISC_P	0x53
#define LLC_UA		0x63
#define LLC_UA_P	0x73
#define LLC_TEST	0xe3
#define LLC_TEST_P	0xf3
#define LLC_FRMR	0x87
#define	LLC_FRMR_P	0x97
#define LLC_DM		0x0f
#define	LLC_DM_P	0x1f
#define LLC_XID		0xaf
#define LLC_XID_P	0xbf
#define LLC_SABME	0x6f
#define LLC_SABME_P	0x7f

/*
 * Supervisory LLC commands
 */
#define	LLC_RR		0x01
#define	LLC_RNR		0x05
#define	LLC_REJ		0x09

/*
 * Info format - dummy only
 */
#define	LLC_INFO	0x00

/*
 * ISO PDTR 10178 contains among others
 */
#define LLC_X25_LSAP	0x7e
#define LLC_SNAP_LSAP	0xaa
#define LLC_ISO_LSAP	0xfe

/*
 * Structure of the FDDI MAC header.
 */
struct fddi_header {
    u_char	fddi_fc;	/* frame control field */
    u_char	fddi_dhost[6];	/* destination address */
    u_char	fddi_shost[6];	/* source address */
};

/*
 * Structure of LLC/SNAP header.
 */
struct llc_header {
  u_char llc_dsap;
  u_char llc_ssap;
  u_char snap_control;
  u_char snap_org_code[3];
  u_short snap_ether_type;
};
  
#define FDDI_HDRLEN	13	/* sizeof(struct fddi_header) */
#define LLC_SNAPLEN	8	/* bytes for LLC/SNAP header */
#define FDDI_HARDHDR_LEN 28     /* Hard header size */

#define FDDIMTU		4352


/* Types of loopback we can do. */
typedef enum {
    loop_none,
    loop_formac,
    loop_plc_lm,
    loop_plc_eb,
    loop_pdx
} LoopbackType;

/* Offset from fifo for writing word with tag. */
#define FIFO_TAG	0x80

#define MAX_FRAME_LEN	4500

void set_ring_op(int up);
void rmt_event(int st);
void set_cf_join(int on);

extern struct device *apfddi_device;
extern struct net_device_stats *apfddi_stats;

