/*
** linux/amigatypes.h -- Types used in Amiga Linux kernel source
**
** Copyright 1992 by Greg Harp
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
** Created 09/29/92 by Greg Harp
**
** Moved all Zorro definitions to asm/zorro.h which is where they
** really belong - 24/11/95 Jes Sorensen
*/

#ifndef _LINUX_AMIGATYPES_H_
#define _LINUX_AMIGATYPES_H_

#ifdef __KERNEL__ /* only if compiling the kernel */
#include <linux/types.h>
#endif

/*
 * Different models of Amiga
 */
#define AMI_UNKNOWN	(0)
#define AMI_500		(1)
#define AMI_500PLUS	(2)
#define AMI_600		(3)
#define AMI_1000	(4)
#define AMI_1200	(5)
#define AMI_2000	(6)
#define AMI_2500	(7)
#define AMI_3000	(8)
#define AMI_3000T	(9)
#define AMI_3000PLUS	(10)
#define AMI_4000	(11)
#define AMI_4000T	(12)
#define AMI_CDTV	(13)
#define AMI_CD32	(14)
#define AMI_DRACO	(15)

/*
 * chipsets
 */
#define CS_STONEAGE (0)
#define CS_OCS	    (1)
#define CS_ECS	    (2)
#define CS_AGA	    (3)

/*
 * Amiga clocks
 */

extern u_long amiga_masterclock;			/* 28 MHz */
extern u_long amiga_colorclock;				/* 3.5 MHz */
#define amiga_eclock	boot_info.bi_amiga.eclock	/* 700 kHz */

#endif /* asm-m68k/amigatypes.h */
