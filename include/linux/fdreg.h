#ifndef _LINUX_FDREG_H
#define _LINUX_FDREG_H
/*
 * This file contains some defines for the floppy disk controller.
 * Various sources. Mostly "IBM Microcomputers: A Programmers
 * Handbook", Sanches and Canton.
 */

#ifdef FDPATCHES

#define FD_IOPORT fdc_state[fdc].address

/* Fd controller regs. S&C, about page 340 */
#define FD_STATUS	(4 + FD_IOPORT )
#define FD_DATA		(5 + FD_IOPORT )

/* Digital Output Register */
#define FD_DOR		(2 + FD_IOPORT )

/* Digital Input Register (read) */
#define FD_DIR		(7 + FD_IOPORT )

/* Diskette Control Register (write)*/
#define FD_DCR		(7 + FD_IOPORT )

#else

#define FD_STATUS	0x3f4
#define FD_DATA		0x3f5
#define FD_DOR		0x3f2		/* Digital Output Register */
#define FD_DIR		0x3f7		/* Digital Input Register (read) */
#define FD_DCR		0x3f7		/* Diskette Control Register (write)*/

#endif

/* Bits of main status register */
#define STATUS_BUSYMASK	0x0F		/* drive busy mask */
#define STATUS_BUSY	0x10		/* FDC busy */
#define STATUS_DMA	0x20		/* 0- DMA mode */
#define STATUS_DIR	0x40		/* 0- cpu->fdc */
#define STATUS_READY	0x80		/* Data reg ready */

/* Bits of FD_ST0 */
#define ST0_DS		0x03		/* drive select mask */
#define ST0_HA		0x04		/* Head (Address) */
#define ST0_NR		0x08		/* Not Ready */
#define ST0_ECE		0x10		/* Equipment chech error */
#define ST0_SE		0x20		/* Seek end */
#define ST0_INTR	0xC0		/* Interrupt code mask */

/* Bits of FD_ST1 */
#define ST1_MAM		0x01		/* Missing Address Mark */
#define ST1_WP		0x02		/* Write Protect */
#define ST1_ND		0x04		/* No Data - unreadable */
#define ST1_OR		0x10		/* OverRun */
#define ST1_CRC		0x20		/* CRC error in data or addr */
#define ST1_EOC		0x80		/* End Of Cylinder */

/* Bits of FD_ST2 */
#define ST2_MAM		0x01		/* Missing Address Mark (again) */
#define ST2_BC		0x02		/* Bad Cylinder */
#define ST2_SNS		0x04		/* Scan Not Satisfied */
#define ST2_SEH		0x08		/* Scan Equal Hit */
#define ST2_WC		0x10		/* Wrong Cylinder */
#define ST2_CRC		0x20		/* CRC error in data field */
#define ST2_CM		0x40		/* Control Mark = deleted */

/* Bits of FD_ST3 */
#define ST3_HA		0x04		/* Head (Address) */
#define ST3_DS		0x08		/* drive is double-sided */
#define ST3_TZ		0x10		/* Track Zero signal (1=track 0) */
#define ST3_RY		0x20		/* drive is ready */
#define ST3_WP		0x40		/* Write Protect */
#define ST3_FT		0x80		/* Drive Fault */

/* Values for FD_COMMAND */
#define FD_RECALIBRATE		0x07	/* move to track 0 */
#define FD_SEEK			0x0F	/* seek track */
#define FD_READ			0xE6	/* read with MT, MFM, SKip deleted */
#define FD_WRITE		0xC5	/* write with MT, MFM */
#define FD_SENSEI		0x08	/* Sense Interrupt Status */
#define FD_SPECIFY		0x03	/* specify HUT etc */
#define FD_FORMAT		0x4D	/* format one track */
#define FD_VERSION		0x10	/* get version code */
#define FD_CONFIGURE		0x13	/* configure FIFO operation */
#define FD_PERPENDICULAR	0x12	/* perpendicular r/w mode */
#define FD_GETSTATUS		0x04	/* read ST3 */
#define FD_DUMPREGS		0x0E	/* dump the contents of the fdc regs */
#define FD_READID		0xEA	/* prints the header of a sector */
#define FD_UNLOCK		0x14	/* Fifo config unlock */
#define FD_LOCK			0x94	/* Fifo config lock */
#define FD_RSEEK_OUT		0x8f	/* seek out (i.e. to lower tracks) */
#define FD_RSEEK_IN		0xcf	/* seek in (i.e. to higher tracks) */

/* DMA commands */
#define DMA_READ	0x46
#define DMA_WRITE	0x4A

/* FDC version return types */
#define FDC_NONE	0x00
#define FDC_UNKNOWN	0x10
#define FDC_8272A	0x20	/* Intel 8272a, NEC 765 */
#define FDC_765ED	0x30	/* Non-Intel 1MB-compatible FDC, can't detect */
#define FDC_82072	0x40	/* Intel 82072; 8272a + FIFO + DUMPREGS */
#define FDC_82077_ORIG	0x50	/* Original version of 82077AA, sans LOCK */
#define FDC_82077	0x52	/* 82077AA-1 */

#define FD_RESET_DELAY 20
#endif
