/*
 *    ixj.h
 *
 *    Device Driver for the Internet PhoneJACK and
 *    Internet LineJACK Telephony Cards.
 *
 *    (c) Copyright 1999 Quicknet Technologies, Inc.
 *
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License
 *    as published by the Free Software Foundation; either version
 *    2 of the License, or (at your option) any later version.
 *
 * Author:          Ed Okerson, <eokerson@quicknet.net>
 *    
 * Contributors:    Greg Herlein, <gherlein@quicknet.net>
 *                  David W. Erhart, <derhart@quicknet.net>
 *                  John Sellers, <jsellers@quicknet.net>
 *                  Mike Preston, <mpreston@quicknet.net>
 *
 * More information about the hardware related to this driver can be found
 * at our website:    http://www.quicknet.net
 *
 * Fixes:
 *	Linux 2.3 port, 	Alan Cox
 */
static char ixj_h_rcsid[] = "$Id: ixj.h,v 3.4 1999/12/16 22:18:36 root Exp root $";

#ifndef _I386_TYPES_H
#include <asm/types.h>
#endif

#include <linux/ixjuser.h>
#include <linux/phonedev.h>

typedef __u16 WORD;
typedef __u32 DWORD;
typedef __u8 BYTE;
typedef __u8 BOOL;

#define IXJMAX 16

#define TRUE 1
#define FALSE 0

#ifndef min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef max
#define max(a,b) (((a)>(b))?(a):(b))
#endif

/******************************************************************************
*
*  This structure when unioned with the structures below makes simple byte
*  access to the registers easier.
*
******************************************************************************/
typedef struct {
	unsigned char low;
	unsigned char high;
} BYTES;

int ixj_WriteDSPCommand(unsigned short, int board);

/******************************************************************************
*
*  This structure represents the Hardware Control Register of the CT8020/8021
*  The CT8020 is used in the Internet PhoneJACK, and the 8021 in the
*  Internet LineJACK
*
******************************************************************************/
typedef struct {
	unsigned int rxrdy:1;
	unsigned int txrdy:1;
	unsigned int status:1;
	unsigned int auxstatus:1;
	unsigned int rxdma:1;
	unsigned int txdma:1;
	unsigned int rxburst:1;
	unsigned int txburst:1;
	unsigned int dmadir:1;
	unsigned int cont:1;
	unsigned int irqn:1;
	unsigned int t:5;
} HCRBIT;

typedef union {
	HCRBIT bits;
	BYTES bytes;
} HCR;

/******************************************************************************
*
*  This structure represents the Hardware Status Register of the CT8020/8021
*  The CT8020 is used in the Internet PhoneJACK, and the 8021 in the
*  Internet LineJACK
*
******************************************************************************/
typedef struct {
	unsigned int controlrdy:1;
	unsigned int auxctlrdy:1;
	unsigned int statusrdy:1;
	unsigned int auxstatusrdy:1;
	unsigned int rxrdy:1;
	unsigned int txrdy:1;
	unsigned int restart:1;
	unsigned int irqn:1;
	unsigned int rxdma:1;
	unsigned int txdma:1;
	unsigned int cohostshutdown:1;
	unsigned int t:5;
} HSRBIT;

typedef union {
	HSRBIT bits;
	BYTES bytes;
} HSR;

/******************************************************************************
*
*  This structure represents the General Purpose IO Register of the CT8020/8021
*  The CT8020 is used in the Internet PhoneJACK, and the 8021 in the
*  Internet LineJACK
*
******************************************************************************/
typedef struct {
	unsigned int x:1;
	unsigned int gpio1:1;
	unsigned int gpio2:1;
	unsigned int gpio3:1;
	unsigned int gpio4:1;
	unsigned int gpio5:1;
	unsigned int gpio6:1;
	unsigned int gpio7:1;
	unsigned int xread:1;
	unsigned int gpio1read:1;
	unsigned int gpio2read:1;
	unsigned int gpio3read:1;
	unsigned int gpio4read:1;
	unsigned int gpio5read:1;
	unsigned int gpio6read:1;
	unsigned int gpio7read:1;
} GPIOBIT;

