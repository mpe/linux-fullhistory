/* arch/arm/mach-s3c2410/cpu.h
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for S3C24XX CPU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     24-Aug-2004 BJD  Start of generic S3C24XX support
 *     18-Oct-2004 BJD  Moved board struct into this file
 *     04-Jan-2005 BJD  New uart initialisation
 *     10-Jan-2005 BJD  Moved generic init here, specific to cpu headers
 *     14-Jan-2005 BJD  Added s3c24xx_init_clocks() call
*/

#define IODESC_ENT(x) { S3C2410_VA_##x, S3C2410_PA_##x, S3C2410_SZ_##x, MT_DEVICE }

#ifndef MHZ
#define MHZ (1000*1000)
#endif

#define print_mhz(m) ((m) / MHZ), ((m / 1000) % 1000)

/* forward declaration */
struct s3c2410_uartcfg;

/* core initialisation functions */

extern void s3c24xx_init_irq(void);

extern void s3c24xx_init_io(struct map_desc *mach_desc, int size);

extern void s3c24xx_init_uarts(struct s3c2410_uartcfg *cfg, int no);

extern void s3c24xx_init_clocks(int xtal);

/* the board structure is used at first initialsation time
 * to get info such as the devices to register for this
 * board. This is done because platfrom_add_devices() cannot
 * be called from the map_io entry.
*/

struct s3c24xx_board {
	struct platform_device  **devices;
	unsigned int              devices_count;

	struct clk		**clocks;
	unsigned int		  clocks_count;
};

extern void s3c24xx_set_board(struct s3c24xx_board *board);

/* timer for 2410/2440 */

struct sys_timer;
extern struct sys_timer s3c24xx_timer;
