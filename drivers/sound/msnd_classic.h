/*********************************************************************
 *
 * msnd_classic.h
 *
 * Turtle Beach MultiSound Soundcard Driver for Linux
 *
 * Some parts of this header file were derived from the Turtle Beach
 * MultiSound Driver Development Kit.
 *
 * Copyright (C) 1998 Andrew Veliath
 * Copyright (C) 1993 Turtle Beach Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * $Id: msnd_classic.h,v 1.3 1998/06/09 20:39:34 andrewtv Exp $
 *
 ********************************************************************/
#ifndef __MSND_CLASSIC_H
#define __MSND_CLASSIC_H

#include <linux/config.h>

#define DSP_NUMIO		0x10

#define	HP_MEMM			0x08

#define	HP_BITM			0x0E
#define	HP_WAIT			0x0D
#define	HP_DSPR			0x0A
#define	HP_PROR			0x0B
#define	HP_BLKS			0x0C

#define	HPPRORESET_OFF		0
#define HPPRORESET_ON		1

#define HPDSPRESET_OFF		0
#define HPDSPRESET_ON		1

#define HPBLKSEL_0		0
#define HPBLKSEL_1		1

#define HPWAITSTATE_0		0
#define HPWAITSTATE_1		1

#define HPBITMODE_16		0
#define HPBITMODE_8		1

#define	HIDSP_INT_PLAY_UNDER	0x00
#define	HIDSP_INT_RECORD_OVER	0x01
#define	HIDSP_INPUT_CLIPPING	0x02
#define	HIDSP_MIDI_IN_OVER	0x10
#define	HIDSP_MIDI_OVERRUN_ERR  0x13

#define	HDEX_BASE	       	0x92
#define	HDEX_PLAY_START		(0 + HDEX_BASE)
#define	HDEX_PLAY_STOP		(1 + HDEX_BASE)
#define	HDEX_PLAY_PAUSE		(2 + HDEX_BASE)
#define	HDEX_PLAY_RESUME	(3 + HDEX_BASE)
#define	HDEX_RECORD_START	(4 + HDEX_BASE)
#define	HDEX_RECORD_STOP	(5 + HDEX_BASE)
#define	HDEX_MIDI_IN_START 	(6 + HDEX_BASE)
#define	HDEX_MIDI_IN_STOP	(7 + HDEX_BASE)
#define	HDEX_MIDI_OUT_START	(8 + HDEX_BASE)
#define	HDEX_MIDI_OUT_STOP	(9 + HDEX_BASE)
#define	HDEX_AUX_REQ		(10 + HDEX_BASE)

#define	HDEXAR_CLEAR_PEAKS	1
#define	HDEXAR_IN_SET_POTS	2
#define	HDEXAR_AUX_SET_POTS	3
#define	HDEXAR_CAL_A_TO_D	4
#define	HDEXAR_RD_EXT_DSP_BITS	5

#define TIME_PRO_RESET_DONE	0x028A
#define TIME_PRO_SYSEX		0x0040
#define TIME_PRO_RESET		0x0032

#define AGND			0x01
#define SIGNAL			0x02

#define EXT_DSP_BIT_DCAL	0x0001
#define EXT_DSP_BIT_MIDI_CON	0x0002

#define BUFFSIZE		0x8000
#define HOSTQ_SIZE		0x40

#define SRAM_CNTL_START		0x7F00
#define SMA_STRUCT_START	0x7F40

#define DAP_BUFF_SIZE		0x2400
#define DAR_BUFF_SIZE		0x2000

#define DAPQ_STRUCT_SIZE	0x10
#define DARQ_STRUCT_SIZE	0x10
#define DAPQ_BUFF_SIZE		(3 * 0x10)
#define DARQ_BUFF_SIZE		(3 * 0x10)
#define MODQ_BUFF_SIZE		0x400
#define MIDQ_BUFF_SIZE		0x200
#define DSPQ_BUFF_SIZE		0x40

#define DAPQ_DATA_BUFF		0x6C00
#define DARQ_DATA_BUFF		0x6C30
#define MODQ_DATA_BUFF		0x6C60
#define MIDQ_DATA_BUFF		0x7060
#define DSPQ_DATA_BUFF		0x7260

#define DAPQ_OFFSET		SRAM_CNTL_START
#define DARQ_OFFSET		(SRAM_CNTL_START + 0x08)
#define MODQ_OFFSET		(SRAM_CNTL_START + 0x10)
#define MIDQ_OFFSET		(SRAM_CNTL_START + 0x18)
#define DSPQ_OFFSET		(SRAM_CNTL_START + 0x20)

#define MOP_PROTEUS		0x10
#define MOP_EXTOUT		0x32
#define MOP_EXTTHRU		0x02
#define MOP_OUTMASK		0x01

#define MIP_EXTIN		0x01
#define MIP_PROTEUS		0x00
#define MIP_INMASK		0x32

struct SMA0_CommonData {
	WORD wCurrPlayBytes;
	WORD wCurrRecordBytes;
	WORD wCurrPlayVolLeft;
	WORD wCurrPlayVolRight;
	WORD wCurrInVolLeft;
	WORD wCurrInVolRight;
	WORD wUser_3;
	WORD wUser_4;
	DWORD dwUser_5;
	DWORD dwUser_6;
	WORD wUser_7;
	WORD wReserved_A;
	WORD wReserved_B;
	WORD wReserved_C;
	WORD wReserved_D;
	WORD wReserved_E;
	WORD wReserved_F;
	WORD wReserved_G;
	WORD wReserved_H;
	WORD wCurrDSPStatusFlags;
	WORD wCurrHostStatusFlags;
	WORD wCurrInputTagBits;
	WORD wCurrLeftPeak;
	WORD wCurrRightPeak;
	WORD wExtDSPbits;
	BYTE bExtHostbits;
	BYTE bBoardLevel;
	BYTE bInPotPosRight;
	BYTE bInPotPosLeft;
	BYTE bAuxPotPosRight;
	BYTE bAuxPotPosLeft;
	WORD wCurrMastVolLeft;
	WORD wCurrMastVolRight;
	BYTE bUser_12;
	BYTE bUser_13;
	WORD wUser_14;
	WORD wUser_15;
	WORD wCalFreqAtoD;
	WORD wUser_16;
	WORD wUser_17;
} GCC_PACKED;

#ifdef HAVE_DSPCODEH
#  include "msndperm.c"
#  include "msndinit.c"
#  define PERMCODE		msndperm
#  define INITCODE		msndinit
#  define PERMCODESIZE		sizeof(msndperm)
#  define INITCODESIZE		sizeof(msndinit)
#else
#  define PERMCODEFILE		CONFIG_MSNDCLAS_PERM_FILE
#  define INITCODEFILE		CONFIG_MSNDCLAS_INIT_FILE
#  define PERMCODE		dspini
#  define INITCODE		permini
#  define PERMCODESIZE		sizeof_dspini
#  define INITCODESIZE		sizeof_permini
#endif
#define LONGNAME		"MultiSound (Classic/Monterey/Tahiti)"

#endif /* __MSND_CLASSIC_H */
