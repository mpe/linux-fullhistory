  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* ap1000 register definitions needed for Linux/AP+ */

#ifndef _AP1000_APREG_H
#define _AP1000_APREG_H
#include <asm/page.h>
#include <asm/ap1000/apservice.h>
#include <asm/ap1000/apbif.h>
#include <linux/tasks.h>

/*
 * Macros for accessing I/O registers.
 */
#define BIF_IN(reg)	(*(volatile unsigned *)(reg))
#define BIF_OUT(reg,v)	(*(volatile unsigned *)(reg) = (v))
#define DMA_IN(reg)	(*(volatile unsigned *)(reg))
#define DMA_OUT(reg,v)	(*(volatile unsigned *)(reg) = (v))
#define MC_IN(reg)	(*(volatile unsigned *)(reg))
#define MC_OUT(reg,v)	(*(volatile unsigned *)(reg) = (v))
#define MSC_IN(reg)	(*(volatile unsigned *)(reg))
#define MSC_OUT(reg,v)	(*(volatile unsigned *)(reg) = (v))
#define MSC_IO(reg)	(*(volatile unsigned *)(reg))
#define RTC_IO(reg)	(*(volatile unsigned *)(reg))
#define MC_IO(reg)	(*(volatile unsigned *)(reg))
#define OPT_IO(reg)	(*(volatile unsigned *)(reg))

/*
 * B-net interface register definitions.
 */
#define BIF 	0xfff30000	
#define BIF_CIDR1 	(BIF+0x004c)	/* cell-id register 1 (for cell mode)*/
#define BIF_SDCSR 	(BIF+0x0070)	/* BIF data control set register  */
#define BIF_DATA 	(BIF+0x0000)	/* BIF send and receive data registe	*/
#define BIF_EDATA  	(BIF+0x0004)	/* BIF end data register 		*/
#define BIF_INTR 	(BIF+0x006c)	/* BIF interrupt control register 	*/

#define SSTT_SET	(BIF+0xe0)	/* set SSTT 			*/
#define SSTT_CLR	(BIF+0xe4)	/* clear SSTT 			*/
#define SSTT_SMSK	(BIF+0xe8)	/* set SSTT mask		*/
#define SSTT_CMSK	(BIF+0xec)	/* clear SSTT mask		*/
#define SSTT_SMD	(BIF+0xf0)	/* set SSYN & SSTT mode		*/
#define SSTT_CMD	(BIF+0xf4)	/* clear SSYN & SSTT mode	*/

/*
** FSTT registers
*/
#define FSTT	BIF	/*	FSTT only system mode	*/
#define FSTT_SET	(FSTT+0xa0)	/* set FSTT 			  */
#define FSTT_CLR	(FSTT+0xa4)	/* clear FSTT 			  */
#define FSTT_SMSK	(FSTT+0xa8)	/* set FSTT mask		  */
#define FSTT_CMSK	(FSTT+0xac)	/* clear FSTT mask		  */
#define FSTT_SMD	(FSTT+0xb0)	/* set FSYN & FSTT mode		  */
#define FSTT_CMD	(FSTT+0xb4)	/* clear FSYN & FSTT mode	  */
#define FSTT_TIM	(FSTT+0xb8)	/* status timer			  */


#define BIF_SDCSR_RB	0x0000001c	/* data in receive FIFO		  */
#define BIF_SDCSR_EB	0x00000100	/* send data that have end bit	  */
#define BIF_SDCSR_BG	0x00001000	/* check if command bus got	  */
#define BIF_SDCSR_BR	0x00000800	/* request command bus		  */
#define BIF_SDCSR_TB	0x000000E0	/* data in send FIFO		  */
#define BIF_SDCSR_PE	0x80000000	/* detect parity error in sync	       	*/
#define BIF_SDCSR_BB    0x00002000      /* check if some BIF use command bus    */

#define BIF_SDCSR_RB_SHIFT 2
#define BIF_SDCSR_TB_SHIFT 5

#define BIF_INTR_GET_SH       15         /* get bus interrupt */
#define BIF_INTR_HEADER_SH    12         /* header interrupt */
#define BIF_INTR_SEND_SH      9          /* send interrupt */
#define BIF_INTR_RECV_SH      6          /* receive interrupt */
#define BIF_INTR_ERR_SH       3          /* error interrupt */
#define BIF_INTR_ATTN_SH      0          /* attention interrupt */


