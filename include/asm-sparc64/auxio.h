/* $Id: auxio.h,v 1.1 1997/03/14 21:05:27 jj Exp $
 * auxio.h:  Definitions and code for the Auxiliary I/O register.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC64_AUXIO_H
#define _SPARC64_AUXIO_H

#include <asm/system.h>

/* FIXME: All of this should be checked for sun4u. It has /sbus/auxio, but
   I don't know whether it is the same and don't have a floppy */

extern unsigned char *auxio_register;

/* This register is an unsigned char in IO space.  It does two things.
 * First, it is used to control the front panel LED light on machines
 * that have it (good for testing entry points to trap handlers and irq's)
 * Secondly, it controls various floppy drive parameters.
 */
#define AUXIO_ORMEIN      0xf0    /* All writes must set these bits. */
#define AUXIO_ORMEIN4M    0xc0    /* sun4m - All writes must set these bits. */
#define AUXIO_FLPY_DENS   0x20    /* Floppy density, high if set. Read only. */
#define AUXIO_FLPY_DCHG   0x10    /* A disk change occurred.  Read only. */
#define AUXIO_EDGE_ON     0x10    /* sun4m - On means Jumper block is in. */
#define AUXIO_FLPY_DSEL   0x08    /* Drive select/start-motor. Write only. */
#define AUXIO_LINK_TEST   0x08    /* sun4m - On means TPE Carrier detect. */

/* Set the following to one, then zero, after doing a pseudo DMA transfer. */
#define AUXIO_FLPY_TCNT   0x04    /* Floppy terminal count. Write only. */

/* Set the following to zero to eject the floppy. */
#define AUXIO_FLPY_EJCT   0x02    /* Eject floppy disk.  Write only. */
#define AUXIO_LED         0x01    /* On if set, off if unset. Read/Write */

#define AUXREG   ((volatile unsigned char *)(auxio_register))

/* These are available on sun4c */
#define TURN_ON_LED   if (AUXREG) *AUXREG = (*AUXREG | AUXIO_ORMEIN | AUXIO_LED)
#define TURN_OFF_LED  if (AUXREG) *AUXREG = ((*AUXREG | AUXIO_ORMEIN) & (~AUXIO_LED))
#define FLIP_LED      if (AUXREG) *AUXREG = ((*AUXREG | AUXIO_ORMEIN) ^ AUXIO_LED)
#define FLPY_MOTORON  if (AUXREG) *AUXREG = ((*AUXREG | AUXIO_ORMEIN) | AUXIO_FLPY_DSEL)
#define FLPY_MOTOROFF if (AUXREG) *AUXREG = ((*AUXREG | AUXIO_ORMEIN) & (~AUXIO_FLPY_DSEL))
#define FLPY_TCNTON   if (AUXREG) *AUXREG = ((*AUXREG | AUXIO_ORMEIN) | AUXIO_FLPY_TCNT)
#define FLPY_TCNTOFF  if (AUXREG) *AUXREG = ((*AUXREG | AUXIO_ORMEIN) & (~AUXIO_FLPY_TCNT))

#ifndef __ASSEMBLY__
extern __inline__ void set_auxio(unsigned char bits_on, unsigned char bits_off)
{
	unsigned char regval;
	unsigned long flags;

	save_flags(flags); cli();

	if(AUXREG) {
		regval = *AUXREG;
		*AUXREG = ((regval | bits_on) & ~bits_off) | AUXIO_ORMEIN4M;
	}
	restore_flags(flags);
}
#endif /* !(__ASSEMBLY__) */


/* AUXIO2 (Power Off Control) */
extern __volatile__ unsigned char * auxio_power_register;

#define	AUXIO_POWER_DETECT_FAILURE	32
#define	AUXIO_POWER_CLEAR_FAILURE	2
#define	AUXIO_POWER_OFF			1


#endif /* !(_SPARC_AUXIO_H) */
