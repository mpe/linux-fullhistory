  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 * Definitions for the AM79C830 FORMAC (Fiber Optic Ring MAC) chip.
 */

typedef int	formac_reg;

struct formac {
    formac_reg	cmdreg1;	/* command register 1 */
    formac_reg	cmdreg2;	/* command register 2 */
#define st1u	cmdreg1		/* status reg 1, upper */
#define st1l	cmdreg2		/* status reg 1, lower */
    formac_reg	st2u;		/* status reg 2, upper */
    formac_reg	st2l;		/* status reg 2, lower */
    formac_reg	imsk1u;		/* interrupt mask 1, upper */
    formac_reg	imsk1l;		/* interrupt mask 1, lower */
    formac_reg	imsk2u;		/* interrupt mask 2, upper */
    formac_reg	imsk2l;		/* interrupt mask 2, lower */
    formac_reg	said;		/* short address, individual */
    formac_reg	laim;		/* long adrs, indiv, MS word */
    formac_reg	laic;		/* long adrs, indiv, middle word */
    formac_reg	lail;		/* long adrs, indiv, LS word */
    formac_reg	sagp;		/* short address, group */
    formac_reg	lagm;		/* short adrs, group, MS word */
    formac_reg	lagc;		/* short adrs, group, middle word */
    formac_reg	lagl;		/* short adrs, group, LS word */
    formac_reg	mdreg1;		/* mode reg 1 */
    formac_reg	stmchn;		/* state machine reg */
    formac_reg	mir1;		/* MAC information reg, upper */
    formac_reg	mir0;		/* MAC information reg, lower */
    formac_reg	tmax;		/* TMax value (2's-comp) */
    formac_reg	tvx;		/* TVX value (2's-comp) */
    formac_reg	trt;		/* TRT timer value */
    formac_reg	tht;		/* THT timer value */
    formac_reg	tneg;		/* current TNeg (2's-comp) */
    formac_reg	tmrs;		/* extra bits of tneg, trt, tht; late count */
    formac_reg	treq0;		/* our TReq (2's-comp), lower */
    formac_reg	treq1;		/* our TReq (2's-comp), upper */
    formac_reg	pri0;		/* priority reg for async queue 0 */
    formac_reg	pri1;		/* priority reg for async queue 1 */
    formac_reg	pri2;		/* priority reg for async queue 2 */
    formac_reg	tsync;		/* TSync value (2's-comp) */
    formac_reg	mdreg2;		/* mode reg 2 */
    formac_reg	frmthr;		/* frame threshold reg */
    formac_reg	eacb;		/* end address of claim/beacon area */
    formac_reg	earv;		/* end address of receive area */
    formac_reg	eas;		/* end address of sync queue */
    formac_reg	eaa0;		/* end address of async queue 0 */
    formac_reg	eaa1;		/* end address of async queue 1 */
    formac_reg	eaa2;		/* end address of async queue 2 */
    formac_reg	sacl;		/* start address of claim frame */
    formac_reg	sabc;		/* start address of beacon frame */
    formac_reg	wpxsf;		/* write pointer, special frames */
    formac_reg	rpxsf;		/* read pointer, special frames */
    formac_reg	dummy1;		/* not used */
    formac_reg	rpr;		/* read pointer, receive */
    formac_reg	wpr;		/* write pointer, receive */
    formac_reg	swpr;		/* shadow write pointer, receive */
    formac_reg	wpxs;		/* write pointer, sync queue */
    formac_reg	wpxa0;		/* write pointer, async queue 0 */
    formac_reg	wpxa1;		/* write pointer, async queue 1 */
    formac_reg	wpxa2;		/* write pointer, async queue 2 */
    formac_reg	swpxs;		/* shadow write pointer, sync queue */
    formac_reg	swpxa0;		/* shadow write pointer, async queue 0 */
    formac_reg	swpxa1;		/* shadow write pointer, async queue 1 */
    formac_reg	swpxa2;		/* shadow write pointer, async queue 2 */
    formac_reg	rpxs;		/* read pointer, sync queue */
    formac_reg	rpxa0;		/* read pointer, async queue 0 */
    formac_reg	rpxa1;		/* read pointer, async queue 1 */
    formac_reg	rpxa2;		/* read pointer, async queue 2 */
    formac_reg	marr;		/* memory address for random reads */
    formac_reg	marw;		/* memory address for random writes */
    formac_reg	mdru;		/* memory data register, upper */
    formac_reg	mdrl;		/* memory data register, lower */
    formac_reg	tmsync;		/* TSync timer value */
    formac_reg	fcntr;		/* frame counter */
    formac_reg	lcntr;		/* lost counter */
    formac_reg	ecntr;		/* error counter */
};