typedef union {
	GPIOBIT bits;
	BYTES bytes;
	unsigned short word;
} GPIO;

/******************************************************************************
*
*  This structure represents the Line Monitor status response
*
******************************************************************************/
typedef struct {
	unsigned int digit:4;
	unsigned int cpf_valid:1;
	unsigned int dtmf_valid:1;
	unsigned int peak:1;
	unsigned int z:1;
	unsigned int f0:1;
	unsigned int f1:1;
	unsigned int f2:1;
	unsigned int f3:1;
	unsigned int frame:4;
} LMON;

typedef union {
	LMON bits;
	BYTES bytes;
} DTMF;

typedef struct {
	unsigned int z:7;
	unsigned int dtmf_en:1;
	unsigned int y:4;
	unsigned int F3:1;
	unsigned int F2:1;
	unsigned int F1:1;
	unsigned int F0:1;
} CP;

typedef union {
	CP bits;
	BYTES bytes;
} CPTF;

/******************************************************************************
*
*  This structure represents the Status Control Register on the Internet
*  LineJACK
*
******************************************************************************/
typedef struct {
	unsigned int c0:1;
	unsigned int c1:1;
	unsigned int stereo:1;
	unsigned int daafsyncen:1;
	unsigned int led1:1;
	unsigned int led2:1;
	unsigned int led3:1;
	unsigned int led4:1;
} PSCRWI;			// Internet LineJACK and Internet PhoneJACK Lite

typedef struct {
	unsigned int eidp:1;
	unsigned int eisd:1;
	unsigned int x:6;
} PSCRWP;			// Internet PhoneJACK PCI

typedef union {
	PSCRWI bits;
	PSCRWP pcib;
	char byte;
} PLD_SCRW;

typedef struct {
	unsigned int c0:1;
	unsigned int c1:1;
	unsigned int x:1;
	unsigned int d0ee:1;
	unsigned int mixerbusy:1;
	unsigned int sci:1;
	unsigned int dspflag:1;
	unsigned int daaflag:1;
} PSCRRI;

typedef struct {
	unsigned int eidp:1;
	unsigned int eisd:1;
	unsigned int x:4;
	unsigned int dspflag:1;
	unsigned int det:1;
} PSCRRP;

typedef union {
	PSCRRI bits;
	PSCRRP pcib;
	char byte;
} PLD_SCRR;

/******************************************************************************
*
*  These structures represents the SLIC Control Register on the
*  Internet LineJACK
*
******************************************************************************/
typedef struct {
	unsigned int c1:1;
	unsigned int c2:1;
	unsigned int c3:1;
	unsigned int b2en:1;
	unsigned int spken:1;
	unsigned int rly1:1;
	unsigned int rly2:1;
	unsigned int rly3:1;
} PSLICWRITE;

typedef struct {
	unsigned int state:3;
	unsigned int b2en:1;
	unsigned int spken:1;
	unsigned int c3:1;
	unsigned int potspstn:1;
	unsigned int det:1;
} PSLICREAD;

typedef struct {
	unsigned int c1:1;
	unsigned int c2:1;
	unsigned int c3:1;
	unsigned int b2en:1;
	unsigned int e1:1;
	unsigned int mic:1;
	unsigned int spk:1;
	unsigned int x:1;
} PSLICPCI;

typedef union {
	PSLICPCI pcib;
	PSLICWRITE bits;
	PSLICREAD slic;
	char byte;
} PLD_SLICW;

typedef union {
	PSLICPCI pcib;
	PSLICREAD bits;
	char byte;
} PLD_SLICR;

/******************************************************************************
*
*  These structures represents the Clock Control Register on the
*  Internet LineJACK
*
******************************************************************************/
typedef struct {
	unsigned int clk0:1;
	unsigned int clk1:1;
	unsigned int clk2:1;
	unsigned int x0:1;
	unsigned int slic_e1:1;
	unsigned int x1:1;
	unsigned int x2:1;
	unsigned int x3:1;
} PCLOCK;

typedef union {
	PCLOCK bits;
	char byte;
} PLD_CLOCK;

/******************************************************************************
*
*  These structures deal with the mixer on the Internet LineJACK
*
******************************************************************************/

