#ifndef _SPARC_VADDRS_H
#define _SPARC_VADDRS_H

/* asm-sparc/vaddrs.h:  Here will be define the virtual addresses at
 *                      which important I/O addresses will be mapped.
 *                      For instance the timer register virtual address
 *                      is defined here.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#define  TIMER_VADDR    0x3000   /* Next page after where the interrupt enable
				  * register gets mapped at boot.
				  */

#endif /* !(_SPARC_VADDRS_H) */