/* Values for cmdreg1 */
#define C1_SOFTWARE_RESET	1
#define C1_IRMEMWI		2
#define C1_IRMEMWO		3
#define C1_IDLE_LISTEN		4
#define C1_CLAIM_LISTEN		5
#define C1_BEACON_LISTEN	6
#define C1_LOAD_TVX		7
#define C1_SEND_NR_TOKEN	0x0c
#define C1_SEND_R_TOKEN		0x0d
#define C1_ENTER_SI_MODE	0x0e
#define C1_EXIT_SI_MODE		0x0f
#define C1_CLR_SYNCQ_LOCK	0x11
#define C1_CLR_ASYNCQ0_LOCK	0x12
#define C1_CLR_ASYNCQ1_LOCK	0x14
#define C1_CLR_ASYNCQ2_LOCK	0x18
#define C1_CLR_RECVQ_LOCK	0x20
#define C1_CLR_ALL_LOCKS	0x3f

/* Values for cmdreg2 */
#define C2_XMIT_SYNCQ		1
#define C2_XMIT_ASYNCQ0		2
#define C2_XMIT_ASYNCQ1		4
#define C2_XMIT_ASYNCQ2		8
#define C2_ABORT_XMIT		0x10
#define C2_RESET_XMITQS		0x20
#define C2_SET_TAG		0x30
#define C2_EN_RECV_FRAME	0x40

/* Bits in (st1u << 16) + st1l (and (imsk1u << 16) + imsk1l) */
#define S1_XMIT_ABORT		0x80000000
#define S1_XABORT_ASYNC2	0x40000000
#define S1_XABORT_ASYNC1	0x20000000
#define S1_XABORT_ASYNC0	0x10000000
#define S1_XABORT_SYNC		0x08000000
#define S1_XBUF_FULL_SYNC	0x04000000
#define S1_XBUF_FULL_ASYNC	0x02000000
#define S1_XDONE_SYNC		0x01000000
#define S1_END_CHAIN_ASYNC2	0x00800000
#define S1_END_CHAIN_ASYNC1	0x00400000
#define S1_END_CHAIN_ASYNC0	0x00200000
#define S1_END_CHAIN_SYNC	0x00100000
#define S1_END_FRAME_ASYNC2	0x00080000
#define S1_END_FRAME_ASYNC1	0x00040000
#define S1_END_FRAME_ASYNC0	0x00020000
#define S1_END_FRAME_SYNC	0x00010000
#define S1_BUF_UNDERRUN_ASYNC2	0x00008000
#define S1_BUF_UNDERRUN_ASYNC1	0x00004000
#define S1_BUF_UNDERRUN_ASYNC0	0x00002000
#define S1_BUF_UNDERRUN_SYNC	0x00001000
#define S1_PAR_ERROR_ASYNC2	0x00000800
#define S1_PAR_ERROR_ASYNC1	0x00000400
#define S1_PAR_ERROR_ASYNC0	0x00000200
#define S1_PAR_ERROR_SYNC	0x00000100
#define S1_XINSTR_FULL_ASYNC2	0x00000080
#define S1_XINSTR_FULL_ASYNC1	0x00000040
#define S1_XINSTR_FULL_ASYNC0	0x00000020
#define S1_XINSTR_FULL_SYNC	0x00000010
#define S1_QUEUE_LOCK_ASYNC2	0x00000008
#define S1_QUEUE_LOCK_ASYNC1	0x00000004
#define S1_QUEUE_LOCK_ASYNC0	0x00000002
#define S1_QUEUE_LOCK_SYNC	0x00000001

/* Bits in (st2u << 16) + st2l (and (imsk2u << 16) + imsk2l) */
#define S2_RECV_COMPLETE	0x80000000
#define S2_RECV_BUF_EMPTY	0x40000000
#define S2_RECV_ABORT		0x20000000
#define S2_RECV_BUF_FULL	0x10000000
#define S2_RECV_FIFO_OVF	0x08000000
#define S2_RECV_FRAME		0x04000000
#define S2_RECV_FRCT_OVF	0x02000000
#define S2_NP_SIMULT_LOAD	0x01000000
#define S2_ERR_SPECIAL_FR	0x00800000
#define S2_CLAIM_STATE		0x00400000
#define S2_MY_CLAIM		0x00200000
#define S2_HIGHER_CLAIM		0x00100000
#define S2_LOWER_CLAIM		0x00080000
#define S2_BEACON_STATE		0x00040000
#define S2_MY_BEACON		0x00020000
#define S2_OTHER_BEACON		0x00010000
#define S2_RING_OP		0x00008000
#define S2_MULTIPLE_DA		0x00004000
#define S2_TOKEN_ERR		0x00002000
#define S2_TOKEN_ISSUED		0x00001000
#define S2_TVX_EXP		0x00000800
#define S2_TRT_EXP		0x00000400
#define S2_MISSED_FRAME		0x00000200
#define S2_ADDRESS_DET		0x00000100
#define S2_PHY_INVALID		0x00000080
#define S2_LOST_CTR_OVF		0x00000040
#define S2_ERR_CTR_OVF		0x00000020
#define S2_FRAME_CTR_OVF	0x00000010
#define S2_SHORT_IFG		0x00000008
#define S2_DUPL_CLAIM		0x00000004
#define S2_TRT_EXP_RECOV	0x00000002

