/*********************************************************************
 *
 * msnd_pinnacle.h
 *
 * Turtle Beach MultiSound Sound Card Driver for Linux
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
 * $Id: msnd_pinnacle.h,v 1.3 1998/06/09 20:39:34 andrewtv Exp $
 *
 ********************************************************************/
#ifndef __MSND_PINNACLE_H
#define __MSND_PINNACLE_H

#include <linux/config.h>

#define DSP_NUMIO		0x08

#define	HP_DSPR			0x04
#define	HP_BLKS			0x04

#define HPDSPRESET_OFF		2
#define HPDSPRESET_ON		0

#define HPBLKSEL_0		2
#define HPBLKSEL_1		3

#define	HIMT_DAT_OFF		0x03

#define	HIDSP_PLAY_UNDER	0x00
#define	HIDSP_INT_PLAY_UNDER	0x01
#define	HIDSP_SSI_TX_UNDER  	0x02
#define HIDSP_RECQ_OVERFLOW	0x08
#define HIDSP_INT_RECORD_OVER	0x09
#define HIDSP_SSI_RX_OVERFLOW	0x0a

#define	HIDSP_MIDI_IN_OVER	0x10

#define	HIDSP_MIDI_FRAME_ERR	0x11
#define	HIDSP_MIDI_PARITY_ERR	0x12
#define	HIDSP_MIDI_OVERRUN_ERR	0x13

#define HIDSP_INPUT_CLIPPING	0x20
#define	HIDSP_MIX_CLIPPING	0x30
#define HIDSP_DAT_IN_OFF	0x21

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

#define	HDEXAR_SET_ANA_IN	0
#define	HDEXAR_CLEAR_PEAKS	1
#define	HDEXAR_IN_SET_POTS	2
#define	HDEXAR_AUX_SET_POTS	3
#define	HDEXAR_CAL_A_TO_D	4
#define	HDEXAR_RD_EXT_DSP_BITS	5

#define	HDEXAR_SET_SYNTH_IN     4
#define	HDEXAR_READ_DAT_IN      5
#define	HDEXAR_MIC_SET_POTS     6
#define	HDEXAR_SET_DAT_IN       7

#define HDEXAR_SET_SYNTH_48     8
#define HDEXAR_SET_SYNTH_44     9

#define TIME_PRO_RESET_DONE	0x028A
#define TIME_PRO_SYSEX		0x001E
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
#define MIDQ_BUFF_SIZE		0x800
#define DSPQ_BUFF_SIZE		0x5A0

#define DAPQ_DATA_BUFF		0x6C00
#define DARQ_DATA_BUFF		0x6C30
#define MODQ_DATA_BUFF		0x6C60
#define MIDQ_DATA_BUFF		0x7060
#define DSPQ_DATA_BUFF		0x7860

#define DAPQ_OFFSET		SRAM_CNTL_START
#define DARQ_OFFSET		(SRAM_CNTL_START + 0x08)
#define MODQ_OFFSET		(SRAM_CNTL_START + 0x10)
#define MIDQ_OFFSET		(SRAM_CNTL_START + 0x18)
#define DSPQ_OFFSET		(SRAM_CNTL_START + 0x20)

#define WAVEHDR_MOP		0
#define EXTOUT_MOP		1
#define HWINIT_MOP		0xFE
#define NO_MOP			0xFF

#define MAX_MOP			1

#define EXTIN_MIP		0
#define WAVEHDR_MIP		1
#define HWINIT_MIP		0xFE

#define MAX_MIP			1

struct SMA0_CommonData {
	WORD wCurrPlayBytes;
	WORD wCurrRecordBytes;
	WORD wCurrPlayVolLeft;
	WORD wCurrPlayVolRight;

	WORD wCurrInVolLeft;
	WORD wCurrInVolRight;
	WORD wCurrMHdrVolLeft;
	WORD wCurrMHdrVolRight;

	DWORD dwCurrPlayPitch;
	DWORD dwCurrPlayRate;

	WORD wCurrMIDIIOPatch;

	WORD wCurrPlayFormat;
	WORD wCurrPlaySampleSize;
	WORD wCurrPlayChannels;
	WORD wCurrPlaySampleRate;

	WORD wCurrRecordFormat;
	WORD wCurrRecordSampleSize;
	WORD wCurrRecordChannels;
	WORD wCurrRecordSampleRate;

	WORD wCurrDSPStatusFlags;
	WORD wCurrHostStatusFlags;

	WORD wCurrInputTagBits;
	WORD wCurrLeftPeak;
	WORD wCurrRightPeak;

	BYTE bMicPotPosLeft;
	BYTE bMicPotPosRight;

	BYTE bMicPotMaxLeft;
	BYTE bMicPotMaxRight;

	BYTE bInPotPosLeft;
	BYTE bInPotPosRight;
	
	BYTE bAuxPotPosLeft;
	BYTE bAuxPotPosRight;
	
	BYTE bInPotMaxLeft;
	BYTE bInPotMaxRight;
	BYTE bAuxPotMaxLeft;
	BYTE bAuxPotMaxRight;
	BYTE bInPotMaxMethod;
	BYTE bAuxPotMaxMethod;

	WORD wCurrMastVolLeft;
	WORD wCurrMastVolRight;

	WORD wCalFreqAtoD;

	WORD wCurrAuxVolLeft;
	WORD wCurrAuxVolRight;

	WORD wCurrPlay1VolLeft;
	WORD wCurrPlay1VolRight;
	WORD wCurrPlay2VolLeft;
	WORD wCurrPlay2VolRight;
	WORD wCurrPlay3VolLeft;
	WORD wCurrPlay3VolRight;
	WORD wCurrPlay4VolLeft;
	WORD wCurrPlay4VolRight;
	WORD wCurrPlay1PeakLeft;
	WORD wCurrPlay1PeakRight;
	WORD wCurrPlay2PeakLeft;
	WORD wCurrPlay2PeakRight;
	WORD wCurrPlay3PeakLeft;
	WORD wCurrPlay3PeakRight;
	WORD wCurrPlay4PeakLeft;
	WORD wCurrPlay4PeakRight;
	WORD wCurrPlayPeakLeft;
	WORD wCurrPlayPeakRight;

	WORD wCurrDATSR;
	WORD wCurrDATRXCHNL;
	WORD wCurrDATTXCHNL;
	WORD wCurrDATRXRate;

	DWORD dwDSPPlayCount;
} GCC_PACKED;

#ifdef HAVE_DSPCODEH
#  include "pndsperm.c"
#  include "pndspini.c"
#  define PERMCODE		pndsperm
#  define INITCODE		pndspini
#  define PERMCODESIZE		sizeof(pndsperm)
#  define INITCODESIZE		sizeof(pndspini)
#else
#  define PERMCODEFILE		CONFIG_MSNDPIN_PERM_FILE
#  define INITCODEFILE		CONFIG_MSNDPIN_INIT_FILE
#  define PERMCODE		dspini
#  define INITCODE		permini
#  define PERMCODESIZE		sizeof_dspini
#  define INITCODESIZE		sizeof_permini
#endif
#define LONGNAME		"MultiSound (Pinnacle/Fiji)"

#endif /* __MSND_PINNACLE_H */
