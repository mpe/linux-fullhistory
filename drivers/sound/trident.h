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
#define PCI_VENDOR_ID_TRIDENT          0x1023
#endif
#ifndef PCI_DEVICE_ID_TRIDENT_4DWAVE_DX 
#define PCI_DEVICE_ID_TRIDENT_4DWAVE_DX 0x2000
#endif
#ifndef PCI_DEVICE_ID_TRIDENT_4DWAVE_NX 
#define PCI_DEVICE_ID_TRIDENT_4DWAVE_NX 0x2001
#endif

/*
 * Direct registers
 */

#ifndef FALSE
#define FALSE 0
#define TRUE  1
#endif

#define TRID_REG( trident, x ) ( (trident) -> iobase + (x) )

#define CHANNEL_REGS    5
#define CHANNEL_START   0xe0   // The first bytes of the contiguous register space.

#define ID_4DWAVE_DX        0x2000
#define ID_4DWAVE_NX        0x2001


// Register definitions

// Global registers

// T2 legacy dma control registers.
#define LEGACY_DMAR0                0x00  // ADR0
#define LEGACY_DMAR4                0x04  // CNT0
#define LEGACY_DMAR11               0x0b  // MOD 
#define LEGACY_DMAR15               0x0f  // MMR 

#define T4D_START_A		     0x80
#define T4D_STOP_A		     0x84
#define T4D_DLY_A		     0x88
#define T4D_SIGN_CSO_A		     0x8c
#define T4D_CSPF_A		     0x90
#define T4D_CEBC_A		     0x94
#define T4D_AINT_A		     0x98
#define T4D_AINTEN_A		     0x9c
#define T4D_LFO_GC_CIR               0xa0
#define T4D_MUSICVOL_WAVEVOL         0xa8
#define T4D_SBDELTA_DELTA_R          0xac
#define T4D_MISCINT                  0xb0
#define T4D_START_B                  0xb4
#define T4D_STOP_B                   0xb8
#define T4D_SBBL_SBCL                0xc0
#define T4D_SBCTRL_SBE2R_SBDD        0xc4
#define T4D_AINT_B                   0xd8
#define T4D_AINTEN_B                 0xdc

// MPU-401 UART
#define T4D_MPU401_BASE             0x20
#define T4D_MPUR0                   0x20
#define T4D_MPUR1                   0x21
#define T4D_MPUR2                   0x22
#define T4D_MPUR3                   0x23

// S/PDIF Registers
#define NX_SPCTRL_SPCSO             0x24
#define NX_SPLBA                    0x28
#define NX_SPESO                    0x2c
#define NX_SPCSTATUS                0x64

// Channel Registers

#define CH_DX_CSO_ALPHA_FMS         0xe0
#define CH_DX_ESO_DELTA             0xe8
#define CH_DX_FMC_RVOL_CVOL         0xec

#define CH_NX_DELTA_CSO             0xe0
#define CH_NX_DELTA_ESO             0xe8
#define CH_NX_ALPHA_FMS_FMC_RVOL_CVOL 0xec

#define CH_LBA                      0xe4
#define CH_GVSEL_PAN_VOL_CTRL_EC    0xf0

// AC-97 Registers

#define DX_ACR0_AC97_W              0x40
#define DX_ACR1_AC97_R              0x44
#define DX_ACR2_AC97_COM_STAT       0x48

#define NX_ACR0_AC97_COM_STAT       0x40
#define NX_ACR1_AC97_W              0x44
#define NX_ACR2_AC97_R_PRIMARY      0x48
#define NX_ACR3_AC97_R_SECONDARY    0x4c

#define AC97_SIGMATEL_DAC2INVERT    0x6E
#define AC97_SIGMATEL_BIAS1         0x70
#define AC97_SIGMATEL_BIAS2         0x72
#define AC97_SIGMATEL_CIC1          0x76
#define AC97_SIGMATEL_CIC2          0x78

#endif				/* __TRID4DWAVE_H */

