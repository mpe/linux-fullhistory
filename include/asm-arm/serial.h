/*
 * linux/include/asm-arm/serial.h
 *
 * Copyright (c) 1996 Russell King.
 *
 * Changelog:
 *  15-10-1996	RMK	Created
 */

#ifndef __ASM_SERIAL_H
#define __ASM_SERIAL_H

#include <asm/arch/serial.h>

#define SERIAL_PORT_DFNS		\
	STD_SERIAL_PORT_DEFNS		\
	EXTRA_SERIAL_PORT_DEFNS

#endif
