#ifndef __TRID4DWAVE_H
#define __TRID4DWAVE_H

/*
 *  audio@tridentmicro.com
 *  Fri Feb 19 15:55:28 MST 1999
 *  Definitions for Trident 4DWave DX/NX chips
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef PCI_VENDOR_ID_TRIDENT
#define PCI_VENDOR_ID_TRIDENT		0x1023
#endif

#ifndef PCI_VENDOR_ID_SI
#define PCI_VENDOR_ID_SI			0x0139
#endif

#ifndef PCI_DEVICE_ID_TRIDENT_4DWAVE_DX
#define PCI_DEVICE_ID_TRIDENT_4DWAVE_DX	0x2000
#endif

#ifndef PCI_DEVICE_ID_TRIDENT_4DWAVE_NX
#define PCI_DEVICE_ID_TRIDENT_4DWAVE_NX	0x2001
#endif

#ifndef PCI_DEVICE_ID_SI_7018
#define PCI_DEVICE_ID_SI_7018		0x7018
#endif

/*
 * Direct registers
 */

#ifndef FALSE
#define FALSE 0
#define TRUE  1
#endif

#define TRID_REG( trident, x ) ( (trident) -> iobase + (x) )

#define CHANNEL_REGS	5
#define CHANNEL_START	0xe0   // The first bytes of the contiguous register space.

#define BANK_A 0
#define BANK_B 1
#define NUM_BANKS 2

#define ID_4DWAVE_DX	0x2000
#define ID_4DWAVE_NX	0x2001
#define ID_SI_7018	0x7018

// Register definitions

// Global registers

// T2 legacy dma control registers.
#define LEGACY_DMAR0		0x00  // ADR0
#define LEGACY_DMAR4		0x04  // CNT0
#define LEGACY_DMAR11		0x0b  // MOD 
#define LEGACY_DMAR15		0x0f  // MMR 

#define T4D_START_A		0x80
#define T4D_STOP_A		0x84
#define T4D_DLY_A			0x88
#define T4D_SIGN_CSO_A		0x8c
#define T4D_CSPF_A		0x90
#define T4D_CEBC_A		0x94
#define T4D_AINT_A		0x98
#define T4D_EINT_A		0x9c
#define T4D_LFO_GC_CIR		0xa0
#define T4D_AINTEN_A		0xa4
#define T4D_MUSICVOL_WAVEVOL	0xa8
#define T4D_SBDELTA_DELTA_R	0xac
#define T4D_MISCINT		0xb0
#define T4D_START_B		0xb4
#define T4D_STOP_B		0xb8
#define T4D_CSPF_B		0xbc
#define T4D_SBBL_SBCL		0xc0
#define T4D_SBCTRL_SBE2R_SBDD	0xc4
#define T4D_STIMER		0xc8
#define T4D_LFO_B_I2S_DELTA	0xcc
#define T4D_AINT_B		0xd8
#define T4D_AINTEN_B		0xdc

// MPU-401 UART
#define T4D_MPU401_BASE		0x20
#define T4D_MPUR0			0x20
#define T4D_MPUR1			0x21
#define T4D_MPUR2			0x22
#define T4D_MPUR3			0x23

// S/PDIF Registers
#define NX_SPCTRL_SPCSO		0x24
#define NX_SPLBA			0x28
#define NX_SPESO			0x2c
#define NX_SPCSTATUS		0x64

// Channel Registers

#define CH_DX_CSO_ALPHA_FMS	0xe0
#define CH_DX_ESO_DELTA		0xe8
#define CH_DX_FMC_RVOL_CVOL	0xec

#define CH_NX_DELTA_CSO		0xe0
#define CH_NX_DELTA_ESO		0xe8
#define CH_NX_ALPHA_FMS_FMC_RVOL_CVOL 0xec

#define CH_LBA			0xe4
#define CH_GVSEL_PAN_VOL_CTRL_EC	0xf0

// AC-97 Registers

#define DX_ACR0_AC97_W		0x40
#define DX_ACR1_AC97_R		0x44
#define DX_ACR2_AC97_COM_STAT	0x48

#define NX_ACR0_AC97_COM_STAT	0x40
#define NX_ACR1_AC97_W		0x44
#define NX_ACR2_AC97_R_PRIMARY	0x48
#define NX_ACR3_AC97_R_SECONDARY	0x4c

