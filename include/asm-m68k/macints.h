/*
** macints.h -- Macintosh Linux interrupt handling structs and prototypes
**
** Copyright 1997 by Michael Schmitz
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
*/

#ifndef _ASM_MACINTS_H_
#define _ASM_MACINTS_H_

#include <asm/irq.h>

/*
** Macintosh Interrupt sources.
**
** Note: these are all routed via the generic VIA interrupt routine!
**
*/

#define SRC_VIA1		0
#define SRC_VIA2		1

#define VIA1_SOURCE_BASE	8
#define VIA2_SOURCE_BASE	16
#define RBV_SOURCE_BASE		24
#define MAC_SCC_SOURCE_BASE	32
#define NUBUS_SOURCE_BASE	56
#define NUBUS_MAX_SOURCES	8

/* FIXME: sources not contigous ... */
#define NUM_MAC_SOURCES   (NUBUS_SOURCE_BASE+NUBUS_MAX_SOURCES-VIA1_SOURCE_BASE)

#define IRQ_SRC_MASK (VIA1_SOURCE_BASE|VIA2_SOURCE_BASE|MAC_SCC_SOURCE_BASE)
#define IRQ_IDX_MASK 7

/* 
 * quick hack to adapt old MACHSPEC-aware source
 */
#define	IRQ_IDX(irq)	(irq)

/* interrupt service types */
#define IRQ_TYPE_SLOW     0
#define IRQ_TYPE_FAST     1
#define IRQ_TYPE_PRIO     2

#define	IRQ_SPURIOUS      (0)

/* auto-vector interrupts */
#define IRQ_AUTO_1        (1)
#define IRQ_AUTO_2        (2)
#define IRQ_AUTO_3        (3)
#define IRQ_AUTO_4        (4)
#define IRQ_AUTO_5        (5)
#define IRQ_AUTO_6        (6)
#define IRQ_AUTO_7        (7)

/* VIA1 interrupts */
#define IRQ_VIA1_0	  (8)	/* one second int. */
#define IRQ_VIA1_1        (9)	/* VBlank int. */
#define IRQ_MAC_VBL	  IRQ_VIA1_1
#define IRQ_VIA1_2 	  (10)	/* ADB SR shifts complete */
#define IRQ_MAC_ADB	  IRQ_VIA1_2
#define IRQ_MAC_ADB_SR	  IRQ_VIA1_2
#define IRQ_VIA1_3	  (11)	/* ADB SR CB2 ?? */
#define IRQ_MAC_ADB_SD	  IRQ_VIA1_3
#define IRQ_VIA1_4        (12)	/* ADB SR ext. clock pulse */
#define IRQ_MAC_ADB_CL	  IRQ_VIA1_4
#define IRQ_VIA1_5	  (13)
#define IRQ_MAC_TIMER_2	  IRQ_VIA1_5
#define IRQ_VIA1_6	  (14)
#define IRQ_MAC_TIMER_1	  IRQ_VIA1_6
#define IRQ_VIA1_7        (15)

/* VIA2 interrupts */
#define IRQ_VIA2_0	  (16)
#define IRQ_MAC_SCSIDRQ	  IRQ_VIA2_0
#define IRQ_VIA2_1        (17)
#define IRQ_MAC_NUBUS	  IRQ_VIA2_1
#define IRQ_VIA2_2 	  (18)
#define IRQ_VIA2_3	  (19)
#define IRQ_MAC_SCSI	  IRQ_VIA2_3
#define IRQ_VIA2_4        (20)
#define IRQ_VIA2_5	  (21)
#define IRQ_VIA2_6	  (22)
#define IRQ_VIA2_7        (23)

#if 0
/* RBV interrupts */
#define IRQ_RBV_0	  (24)
#define IRQ_RBV_1	  (25)
#define IRQ_RBV_2 	  (26)
#define IRQ_RBV_3	  (27)
#define IRQ_RBV_4	  (28)
#define IRQ_RBV_5	  (29)
#define IRQ_RBV_6	  (30)
#define IRQ_RBV_7	  (31)
#endif

/* Level 3 (PSC, AV Macs only) interrupts */
#define IRQ_PSC3_0	  (24)
#define IRQ_MAC_MACE	  IRQ_PSC3_0
#define IRQ_PSC3_1	  (25)
#define IRQ_PSC3_2	  (26)
#define IRQ_PSC3_3	  (27)

/* Level 4 (SCC) interrupts */
#define IRQ_SCC 	     (32)
#define IRQ_SCCB	     (33)
#define IRQ_SCCA	     (34)
#if 0 /* FIXME: are there multiple interrupt conditions on the SCC ?? */
/* SCC interrupts */
#define IRQ_SCCB_TX	     (32)
#define IRQ_SCCB_STAT	     (33)
#define IRQ_SCCB_RX	     (34)
#define IRQ_SCCB_SPCOND	     (35)
#define IRQ_SCCA_TX	     (36)
#define IRQ_SCCA_STAT	     (37)
#define IRQ_SCCA_RX	     (38)
#define IRQ_SCCA_SPCOND	     (39)
#endif

/* Level 4 (PSC, AV Macs only) interrupts */
#define IRQ_PSC4_0	  (32)
#define IRQ_PSC4_1	  (33)
#define IRQ_PSC4_2	  (34)
#define IRQ_PSC4_3	  (35)
#define IRQ_MAC_MACE_DMA  IRQ_PSC4_3

/* Level 5 (PSC, AV Macs only) interrupts */
#define IRQ_PSC5_0	  (40)
#define IRQ_PSC5_1	  (41)
#define IRQ_PSC5_2	  (42)
#define IRQ_PSC5_3	  (43)

/* Level 6 (PSC, AV Macs only) interrupts */
#define IRQ_PSC6_0	  (48)
#define IRQ_PSC6_1	  (49)
#define IRQ_PSC6_2	  (50)
#define IRQ_PSC6_3	  (51)

/* Nubus interrupts (cascaded to VIA2) */
#define IRQ_NUBUS_1	  (56)

#define INT_CLK   24576	    /* CLK while int_clk =2.456MHz and divide = 100 */
#define INT_TICKS 246	    /* to make sched_time = 99.902... HZ */


#define VIA_ENABLE	0
#define VIA_PENDING	1
#define VIA_SERVICE	2
#define VIA_MASK	3

/* 
 * Utility functions for setting/clearing bits in the interrupt registers of
 * the VIA. 
 */

void mac_enable_irq( unsigned irq );
void mac_disable_irq( unsigned irq );
void mac_turnon_irq( unsigned irq );
void mac_turnoff_irq( unsigned irq );
void mac_clear_pending_irq( unsigned irq );
int  mac_irq_pending( unsigned irq );
int  nubus_request_irq(int slot, void *dev_id, void (*handler)(int,void *,struct pt_regs *));
int  nubus_free_irq(int slot);

unsigned long mac_register_nubus_int( void );
void mac_unregister_nubus_int( unsigned long );

extern void mac_default_handler(int irq, void *dev_id, struct pt_regs *regs);
extern void via1_irq(int irq, void *dev_id, struct pt_regs *regs);
extern void via2_irq(int irq, void *dev_id, struct pt_regs *regs);
extern void  rbv_irq(int irq, void *dev_id, struct pt_regs *regs);
extern void mac_bang(int irq, void *dev_id, struct pt_regs *regs);
		
extern void mac_SCC_handler(int irq, void *dev_id, struct pt_regs *regs);

#endif /* asm/macints.h */