#define BIF_HEADER_HS	0x00000200	/* header strip		*/
#define BIF_HEADER_RS	0x00000100	/* bus release 		*/
#define BIF_HEADER_IN	0x00001000	/* interrupt bit	*/
#define BIF_HEADER_BR   0x00008000    /* broad bit    */
#define BIF_INTR_HS		0x00004000	/* header interrupt select  */
#define HOST_CID      0x1000
#define MAKE_HEADER(cid) (BIF_HEADER_IN | \
			  ((cid)==-1?BIF_HEADER_BR:((cid)<<16) | (1<<13)))

#define BIF_RDCSR 	(BIF+0x0074)	/* BIF data control reset reregister */

/*
 * Interrupt levels for AP+ devices
 */
#define APBIFGET_IRQ	1		/* have acquired B-net */
#define APOPT0_IRQ	2		/* option interrupt level 0 */
#define APSYNC_IRQ	3		/* sync (S-net) interrupt */
#define APDMA_IRQ	4		/* DMA complete interrupt */
#define APRTC_IRQ	5		/* RTC data transfer interrupt */
#define APIPORT_IRQ	6		/* Interrupt port interrupt */
#define APOPT1_IRQ	7		/* option interrupt level 1 */
#define APBIF_IRQ	8		/* B-net interface interrupt */
#define APMAS_IRQ	9		/* Send/Recv mem acc. seq. intr */
#define APTIM1_IRQ	10		/* Timer 1 interrupt */
#define APMSC_IRQ	11		/* MSC+ ring buf/queue spill etc. */
#define APLBUS_IRQ	12		/* LBUS error interrupt */
#define APATTN_IRQ	13		/* Attention interrupt */
#define APTIM0_IRQ	14		/* Timer 0 interrupt */
#define APMEM_IRQ	15		/* Memory error interrupt */

/*
 * LBUS DMA controller register definitions
 */
#define DMA     0xfff00000 /* dma controller address */
#define DMA3    (DMA+0xc0) /* DMA channel 3 */
#define DMA_DMST    0x04
#define DMA_MADDR   0x10
#define DMA_HSKIP   0x08
#define DMA_HCNT    0x0a
#define DMA_VSKIP   0x0c
#define DMA_VCNT    0x0e
#define DMA_DCMD    0x00
#define DMA_HDRP    0x28
#define DMA_DSIZE   0x02
#define DMA_CSIZE   0x06
#define DMA_VCNT    0x0e

#define DMA_BIF_BCMD	(DMA+0x120)	/* BIF receive command register	*/
#define	DMA_BIF_BRST	(DMA+0x124)	/* BIF receive status register */
#define	DMA_BCMD_SA		0x40000000	/* software abort   */
#define	DMA_DMST_AC		0x80000000	/* channel active   */
#define DMA_DMST_RST		0xffe40000  /* reset bits and reqs */
#define	DMA_DCMD_ST		0x80000000	/* start operation    */
#define	DMA_DCMD_TYP_AUTO	0x30000000	/* 11: auto  */

#define	DMA_DCMD_TD_MD		0x04000000	/* transfer mem->dev */
#define	DMA_DCMD_TD_DM		0x00000000	/* transfer direction dev->mem*/

#define DMA_CH2    (DMA+0x80)	/* DMA channel 2	*/
#define DMA_CH3    (DMA+0xc0)	/* DMA channel 3	*/
#define DMA2_DMST  (DMA_CH2+0x04)		/* DMA2 status register	  */
#define DMA3_DMST  (DMA_CH3+0x04)		/* DMA3 status register	  */
#define DMA2_DCMD   (DMA_CH2+0x00)		/* DMA2 command register  */

#define DMA_INTR_NORMAL_SH     19               /* normal DMA interrupt */
#define DMA_INTR_ERROR_SH      16               /* error DMA interrupt */

#define	DMA_DCMD_SA		0x40000000	/* software abort     */


#define DMA_MAX_TRANS_SIZE		(0xffff<<2)
#define DMA_TRANS_BLOCK_SIZE	(64<<2)

#define WORD_SIZE 4
#define B2W(x)    (((x) + WORD_SIZE - 1) / WORD_SIZE)
#define W2B(x)    ((x) * WORD_SIZE)

#define DMA_GEN		0xfff00180		/* DMA general control reg */

/* AP1000+ Message Controller (MSC+) */

#define MSC_BASE0 0xfa008000

#define MSC_SQCTRL	(MSC_BASE0 + 0x0)	/* Send Queue control */

