/* $Id: auxio.h,v 1.11 1996/04/25 06:12:45 davem Exp $
 * auxio.h:  Definitions and code for the Auxiliary I/O register.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_AUXIO_H
#define _SPARC_AUXIO_H

#include <asm/system.h>
#include <asm/vaddrs.h>

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

/* Set the following to one, then zero, after doing a pseudo DMA transfer. */
#define AUXIO_FLPY_TCNT   0x04    /* Floppy terminal count. Write only. */

/* Set the following to zero to eject the floppy. */
#define AUXIO_FLPY_EJCT   0x02    /* Eject floppy disk.  Write only. */
#define AUXIO_LED         0x01    /* On if set, off if unset. Read/Write */

#define AUXREG   ((volatile unsigned char *)(auxio_register))

#define TURN_ON_LED   *AUXREG = (*AUXREG | AUXIO_ORMEIN | AUXIO_LED)
#define TURN_OFF_LED  *AUXREG = ((*AUXREG | AUXIO_ORMEIN) & (~AUXIO_LED))
#define FLIP_LED      *AUXREG = ((*AUXREG | AUXIO_ORMEIN) ^ AUXIO_LED)
#define FLPY_MOTORON  *AUXREG = ((*AUXREG | AUXIO_ORMEIN) | AUXIO_FLPY_DSEL)
#define FLPY_MOTOROFF *AUXREG = ((*AUXREG | AUXIO_ORMEIN) & (~AUXIO_FLPY_DSEL))
#define FLPY_TCNTON   *AUXREG = ((*AUXREG | AUXIO_ORMEIN) | AUXIO_FLPY_TCNT)
#define FLPY_TCNTOFF  *AUXREG = ((*AUXREG | AUXIO_ORMEIN) & (~AUXIO_FLPY_TCNT))

#ifndef __ASSEMBLY__
extern inline void set_auxio(unsigned char bits_on, unsigned char bits_off)
{
	unsigned char regval;
	unsigned long flags;

	save_flags(flags); cli();

	switch(sparc_cpu_model) {
	case sun4c:
		regval = *AUXREG;
		*AUXREG = ((regval | bits_on) & ~bits_off) | AUXIO_ORMEIN;
		break;
	case sun4m:
		regval = *AUXREG;
		*AUXREG = ((regval | bits_on) & ~bits_off) | AUXIO_ORMEIN4M;
		break;
	default:
		panic("Can't set AUXIO register on this machine.");
	};

	restore_flags(flags);
}
#endif /* !(__ASSEMBLY__) */

#endif /* !(_SPARC_AUXIO_H) */
