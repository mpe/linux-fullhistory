/*
 * linux/include/asm-arm/arch-vnc/irqs.h
 *
 * Copyright (C) 1998 Russell King
 */

#define NR_IRQS			32

/*
 * This is a list of all interrupts that the 21285
 * can generate
 */
#define IRQ_SOFTIRQ		1	/* from FB.1 */
#define IRQ_CONRX		2	/* from FB.2 */
#define IRQ_CONTX		3	/* from FB.3 */
#define IRQ_TIMER0		4	/* from FB.4 */
#define IRQ_TIMER1		5	/* from FB.5 */
#define IRQ_TIMER2		6	/* from FB.6 */
#define IRQ_WATCHDOG		7	/* from FB.7 */
#define IRQ_ETHER10		8	/* from FB.8 */
#define IRQ_ETHER100		9	/* from FB.9 */
#define IRQ_VIDCOMP		10	/* from FB.10 */
#define IRQ_EXTERN_IRQ		11	/* from FB.11: chain to IDE irq's */
#define IRQ_DMA1		12	/* from future */
#define IRQ_PCI_ERR		15	/* from FB.[28:31] */

#define IRQ_TIMER4		16	/* from 553.0 */
#define IRQ_KEYBOARD		17	/* from 553.1 */
#define IRQ_PIC_HI		18	/* from 533.2: chained to 553.[8:15] */
#define IRQ_UART2		19	/* from 553.3 */
#define IRQ_UART		20	/* from 553.4 */
#define IRQ_MOUSE		21	/* from 553.5 */
#define IRQ_UART_IR		22	/* from 553.6 */
#define IRQ_PRINTER		23	/* from 553.7 */
#define IRQ_RTC_ALARM		24	/* from 553.8 */
#define IRQ_POWERLOW		26	/* from 553.10 */
#define IRQ_VGA			27	/* from 553.11 */
#define IRQ_SOUND		28	/* from 553.12 */
#define IRQ_HARDDISK		30	/* from 553.14 */

/* These defines handle the translation from the above FB #defines
 * into physical bits for the FootBridge IRQ registers
 */
#define IRQ_MASK_SOFTIRQ	0x00000002
#define IRQ_MASK_UART_DEBUG	0x0000000C
#define IRQ_MASK_TIMER0		0x00000010
#define IRQ_MASK_TIMER1		0x00000020
#define IRQ_MASK_TIMER2		0x00000040
#define IRQ_MASK_WATCHDOG	0x00000080
#define IRQ_MASK_ETHER10	0x00000100
#define IRQ_MASK_ETHER100	0x00000200
#define IRQ_MASK_VIDCOMP	0x00000400
#define IRQ_MASK_EXTERN_IRQ	0x00000800
#define IRQ_MASK_DMA1		0x00030000
#define IRQ_MASK_PCI_ERR	0xf8800000

/*
 * Now map them to the Linux interrupts
 */
#undef IRQ_TIMER
#define IRQ_TIMER		IRQ_TIMER0
#undef RTC_IRQ
#define RTC_IRQ			IRQ_RTC_ALARM
#undef AUX_IRQ
#define AUX_IRQ			IRQ_MOUSE

#define irq_cannonicalize(i)	(i)