/* Bits in mdreg1 */
#define M1_SINGLE_FRAME		0x8000
#define M1_MODE			0x7000
#define M1_MODE_INITIALIZE	0x0000
#define M1_MODE_MEMORY		0x1000
#define M1_MODE_ONLINE_SP	0x2000
#define M1_MODE_ONLINE		0x3000
#define M1_MODE_INT_LOOP	0x4000
#define M1_MODE_EXT_LOOP	0x7000
#define M1_SHORT_ADRS		0x0800
#define M1_ADDET		0x0700
#define M1_ADDET_NORM		0x0000
#define M1_ADDET_METOO		0x0100
#define M1_ADDET_NSA_NOTME	0x0200
#define M1_ADDET_NSA		0x0300
#define M1_ADDET_DISABLE_RECV	0x0400
#define M1_ADDET_LIM_PROMISC	0x0600
#define M1_ADDET_PROMISC	0x0700
#define M1_SELECT_RA		0x0080
#define M1_DISABLE_CARRY	0x0040
#define M1_EXT_GRP		0x0030
#define M1_EXT_GRP_MYGRP	0x0000
#define M1_EXT_GRP_SOFT		0x0010
#define M1_EXT_GRP_UPPER24	0x0020
#define M1_EXT_GRP_UPPER16	0x0030
#define M1_LOCK_XMIT_QS		0x0008
#define M1_FULL_DUPLEX		0x0004
#define M1_XMTINH_PIN		0x0002

/* Bits in mdreg2 */
#define M2_TAGMODE		0x8000
#define M2_STRIP_FCS		0x4000
#define M2_CHECK_PARITY		0x2000
#define M2_EVEN_PARITY		0x1000
#define M2_LSB_FIRST		0x0800
#define M2_RCV_BYTE_BDRY_MASK	0x0600
#define M2_RCV_BYTE_BDRY	0x0200
#define M2_ENABLE_HSREQ		0x0100
#define M2_ENABLE_NPDMA		0x0080
#define M2_SYNC_NPDMA		0x0040
#define M2_SYMBOL_CTRL		0x0020
#define M2_RECV_BAD_FRAMES	0x0010
#define M2_AFULL_MASK		0x000f
#define M2_AFULL		0x0001

/* Bits in stmchn */
#define SM_REV_MASK		0xe000
#define SM_REV			0x2000
#define SM_SEND_IMM_MODE	0x1000
#define SM_TOKEN_MODE		0x0c00
#define SM_TOKEN_MODE_NR	0x0000
#define SM_TOKEN_MODE_ENTER_R	0x0400
#define SM_TOKEN_MODE_ENTER_NR	0x0800
#define SM_TOKEN_MODE_R		0x0c00
#define SM_RCV_STATE		0x0380
#define SM_XMIT_STATE		0x0070
#define SM_MDR_PENDING		0x0008
#define SM_MDR_TAG		0x0004

/* Bits in transmit descriptor */
#define TD_MORE			0x80000000
#define TD_MAGIC		0x40000000
#define TD_BYTE_BDRY_MASK	0x18000000
#define TD_BYTE_BDRY_1		0x08000000
#define TD_XMIT_DONE		0x04000000
#define TD_NO_FCS		0x02000000
#define TD_XMIT_ABORT		0x01000000
#define TD_BYTE_BDRY_LG		27

/* Bits in pointer in buffer memory (nontag mode) */
#define PT_MAGIC		0xa0000000

/* Bits in receive status word */
#define RS_VALID		0x80000000
#define RS_ABORTED		0x40000000
#define RS_SRC_ROUTE		0x10000000
#define RS_E_INDIC		0x08000000
#define RS_A_INDIC		0x04000000
#define RS_C_INDIC		0x02000000
#define RS_ERROR		0x01000000
#define RS_ADDR_MATCH		0x00800000
#define RS_FRAME_TYPE		0x00700000
#define RS_FT_SMT		0x00000000
#define RS_FT_LLC		0x00100000
#define RS_FT_IMPL		0x00200000
#define RS_FT_MAC		0x00400000
#define RS_FT_LLC_SYNC		0x00500000
#define RS_FT_IMPL_SYNC		0x00600000
#define RS_BYTE_BDRY_MASK	0x00030000
#define RS_BYTE_BDRY		0x00010000
#define RS_BYTE_BDRY_LG		16

#define RS_LENGTH		0x0000ffff