typedef struct {
	unsigned short vol[10];
	unsigned int recsrc;
	unsigned int modcnt;
	unsigned short micpreamp;
} MIX;

/******************************************************************************
*
*  These structures deal with the DAA on the Internet LineJACK
*
******************************************************************************/

typedef struct _DAA_REGS {
	//-----------------------------------------------
	// SOP Registers
	//
	BYTE bySOP;

	union _SOP_REGS {
		struct _SOP {
			union	// SOP - CR0 Register
			 {
				BYTE reg;
				struct _CR0_BITREGS {
					BYTE CLK_EXT:1;		// cr0[0:0]

					BYTE RIP:1;	// cr0[1:1]

					BYTE AR:1;	// cr0[2:2]

					BYTE AX:1;	// cr0[3:3]

					BYTE FRR:1;	// cr0[4:4]

					BYTE FRX:1;	// cr0[5:5]

					BYTE IM:1;	// cr0[6:6]

					BYTE TH:1;	// cr0[7:7]

				} bitreg;
			} cr0;

			union	// SOP - CR1 Register
			 {
				BYTE reg;
				struct _CR1_REGS {
					BYTE RM:1;	// cr1[0:0]

					BYTE RMR:1;	// cr1[1:1]

					BYTE No_auto:1;		// cr1[2:2]

					BYTE Pulse:1;	// cr1[3:3]

					BYTE P_Tone1:1;		// cr1[4:4]

					BYTE P_Tone2:1;		// cr1[5:5]

					BYTE E_Tone1:1;		// cr1[6:6]

					BYTE E_Tone2:1;		// cr1[7:7]

				} bitreg;
			} cr1;

			union	// SOP - CR2 Register
			 {
				BYTE reg;
				struct _CR2_REGS {
					BYTE Call_II:1;		// CR2[0:0]

					BYTE Call_I:1;	// CR2[1:1]

					BYTE Call_en:1;		// CR2[2:2]

					BYTE Call_pon:1;	// CR2[3:3]

					BYTE IDR:1;	// CR2[4:4]

					BYTE COT_R:3;	// CR2[5:7]

				} bitreg;
			} cr2;

			union	// SOP - CR3 Register
			 {
				BYTE reg;
				struct _CR3_REGS {
					BYTE DHP_X:1;	// CR3[0:0]

					BYTE DHP_R:1;	// CR3[1:1]

					BYTE Cal_pctl:1;	// CR3[2:2]

					BYTE SEL:1;	// CR3[3:3]

					BYTE TestLoops:4;	// CR3[4:7]

				} bitreg;
			} cr3;

			union	// SOP - CR4 Register
			 {
				BYTE reg;
				struct _CR4_REGS {
					BYTE Fsc_en:1;	// CR4[0:0]

					BYTE Int_en:1;	// CR4[1:1]

					BYTE AGX:2;	// CR4[2:3]

					BYTE AGR_R:2;	// CR4[4:5]

					BYTE AGR_Z:2;	// CR4[6:7]

				} bitreg;
			} cr4;

			union	// SOP - CR5 Register
			 {
				BYTE reg;
				struct _CR5_REGS {
					BYTE V_0:1;	// CR5[0:0]

					BYTE V_1:1;	// CR5[1:1]

					BYTE V_2:1;	// CR5[2:2]

					BYTE V_3:1;	// CR5[3:3]

					BYTE V_4:1;	// CR5[4:4]

					BYTE V_5:1;	// CR5[5:5]

					BYTE V_6:1;	// CR5[6:6]

					BYTE V_7:1;	// CR5[7:7]

				} bitreg;
			} cr5;

			union	// SOP - CR6 Register
			 {
				BYTE reg;
				struct _CR6_REGS {
					BYTE reserved:8;	// CR6[0:7]

				} bitreg;
			} cr6;

			union	// SOP - CR7 Register
			 {
				BYTE reg;
				struct _CR7_REGS {
					BYTE reserved:8;	// CR7[0:7]

				} bitreg;
			} cr7;
		} SOP;

		BYTE ByteRegs[sizeof(struct _SOP)];

	} SOP_REGS;

	// DAA_REGS.SOP_REGS.SOP.CR5.reg
	// DAA_REGS.SOP_REGS.SOP.CR5.bitreg
	// DAA_REGS.SOP_REGS.SOP.CR5.bitreg.V_2
	// DAA_REGS.SOP_REGS.ByteRegs[5]

	//-----------------------------------------------
	// XOP Registers
	//
	BYTE byXOP;

	union _XOP_REGS {
		struct _XOP {
			union	// XOP - XR0 Register - Read values
			 {
				BYTE reg;
				struct _XR0_BITREGS {
					BYTE SI_0:1;	// XR0[0:0] - Read

					BYTE SI_1:1;	// XR0[1:1] - Read

					BYTE VDD_OK:1;	// XR0[2:2] - Read

					BYTE Caller_ID:1;	// XR0[3:3] - Read

					BYTE RING:1;	// XR0[4:4] - Read

					BYTE Cadence:1;		// XR0[5:5] - Read

					BYTE Wake_up:1;		// XR0[6:6] - Read

					BYTE unused:1;	// XR0[7:7] - Read

				} bitreg;
			} xr0;

			union	// XOP - XR1 Register
			 {
				BYTE reg;
				struct _XR1_BITREGS {
					BYTE M_SI_0:1;	// XR1[0:0]

					BYTE M_SI_1:1;	// XR1[1:1]

					BYTE M_VDD_OK:1;	// XR1[2:2]

					BYTE M_Caller_ID:1;	// XR1[3:3]

					BYTE M_RING:1;	// XR1[4:4]

					BYTE M_Cadence:1;	// XR1[5:5]

					BYTE M_Wake_up:1;	// XR1[6:6]

					BYTE unused:1;	// XR1[7:7]

				} bitreg;
			} xr1;

			union	// XOP - XR2 Register
			 {
				BYTE reg;
				struct _XR2_BITREGS {
					BYTE CTO0:1;	// XR2[0:0]

					BYTE CTO1:1;	// XR2[1:1]

					BYTE CTO2:1;	// XR2[2:2]

					BYTE CTO3:1;	// XR2[3:3]

					BYTE CTO4:1;	// XR2[4:4]

					BYTE CTO5:1;	// XR2[5:5]

					BYTE CTO6:1;	// XR2[6:6]

					BYTE CTO7:1;	// XR2[7:7]

				} bitreg;
			} xr2;

			union	// XOP - XR3 Register
			 {
				BYTE reg;
				struct _XR3_BITREGS {
					BYTE DCR0:1;	// XR3[0:0]

					BYTE DCR1:1;	// XR3[1:1]

					BYTE DCI:1;	// XR3[2:2]

					BYTE DCU0:1;	// XR3[3:3]

					BYTE DCU1:1;	// XR3[4:4]

					BYTE B_off:1;	// XR3[5:5]

					BYTE AGB0:1;	// XR3[6:6]

					BYTE AGB1:1;	// XR3[7:7]

				} bitreg;
			} xr3;

			union	// XOP - XR4 Register
			 {
				BYTE reg;
				struct _XR4_BITREGS {
					BYTE C_0:1;	// XR4[0:0]

					BYTE C_1:1;	// XR4[1:1]

					BYTE C_2:1;	// XR4[2:2]

					BYTE C_3:1;	// XR4[3:3]

					BYTE C_4:1;	// XR4[4:4]

					BYTE C_5:1;	// XR4[5:5]

					BYTE C_6:1;	// XR4[6:6]

					BYTE C_7:1;	// XR4[7:7]

				} bitreg;
			} xr4;

			union	// XOP - XR5 Register
			 {
				BYTE reg;
				struct _XR5_BITREGS {
					BYTE T_0:1;	// XR5[0:0]

					BYTE T_1:1;	// XR5[1:1]

					BYTE T_2:1;	// XR5[2:2]

					BYTE T_3:1;	// XR5[3:3]

					BYTE T_4:1;	// XR5[4:4]

					BYTE T_5:1;	// XR5[5:5]

					BYTE T_6:1;	// XR5[6:6]

					BYTE T_7:1;	// XR5[7:7]

				} bitreg;
			} xr5;

			union	// XOP - XR6 Register - Read Values
			 {
				BYTE reg;
				struct _XR6_BITREGS {
					BYTE CPS0:1;	// XR6[0:0]

					BYTE CPS1:1;	// XR6[1:1]

					BYTE unused1:2;		// XR6[2:3]

					BYTE CLK_OFF:1;		// XR6[4:4]

					BYTE unused2:3;		// XR6[5:7]

				} bitreg;
			} xr6;

			union	// XOP - XR7 Register
			 {
				BYTE reg;
				struct _XR7_BITREGS {
					BYTE unused1:1;		// XR7[0:0]

					BYTE Vdd0:1;	// XR7[1:1]

					BYTE Vdd1:1;	// XR7[2:2]

					BYTE unused2:5;		// XR7[3:7]

				} bitreg;
			} xr7;
		} XOP;

		BYTE ByteRegs[sizeof(struct _XOP)];

	} XOP_REGS;

	// DAA_REGS.XOP_REGS.XOP.XR7.reg
	// DAA_REGS.XOP_REGS.XOP.XR7.bitreg
	// DAA_REGS.XOP_REGS.XOP.XR7.bitreg.Vdd0
	// DAA_REGS.XOP_REGS.ByteRegs[7]

	//-----------------------------------------------
	// COP Registers
	//
	BYTE byCOP;

	union _COP_REGS {
		struct _COP {
			BYTE THFilterCoeff_1[8];	// COP - TH Filter Coefficients,      CODE=0, Part 1

			BYTE THFilterCoeff_2[8];	// COP - TH Filter Coefficients,      CODE=1, Part 2

			BYTE THFilterCoeff_3[8];	// COP - TH Filter Coefficients,      CODE=2, Part 3

			BYTE RingerImpendance_1[8];	// COP - Ringer Impendance Coefficients,  CODE=3, Part 1

			BYTE IMFilterCoeff_1[8];	// COP - IM Filter Coefficients,      CODE=4, Part 1

			BYTE IMFilterCoeff_2[8];	// COP - IM Filter Coefficients,      CODE=5, Part 2

			BYTE RingerImpendance_2[8];	// COP - Ringer Impendance Coefficients,  CODE=6, Part 2

			BYTE FRRFilterCoeff[8];		// COP - FRR Filter Coefficients,      CODE=7

			BYTE FRXFilterCoeff[8];		// COP - FRX Filter Coefficients,      CODE=8

			BYTE ARFilterCoeff[4];	// COP - AR Filter Coefficients,      CODE=9

			BYTE AXFilterCoeff[4];	// COP - AX Filter Coefficients,      CODE=10 

			BYTE Tone1Coeff[4];	// COP - Tone1 Coefficients,        CODE=11

			BYTE Tone2Coeff[4];	// COP - Tone2 Coefficients,        CODE=12

			BYTE LevelmeteringRinging[4];	// COP - Levelmetering Ringing,        CODE=13

			BYTE CallerID1stTone[8];	// COP - Caller ID 1st Tone,        CODE=14

			BYTE CallerID2ndTone[8];	// COP - Caller ID 2nd Tone,        CODE=15

		} COP;

		BYTE ByteRegs[sizeof(struct _COP)];

	} COP_REGS;

	// DAA_REGS.COP_REGS.COP.XR7.Tone1Coeff[3]
	// DAA_REGS.COP_REGS.COP.XR7.bitreg
	// DAA_REGS.COP_REGS.COP.XR7.bitreg.Vdd0
	// DAA_REGS.COP_REGS.ByteRegs[57]

	//-----------------------------------------------
	// CAO Registers
	//
	BYTE byCAO;

	union _CAO_REGS {
		struct _CAO {
			BYTE CallerID[512];	// CAO - Caller ID Bytes

		} CAO;

		BYTE ByteRegs[sizeof(struct _CAO)];
	} CAO_REGS;

	union			// XOP - XR0 Register - Write values
	 {
		BYTE reg;
		struct _XR0_BITREGSW {
			BYTE SO_0:1;	// XR1[0:0] - Write

			BYTE SO_1:1;	// XR1[1:1] - Write

			BYTE SO_2:1;	// XR1[2:2] - Write

			BYTE unused:5;	// XR1[3:7] - Write

		} bitreg;
	} XOP_xr0_W;

	union			// XOP - XR6 Register - Write values
	 {
		BYTE reg;
		struct _XR6_BITREGSW {
			BYTE unused1:4;		// XR6[0:3]

			BYTE CLK_OFF:1;		// XR6[4:4]

			BYTE unused2:3;		// XR6[5:7]

		} bitreg;
	} XOP_xr6_W;

} DAA_REGS;

