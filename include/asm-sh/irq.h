#ifndef __ASM_SH_IRQ_H
#define __ASM_SH_IRQ_H

/*
 *
 * linux/include/asm-sh/irq.h
 *
 * Copyright (C) 1999  Niibe Yutaka & Takeshi Yaegashi
 * Copyright (C) 2000  Kazumoto Kojima
 *
 */

#include <linux/config.h>

#if defined(__sh3__)
#define INTC_IPRA  	0xfffffee2UL
#define INTC_IPRB  	0xfffffee4UL
#elif defined(__SH4__)
#define INTC_IPRA	0xffd00004UL
#define INTC_IPRB	0xffd00008UL
#define INTC_IPRC	0xffd0000cUL
#endif

#define TIMER_IRQ	16
#define TIMER_IPR_ADDR	INTC_IPRA
#define TIMER_IPR_POS	 3
#define TIMER_PRIORITY	 2

#define RTC_IRQ		22
#define RTC_IPR_ADDR	INTC_IPRA
#define RTC_IPR_POS	 0
#define RTC_PRIORITY	TIMER_PRIORITY

#if defined(__SH4__)
/*
 * 48 = 32+16
 *
 * 32 for on chip support modules.
 * 16 for external interrupts.
 *
 */
#define NR_IRQS	48
#elif defined(CONFIG_CPU_SUBTYPE_SH7708)
#define NR_IRQS 32
#elif defined(CONFIG_CPU_SUBTYPE_SH7709)
#define NR_IRQS 61
#endif

extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);

/*
 * Function for "on chip support modules".
 */
extern void set_ipr_data(unsigned int irq, unsigned int addr,
			 int pos,  int priority);
extern void make_ipr_irq(unsigned int irq);
extern void make_imask_irq(unsigned int irq);

#if defined(CONFIG_CPU_SUBTYPE_SH7709)
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

#define IRQ0_IRQ	32
#define IRQ1_IRQ	33
#define IRQ2_IRQ	34
#define IRQ3_IRQ	35
#define IRQ4_IRQ	36
#define IRQ5_IRQ	37

#define IRQ0_IRP_ADDR	INTC_IPRC
#define IRQ1_IRP_ADDR	INTC_IPRC
#define IRQ2_IRP_ADDR	INTC_IPRC
#define IRQ3_IRP_ADDR	INTC_IPRC
#define IRQ4_IRP_ADDR	INTC_IPRD
#define IRQ5_IRP_ADDR	INTC_IPRD

#define IRQ0_IRP_POS	0
#define IRQ1_IRP_POS	1
#define IRQ2_IRP_POS	2
#define IRQ3_IRP_POS	3
#define IRQ4_IRP_POS	0
#define IRQ5_IRP_POS	1

#define IRQ0_PRIORITY	1
#define IRQ1_PRIORITY	1
#define IRQ2_PRIORITY	1
#define IRQ3_PRIORITY	1
#define IRQ4_PRIORITY	1
#define IRQ5_PRIORITY	1
#endif

#endif /* __ASM_SH_IRQ_H */