/* bits in MSC_SQCTRL */
#define MSC_SQC_STABLE	0x400			/* Send Queue stable */
#define MSC_SQC_MODE	0x300			/* Send Queue mode: */
#define MSC_SQC_MODE_BLOCK	0			/* blocking */
#define MSC_SQC_MODE_THRU	0x100			/* through */
#define MSC_SQC_MODE_NORMAL	0x200			/* or normal */
#define MSC_SQC_SPLF_SH		3		/* bit# for spill flags */
#define MSC_SQC_SPLF_M		0x1f		/* 5 bits wide */
#define MSC_SQC_REPLYF	0x080			/* Reply queue full */
#define MSC_SQC_REMRF	0x040			/* Remote reply queue full */
#define MSC_SQC_USERF	0x020			/* User queue full */
#define MSC_SQC_REMAF	0x010			/* Remote access queue full */
#define MSC_SQC_SYSF	0x008			/* System queue full */
#define MSC_SQC_PAUSE	0x004			/* Send Queue pause */
#define MSC_SQC_RMODE	0x003			/* Requested mode: */
#define MSC_SQC_RMODE_BLOCK	0			/* blocking */
#define MSC_SQC_RMODE_THRU	1			/* through */
#define MSC_SQC_RMODE_NORMAL	2			/* or normal */

#define MSC_SQPTR0	(MSC_BASE0 + 0x8)	/* Send Queue 0 pointers */
#define MSC_SQPTR1	(MSC_BASE0 + 0x10)	/* Send Queue 1 pointers */
#define MSC_SQPTR2	(MSC_BASE0 + 0x18)	/* Send Queue 2 pointers */
#define MSC_SQPTR3	(MSC_BASE0 + 0x20)	/* Send Queue 3 pointers */
#define MSC_SQPTR4	(MSC_BASE0 + 0x28)	/* Send Queue 4 pointers */

/* bits in MSC_SQPTR[0-4] */
#define MSC_SQP_MODE	(1 << 20)		/* 64/32 word queue mode */
#define MSC_SQP_BP_SH	17			/* bit no. for base ptr */
#define MSC_SQP_BP_M	7			/* (it's 3 bits wide) */
#define MSC_SQP_CNT_SH	12			/* bit no. for count */
#define MSC_SQP_CNT_M	0x1f			/* (it's 5 bits wide) */
#define MSC_SQP_RP_SH	6			/* bit no. for read ptr */
#define MSC_SQP_RP_M	0x3f			/* (it's 6 bits wide() */
#define MSC_SQP_WP_SH	0			/* bit no. for write ptr */
#define MSC_SQP_WP_M	0x3f			/* (it's 6 bits wide() */

#define MSC_OPTADR	(MSC_BASE0 + 0x30)	/* option memory address */

#define MSC_MASCTRL	(MSC_BASE0 + 0x38)	/* Mem Access Sequencer ctrl */

/* Bits in MSC_MASCTRL */
#define MSC_MASC_SPAUSE	0x80			/* Send MAS pause */
#define MSC_MASC_RPAUSE	0x40			/* Recv MAS pause */
#define MSC_MASC_SFEXIT	0x20			/* Send MAS fault/exit */
#define MSC_MASC_RFEXIT	0x10			/* Recv MAS fault/exit */
#define MSC_MASC_SREADY	0x08			/* Send MAS ready */
#define MSC_MASC_RREADY	0x04			/* Recv MAS ready */
#define MSC_MASC_SSTOP	0x02			/* Send MAS is stopped */
#define MSC_MASC_RSTOP	0x01			/* Recv MAS is stopped */

#define MSC_SMASADR	(MSC_BASE0 + 0x40)	/* Send Mem Acc Seq address */
#define MSC_RMASADR	(MSC_BASE0 + 0x48)	/* Recv Mem Acc Seq address */

#define MSC_PID		(MSC_BASE0 + 0x50)	/* Context number (proc id) */

#define MSC_QWORDCNT	(MSC_BASE0 + 0x60)	/* Queue word counts */

/* Fields in MSC_QWORDCNT */
#define MSC_QWDC_SYSCNT_SH	24		/* bit# for system count */
#define MSC_QWDC_SYSCNT_M	0x3f		/* 6 bits wide */
#define MSC_QWDC_SYSLEN_SH	16		/* bit# for len of sys cmd */
#define MSC_QWDC_SYSLEN_M	0x3f		/* 6 bits wide */
#define MSC_QWDC_USRCNT_SH	8		/* bit# for user count */
#define MSC_QWDC_USRCNT_M	0x3f		/* 6 bits wide */
#define MSC_QWDC_USRLEN_SH	0		/* bit# for len of user cmd */
#define MSC_QWDC_USRLEN_M	0x3f		/* 6 bits wide */

#define MSC_INTR	(MSC_BASE0 + 0x70)	/* Interrupt control/status */

