/* arch/arm/mach-s3c2410/cpu.h
 *
 * Copyright (c) 2004 Simtec Electronics
 * Ben Dooks <ben@simtec.co.uk>
 *
 * Header file for S3C24XX CPU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     24-Aug-2004 BJD  Start of generic S3C24XX support
*/

#define IODESC_ENT(x) { S3C2410_VA_##x, S3C2410_PA_##x, S3C2410_SZ_##x, MT_DEVICE }

#ifndef MHZ
#define MHZ (1000*1000)
#endif

#define print_mhz(m) ((m) / MHZ), ((m / 1000) % 1000)

#ifdef CONFIG_CPU_S3C2410
extern  int s3c2410_init(void);
extern void s3c2410_map_io(struct map_desc *mach_desc, int size);
#else
#define s3c2410_map_io NULL
#define s3c2410_init NULL
#endif

#ifdef CONFIG_CPU_S3C2440
extern  int s3c2440_init(void);
extern void s3c2440_map_io(struct map_desc *mach_desc, int size);
#else
#define s3c2440_map_io NULL
#define s3c2440_init NULL
#endif

extern void s3c24xx_init_io(struct map_desc *mach_desc, int size);

