/* auxio.h:  Definitons and code for the Auxiliary I/O register.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_AUXIO_H
#define _SPARC_AUXIO_H

/* This defines the register as I know it on the Sun4c, it may be
 * different or not exist at all on sun4m's.
 */

#define AUXIO_IOADDR  0xf7400000  /* Physical address is IO space */

/* This register is an unsigned char in IO space.  It does two things.
 * First, it is used to control the front panel LED light on machines
 * that have it (good for testing entry points to trap handlers and irq's)
 * Secondly, it controls various floppy drive parameters on machines that
 * have a drive.
 */

#define AUXIO_ORMEIN      0xf0    /* All writes must set these bits. */
#define AUXIO_FLPY_DENS   0x20    /* Floppy density, high if set. */
#define AUXIO_FLPY_DCHG   0x10    /* A disk change occurred. */
#define AUXIO_FLPY_DSEL   0x08    /* Drive select, 0 'a drive' 1 'b drive'. */
#define AUXIO_FLPY_TCNT   0x04    /* Floppy terminal count... ??? */
#define AUXIO_FLPY_EJCT   0x02    /* Eject floppy disk. */
#define AUXIO_LED         0x01    /* On if set, off if unset. */

#define AUXREG ((volatile unsigned char *)(AUXIO_VADDR + 3))

#define TURN_ON_LED  *AUXREG = AUXIO_ORMEIN | AUXIO_FLPY_EJCT | AUXIO_LED
#define TURN_OFF_LED *AUXREG = AUXIO_ORMEIN | AUXIO_FLPY_EJCT
#define FLIP_LED     *AUXREG = (*AUXREG | AUXIO_ORMEIN) ^ AUXIO_LED

#endif /* !(_SPARC_AUXIO_H) */
