/*
 * linux/include/asm-arm/arch-ebsa285/irqs.h
 *
 * Copyright (C) 1998 Russell King
 * Copyright (C) 1998 Phil Blundell
 */

#define NR_IRQS			48

/*
 * This is a list of all interrupts that the 21285
 * can generate
 */
#define IRQ_RESERVED		0
#define IRQ_SOFTIRQ		1
#define IRQ_CONRX		2
#define IRQ_CONTX		3
#define IRQ_TIMER1		4
#define IRQ_TIMER2		5
#define IRQ_TIMER3		6
#define IRQ_TIMER4		7
#define IRQ_IN0			8
#define IRQ_IN1			9
#define IRQ_IN2			10
#define IRQ_IN3			11
#define IRQ_XCS0		12
#define IRQ_XCS1		13
#define IRQ_XCS2		14
#define IRQ_DOORBELLHOST	15
#define IRQ_DMA1		16
#define IRQ_DMA2		17
#define IRQ_PCI			18
#define IRQ_BIST		22
#define IRQ_SERR		23
#define IRQ_SDRAMPARITY		24
#define IRQ_I2OINPOST		25
#define IRQ_DISCARDTIMER	27
#define IRQ_PCIDATAPARITY	28
#define IRQ_PCIMASTERABORT	29
#define IRQ_PCITARGETABORT	30
#define IRQ_PCIPARITY		31

/* IRQs 32-47 are the 16 ISA interrupts on a CATS board.  */
#define IRQ_ISA_PIC	IRQ_IN2
#define IRQ_IS_ISA(_x)	(((_x) >= 32) && ((_x) <= 47))
#define IRQ_ISA(_x)	((_x) + 0x20)
#define IRQ_ISA_CASCADE		IRQ_ISA(2)

/*
 * Now map them to the Linux interrupts
 */
#define IRQ_TIMER		IRQ_TIMER1
#define IRQ_FLOPPYDISK		IRQ_ISA(6)
#define IRQ_HARDDISK		IRQ_ISA(14)
#define IRQ_HARDDISK_SECONDARY	IRQ_ISA(15)

#define irq_cannonicalize(_i)	(((_i) == IRQ_ISA_CASCADE) ? IRQ_ISA(9) : _i)