#define ALISDAA_ID_BYTE      0x81
#define ALISDAA_CALLERID_SIZE  512

//------------------------------
//
//  Misc definitions
//

// Power Up Operation
#define SOP_PU_SLEEP    0
#define SOP_PU_RINGING    1
#define SOP_PU_CONVERSATION  2
#define SOP_PU_PULSEDIALING  3

#define ALISDAA_CALLERID_SIZE 512

#define PLAYBACK_MODE_COMPRESSED	0	//        Selects: Compressed modes, TrueSpeech 8.5-4.1, G.723.1, G.722, G.728, G.729
#define PLAYBACK_MODE_TRUESPEECH_V40	0	//        Selects: TrueSpeech 8.5, 6.3, 5.3, 4.8 or 4.1 Kbps
#define PLAYBACK_MODE_TRUESPEECH	8	//        Selects: TrueSpeech 8.5, 6.3, 5.3, 4.8 or 4.1 Kbps Version 5.1
#define PLAYBACK_MODE_ULAW		2	//        Selects: 64 Kbit/sec MuA-law PCM
#define PLAYBACK_MODE_ALAW		10	//        Selects: 64 Kbit/sec A-law PCM
#define PLAYBACK_MODE_16LINEAR		6	//        Selects: 128 Kbit/sec 16-bit linear
#define PLAYBACK_MODE_8LINEAR		4	//        Selects: 64 Kbit/sec 8-bit signed linear
#define PLAYBACK_MODE_8LINEAR_WSS	5	//        Selects: 64 Kbit/sec WSS 8-bit unsigned linear

