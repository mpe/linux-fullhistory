#ifndef __ASM_SH_IRQ_H
#define __ASM_SH_IRQ_H

/*
 *
 * linux/include/asm-sh/irq.h
 *
 * Copyright (C) 1999  Niibe Yutaka & Takeshi Yaegashi
 * Copyright (C) 2000  Kazumoto Kojima
 * Copyright (C) 2003  Paul Mundt
 *
 */

#include <linux/config.h>
#include <asm/machvec.h>
#include <asm/ptrace.h>		/* for pt_regs */

#if defined(CONFIG_CPU_SH3)
#define INTC_IPRA  	0xfffffee2UL
#define INTC_IPRB  	0xfffffee4UL
#elif defined(CONFIG_CPU_SH4)
#define INTC_IPRA	0xffd00004UL
#define INTC_IPRB	0xffd00008UL
#define INTC_IPRC	0xffd0000cUL
#define INTC_IPRD	0xffd00010UL
#endif

#define TIMER_IRQ	16
#define TIMER_IPR_ADDR	INTC_IPRA
#define TIMER_IPR_POS	 3
#define TIMER_PRIORITY	 2

#define RTC_IRQ		22
#define RTC_IPR_ADDR	INTC_IPRA
#define RTC_IPR_POS	 0
#define RTC_PRIORITY	TIMER_PRIORITY

#if defined(CONFIG_CPU_SH3)
#define DMTE0_IRQ	48
#define DMTE1_IRQ	49
#define DMTE2_IRQ	50
#define DMTE3_IRQ	51
#define DMA_IPR_ADDR	INTC_IPRE
#define DMA_IPR_POS	3
#define DMA_PRIORITY	7
#elif defined(CONFIG_CPU_SH4)
#define DMTE0_IRQ	34
#define DMTE1_IRQ	35
#define DMTE2_IRQ	36
#define DMTE3_IRQ	37
#define DMTE4_IRQ	44	/* 7751R only */
#define DMTE5_IRQ	45	/* 7751R only */
#define DMTE6_IRQ	46	/* 7751R only */
#define DMTE7_IRQ	47	/* 7751R only */
#define DMAE_IRQ	38
#define DMA_IPR_ADDR	INTC_IPRC
#define DMA_IPR_POS	2
#define DMA_PRIORITY	7
#endif

#if defined (CONFIG_CPU_SUBTYPE_SH7707) || defined (CONFIG_CPU_SUBTYPE_SH7708) || \
    defined (CONFIG_CPU_SUBTYPE_SH7709) || defined (CONFIG_CPU_SUBTYPE_SH7750) || \
    defined (CONFIG_CPU_SUBTYPE_SH7751)
#define SCI_ERI_IRQ	23
#define SCI_RXI_IRQ	24
#define SCI_TXI_IRQ	25
#define SCI_IPR_ADDR	INTC_IPRB
#define SCI_IPR_POS	1
#define SCI_PRIORITY	3
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7707) || defined(CONFIG_CPU_SUBTYPE_SH7709)
#define SCIF_ERI_IRQ	56
#define SCIF_RXI_IRQ	57
#define SCIF_BRI_IRQ	58
#define SCIF_TXI_IRQ	59
#define SCIF_IPR_ADDR	INTC_IPRE
#define SCIF_IPR_POS	1
#define SCIF_PRIORITY	3

#define IRDA_ERI_IRQ	52
#define IRDA_RXI_IRQ	53
#define IRDA_BRI_IRQ	54
#define IRDA_TXI_IRQ	55
#define IRDA_IPR_ADDR	INTC_IPRE
#define IRDA_IPR_POS	2
#define IRDA_PRIORITY	3
#elif defined(CONFIG_CPU_SUBTYPE_SH7750) || defined(CONFIG_CPU_SUBTYPE_SH7751) || \
      defined(CONFIG_CPU_SUBTYPE_ST40STB1)
#define SCIF_ERI_IRQ	40
#define SCIF_RXI_IRQ	41
#define SCIF_BRI_IRQ	42
#define SCIF_TXI_IRQ	43
#define SCIF_IPR_ADDR	INTC_IPRC
#define SCIF_IPR_POS	1
#define SCIF_PRIORITY	3
#if defined(CONFIG_CPU_SUBTYPE_ST40STB1)
#define SCIF1_ERI_IRQ	23
#define SCIF1_RXI_IRQ	24
#define SCIF1_BRI_IRQ	25
#define SCIF1_TXI_IRQ	26
#define SCIF1_IPR_ADDR	INTC_IPRB
#define SCIF1_IPR_POS	1
#define SCIF1_PRIORITY	3
#endif
#endif

