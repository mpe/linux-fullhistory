/*
 * linux/include/asm-arm/arch-nexuspci/irqs.h
 *
 * Copyright (C) 1997 Philip Blundell
 */

#define IRQ_DUART		0
#define IRQ_PLX 		1
#define IRQ_PCI_D		2
#define IRQ_PCI_C		3
#define IRQ_PCI_B		4
#define IRQ_PCI_A		5
#define IRQ_SYSERR		6

/* timer is part of the DUART */
#define IRQ_TIMER		IRQ_DUART

#define irq_cannonicalize(i)	(i)