#define RECORD_MODE_COMPRESSED		0	//        Selects: Compressed modes, TrueSpeech 8.5-4.1, G.723.1, G.722, G.728, G.729
#define RECORD_MODE_TRUESPEECH		0	//        Selects: TrueSpeech 8.5, 6.3, 5.3, 4.8 or 4.1 Kbps
#define RECORD_MODE_ULAW		4	//        Selects: 64 Kbit/sec Mu-law PCM
#define RECORD_MODE_ALAW		12	//        Selects: 64 Kbit/sec A-law PCM
#define RECORD_MODE_16LINEAR		5	//        Selects: 128 Kbit/sec 16-bit linear
#define RECORD_MODE_8LINEAR		6	//        Selects: 64 Kbit/sec 8-bit signed linear
#define RECORD_MODE_8LINEAR_WSS		7	//        Selects: 64 Kbit/sec WSS 8-bit unsigned linear

enum SLIC_STATES {
	PLD_SLIC_STATE_OC = 0,
	PLD_SLIC_STATE_RINGING,
	PLD_SLIC_STATE_ACTIVE,
	PLD_SLIC_STATE_OHT,
	PLD_SLIC_STATE_TIPOPEN,
	PLD_SLIC_STATE_STANDBY,
	PLD_SLIC_STATE_APR,
	PLD_SLIC_STATE_OHTPR
};