#define SI_AC97_WRITE		0x40
#define SI_AC97_READ		0x44
#define SI_SERIAL_INTF_CTRL	0x48
#define SI_AC97_GPIO		0x4c

#define AC97_SIGMATEL_DAC2INVERT	0x6E
#define AC97_SIGMATEL_BIAS1	0x70
#define AC97_SIGMATEL_BIAS2	0x72
#define AC97_SIGMATEL_CIC1	0x76
#define AC97_SIGMATEL_CIC2	0x78

#define SI_AC97_BUSY_WRITE 0x8000
#define SI_AC97_AUDIO_BUSY 0x4000
#define DX_AC97_BUSY_WRITE 0x8000
#define NX_AC97_BUSY_WRITE 0x0800
#define SI_AC97_BUSY_READ  0x8000
#define DX_AC97_BUSY_READ  0x8000
#define NX_AC97_BUSY_READ  0x0800
#define AC97_REG_ADDR      0x000000ff

enum serial_intf_ctrl_bits {
	WARM_REST   = 0x00000001, COLD_RESET  = 0x00000002,
	I2S_CLOCK   = 0x00000004, PCM_SEC_AC97= 0x00000008,
	AC97_DBL_RATE = 0x00000010, SPDIF_EN  = 0x00000020,
	I2S_OUTPUT_EN = 0x00000040, I2S_INPUT_EN = 0x00000080,
	PCMIN       = 0x00000100, LINE1IN     = 0x00000200,
	MICIN       = 0x00000400, LINE2IN     = 0x00000800,
};

enum global_control_bits {
	CHANNLE_IDX = 0x0000003f, PB_RESET    = 0x00000100,
	PAUSE_ENG   = 0x00000200,
	OVERRUN_IE  = 0x00000400, UNDERRUN_IE = 0x00000800,
	ENDLP_IE    = 0x00001000, MIDLP_IE    = 0x00002000,
	ETOG_IE     = 0x00004000,
	EDROP_IE    = 0x00008000, BANK_B_EN   = 0x00010000
};

enum miscint_bits {
	PB_UNDERRUN_IRO = 0x00000001, REC_OVERRUN_IRQ = 0x00000002,
	SB_IRQ          = 0x00000004, MPU401_IRQ      = 0x00000008,
	OPL3_IRQ        = 0x00000010, ADDRESS_IRQ     = 0x00000020,
	ENVELOPE_IRQ    = 0x00000040, ST_IRQ          = 0x00000080,
	PB_UNDERRUN     = 0x00000100, REC_OVERRUN     = 0x00000200,
	MIXER_UNDERFLOW = 0x00000400, MIXER_OVERFLOW  = 0x00000800,
	ST_TARGET_REACHED = 0x00008000, PB_24K_MODE   = 0x00010000, 
	ST_IRQ_EN       = 0x00800000, ACGPIO_IRQ      = 0x01000000
};

#define IWriteAinten( x ) \
	{int i; \
	 for( i= 0; i < ChanDwordCount; i++) \
		outl((x)->lpChAinten[i], TRID_REG(trident, (x)->lpAChAinten[i]));}

#define IReadAinten( x ) \
	{int i; \
	 for( i= 0; i < ChanDwordCount; i++) \
	 (x)->lpChAinten[i] = inl(TRID_REG(trident, (x)->lpAChAinten[i]));}

#define ReadAint( x ) \
	IReadAint( x ) 

#define WriteAint( x ) \
	IWriteAint( x ) 

#define IWriteAint( x ) \
	{int i; \
	 for( i= 0; i < ChanDwordCount; i++) \
	 outl((x)->lpChAint[i], TRID_REG(trident, (x)->lpAChAint[i]));}

#define IReadAint( x ) \
	{int i; \
	 for( i= 0; i < ChanDwordCount; i++) \
	 (x)->lpChAint[i] = inl(TRID_REG(trident, (x)->lpAChAint[i]));}

#define VALIDATE_MAGIC(FOO,MAG)				\
({						  \
	if (!(FOO) || (FOO)->magic != MAG) { \
		printk(invalid_magic,__FUNCTION__);	       \
		return -ENXIO;			  \
	}					  \
})

#define VALIDATE_STATE(a) VALIDATE_MAGIC(a,TRIDENT_STATE_MAGIC)
#define VALIDATE_CARD(a) VALIDATE_MAGIC(a,TRIDENT_CARD_MAGIC)

#endif /* __TRID4DWAVE_H */

