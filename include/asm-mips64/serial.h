/* $Id: serial.h,v 1.2 2000/01/17 23:32:47 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_SERIAL_H
#define _ASM_SERIAL_H

#include <linux/config.h>

#include <asm/sn/sn0/ip27.h>

/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD (1843200 / 16)

/* Standard COM flags (except for COM4, because of the 8514 problem) */
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

/*
 * The IOC3 serials use a 22MHz clock rate with an additional divider by 3.
 */
#define IOC3_BAUD (22000000 / (3*16))
#define IOC3_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST | ASYNC_IOC3)

/* Let the compiler figure out the size.  */
#define RS_TABLE_SIZE

#ifdef CONFIG_SGI_IP27
#define _ORIGIN_SERIAL_INIT(int, base)					\
	{ baud_base: IOC3_BAUD, irq: int, flags: IOC3_COM_FLAGS,	\
	  iomem_base: (u8 *) base, iomem_reg_shift: 0,			\
	  io_type: SERIAL_IO_MEM }
#define ORIGIN_SERIAL_PORT_DFNS						\
	_ORIGIN_SERIAL_INIT(0, 0x9200000008620178UL),			\
	_ORIGIN_SERIAL_INIT(0, 0x9200000008620170UL),

#else
#define ORIGIN_SERIAL_PORT_DFNS
#endif

#define SERIAL_PORT_DFNS						\
	ORIGIN_SERIAL_PORT_DFNS

#endif /* _ASM_SERIAL_H */
