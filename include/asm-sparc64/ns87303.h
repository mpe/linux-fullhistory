/* $Id: ns87303.h,v 1.2 1998/09/13 15:38:50 ecd Exp $
 * ns87303.h: Configuration Register Description for the
 *            National Semiconductor PC87303 (SuperIO).
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef _SPARC_NS87303_H
#define _SPARC_NS87303_H 1

/*
 * Controll Register Index Values
 */
#define FER	0x00
#define FAR	0x01
#define PTR	0x02
#define FCR	0x03
#define PCR	0x04
#define KRR	0x05
#define PMC	0x06
#define TUP	0x07
#define SID	0x08
#define ASC	0x09
#define CS0CF0	0x0a
#define CS0CF1	0x0b
#define CS1CF0	0x0c
#define CS1CF1	0x0d

/* Function Enable Register (FER) bits */
#define FER_EDM		0x10	/* Encoded Drive and Motor pin information   */

/* Function Address Register (FAR) bits */
#define FAR_LPT_MASK	0x03
#define FAR_LPTB	0x00
#define FAR_LPTA	0x01
#define FAR_LPTC	0x02

/* Power and Test Register (PTR) bits */
#define PTR_LPTB_IRQ7	0x08
#define PTR_LEVEL_IRQ	0x80	/* When not ECP/EPP: Use level IRQ           */
#define PTR_LPT_REG_DIR	0x80	/* When ECP/EPP: LPT CTR controlls direction */
				/*               of the parallel port	     */

/* Function Control Register (FCR) bits */
#define FCR_LDE		0x10	/* Logical Drive Exchange                    */
#define FCR_ZWS_ENA	0x20	/* Enable short host read/write in ECP/EPP   */

/* Printer Controll Register (PCR) bits */
#define PCR_EPP_ENABLE	0x01
#define PCR_EPP_IEEE	0x02	/* Enable EPP Version 1.9 (IEEE 1284)        */
#define PCR_ECP_ENABLE	0x04
#define PCR_ECP_CLK_ENA	0x08	/* If 0 ECP Clock is stopped on Power down   */
#define PCR_IRQ_POLAR	0x20	/* If 0 IRQ is level high or negative pulse, */
				/* if 1 polarity is inverted                 */
#define PCR_IRQ_ODRAIN	0x40	/* If 1, IRQ is open drain                   */

/* Tape UARTs and Parallel Port Config Register (TUP) bits */
#define TUP_EPP_TIMO	0x02	/* Enable EPP timeout IRQ                    */

/* Advanced SuperIO Config Register (ASC) bits */
#define ASC_LPT_IRQ7	0x01	/* Allways use IRQ7 for LPT                  */
#define ASC_DRV2_SEL	0x02	/* Logical Drive Exchange controlled by TDR  */

#ifdef __KERNEL__

#include <asm/system.h>
#include <asm/io.h>

static __inline__ void ns87303_writeb(unsigned long port, int index,
				     unsigned char value)
{
	unsigned long flags;

	save_flags(flags); cli();
	outb(index, port);
	outb(value, port + 1);
	outb(value, port + 1);
	restore_flags(flags);
}

static __inline__ unsigned char ns87303_readb(unsigned long port, int index)
{
	outb(index, port);
	return inb(port + 1);
}

#endif /* __KERNEL__ */

#endif /* !(_SPARC_NS87303_H) */