enum SCI_CONTROL {
	SCI_End = 0,
	SCI_Enable_DAA,
	SCI_Enable_Mixer,
	SCI_Enable_EEPROM
};

enum Mode {
	T63, T53, T48, T40
};
enum Dir {
	V3_TO_V4, V4_TO_V3, V4_TO_V5, V5_TO_V4
};

typedef struct Proc_Info_Tag {
	enum Mode convert_mode;
	enum Dir convert_dir;
	int Prev_Frame_Type;
	int Current_Frame_Type;
} Proc_Info_Type;

enum PREVAL {
	NORMAL = 0,
	NOPOST,
	POSTONLY,
	PREERROR
};

enum IXJ_EXTENSIONS {
	G729LOADER = 0,
	TS85LOADER,
	PRE_READ,
	POST_READ,
	PRE_WRITE,
	POST_WRITE,
	PRE_IOCTL,
	POST_IOCTL
};

typedef struct {
	unsigned int busytone:1;
	unsigned int dialtone:1;
	unsigned int ringback:1;
	unsigned int ringing:1;
	unsigned int cringing:1;
	unsigned int play_first_frame:1;
	unsigned int pstn_present:1;
	unsigned int pstn_ringing:1;
	unsigned int pots_correct:1;
	unsigned int pots_pstn:1;
	unsigned int g729_loaded:1;
	unsigned int ts85_loaded:1;
	unsigned int dtmf_oob:1;	// DTMF Out-Of-Band

} IXJ_FLAGS;

