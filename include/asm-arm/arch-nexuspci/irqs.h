/*
 * linux/include/asm-arm/arch-nexuspci/irqs.h
 *
 * Copyright (C) 1997, 1998 Philip Blundell
 */

/* Most of the IRQ sources can generate both FIQs and IRQs.
   The exceptions to this are the DUART, which can only generate IRQs,
   and the PLX SYSERR output, which can only generate FIQs.  We route
   both FIQs and IRQs through the generic IRQ handling system and the
   choice by the driver of which to use is basically an arbitrary one.  */

#define TREAT_FIQS_AS_IRQS

#define FIQ_PLX			0
#define FIQ_PCI_D		1
#define FIQ_PCI_C		2
#define FIQ_PCI_B		3
#define FIQ_PCI_A		4
#define FIQ_SYSERR		5

#define IRQ_DUART		6
#define IRQ_PLX 		7
#define IRQ_PCI_D		8
#define IRQ_PCI_C		9
#define IRQ_PCI_B		10
#define IRQ_PCI_A	        11

/* timer is part of the DUART */
#define IRQ_TIMER		IRQ_DUART

#define irq_cannonicalize(i)	(i)
