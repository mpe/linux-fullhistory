/*
 * Amiga Linux/68k A2065 Ethernet Driver
 *
 * (C) Copyright 1995 by Geert Uytterhoeven
 *			(Geert.Uytterhoeven@cs.kuleuven.ac.be)
 *
 * ---------------------------------------------------------------------------
 *
 * This program is based on
 *
 *	ariadne.?:	Amiga Linux/68k Ariadne Ethernet Driver
 *			(C) Copyright 1995 by Geert Uytterhoeven,
 *			Peter De Schrijver
 *
 *	lance.c:	An AMD LANCE ethernet driver for linux.
 *			Written 1993-94 by Donald Becker.
 *
 *	Am79C960:	PCnet(tm)-ISA Single-Chip Ethernet Controller
 *			Advanced Micro Devices
 *			Publication #16907, Rev. B, Amendment/0, May 1994
 *
 * ---------------------------------------------------------------------------
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 *
 * ---------------------------------------------------------------------------
 *
 * The A2065 is a Zorro-II board made by Commodore/Ameristar. It contains:
 *
 *	- an Am7990 Local Area Network Controller for Ethernet (LANCE) with
 *	  both 10BASE-2 (thin coax) and AUI (DB-15) connectors
 */


/*
 *		Am7990 Local Area Network Controller for Ethernet (LANCE)
 */

struct Am7990 {
	volatile u_short RDP;		/* Register Data Port */
	volatile u_short RAP;		/* Register Address Port */
};


/*
 *		Am7990 Control and Status Registers
 */

#define CSR0		0x0000		/* LANCE Controller Status */
#define CSR1		0x0001		/* IADR[15:0] */
#define CSR2		0x0002		/* IADR[23:16] */
#define CSR3		0x0003		/* Misc */


/*
 *		Bit definitions for CSR0 (LANCE Controller Status)
 */

#define ERR		0x8000		/* Error */
#define BABL		0x4000		/* Babble: Transmitted too many bits */
#define CERR		0x2000		/* No Heartbeat (10BASE-T) */
#define MISS		0x1000		/* Missed Frame */
#define MERR		0x0800		/* Memory Error */
#define RINT		0x0400		/* Receive Interrupt */
#define TINT		0x0200		/* Transmit Interrupt */
#define IDON		0x0100		/* Initialization Done */
#define INTR		0x0080		/* Interrupt Flag */
#define INEA		0x0040		/* Interrupt Enable */
#define RXON		0x0020		/* Receive On */
#define TXON		0x0010		/* Transmit On */
#define TDMD		0x0008		/* Transmit Demand */
#define STOP		0x0004		/* Stop */
#define STRT		0x0002		/* Start */
#define INIT		0x0001		/* Initialize */


/*
 *		Bit definitions for CSR3
 */

#define BSWP		0x0004		/* Byte Swap
					   (on for big endian byte order) */
#define ACON		0x0002		/* ALE Control
					   (on for active low ALE) */
#define BCON		0x0001		/* Byte Control */


/*
 *		Initialization Block
 */

struct InitBlock {
	u_short Mode;			/* Mode */
	u_char PADR[6];			/* Physical Address */
	u_long LADRF[2];		/* Logical Address Filter */
	u_short RDRA;			/* Receive Descriptor Ring Address */
	u_short RLEN;			/* Receive Descriptor Ring Length */
	u_short TDRA;			/* Transmit Descriptor Ring Address */
	u_short TLEN;			/* Transmit Descriptor Ring Length */
};


/*
 *		Mode Flags
 */

#define PROM		0x8000		/* Promiscuous Mode */
#define INTL		0x0040		/* Internal Loopback */
#define DRTY		0x0020		/* Disable Retry */
#define FCOLL		0x0010		/* Force Collision */
#define DXMTFCS		0x0008		/* Disable Transmit CRC */
#define LOOP		0x0004		/* Loopback Enable */
#define DTX		0x0002		/* Disable Transmitter */
#define DRX		0x0001		/* Disable Receiver */


/*
 *		Receive Descriptor Ring Entry
 */

struct RDRE {
	volatile u_short RMD0;		/* LADR[15:0] */
	volatile u_short RMD1;		/* HADR[23:16] | Receive Flags */
	volatile u_short RMD2;		/* Buffer Byte Count
					   (two's complement) */
	volatile u_short RMD3;		/* Message Byte Count */
};


/*
 *		Transmit Descriptor Ring Entry
 */

struct TDRE {
	volatile u_short TMD0;		/* LADR[15:0] */
	volatile u_short TMD1;		/* HADR[23:16] | Transmit Flags */
	volatile u_short TMD2;		/* Buffer Byte Count
					   (two's complement) */
	volatile u_short TMD3;		/* Error Flags */
};


/*
 *		Receive Flags
 */

#define RF_OWN		0x8000		/* LANCE owns the descriptor */
#define RF_ERR		0x4000		/* Error */
#define RF_FRAM		0x2000		/* Framing Error */
#define RF_OFLO		0x1000		/* Overflow Error */
#define RF_CRC		0x0800		/* CRC Error */
#define RF_BUFF		0x0400		/* Buffer Error */
#define RF_STP		0x0200		/* Start of Packet */
#define RF_ENP		0x0100		/* End of Packet */


/*
 *		Transmit Flags
 */

#define TF_OWN		0x8000		/* LANCE owns the descriptor */
#define TF_ERR		0x4000		/* Error */
#define TF_RES		0x2000		/* Reserved,
					   LANCE writes this with a zero */
#define TF_MORE		0x1000		/* More than one retry needed */
#define TF_ONE		0x0800		/* One retry needed */
#define TF_DEF		0x0400		/* Deferred */
#define TF_STP		0x0200		/* Start of Packet */
#define TF_ENP		0x0100		/* End of Packet */


/*
 *		Error Flags
 */

#define EF_BUFF 	0x8000		/* Buffer Error */
#define EF_UFLO 	0x4000		/* Underflow Error */
#define EF_LCOL 	0x1000		/* Late Collision */
#define EF_LCAR 	0x0800		/* Loss of Carrier */
#define EF_RTRY 	0x0400		/* Retry Error */
#define EF_TDR		0x003f		/* Time Domain Reflectometry */


/*
 *		A2065 Expansion Board Structure
 */

struct A2065Board {
	u_char Pad1[0x4000];
	struct Am7990 Lance;
	u_char Pad2[0x3ffc];
	volatile u_char RAM[0x8000];
};