/* NR_IRQS is made from three components:
 *   1. ONCHIP_NR_IRQS - number of IRLS + on-chip peripherial modules
 *   2. PINT_NR_IRQS   - number of PINT interrupts
 *   3. OFFCHIP_NR_IRQS - numbe of IRQs from off-chip peripherial modules
 */

/* 1. ONCHIP_NR_IRQS */
#ifdef CONFIG_SH_GENERIC
# define ONCHIP_NR_IRQS 144
#else
# if defined(CONFIG_CPU_SUBTYPE_SH7604)
#  define ONCHIP_NR_IRQS 24	// Actually 21
# elif defined(CONFIG_CPU_SUBTYPE_SH7707)
#  define ONCHIP_NR_IRQS 64
#  define PINT_NR_IRQS   16
# elif defined(CONFIG_CPU_SUBTYPE_SH7708)
#  define ONCHIP_NR_IRQS 32
# elif defined(CONFIG_CPU_SUBTYPE_SH7709)
#  define ONCHIP_NR_IRQS 64	// Actually 61
#  define PINT_NR_IRQS   16
# elif defined(CONFIG_CPU_SUBTYPE_SH7750)
#  define ONCHIP_NR_IRQS 48	// Actually 44
# elif defined(CONFIG_CPU_SUBTYPE_SH7751)
#  define ONCHIP_NR_IRQS 72
# elif defined(CONFIG_CPU_SUBTYPE_SH7760)
#  define ONCHIP_NR_IRQS 110
# elif defined(CONFIG_CPU_SUBTYPE_ST40STB1)
#  define ONCHIP_NR_IRQS 144
# endif
#endif

/* 2. PINT_NR_IRQS */
#ifdef CONFIG_SH_GENERIC
# define PINT_NR_IRQS 16
#else
# ifndef PINT_NR_IRQS
#  define PINT_NR_IRQS 0
# endif
#endif

#if PINT_NR_IRQS > 0
# define PINT_IRQ_BASE  ONCHIP_NR_IRQS
#endif

/* 3. OFFCHIP_NR_IRQS */
#ifdef CONFIG_SH_GENERIC
# define OFFCHIP_NR_IRQS 16
#else
# if defined(CONFIG_HD64461)
#  define OFFCHIP_NR_IRQS 16
# elif defined (CONFIG_SH_BIGSUR) /* must be before CONFIG_HD64465 */
#  define OFFCHIP_NR_IRQS 48
# elif defined(CONFIG_HD64465)
#  define OFFCHIP_NR_IRQS 16
# elif defined (CONFIG_SH_EC3104)
#  define OFFCHIP_NR_IRQS 16
# elif defined (CONFIG_SH_DREAMCAST)
#  define OFFCHIP_NR_IRQS 96
# else
#  define OFFCHIP_NR_IRQS 0
# endif
#endif

#if OFFCHIP_NR_IRQS > 0
# define OFFCHIP_IRQ_BASE (ONCHIP_NR_IRQS + PINT_NR_IRQS)
#endif

/* NR_IRQS. 1+2+3 */
#define NR_IRQS (ONCHIP_NR_IRQS + PINT_NR_IRQS + OFFCHIP_NR_IRQS)

/* In a generic kernel, NR_IRQS is an upper bound, and we should use
 * ACTUAL_NR_IRQS (which uses the machine vector) to get the correct value.
 */
#ifdef CONFIG_SH_GENERIC
# define ACTUAL_NR_IRQS (sh_mv.mv_nr_irqs)
#else
# define ACTUAL_NR_IRQS NR_IRQS
#endif


extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);

/*
 * Simple Mask Register Support
 */
extern void make_maskreg_irq(unsigned int irq);
extern unsigned short *irq_mask_register;

/*
 * Function for "on chip support modules".
 */
extern void make_ipr_irq(unsigned int irq, unsigned int addr,
			 int pos,  int priority);
extern void make_imask_irq(unsigned int irq);

#if defined(CONFIG_CPU_SUBTYPE_SH7604)
#define INTC_IPRA	0xfffffee2UL
#define INTC_IPRB	0xfffffe60UL

#define INTC_VCRA	0xfffffe62UL
#define INTC_VCRB	0xfffffe64UL
#define INTC_VCRC	0xfffffe66UL
#define INTC_VCRD	0xfffffe68UL

#define INTC_VCRWDT	0xfffffee4UL
#define INTC_VCRDIV	0xffffff0cUL
#define INTC_VCRDMA0	0xffffffa0UL
#define INTC_VCRDMA1	0xffffffa8UL