/* Bit offsets of interrupt fields in MSC_INTR */
#define MSC_INTR_QBMFUL_SH	28		/* Queue buffer full intr */
#define MSC_INTR_SQFILL_SH	24		/* Send queue fill intr */
#define MSC_INTR_RBMISS_SH	20		/* Ring buffer miss intr */
#define MSC_INTR_RBFULL_SH	16		/* Ring buffer full intr */
#define MSC_INTR_RMASF_SH	12		/* Recv MAS fault intr */
#define MSC_INTR_RMASE_SH	8		/* Recv MAS error intr */
#define MSC_INTR_SMASF_SH	4		/* Send MAS fault intr */
#define MSC_INTR_SMASE_SH	0		/* Send MAS error intr */

#define MSC_PPIO	(MSC_BASE0 + 0x1000)	/* PArallel port I/O */
#define MSC_PACSELECT	(MSC_BASE0 + 0x1008)	/* Performance analyser sel. */

#define MSC_CIDRANGE	(MSC_BASE0 + 0x1010)	/* Rel. Cell-id range limits */

/* Fields in MSC_CIDRANGE */
#define MSC_CIDR_LRX_SH		24		/* Rel. X lower limit bit# */
#define MSC_CIDR_LRX_M		0xFF		/* it's 8 bits wide */
#define MSC_CIDR_HRX_SH		16		/* Rel. X upper limit bit# */
#define MSC_CIDR_HRX_M		0xFF		/* it's 8 bits wide */
#define MSC_CIDR_LRY_SH		8		/* Rel. Y lower limit bit# */
#define MSC_CIDR_LRY_M		0xFF		/* it's 8 bits wide */
#define MSC_CIDR_HRY_SH		0		/* Rel. Y upper limit bit# */
#define MSC_CIDR_HRY_M		0xFF		/* it's 8 bits wide */

#define MSC_QBMPTR	(MSC_BASE0 + 0x1018)	/* Queue buffer mgr. ptrs */

/* Fields in MSC_QBMPTR */
#define MSC_QBMP_LIM_SH		24		/* Pointer limit bit# */
#define MSC_QBMP_LIM_M		0x3F		/* (6 bits wide) */
#define MSC_QBMP_BP_SH		16		/* Base pointer bit# */
#define MSC_QBMP_BP_M		0xFF		/* (8 bits wide) */
#define MSC_QBMP_WP_SH		0		/* Write pointer bit# */
#define MSC_QBMP_WP_M		0xFFFF		/* (16 bits wide) */

#define MSC_SMASTWP	(MSC_BASE0 + 0x1030)	/* Send MAS virt page etc. */
#define MSC_SMASREG	(MSC_BASE0 + 0x1038)	/* Send MAS context etc. */
#define MSC_RMASTWP	(MSC_BASE0 + 0x1040)	/* Recv MAS virt page etc. */
#define MSC_RMASREG	(MSC_BASE0 + 0x1048)	/* Recv MAS context etc. */

/* Bits in MSC_[SR]MASREG */
#define MSC_MASR_CONTEXT_SH	20		/* Context at bit 20 */
#define MSC_MASR_CONTEXT_M	0xfff		/* 12 bits wide */
#define MSC_MASR_AVIO		8		/* Address violation bit */
#define MSC_MASR_CMD		7		/* MAS command bits */
#define MSC_MASR_CMD_XFER	0		/* transfer data cmd */
#define MSC_MASR_CMD_FOP	5		/* fetch & operate cmd */
#define MSC_MASR_CMD_INC	6		/* increment cmd (i.e. flag) */
#define MSC_MASR_CMD_CSI	7		/* compare & swap cmd */

#define MSC_HDGERRPROC	(MSC_BASE0 + 0x1050)	/* Header gen. error process */
#define MSC_RHDERRPROC	(MSC_BASE0 + 0x1058)	/* Recv. header decoder err. */

#define MSC_SMASCNT	(MSC_BASE0 + 0x1060)	/* Send MAS counters */

/* Bits in MSC_SMASCNT */
#define MSC_SMCT_ACCSZ_SH	28		/* Access size at bit 28 */
#define MSC_SMCT_ACCSZ_M	7		/* 3 bits wide */
#define MSC_SMCT_MCNT_SH	8		/* M(?) count at bit 8 */
#define MSC_SMCT_MCNT_M		0xfffff		/* 20 bits wide */
#define MSC_SMCT_ICNT_SH	0		/* I(?) count at bit 0 */
#define MSC_SMCT_ICNT_M		0xff		/* 8 bits wide */

#define MSC_IRL		(MSC_BASE0 + 0x1070)	/* highest current int req */
#define MSC_SIMMCHK	(MSC_BASE0 + 0x1078)	/* DRAM type installed */

#define MSC_SIMMCHK_MASK 0x00000008

#define MSC_SQRAM	(MSC_BASE0 + 0x2000)	/* Send Queue RAM (to +23f8) */

