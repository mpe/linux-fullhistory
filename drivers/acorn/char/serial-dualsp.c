/*
 * linux/arch/arm/drivers/char/serial-dualsp.c
 *
 * Copyright (c) 1996 Russell King.
 *
 * Changelog:
 *  30-07-1996	RMK	Created
 */
#define MY_CARD_LIST { MANU_SERPORT, PROD_SERPORT_DSPORT }
#define MY_NUMPORTS 2
#define MY_BAUD_BASE (3686400 / 16)
#define MY_INIT dualsp_serial_init
#define MY_BASE_ADDRESS(ec) \
	ecard_address (ec, ECARD_IOC, ECARD_SLOW) + (0x2000 >> 2)
#define MY_PORT_ADDRESS(port,cardaddress) \
	((cardaddress) + (port) * 8)
#include "serial-card.c"
