/*
 * linux/include/asm-arm/arch-arc/serial.h
 *
 * Copyright (c) 1996 Russell King.
 *
 * Changelog:
 *  15-10-1996	RMK	Created
 *  04-04-1998	PJB	Merged `arc' and `a5k' architectures
 */
#ifndef __ASM_ARCH_SERIAL_H
#define __ASM_ARCH_SERIAL_H

#include <linux/config.h>

/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD (1843200 / 16)

#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

#if defined(CONFIG_ARCH_A5K)
     /* UART CLK        PORT  IRQ     FLAGS        */
#define SERIAL_PORT_DFNS \
	{ 0, BASE_BAUD, 0x3F8, 10, STD_COM_FLAGS },	/* ttyS0 */	\
	{ 0, BASE_BAUD, 0x2F8, 10, STD_COM_FLAGS },	/* ttyS1 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS2 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS3 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS }, 	/* ttyS4 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS5 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS6 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS7 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS8 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS9 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS10 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS11 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS12 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS13 */

#elif defined(CONFIG_ARCH_ARC)

     /* UART CLK        PORT  IRQ     FLAGS        */
#define SERIAL_PORT_DFNS \
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS0 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS1 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS2 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS3 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS }, 	/* ttyS4 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS5 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS6 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS7 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS8 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS9 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS10 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS11 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS12 */	\
	{ 0, BASE_BAUD, 0    ,  0, STD_COM_FLAGS },	/* ttyS13 */

#endif
#endif