#define MSC_VERSION	(MSC_BASE0 + 0x3000)	/* MSC+ version */

#define MSC_NR_RBUFS 3

#define MSC_RBMBWP0	(MSC_BASE0 + 0x4000)	/* Ring buf 0 base/write ptr */
#define MSC_RBMMODE0	(MSC_BASE0 + 0x4008)	/* Ring buf 0 mode/context */
#define MSC_RBMBWP1	(MSC_BASE0 + 0x4010)	/* Ring buf 1 base/write ptr */
#define MSC_RBMMODE1	(MSC_BASE0 + 0x4018)	/* Ring buf 1 mode/context */
#define MSC_RBMBWP2	(MSC_BASE0 + 0x4020)	/* Ring buf 2 base/write ptr */
#define MSC_RBMMODE2	(MSC_BASE0 + 0x4028)	/* Ring buf 2 mode/context */

#define MSC_RBMRP0	(MSC_BASE0 + 0x5000)	/* Ring buf 0 read pointer */
#define MSC_RBMRP1	(MSC_BASE0 + 0x6000)	/* Ring buf 1 read pointer */
#define MSC_RBMRP2	(MSC_BASE0 + 0x7000)	/* Ring buf 2 read pointer */

/* locations of queues in virtual memory */
#define MSC_QUEUE_BASE   0xfa800000
#define MSC_PUT_QUEUE_S  (MSC_QUEUE_BASE + 0*PAGE_SIZE)
#define MSC_GET_QUEUE_S  (MSC_QUEUE_BASE + 1*PAGE_SIZE)
#define MSC_XYG_QUEUE_S  (MSC_QUEUE_BASE + 2*PAGE_SIZE)
#define MSC_SEND_QUEUE_S  (MSC_QUEUE_BASE + 3*PAGE_SIZE)
#define MSC_CPUT_QUEUE_S  (MSC_QUEUE_BASE + 4*PAGE_SIZE)
#define MSC_BSEND_QUEUE_S  (MSC_QUEUE_BASE + 5*PAGE_SIZE)
#define MSC_CXYG_QUEUE_S  (MSC_QUEUE_BASE + 6*PAGE_SIZE)
#define MSC_CGET_QUEUE_S  (MSC_QUEUE_BASE + 7*PAGE_SIZE)

/* the 4 interrupt ports - physical addresses (on bus 8) */
#define MC_INTP_0        0x80004000
#define MC_INTP_1        0x80005000
#define MC_INTP_2        0x80006000
#define MC_INTP_3        0x80007000

/* the address used to send a remote signal - note that 32 pages 
 are used here - none of them are mapped to anything though */
#define MSC_REM_SIGNAL          (MSC_QUEUE_BASE + 0x10 * PAGE_SIZE)

#define MSC_PUT_QUEUE   (MSC_QUEUE_BASE + 0x100*PAGE_SIZE)
#define MSC_GET_QUEUE   (MSC_QUEUE_BASE + 0x101*PAGE_SIZE)
#define MSC_SEND_QUEUE  (MSC_QUEUE_BASE + 0x102*PAGE_SIZE)
#define MSC_XY_QUEUE    (MSC_QUEUE_BASE + 0x103*PAGE_SIZE)
#define MSC_X_QUEUE     (MSC_QUEUE_BASE + 0x104*PAGE_SIZE)
#define MSC_Y_QUEUE     (MSC_QUEUE_BASE + 0x105*PAGE_SIZE)
#define MSC_XYG_QUEUE   (MSC_QUEUE_BASE + 0x106*PAGE_SIZE)
#define MSC_XG_QUEUE    (MSC_QUEUE_BASE + 0x107*PAGE_SIZE)
#define MSC_YG_QUEUE    (MSC_QUEUE_BASE + 0x108*PAGE_SIZE)
#define MSC_CSI_QUEUE   (MSC_QUEUE_BASE + 0x109*PAGE_SIZE)
#define MSC_FOP_QUEUE   (MSC_QUEUE_BASE + 0x10a*PAGE_SIZE)

#define SYSTEM_RINGBUF_BASE    (MSC_QUEUE_BASE + 0x200*PAGE_SIZE)
#define SYSTEM_RINGBUF_ORDER 5
#define SYSTEM_RINGBUF_SIZE ((1<<SYSTEM_RINGBUF_ORDER)*PAGE_SIZE)

#define MSC_SYSTEM_DIRECT	(MSC_QUEUE_BASE + 0x700 * PAGE_SIZE)
#define MSC_USER_DIRECT		(MSC_QUEUE_BASE + 0x701 * PAGE_SIZE)
#define MSC_REMOTE_DIRECT	(MSC_QUEUE_BASE + 0x702 * PAGE_SIZE)
#define MSC_REPLY_DIRECT	(MSC_QUEUE_BASE + 0x703 * PAGE_SIZE)
#define MSC_REMREPLY_DIRECT	(MSC_QUEUE_BASE + 0x704 * PAGE_SIZE)

