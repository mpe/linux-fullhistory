/* $Id: firehose.h,v 1.3 1998/06/10 07:28:43 davem Exp $
 * firehose.h: Defines for the Fire Hose Controller (FHC) found
 *             on Sunfire/Starfire/Wildfire systems.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_FIREHOSE_H
#define _SPARC64_FIREHOSE_H

#include <linux/types.h>

/* XXX I have not fully verified the register sizes in this file yet... -DaveM */

/* Fire Hose Controller Internal Registers */
struct fhc_internal_regs {
/*0x0000*/	u32	fhc_id;		/* FHC ID Register			*/
		u32	_unused1[3];
/*0x0010*/	u32	fhc_rcs;	/* FHC Reset Control/Status Register	*/
		u32	_unused2[3];
/*0x0020*/	u32	fhc_control;	/* FHC Control Register			*/
		u32	_unused3[3];
/*0x0030*/	u32	fhc_bsr;	/* FHC Board Status Register		*/
		u32	_unused4[3];
/*0x0040*/	u32	fhc_ecc;	/* FHC ECC Control Register (16 bits)	*/
		u32	_unused5[43];
/*0x00f0*/	u32	fhc_jtag_ctrl;	/* FHC JTAG Control Register		*/
		u32	_unused6[3];
/*0x0100*/	u32	fhc_jtag_cmd;	/* FHC JTAG Command Register		*/
};

/* Part of same space of regs, but mapped separately in PROM reg property
 * for the FHC, thus we have the following few structs...
 */
struct fhc_ign_reg {
/*0x2000*/	u64	fhc_ign;		/* FHC Interrupt Group Number	*/
};

struct fhc_fanfail_regs {
/*0x4000*/	u32	_pad0, fhc_ff_imap;	/* FHC FanFail Interrupt Map	*/
		u64	_pad1;
/*0x4010*/	u32	_pad2, fhc_ff_iclr;	/* FHC FanFail Interrupt Clear	*/
};

struct fhc_system_regs {
/*0x6000*/	u32	_pad0, fhc_sys_imap;	/* FHC System Interrupt Map	*/
		u64	_pad1;
/*0x6010*/	u32	_pad2, fhc_sys_iclr;	/* FHC System Interrupt Clear	*/
};

struct fhc_uart_regs {
/*0x8000*/	u32	_pad0, fhc_uart_imap;	/* FHC UART Interrupt Map	*/
		u64	_pad1;
/*0x8010*/	u32	_pad2, fhc_uart_iclr;	/* FHC UART Interrupt Clear	*/
};

struct fhc_tod_regs {
/*0xa000*/	u32	_pad0, fhc_tod_imap;	/* FHC TOD Interrupt Map	*/
		u64	_pad1;
/*0xa010*/	u32	_pad2, fhc_tod_iclr;	/* FHC TOD Interrupt Clear	*/
};

/* All of the above. */
struct fhc_regs {
	struct fhc_internal_regs	*pregs;
	struct fhc_ign_reg		*ireg;
	struct fhc_fanfail_regs		*ffregs;
	struct fhc_system_regs		*sregs;
	struct fhc_uart_regs		*uregs;
	struct fhc_tod_regs		*tregs;
};

/* FHC ID Register */
#define FHC_ID_VERS		0xf0000000 /* Version of this FHC		*/
#define FHC_ID_PARTID		0x0ffff000 /* Part ID code (0x0f9f == FHC)	*/
#define FHC_ID_MANUF		0x0000007e /* Manufacturer (0x3e == SUN's JEDEC)*/
#define FHC_ID_RESV		0x00000001 /* Read as one			*/