/******************************************************************************
*
*  This structure represents the Internet PhoneJACK and Internet LineJACK
*
******************************************************************************/

typedef struct {
	struct phone_device p;
	unsigned int board;
	unsigned int DSPbase;
	unsigned int XILINXbase;
	unsigned int serial;
	struct phone_capability caplist[30];
	unsigned int caps;
	struct pci_dev *dev;
	unsigned int cardtype;
	unsigned int rec_codec;
	char rec_mode;
	unsigned int play_codec;
	char play_mode;
	IXJ_FLAGS flags;
	unsigned int rec_frame_size;
	unsigned int play_frame_size;
	int aec_level;
	int readers, writers;
	wait_queue_head_t poll_q;
	wait_queue_head_t read_q;
	char *read_buffer, *read_buffer_end;
	char *read_convert_buffer;
	unsigned int read_buffer_size;
	unsigned int read_buffer_ready;
	wait_queue_head_t write_q;
	char *write_buffer, *write_buffer_end;
	char *write_convert_buffer;
	unsigned int write_buffer_size;
	unsigned int write_buffers_empty;
	unsigned long drybuffer;
	char *write_buffer_rp, *write_buffer_wp;
	char dtmfbuffer[80];
	char dtmf_current;
	int dtmf_wp, dtmf_rp, dtmf_state, dtmf_proc;
	int tone_off_time, tone_on_time;
	struct fasync_struct *async_queue;
	unsigned long tone_start_jif;
	char tone_index;
	char tone_state;
	char maxrings;
	IXJ_CADENCE *cadence_t;
	int tone_cadence_state;
	DTMF dtmf;
	CPTF cptf;
	BYTES dsp;
	BYTES ver;
	BYTES scr;
	BYTES ssr;
	BYTES baseframe;
	HSR hsr;
	GPIO gpio;
	PLD_SCRR pld_scrr;
	PLD_SCRW pld_scrw;
	PLD_SLICW pld_slicw;
	PLD_SLICR pld_slicr;
	PLD_CLOCK pld_clock;
	MIX mix;
	unsigned short ring_cadence;
	int ring_cadence_t;
	unsigned long ring_cadence_jif;
	int intercom;
	int m_hook;
	int r_hook;
	char pstn_envelope;
	char pstn_cid_intr;
	unsigned pstn_cid_recieved;
	IXJ_CID cid;
	unsigned long pstn_ring_start;
	unsigned long pstn_winkstart;
	unsigned int winktime;
	char port;
	union telephony_exception ex;
	char daa_mode;
	unsigned long pstn_sleeptil;
	DAA_REGS m_DAAShadowRegs;
	Proc_Info_Type Info_read;
	Proc_Info_Type Info_write;
	unsigned short frame_count;
	unsigned int filter_cadence;
	unsigned int filter_hist[4];
	unsigned short proc_load;
	unsigned long framesread;
	unsigned long frameswritten;
	unsigned long read_wait;
	unsigned long write_wait;
	unsigned long timerchecks;
	unsigned long txreadycheck;
	unsigned long rxreadycheck;
} IXJ;

typedef int (*IXJ_REGFUNC) (IXJ * j, unsigned long arg);

int ixj_register(int index, IXJ_REGFUNC regfunc);
int ixj_unregister(int index);
