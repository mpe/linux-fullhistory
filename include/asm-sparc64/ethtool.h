/* $Id: ethtool.h,v 1.1 1998/12/19 15:09:40 davem Exp $
 * ethtool.h: Defines for SparcLinux ethtool.
 *
 * Copyright (C) 1998 David S. Miller (davem@dm.cobaltmicro.com)
 */

#ifndef _SPARC64_ETHTOOL_H
#define _SPARC64_ETHTOOL_H

/* We do things like this so it does not matter what kernel
 * headers you have on your system etc.
 */
#undef SIOCETHTOOL
#define SIOCETHTOOL	(SIOCDEVPRIVATE + 0x0f)

/* This should work for both 32 and 64 bit userland. */
struct ethtool_cmd {
	u32	cmd;
	u32	supported;
	u16	speed;
	u8	duplex;
	u8	port;
	u8	phy_address;
	u8	transceiver;
	u8	autoneg;
};

/* CMDs currently supported */
#define SPARC_ETH_GSET		0x00000001	/* Get settings, non-privileged. */
#define SPARC_ETH_SSET		0x00000002	/* Set settings, privileged. */

/* Indicates what features are supported by the interface. */
#define SUPPORTED_10baseT_Half		0x00000001
#define SUPPORTED_10baseT_Full		0x00000002
#define SUPPORTED_100baseT_Half		0x00000004
#define SUPPORTED_100baseT_Full		0x00000008
#define SUPPORTED_1000baseT_Half	0x00000010
#define SUPPORTED_1000baseT_Full	0x00000020
#define SUPPORTED_Autoneg		0x00000040
#define SUPPORTED_TP			0x00000080
#define SUPPORTED_AUI			0x00000100
#define SUPPORTED_MII			0x00000200
#define SUPPORTED_FIBRE			0x00000400

/* The following are all involved in forcing a particular link
 * mode for the device for setting things.  When getting the
 * devices settings, these indicate the current mode and whether
 * it was foced up into this mode or autonegotiated.
 */

/* The forced speec, 10Mb, 100Mb, gigabit. */
#define SPEED_10		10
#define SPEED_100		100
#define SPEED_1000		1000

/* Duplex, half or full. */
#define DUPLEX_HALF		0x00
#define DUPLEX_FULL		0x01

/* Which connector port. */
#define PORT_TP			0x00
#define PORT_AUI		0x01
#define PORT_MII		0x02
#define PORT_FIBRE		0x03

/* Which tranceiver to use. */
#define XCVR_INTERNAL		0x00
#define XCVR_EXTERNAL		0x01
#define XCVR_DUMMY1		0x02
#define XCVR_DUMMY2		0x03
#define XCVR_DUMMY3		0x04

/* Enable or disable autonegotiation.  If this is set to enable,
 * the forced link modes above are completely ignored.
 */
#define AUTONEG_DISABLE		0x00
#define AUTONEG_ENABLE		0x01

#endif /* _SPARC64_ETHTOOL_H */