#define MSC_SYSTEM_DIRECT_END	(MSC_QUEUE_BASE + 0x708 * PAGE_SIZE)
#define MSC_USER_DIRECT_END	(MSC_QUEUE_BASE + 0x709 * PAGE_SIZE)
#define MSC_REMOTE_DIRECT_END	(MSC_QUEUE_BASE + 0x70a * PAGE_SIZE)
#define MSC_REPLY_DIRECT_END	(MSC_QUEUE_BASE + 0x70b * PAGE_SIZE)
#define MSC_REMREPLY_DIRECT_END	(MSC_QUEUE_BASE + 0x70c * PAGE_SIZE)

/* AP1000+ Memory Controller (MC+) */

#define MC_BASE0	0xfa000000

#define MC_DRAM_CTRL	(MC_BASE0 + 0x0)	/* DRAM control */
#define MC_DRAM_CHKBIT	(MC_BASE0 + 0x8)	/* DRAM check bits */
#define MC_DRAM_ERRADR	(MC_BASE0 + 0x10)	/* DRAM error address */
#define MC_DRAM_ERRSYN	(MC_BASE0 + 0x18)	/* DRAM error syndrome */

#define MC_FREERUN	(MC_BASE0 + 0x20)	/* Free run ctr (12.5MHz) */
#define MC_ITIMER0	(MC_BASE0 + 0x28)	/* Interval timer 0 */
#define MC_ITIMER1	(MC_BASE0 + 0x30)	/* Interval timer 1 */

#define MC_INTR		(MC_BASE0 + 0x38)	/* Interrupt control/status */

/* Interrupt control/status fields in MC_INTR */
#define MC_INTR_ECCD_SH		12		/* ECC double (uncorr.) err */
#define MC_INTR_ECCS_SH		8		/* ECC single (corr.) error */
#define MC_INTR_ITIM1_SH	4		/* Interval timer 1 intr */
#define MC_INTR_ITIM0_SH	0		/* Interval timer 0 intr */

#define MC_CTP		(MC_BASE0 + 0x50)	/* Context table pointer */

#define MC_VBUS_FAST	(MC_BASE0 + 0x60)	/* VBus fast data mode ctrl */

#define MC_INTR_PORT	(MC_BASE0 + 0x68)	/* Interrupt port ctrl/stat */

/* Interrupt control/status fields in MC_INTR_PORT */
#define MC_INTP_3_SH		12		/* port 0 (880007000) */
#define MC_INTP_2_SH		8		/* port 1 (880006000) */
#define MC_INTP_1_SH		4		/* port 1 (880005000) */
#define MC_INTP_0_SH		0		/* port 1 (880004000) */

#define MC_PAC_COUNT	(MC_BASE0 + 0x1000)	/* Perf. an. counters */
#define MC_PAC_SELECT	(MC_BASE0 + 0x1008)	/* Perf. an. ctr. select */

#define MC_VERSION	(MC_BASE0 + 0x3000)	/* MC+ version/date */

#define MC_MMU_TLB4K	(MC_BASE0 + 0x6000)	/* MC+ TLB for 4k pages */
#define MC_MMU_TLB256K	(MC_BASE0 + 0x7000)	/* MC+ TLB for 256k pages */
#define MC_MMU_TLB4K_SIZE 256
#define MC_MMU_TLB256K_SIZE 64


/*
 * Bit values for a standard AP1000 3-bit interrupt control/status field.
 */
#define AP_INTR_REQ		1	/* interrupt request bit */
#define AP_INTR_MASK		2	/* interrupt mask bit (1 disables) */
#define AP_INTR_WENABLE		4	/* enable write to mask/req bits */
#define AP_CLR_INTR_REQ		4	/* clear req. bit (dismiss intr) */
#define AP_CLR_INTR_MASK	5	/* clear mask bit (enable ints) */
#define AP_SET_INTR_REQ		6	/* set request bit */
#define AP_SET_INTR_MASK	7	/* set mask bit (disable ints) */

/*
 * Bit field extraction/insertion macros.
 */
#define EXTFIELD(val, fld)	(((val) >> fld ## _SH) & fld ## _M)
#define MKFIELD(val, fld)	(((val) & fld ## _M) << fld ## _SH)
#define INSFIELD(dst, val, fld)	(((dst) & ~(fld ## _M << fld ## _SH)) \
				 | MKFIELD(val, fld))

