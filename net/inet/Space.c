/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Holds initial configuration information for devices.
 *
 * NOTE:	This file is a nice idea, but its current format does not work
 *		well for drivers that support multiple units, like the SLIP
 *		driver.  We should actually have only one pointer to a driver
 *		here, with the driver knowing how many units it supports.
 *		Currently, the SLIP driver abuses the "base_addr" integer
 *		field of the 'device' structure to store the unit number...
 *		-FvK
 *
 * Version:	@(#)Space.c	1.0.7	08/12/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald J. Becker, <becker@super.org>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/ddi.h>
#include "dev.h"

#define LOOPBACK			/* always present, right?	*/

#define	NEXT_DEV	NULL


#if defined(D_LINK) || defined(CONFIG_DE600)
    extern int d_link_init(struct device *);
    static struct device d_link_dev = {
	"dl0",
	0,
	0,
	0,
	0,
	D_LINK_IO,
	D_LINK_IRQ,
	0, 0, 0,
	NEXT_DEV,
	d_link_init
    };
#   undef NEXT_DEV
#   define NEXT_DEV	(&d_link_dev)
#endif

#ifdef CONFIG_EL1
#error 
#   ifndef EL1_IRQ
#	define EL1_IRQ 9
#   endif
#   ifndef EL1
#	define EL1 0
#   endif
    extern int el1_init(struct device *);
    static struct device el1_dev = {
        "el0", 0, 0, 0, 0, EL1,	EL1_IRQ, 0, 0, 0, NEXT_DEV, el1_init
    };
#   undef NEXT_DEV
#   define NEXT_DEV	(&el1_dev)
#endif  /* EL1 */

#ifdef CONFIG_DEPCA
    extern int depca_probe(struct device *);
    static struct device depca_dev = {
        "depca0", 0,0,0,0, 0, 0, 0, 0, 0, NEXT_DEV, depca_probe,
    };
#   undef NEXT_DEV
#   define NEXT_DEV	(&depca_dev)
#endif  /* CONFIG_DEPCA */


#ifdef CONFIG_ATP		/* AT-LAN-TEC (RealTek) pocket adaptor. */
    extern int atp_probe(struct device *);
    static struct device atp_dev = {
        "atp0", 0,0,0,0, 0, 0, 0, 0, 0, NEXT_DEV, atp_probe,
    };
#   undef NEXT_DEV
#   define NEXT_DEV	(&atp_dev)
#endif  /* CONFIG_ATP */

#ifdef CONFIG_EL3
    extern int el3_probe(struct device *);
    static struct device eliii0_dev = {
        "eliii0", 0,0,0,0, 0, 0, 0, 0, 0, NEXT_DEV, el3_probe,
    };
#   undef NEXT_DEV
#   define NEXT_DEV	(&eliii0_dev)
#endif  /* CONFIG_3C509 aka EL3 */

#ifdef CONFIG_ZNET
    extern int znet_probe(struct device *);
    static struct device znet_dev = {
	"znet", 0,0,0,0, 0, 0, 0, 0, 0, NEXT_DEV, znet_probe, };
#   undef NEXT_DEV
#   define NEXT_DEV	(&znet_dev)
#endif  /* CONFIG_ZNET */

#ifdef CONFIG_EEXPRESS
    extern int express_probe(struct device *);
    static struct device express0_dev = {
	"exp0", 0,0,0,0, 0, 0, 0, 0, 0, NEXT_DEV, express_probe, };
#   undef NEXT_DEV
#   define NEXT_DEV	(&express0_dev)
#endif  /* CONFIG_EEPRESS */

#ifdef CONFIG_AT1500
    extern int at1500_probe(struct device *);
    static struct device lance_dev = {
        "le0",
	0,0,0,0, 0 /* I/O Base */, 0 /* pre-set IRQ */,
        0, 0, 0, NEXT_DEV, at1500_probe,
    };
#   undef NEXT_DEV
#   define NEXT_DEV	(&lance_dev)
#endif  /* AT1500BT */

#if defined(EI8390) || defined(CONFIG_EL2) || defined(CONFIG_NE2000) \
    || defined(CONFIG_WD80x3) || defined(CONFIG_HPLAN)
#   ifndef EI8390
#	define EI8390 0
#   endif
#   ifndef EI8390_IRQ
#	define EI8390_IRQ 0
#   endif
    extern int ethif_init(struct device *);
    static struct device ei8390_dev = {
	"eth0",
	0,				/* auto-config			*/
	0,
	0,
	0,
	EI8390,
	EI8390_IRQ,
	0, 0, 0,
	NEXT_DEV,
	ethif_init
    };
#   undef NEXT_DEV
#   define NEXT_DEV	(&ei8390_dev)
#endif  /* The EI8390 drivers. */

#if defined(PLIP) || defined(CONFIG_PLIP)
    extern int plip_init(struct device *);
    static struct device plip2_dev = {
	"plip2", 0, 0, 0, 0, 0x278, 2, 0, 0, 0, NEXT_DEV, plip_init, };
    static struct device plip1_dev = {
	"plip1", 0, 0, 0, 0, 0x378, 7, 0, 0, 0, &plip2_dev, plip_init, };
    static struct device plip0_dev = {
	"plip0", 0, 0, 0, 0, 0x3BC, 5, 0, 0, 0, &plip1_dev, plip_init, };
#   undef NEXT_DEV
#   define NEXT_DEV	(&plip0_dev)
#endif  /* PLIP */

#if defined(SLIP) || defined(CONFIG_SLIP)
    extern int slip_init(struct device *);
    static struct device slip3_dev = {
	"sl3",			/* Internal SLIP driver, channel 3	*/
	0x0,			/* recv memory end			*/
	0x0,			/* recv memory start			*/
	0x0,			/* memory end				*/
	0x0,			/* memory start				*/
	0x3,			/* base I/O address			*/
	0,			/* IRQ					*/
	0, 0, 0,		/* flags				*/
	NEXT_DEV,		/* next device				*/
	slip_init		/* slip_init should set up the rest	*/
    };
    static struct device slip2_dev = {
	"sl2",			/* Internal SLIP driver, channel 2	*/
	0x0,			/* recv memory end			*/
	0x0,			/* recv memory start			*/
	0x0,			/* memory end				*/
	0x0,			/* memory start				*/
	0x2,			/* base I/O address			*/
	0,			/* IRQ					*/
	0, 0, 0,		/* flags				*/
	&slip3_dev,		/* next device				*/
	slip_init		/* slip_init should set up the rest	*/
    };
    static struct device slip1_dev = {
	"sl1",			/* Internal SLIP driver, channel 1	*/
	0x0,			/* recv memory end			*/
	0x0,			/* recv memory start			*/
	0x0,			/* memory end				*/
	0x0,			/* memory start				*/
	0x1,			/* base I/O address			*/
	0,			/* IRQ					*/
	0, 0, 0,		/* flags				*/
	&slip2_dev,		/* next device				*/
	slip_init		/* slip_init should set up the rest	*/
    };
    static struct device slip0_dev = {
	"sl0",			/* Internal SLIP driver, channel 0	*/
	0x0,			/* recv memory end			*/
	0x0,			/* recv memory start			*/
	0x0,			/* memory end				*/
	0x0,			/* memory start				*/
	0x0,			/* base I/O address			*/
	0,			/* IRQ					*/
	0, 0, 0,		/* flags				*/
	&slip1_dev,		/* next device				*/
	slip_init		/* slip_init should set up the rest	*/
    };
#   undef	NEXT_DEV
#   define	NEXT_DEV	(&slip0_dev)
#endif	/* SLIP */


#ifdef LOOPBACK
    extern int loopback_init(struct device *dev);
    static struct device loopback_dev = {
	"lo",			/* Software Loopback interface		*/
	0x0,			/* recv memory end			*/
	0x0,			/* recv memory start			*/
	0x0,			/* memory end				*/
	0x0,			/* memory start				*/
	0,			/* base I/O address			*/
	0,			/* IRQ					*/
	0, 0, 0,		/* flags				*/
	NEXT_DEV,		/* next device				*/
	loopback_init		/* loopback_init should set up the rest	*/
    };
#   undef	NEXT_DEV
#   define	NEXT_DEV	(&loopback_dev)
#endif


struct device *dev_base = NEXT_DEV;