/* FHC Control Register */
#define FHC_CONTROL_ICS		0x00100000 /* Ignore Centerplane Signals	*/
#define FHC_CONTROL_FRST	0x00080000 /* Fatal Error Reset Enable		*/
#define FHC_CONTROL_LFAT	0x00040000 /* AC/DC signalled a local error	*/
#define FHC_CONTROL_SLINE	0x00010000 /* Firmware Synchronization Line	*/
#define FHC_CONTROL_DCD		0x00008000 /* DC-->DC Converter Disable		*/
#define FHC_CONTROL_POFF	0x00004000 /* AC/DC Controller PLL Disable	*/
#define FHC_CONTROL_FOFF	0x00002000 /* FHC Controller PLL Disable	*/
#define FHC_CONTROL_AOFF	0x00001000 /* CPU A SRAM/SBD Low Power Mode	*/
#define FHC_CONTROL_BOFF	0x00000800 /* CPU B SRAM/SBD Low Power Mode	*/
#define FHC_CONTROL_PSOFF	0x00000400 /* Turns off this FHC's power supply	*/
#define FHC_CONTROL_IXIST	0x00000200 /* 0=FHC tells clock board it exists	*/
#define FHC_CONTROL_XMSTR	0x00000100 /* 1=Causes this FHC to be XIR master*/
#define FHC_CONTROL_LLED	0x00000040 /* 0=Left LED ON			*/
#define FHC_CONTROL_MLED	0x00000020 /* 1=Middle LED ON			*/
#define FHC_CONTROL_RLED	0x00000010 /* 1=Right LED			*/
#define FHC_CONTROL_BPINS	0x00000003 /* Spare Bidirectional Pins		*/

/* FHC Reset Control/Status Register */
#define FHC_RCS_POR		0x80000000 /* Last reset was a power cycle	*/
#define FHC_RCS_SPOR		0x40000000 /* Last reset was sw power on reset	*/
#define FHC_RCS_SXIR		0x20000000 /* Last reset was sw XIR reset	*/
#define FHC_RCS_BPOR		0x10000000 /* Last reset was due to POR button	*/
#define FHC_RCS_BXIR		0x08000000 /* Last reset was due to XIR button	*/
#define FHC_RCS_WEVENT		0x04000000 /* CPU reset was due to wakeup event	*/
#define FHC_RCS_CFATAL		0x02000000 /* Centerplane Fatal Error signalled	*/
#define FHC_RCS_FENAB		0x01000000 /* Fatal errors elicit system reset	*/

/* FHC Board Status Register */
#define FHC_BSD_DA64		0x00040000 /* Port A: 0=128bit 1=64bit data path */
#define FHC_BSD_DB64		0x00020000 /* Port B: 0=128bit 1=64bit data path */
#define FHC_BSD_BID		0x0001e000 /* Board ID                           */
#define FHC_BSD_SA		0x00001c00 /* Port A UPA Speed (from the pins)   */
#define FHC_BSD_SB		0x00000380 /* Port B UPA Speed (from the pins)   */
#define FHC_BSD_NDIAG		0x00000040 /* Not in Diag Mode                   */
#define FHC_BSD_NTBED		0x00000020 /* Not in TestBED Mode                */
#define FHC_BSD_NIA		0x0000001c /* Jumper, bit 18 in PROM space       */
#define FHC_BSD_SI		0x00000001 /* Spare input pin value              */

/* FHC then has an Interrupt Group Number register, essentially this is a 32-bit
 * register with the low 5 bits specifying the IGN of this FHC for interrupt
 * generation purposes, it is a product of the BoardID/Pins seen by the FHC
 * at power on time.  I suspect the firmware really sets this value though
 * during POST.  On board FHC devices generate fixed INO interrupt packet
 * values, of course these are concatenated with the IGN before it reaches the
 * CPU:
 *
 *	IRQ Source		INO Value
 *	----------------------------------------
 *	"System" Interrupt	0x38
 *	Zilogs			0x39
 *	Mostek			0x3a
 *	Fan Failure		0x3b
 *	Spare 1			0x3c
 *	Spare 2			0x3d
 *
 * Consult the sysio.h header for the layout of the Interrupt Mapping and
 * Interrupt Clear register bits as they are the same. -DaveM
 */

#endif /* !(_SPARC64_FIREHOSE_H) */