/* 
 * RTC registers 
 */
#define RTC 0xfff10000	/*	RTC system mode	*/
#define RTC_CSR 	(RTC+0x0010)	/* RTC control	register   */
#define RTC_STR   	(RTC+0x0020)	/* RTC status register	  */
#define RTC_ITRR   	(RTC+0x0030)	/* RTC interrupt register  */
#define RTC_RSTR   	(RTC+0x0070)	/* RTC reset register	   */	 
#define RTC_RSTR_TR		0x00008000	/* RTC through mode */
#define RTC_RSTR_TS		0x00004000	/* RTC test mode    */
#define RTC_RSTR_ED		0x00002000	/* RTC reverse mode    */
#define RTC_RSTR_AC		0x00001000	/* RTC long mode	*/
#define RTC_RSTR_SN		0x00000800	/* SOUTH/NORTH direction */
#define RTC_RSTR_EW		0x00000400	/* EAST/WEST direction	 */
#define RTC_RSTR_NC		0x00000200	/* get NORTH channel   */
#define RTC_RSTR_SC		0x00000100	/* get SOUTH channel	*/
#define RTC_RSTR_WC		0x00000080	/* get WEST channel	*/
#define RTC_RSTR_EC		0x00000040	/* get EAST channel	*/
#define RTC_RSTR_BM		0x00000020	/* broad also my cell	*/
#define RTC_RSTR_RT		0x00000020	/* reset  		*/


#define RTC_ITRR_PA		0x00040000	/* parity error for LBUS */
#define RTC_ITRR_LR		0x00020000	/* MSC read but FIFO is empty*/
#define RTC_ITRR_LW		0x00010000	/* MSC write but FIFO is full*/
#define RTC_ITRR_AL		0x00008000	/* specify end data in data transfer */
#define RTC_ITRR_DN		0x00002000	/* parity error in NORTH channel  */
#define RTC_ITRR_DS		0x00001000	/* parity error in SOUTH channel		*/
#define RTC_ITRR_DW		0x00000800	/* parity error in WEST channel			*/
#define RTC_ITRR_DE		0x00000400	/* parity error in EAST channel			*/
#define RTC_ITRR_BD		0x00000200	/* receive 2 kind of broad data			*/
#define RTC_ITRR_EW		0x00000100	/* control to write error bits			*/
#define RTC_ITRR_EM		0x00000080	/* mask error interrupt request			*/
#define RTC_ITRR_ER		0x00000040	/* error interrput request				*/
#define RTC_ITRR_SW		0x00000020	/* control to write SR, SM  */
#define RTC_ITRR_SM		0x00000010	/* mask send interrupt					*/
#define RTC_ITRR_SR		0x00000008	/* send interrupt request				*/
#define RTC_ITRR_RW		0x00000004	/* icontrol to read RR, RM				*/
#define RTC_ITRR_RM		0x00000002	/* mask read interrupt					*/
#define RTC_ITRR_RR		0x00000001	/* receive interrupt request			*/

#define RTC_ITRR_RWM (RTC_ITRR_RW|RTC_ITRR_RM)
#define RTC_ITRR_SWM (RTC_ITRR_SW|RTC_ITRR_SM)
#define RTC_ITRR_EWM (RTC_ITRR_EW|RTC_ITRR_EM)
#define RTC_ITRR_RWR (RTC_ITRR_RW|RTC_ITRR_RR)
#define RTC_ITRR_SWR (RTC_ITRR_SW|RTC_ITRR_SR)
#define RTC_ITRR_EWR (RTC_ITRR_EW|RTC_ITRR_ER)
#define RTC_ITRR_RRM (RTC_ITRR_RM|RTC_ITRR_RR)
#define RTC_ITRR_SRM (RTC_ITRR_SM|RTC_ITRR_SR)
#define RTC_ITRR_ERM (RTC_ITRR_EM|RTC_ITRR_ER)
#define RTC_ITRR_RWMR   (RTC_ITRR_RW|RTC_ITRR_RM|RTC_ITRR_RR)
#define RTC_ITRR_SWMR   (RTC_ITRR_SW|RTC_ITRR_SM|RTC_ITRR_SR)
#define RTC_ITRR_EWMR   (RTC_ITRR_EW|RTC_ITRR_EM|RTC_ITRR_ER)

#define RTC_ITRR_ALLMSK (RTC_ITRR_RWM|RTC_ITRR_SWM|RTC_ITRR_EWM)
#define RTC_ITRR_ALLCLR (RTC_ITRR_RW|RTC_ITRR_SW|RTC_ITRR_EW)
#define RTC_ITRR_ALLWR  (RTC_ITRR_RWMR|RTC_ITRR_SWMR|RTC_ITRR_EWMR)
#define RTC_ITRR_ALLRD  (RTC_ITRR_RRM|RTC_ITRR_SRM|RTC_ITRR_ERM)


