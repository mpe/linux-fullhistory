#ifndef _ASM_M32R_SERIAL_H
#define _ASM_M32R_SERIAL_H

/*
 * include/asm-m32r/serial.h
 */

#include <linux/config.h>
#include <asm/m32r.h>

/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD ( 1843200 / 16 )

/* Standard COM flags */
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

/* Standard PORT definitions */
#if defined(CONFIG_PLAT_USRV)

#define STD_SERIAL_PORT_DEFNS						\
       /* UART  CLK     PORT   IRQ            FLAGS */			\
	{ 0, BASE_BAUD, 0x3F8, PLD_IRQ_UART0, STD_COM_FLAGS }, /* ttyS0 */ \
	{ 0, BASE_BAUD, 0x2F8, PLD_IRQ_UART1, STD_COM_FLAGS }, /* ttyS1 */

#else /* !CONFIG_PLAT_USRV */

#if defined(CONFIG_SERIAL_M32R_PLDSIO)
#define STD_SERIAL_PORT_DEFNS						\
	{ 0, BASE_BAUD, ((unsigned long)PLD_ESIO0CR), PLD_IRQ_SIO0_RCV,	\
	  STD_COM_FLAGS }, /* ttyS0 */
#else
#define STD_SERIAL_PORT_DEFNS						\
	{ 0, BASE_BAUD, M32R_SIO_OFFSET, M32R_IRQ_SIO0_R,		\
	  STD_COM_FLAGS }, /* ttyS0 */
#endif

#endif /* !CONFIG_PLAT_USRV */

#define SERIAL_PORT_DFNS	STD_SERIAL_PORT_DEFNS

#endif  /* _ASM_M32R_SERIAL_H */
