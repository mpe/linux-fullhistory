/*
 * linux/include/asm-arm/arch-brutus/uncompress.h
 *
 * (C) 1999 Nicolas Pitre <nico@cam.org>
 */

#include <linux/config.h>

#if	defined(CONFIG_SA1100_EMPEG) || \
	defined(CONFIG_SA1100_VICTOR) || \
	defined(CONFIG_SA1100_LART)
#define SERBASE _Ser3UTCR0;
#elif	defined(CONFIG_SA1100_BRUTUS)
#define SERBASE _Ser1UTCR0;
#endif


#ifdef SERBASE

#include "hardware.h"
#include "serial_reg.h"

static volatile unsigned long* serial_port = (unsigned long*)SERBASE;

/*
 * The following code assumes the serial port has already been
 * initialized by the bootloader or such...
 */

static void puts( const char *s )
{
    int i;

    for (i = 0; *s; i++, s++) {
	/* wait for space in the UART's transmiter */
	while( !(serial_port[UTSR1] & UTSR1_TNF) );

	/* send the character out. */
	serial_port[UART_TX] = *s;

	/* if a LF, also do CR... */
	if (*s == 10) {
	    while( !(serial_port[UTSR1] & UTSR1_TNF) );
	    serial_port[UART_TX] = 13;
	}
    }
}

#else

static inline void puts( const char *s ) {}

#endif


/* Nothing to do for these */
#define arch_decomp_setup()
#define arch_decomp_wdog()