#define INTC_ICR	0xfffffee0UL
#elif defined(CONFIG_CPU_SUBTYPE_SH7707) || defined(CONFIG_CPU_SUBTYPE_SH7709)
#define INTC_IRR0	0xa4000004UL
#define INTC_IRR1	0xa4000006UL
#define INTC_IRR2	0xa4000008UL

#define INTC_ICR0  	0xfffffee0UL
#define INTC_ICR1  	0xa4000010UL
#define INTC_ICR2  	0xa4000012UL
#define INTC_INTER 	0xa4000014UL

#define INTC_IPRC  	0xa4000016UL
#define INTC_IPRD  	0xa4000018UL
#define INTC_IPRE  	0xa400001aUL
#if defined(CONFIG_CPU_SUBTYPE_SH7707)
#define INTC_IPRF	0xa400001cUL
#endif

#define PORT_PACR	0xa4000100UL
#define PORT_PBCR	0xa4000102UL
#define PORT_PCCR	0xa4000104UL
#define PORT_PFCR	0xa400010aUL
#define PORT_PADR  	0xa4000120UL
#define PORT_PBDR  	0xa4000122UL
#define PORT_PCDR  	0xa4000124UL
#define PORT_PFDR  	0xa400012aUL

#define IRQ0_IRQ	32
#define IRQ1_IRQ	33
#define IRQ2_IRQ	34
#define IRQ3_IRQ	35
#define IRQ4_IRQ	36
#define IRQ5_IRQ	37

#define IRQ0_IPR_ADDR	INTC_IPRC
#define IRQ1_IPR_ADDR	INTC_IPRC
#define IRQ2_IPR_ADDR	INTC_IPRC
#define IRQ3_IPR_ADDR	INTC_IPRC
#define IRQ4_IPR_ADDR	INTC_IPRD
#define IRQ5_IPR_ADDR	INTC_IPRD

#define IRQ0_IPR_POS	0
#define IRQ1_IPR_POS	1
#define IRQ2_IPR_POS	2
#define IRQ3_IPR_POS	3
#define IRQ4_IPR_POS	0
#define IRQ5_IPR_POS	1

#define IRQ0_PRIORITY	1
#define IRQ1_PRIORITY	1
#define IRQ2_PRIORITY	1
#define IRQ3_PRIORITY	1
#define IRQ4_PRIORITY	1
#define IRQ5_PRIORITY	1

#define PINT0_IRQ	40
#define PINT8_IRQ	41

#define PINT0_IPR_ADDR	INTC_IPRD
#define PINT8_IPR_ADDR	INTC_IPRD

#define PINT0_IPR_POS	3
#define PINT8_IPR_POS	2
#define PINT0_PRIORITY	2
#define PINT8_PRIORITY	2

extern int ipr_irq_demux(int irq);
#define __irq_demux(irq) ipr_irq_demux(irq)

#else
#define __irq_demux(irq) irq
#endif /* CONFIG_CPU_SUBTYPE_SH7707 || CONFIG_CPU_SUBTYPE_SH7709 */

#if defined(CONFIG_CPU_SUBTYPE_SH7750) || defined(CONFIG_CPU_SUBTYPE_SH7751) || \
    defined(CONFIG_CPU_SUBTYPE_ST40STB1)
#define INTC_ICR        0xffd00000
#define INTC_ICR_NMIL	(1<<15)
#define INTC_ICR_MAI	(1<<14)
#define INTC_ICR_NMIB	(1<<9)
#define INTC_ICR_NMIE	(1<<8)
#define INTC_ICR_IRLM	(1<<7)
#endif

#ifdef CONFIG_CPU_SUBTYPE_ST40STB1
#define INTC2_FIRST_IRQ 64
#define NR_INTC2_IRQS 25
 
#define INTC2_BASE0 0xfe080000
#define INTC2_INTC2MODE  (INTC2_BASE0+0x80)
 
#define INTC2_INTPRI_OFFSET	0x00
#define INTC2_INTREQ_OFFSET	0x20
#define INTC2_INTMSK_OFFSET	0x40
#define INTC2_INTMSKCLR_OFFSET	0x60
 
extern void make_intc2_irq(unsigned int irq,unsigned int addr,
                           unsigned int group,int pos,int priority);
 
#endif                                                                        
       
static inline int generic_irq_demux(int irq)
{
	return irq;
}

#define irq_canonicalize(irq)	(irq)
#define irq_demux(irq)		__irq_demux(sh_mv.mv_irq_demux(irq))

#endif /* __ASM_SH_IRQ_H */
