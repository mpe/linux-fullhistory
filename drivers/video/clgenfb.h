/*
 * drivers/video/clgenfb.h - Cirrus Logic chipset constants
 *
 * Copyright 1999 Jeff Garzik <jgarzik@pobox.com>
 *
 * Original clgenfb author:  Frank Neumann
 *
 * Based on retz3fb.c and clgen.c:
 *      Copyright (C) 1997 Jes Sorensen
 *      Copyright (C) 1996 Frank Neumann
 *
 ***************************************************************
 *
 * Format this code with GNU indent '-kr -i8 -pcs' options.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */


/* OLD COMMENT: definitions for Piccolo/SD64 VGA controller chip   */
/* OLD COMMENT: these definitions might most of the time also work */
/* OLD COMMENT: for other CL-GD542x/543x based boards..            */

/*** External/General Registers ***/
#define POS102	0x102  	/* POS102 register */
#define VSSM	0x46e8 	/* Adapter Sleep */
#define VSSM2	0x3c3	/* Motherboard Sleep */

/*** VGA Sequencer Registers ***/
#define SEQR0	0x0	/* Reset */
/* the following are from the "extension registers" group */
#define SEQR6	0x6	/* Unlock ALL Extensions */
#define SEQR7	0x7	/* Extended Sequencer Mode */
#define SEQR8	0x8	/* EEPROM Control */
#define SEQR9	0x9	/* Scratch Pad 0 (do not access!) */
#define SEQRA	0xa	/* Scratch Pad 1 (do not access!) */
#define SEQRB	0xb	/* VCLK0 Numerator */
#define SEQRC	0xc	/* VCLK1 Numerator */
#define SEQRD	0xd	/* VCLK2 Numerator */
#define SEQRE	0xe	/* VCLK3 Numerator */
#define SEQRF	0xf	/* DRAM Control */
#define SEQR10	0x10	/* Graphics Cursor X Position */
#define SEQR11	0x11	/* Graphics Cursor Y Position */
#define SEQR12	0x12	/* Graphics Cursor Attributes */
#define SEQR13	0x13	/* Graphics Cursor Pattern Address Offset */
#define SEQR14	0x14	/* Scratch Pad 2 (CL-GD5426/'28 Only) (do not access!) */
#define SEQR15	0x15	/* Scratch Pad 3 (CL-GD5426/'28 Only) (do not access!) */
#define SEQR16	0x16	/* Performance Tuning (CL-GD5424/'26/'28 Only) */
#define SEQR17	0x17	/* Configuration ReadBack and Extended Control (CL-GF5428 Only) */
#define SEQR18	0x18	/* Signature Generator Control (Not CL-GD5420) */
#define SEQR19	0x19	/* Signature Generator Result Low Byte (Not CL-GD5420) */
#define SEQR1A	0x1a	/* Signature Generator Result High Byte (Not CL-GD5420) */
#define SEQR1B	0x1b	/* VCLK0 Denominator and Post-Scalar Value */
#define SEQR1C	0x1c	/* VCLK1 Denominator and Post-Scalar Value */
#define SEQR1D	0x1d	/* VCLK2 Denominator and Post-Scalar Value */
#define SEQR1E	0x1e	/* VCLK3 Denominator and Post-Scalar Value */
#define SEQR1F	0x1f	/* BIOS ROM write enable and MCLK Select */

/*** CRT Controller Registers ***/
#define CRT22	0x22	/* Graphics Data Latches ReadBack */
#define CRT24	0x24	/* Attribute Controller Toggle ReadBack */
#define CRT26	0x26	/* Attribute Controller Index ReadBack */
/* the following are from the "extension registers" group */
#define CRT19	0x19	/* Interlace End */
#define CRT1A	0x1a	/* Interlace Control */
#define CRT1B	0x1b	/* Extended Display Controls */
#define CRT1C	0x1c	/* Sync adjust and genlock register */
#define CRT1D	0x1d	/* Overlay Extended Control register */
#define CRT25	0x25	/* Part Status Register */
#define CRT27	0x27	/* ID Register */
#define CRT51	0x51	/* P4 disable "flicker fixer" */

/*** Graphics Controller Registers ***/
/* the following are from the "extension registers" group */
#define GR9	0x9	/* Offset Register 0 */
#define GRA	0xa	/* Offset Register 1 */
#define GRB	0xb	/* Graphics Controller Mode Extensions */
#define GRC	0xc	/* Color Key (CL-GD5424/'26/'28 Only) */
#define GRD	0xd	/* Color Key Mask (CL-GD5424/'26/'28 Only) */
#define GRE	0xe	/* Miscellaneous Control (Cl-GD5428 Only) */
#define GRF	0xf	/* Display Compression Control register */
#define GR10	0x10	/* 16-bit Pixel BG Color High Byte (Not CL-GD5420) */
#define GR11	0x11	/* 16-bit Pixel FG Color High Byte (Not CL-GD5420) */
#define GR12	0x12	/* Background Color Byte 2 Register */
#define GR13	0x13	/* Foreground Color Byte 2 Register */
#define GR14	0x14	/* Background Color Byte 3 Register */
#define GR15	0x15	/* Foreground Color Byte 3 Register */
/* the following are CL-GD5426/'28 specific blitter registers */
#define GR20	0x20	/* BLT Width Low */
#define GR21	0x21	/* BLT Width High */
#define GR22	0x22	/* BLT Height Low */
#define GR23	0x23	/* BLT Height High */
#define GR24	0x24	/* BLT Destination Pitch Low */
#define GR25	0x25	/* BLT Destination Pitch High */
#define GR26	0x26	/* BLT Source Pitch Low */
#define GR27	0x27	/* BLT Source Pitch High */
#define GR28	0x28	/* BLT Destination Start Low */
#define GR29	0x29	/* BLT Destination Start Mid */
#define GR2A    0x2a	/* BLT Destination Start High */
#define GR2C	0x2c	/* BLT Source Start Low */
#define GR2D	0x2d	/* BLT Source Start Mid */
#define GR2E	0x2e	/* BLT Source Start High */
#define GR2F	0x2f	/* Picasso IV Blitter compat mode..? */
#define GR30	0x30	/* BLT Mode */
#define GR31	0x31	/* BLT Start/Status */
#define GR32	0x32	/* BLT Raster Operation */
#define GR33	0x33	/* another P4 "compat" register.. */
#define GR34	0x34	/* Transparent Color Select Low */
#define GR35	0x35	/* Transparent Color Select High */
#define GR38	0x38	/* Source Transparent Color Mask Low */
#define GR39	0x39	/* Source Transparent Color Mask High */

/*** Attribute Controller Registers ***/
#define AR33	0x33	/* The "real" Pixel Panning register (?) */
#define AR34	0x34	/* *TEST* */

/* board types */
#define BT_NONE     0
#define BT_SD64     1
#define BT_PICCOLO  2
#define BT_PICASSO  3
#define BT_SPECTRUM 4
#define BT_PICASSO4 5
#define BT_GD543X    6		/* Alpine family */
#define BT_GD5480    7