/*
 * macros to manipulate context/task/pid numbers for parallel programs
 */
#define MPP_CONTEXT_BASE (AP_NUM_CONTEXTS - (NR_TASKS - MPP_TASK_BASE))
#define MPP_TASK_TO_CTX(taskid) (((taskid) - MPP_TASK_BASE)+MPP_CONTEXT_BASE)
#define MPP_CTX_TO_TASK(ctx) (((ctx)-MPP_CONTEXT_BASE)+MPP_TASK_BASE)
#define MPP_IS_PAR_TASK(taskid) ((taskid) >= MPP_TASK_BASE)
#define MPP_IS_PAR_CTX(ctx) ((ctx) >= MPP_CONTEXT_BASE)


/*
 * ioctls available on the ring buffer
 */
#define CAP_GETINIT 1
#define CAP_SYNC    2
#define CAP_SETTASK 3
#define CAP_SETGANG 4
#define CAP_MAP     5

/*
 * the structure shared by the kernel and the parallel tasks in the
 * front of the cap_shared area
 */
#ifndef _ASM_
#ifdef _APLIB_
struct _kernel_cap_shared {
	unsigned rbuf_read_ptr;
	unsigned dummy[32]; /* for future expansion */
};
#endif
#endif

/*
 * the mmap'd ringbuffer region is layed out like this:

 shared page - one page
 queue pages - 11 pages
 ring buffer - xx pages
 mirror of ring buffer - xx pages
 */
#define RBUF_VBASE 0xd0000000
#define RBUF_SHARED_PAGE_OFF 0
#define RBUF_PUT_QUEUE             PAGE_SIZE
#define RBUF_GET_QUEUE           2*PAGE_SIZE
#define RBUF_SEND_QUEUE          3*PAGE_SIZE
#define RBUF_XY_QUEUE            4*PAGE_SIZE
#define RBUF_X_QUEUE             5*PAGE_SIZE
#define RBUF_Y_QUEUE             6*PAGE_SIZE
#define RBUF_XYG_QUEUE           7*PAGE_SIZE
#define RBUF_XG_QUEUE            8*PAGE_SIZE
#define RBUF_YG_QUEUE            9*PAGE_SIZE
#define RBUF_CSI_QUEUE          10*PAGE_SIZE
#define RBUF_FOP_QUEUE          11*PAGE_SIZE
#define RBUF_RING_BUFFER_OFFSET 15*PAGE_SIZE


/*
 * number of MMU contexts to use
 */
#define AP_NUM_CONTEXTS 1024
#define SYSTEM_CONTEXT 1

/*
 * the default gang scheduling factor
*/
#define DEF_GANG_FACTOR 15

/*
 * useful for bypassing the cache
*/
#ifdef _APLIB_
#ifndef _ASM_
static inline unsigned long phys_8_in(unsigned long paddr)
{
	unsigned long word;
	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (word) :
			     "r" (paddr), "i" (0x28) :
			     "memory");
	return word;
}

/*
 * useful for bypassing the cache
*/
static inline unsigned long phys_9_in(unsigned long paddr)
{
	unsigned long word;
	__asm__ __volatile__("lda [%1] %2, %0\n\t" :
			     "=r" (word) :
			     "r" (paddr), "i" (0x29) :
			     "memory");
	return word;
}
#endif
#endif

/*
 * DDV definitions 
*/
#define OBASE        (0xfff40010)
#define OPTION_BASE 0xfc000000
#define _OPIBUS_BASE (OPTION_BASE + 0x800000)
#ifdef CAP2_OPTION
#define OPIBUS_BASE 0
#else
#define OPIBUS_BASE _OPIBUS_BASE
#endif
#define PBUF0        (OPIBUS_BASE+0x7e0080)
#define PBUF1        (OPIBUS_BASE+0x7e0084)
#define PBUF2        (OPIBUS_BASE+0x7e0088)
#define PBUF3        (OPIBUS_BASE+0x7e008c)
#define PIRQ         (OPIBUS_BASE+0x7e0090)
#define PRST         (OPIBUS_BASE+0x7e0094)

#define IRC0         (OPIBUS_BASE+0x7d00a0)
#define IRC1         (OPIBUS_BASE+0x7d00a4)

#define PRST_IRST    (0x00000001)

#define OPIU_RESET			(0x00000000)
#define OPIU_OP				(PBUF0)

#define LSTR(s) (_OPIBUS_BASE + (s))

#endif /* _AP1000_APREG_H */

