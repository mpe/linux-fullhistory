/*
 * asm-m68k/zorro.h -- Amiga AutoConfig (Zorro) Expansion Device Definitions
 *
 * Copyright (C) 1995 Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_M68K_ZORRO_H_
#define _ASM_M68K_ZORRO_H_

#ifndef __ASSEMBLY__

#include <linux/config.h>
#include <asm/amigatypes.h>


/*
 * Defined Board Manufacturers
 *
 * Please update arch/m68k/amiga/zorro.c if you make changes here
 * Many IDs were obtained by using ExpName V1.4 ((C) Richard Körber)
 * and by looking at the NetBSD-Amiga kernel source
 */

#define MANUF_MEMPHIS          (0x0100)	/* Memphis */
#define PROD_STORMBRINGER      (0x00)	/* Stormbringer */

#define MANUF_COMMODORE2       (0x0201)	/* Commodore Germany */
#define PROD_A2088             (0x01)	/* CBM A2088 Bridgeboard */
#define PROD_A2386SX           (0x67)	/* CBM A2386-SX Bridgeboard */

#define MANUF_COMMODORE        (0x0202)	/* Commodore USA */
#define PROD_A2090A            (0x01)	/* CBM A2090/A2090A HD Controller */
#define PROD_A590              (0x02)	/* CBM A590 SCSI Controller */
#define PROD_A2091             (0x03)	/* CBM A2091 SCSI Controller */
#define PROD_A2090B            (0x04)	/* CBM A2090B 2090 Autoboot Card */
#define PROD_ARCNET            (0x09)	/* CBM A2060 Arcnet Card */
#define PROD_CBMRAM            (0x0A)	/* CBM A2052/58.RAM | 590/2091.RAM */
#define PROD_A560RAM           (0x20)	/* CBM A560 Memory Module */
#define PROD_A2232PROTO        (0x45)	/* CBM A2232 Serial Prototype */
#define PROD_A2232             (0x46)	/* CBM A2232 Serial Production */
#define PROD_A2620             (0x50)	/* CBM A2620 68020/RAM Card */
#define PROD_A2630             (0x51)	/* CBM A2630 68030/RAM Card */
#define PROD_A4091             (0x54)	/* CBM A4091 SCSI Controller */
#define PROD_ROMULATOR         (0x60)	/* CBM Romulator Card */
#define PROD_A3000TESTFIX      (0x61)	/* CBM A3000 Test Fixture */
#define PROD_A2065             (0x70)	/* CBM A2065 Ethernet Card */

#define MANUF_CARDCO           (0x03EC)	/* Cardco */
#define PROD_CC_A2410          (0xF5)	/* Cardco A2410 Hires Graphics Card */

#define MANUF_MICROBOTICS      (0x03F2)	/* MicroBotics */
#define PROD_VXL_30            (0x45)	/* VXL-30 Turbo Board */

#define MANUF_ASDG             (0x03FF)	/* ASDG */
#define PROD_LAN_ROVER         (0xFE)	/* Lan Rover Ethernet */
#define PROD_ASDG_DUAL_SERIAL  (0xFF)	/* Dual Serial Card */

#define MANUF_UNIV_OF_LOWELL   (0x0406)	/* University of Lowell */
#define PROD_A2410             (0x00)	/* CBM A2410 Hires Graphics Card */

#define MANUF_AMERISTAR        (0x041D)	/* Ameristar */
#define PROD_AMERISTAR2065     (0x01)	/* A2065 Ethernet Card */
#define PROD_A560              (0x09)	/* Arcnet Card */
#define PROD_A4066             (0x0A)	/* A4066 Ethernet Card */

#define MANUF_SUPRA            (0x0420)	/* Supra */
#define PROD_WORDSYNC          (0x0C)	/* Supra Wordsync SCSI Controller */
#define PROD_WORDSYNC_II       (0x0D)	/* Supra Wordsync II SCSI Controller */
#define PROD_SUPRA_2400MODEM   (0x10)	/* Supra 2400 Modem */

#define MANUF_CSA              (0x0422)	/* CSA */
#define PROD_MAGNUM            (0x11)	/* Magnum 40 SCSI Controller */
#define PROD_12GAUGE           (0x15)	/* 12 Gauge SCSI Controller */

#define MANUF_HACKER           (0x07DB)	/* Test only: no product definitions */

#define MANUF_POWER_COMPUTING  (0x07DC)	/* Power Computing */
#define PROD_DKB_1240          (0x12)	/* Viper II Turbo Board (DKB 1240) */

#define MANUF_GVP              (0x07E1)	/* Great Valley Products */
#define PROD_GVPIISCSI         (0x0B)	/* GVP Series II SCSI Controller */
#define PROD_GVPIISCSI_2       (0x09)	/* evidence that the driver works
					   for this product code also */
#define PROD_GVPIIRAM          (0x0A)	/* GVP Series II RAM */
#define PROD_GVP               (0x0B)	/* This code is used by a wide range of
					   GVP products - use the epc to
					   identify it correctly */
#define PROD_GVP_A2000_030     (0x0D)	/* GVP A2000 68030 Turbo Board */
#define PROD_GFORCE_040_SCSI   (0x16)	/* GForce 040 with SCSI (new) */
#define PROD_GVPIV_24          (0x20)	/* GVP IV-24 Graphics Board */
/* #define PROD_GVPIO_EXT      (0xFF)*/	/* GVP I/O Extender */

#define MANUF_PPI              (0x07EA)	/* Progressive Peripherals Inc. */
#define PROD_MERCURY           (0x00)	/* Mercury Turbo Board */
#define PROD_PPS_A3000_040     (0x01)	/* PP&S A3000 68040 Turbo Board */
#define PROD_PPS_A2000_040     (0x69)	/* PP&S A2000 68040 Turbo Board */
#define PROD_ZEUS              (0x96)	/* Zeus SCSI Controller */
#define PROD_PPS_A500_040      (0xBB)	/* PP&S A500 68040 Turbo Board */

#define MANUF_BSC              (0x07FE)	/* BSC */
#define PROD_ALF_3_SCSI        (0x03)	/* BSC ALF 3 SCSI Controller */

#define MANUF_C_LTD            (0x0802)	/* C Ltd. */
#define PROD_KRONOS_SCSI       (0x04)	/* Kronos SCSI Controller */

#define MANUF_JOCHHEIM         (0x0804)	/* Jochheim */
#define PROD_JOCHHEIM_RAM      (0x01)	/* Jochheim RAM */

#define MANUF_CHECKPOINT       (0x0807)	/* Checkpoint Technologies */
#define PROD_SERIAL_SOLUTION   (0x00)	/* Serial Solution */

#define MANUF_GOLEM            (0x0819)	/* Golem */
#define PROD_GOLEM_SCSI_II     (0x02)	/* Golem SCSI-II Controller */

#define MANUF_HARDITAL_SYNTHES (0x0817)	/* Hardital Synthesis */
#define PROD_HARDITAL_SCSI     (0x01)	/* Hardital Synthesis SCSI Controller */

#define MANUF_HARDITAL2        (0x0820)	/* Hardital Synthesis */
#define PROD_TQM               (0x14)	/* TQM 68030+68882 Turbo Board */

#define MANUF_BSC2             (0x082C)	/* BSC */
#define PROD_OKTAGON_SCSI      (0x05)	/* BSC Oktagon 2008 SCSI Controller */
#define PROD_TANDEM            (0x06)	/* BSC Tandem */
#define PROD_OKTAGON_RAM       (0x08)	/* BSC Oktagon 2008 RAM */
#define PROD_MULTIFACE_I       (0x10)	/* Alfa Data MultiFace I */
#define PROD_MULTIFACE_II      (0x11)	/* Alfa Data MultiFace II */
#define PROD_MULTIFACE_III     (0x12)	/* Alfa Data MultiFace III */
#define PROD_ISDN_MASTER       (0x40)	/* BSC ISDN Master */

#define MANUF_ADV_SYS_SOFT     (0x0836)	/* Advanced Systems & Software */
#define PROD_NEXUS_SCSI        (0x01)	/* Nexus SCSI Controller */
#define PROD_NEXUS_RAM         (0x08)	/* Nexus RAM */

#define MANUF_IVS              (0x0840)	/* IVS */
#define PROD_TRUMPCARD_500     (0x30)	/* Trumpcard 500 SCSI Controller */
#define PROD_TRUMPCARD         (0x34)	/* Trumpcard SCSI Controller */
#define PROD_VECTOR            (0xF3)	/* Vector SCSI Controller */

#define MANUF_XPERT_PRODEV     (0x0845)	/* XPert/ProDev */
#define PROD_MERLIN_RAM        (0x03)	/* Merlin Graphics Board */
#define PROD_MERLIN_REG        (0x04)

#define MANUF_HYDRA_SYSTEMS    (0x0849)	/* Hydra Systems */
#define PROD_AMIGANET          (0x01)	/* Amiganet Board */

#define MANUF_DIG_MICRONICS    (0x0851)	/* Digital Micronics Inc */
#define PROD_DMI_RESOLVER      (0x01)	/* DMI Resolver Graphics Board */

#define MANUF_HELFRICH1        (0x0861)	/* Helfrich */
#define PROD_RAINBOW3          (0x21)	/* Rainbow3 Graphics Board */

#define MANUF_SW_RESULT_ENTS   (0x0866)	/* Software Result Enterprises */
#define PROD_GG2PLUS           (0x01)	/* GG2+ Bus Converter */

#define MANUF_VILLAGE_TRONIC   (0x0877)	/* Village Tronic */
#define PROD_PICASSO_II_RAM    (0x0B)	/* Picasso II Graphics Board */
#define PROD_PICASSO_II_REG    (0x0C)
#define PROD_ARIADNE           (0xC9)	/* Ariadne Ethernet */

#define MANUF_UTILITIES_ULTD   (0x087B)	/* Utilities Unlimited */
#define PROD_EMPLANT_DELUXE    (0x15)	/* Emplant Deluxe SCSI Controller */
#define PROD_EMPLANT_DELUXE2   (0x20)	/* Emplant Deluxe SCSI Controller */

#define MANUF_MTEC             (0x0890)	/* MTEC Germany */
#define PROD_MTEC_68030        (0x03)	/* 68030 Turbo Board */
#define PROD_MTEC_T1230        (0x20)	/* MTEC T1230/28 Turbo Board */

#define MANUF_GVP2             (0x0891)	/* Great Valley Products */
#define PROD_SPECTRUM_RAM      (0x01)	/* GVP Spectrum Graphics Board */
#define PROD_SPECTRUM_REG      (0x02)

#define MANUF_HELFRICH2        (0x0893)	/* Helfrich */
#define PROD_PICCOLO_RAM       (0x05)	/* Piccolo Graphics Board */
#define PROD_PICCOLO_REG       (0x06)
#define PROD_PEGGY_PLUS        (0x07)	/* PeggyPlus MPEG Decoder Board */
#define PROD_SD64_RAM          (0x0A)	/* SD64 Graphics Board */
#define PROD_SD64_REG          (0x0B)

#define MANUF_MACROSYSTEMS     (0x089B)	/* MacroSystems USA */
#define PROD_WARP_ENGINE       (0x13)	/* Warp Engine SCSI Controller */

#define MANUF_HARMS_PROF       (0x0A00)	/* Harms Professional */
#define PROD_3500_TURBO        (0xD0)	/* 3500 Turbo board */

#define MANUF_VORTEX           (0x2017)	/* Vortex */
#define PROD_GOLDEN_GATE_386   (0x07)	/* Golden Gate 80386 Board */
#define PROD_GOLDEN_GATE_RAM   (0x08)	/* Golden Gate RAM */
#define PROD_GOLDEN_GATE_486   (0x09)	/* Golden Gate 80486 Board */

#define MANUF_DATAFLYER        (0x2062)	/* DataFlyer */
#define PROD_DATAFLYER_4000SX  (0x01)	/* DataFlyer 4000SX SCSI Controller */

#define MANUF_PHASE5           (0x2140)	/* Phase5 */
#define PROD_FASTLANE_RAM      (0x0A)	/* FastLane RAM */
#define PROD_FASTLANE_SCSI     (0x0B)	/* FastLane/Blizzard 1230-II SCSI */
#define PROD_CYBERSTORM_SCSI   (0x0C)	/* CyberStorm Fast SCSI-II Controller */
#define PROD_BLIZZARD_1230_III (0x0D)	/* Blizzard 1230-III Turbo Board */
#define PROD_BLIZZARD_1230_IV  (0x11)	/* Blizzard 1230-IV Turbo Board */
#define PROD_CYBERVISION       (0x22)	/* CyberVision64 Graphics Board */

#define MANUF_APOLLO           (0x2222)	/* Apollo */
#define PROD_AT_APOLLO         (0x22)	/* AT-Apollo */
#define PROD_APOLLO_TURBO      (0x23)	/* Apollo Turbo Board */

#define MANUF_UWE_GERLACH      (0x3FF7)	/* Uwe Gerlach */
#define PROD_UG_RAM_ROM        (0xd4)	/* RAM/ROM */

#define MANUF_MACROSYSTEMS2    (0x4754)	/* MacroSystems Germany */
#define PROD_MAESTRO           (0x03)	/* Maestro */
#define PROD_VLAB              (0x04)	/* VLab */
#define PROD_MAESTRO_PRO       (0x05)	/* Maestro Pro */
#define PROD_RETINA_Z2         (0x06)	/* Retina Z2 Graphics Board */
#define PROD_MULTI_EVOLUTION   (0x08)	/* MultiEvolution */
#define PROD_RETINA_Z3         (0x10)	/* Retina Z3 Graphics Board */
#define PROD_FALCON_040        (0xFD)	/* Falcon '040 Turbo Board */


/* Illegal Manufacturer IDs. These do NOT appear in amiga/zorro.c! */

#define MANUF_HACKER_INC       (0x07DB)	/* Hacker Inc. */
#define PROD_HACKER_SCSI       (0x01)	/* Hacker Inc. SCSI Controller */

#define MANUF_RES_MNGT_FORCE   (0x07DB)	/* Resource Management Force */
#define PROD_QUICKNET          (0x02)	/* QuickNet Ethernet */


/*
 * GVP's identifies most of their product through the 'extended
 * product code' (epc). The epc has to be and'ed with the GVPEPCMASK
 * before the identification.
 */

#define GVP_EPCMASK (0xf8)

enum GVP_ident {
  GVP_GFORCE_040      = 0x20,
  GVP_GFORCE_040_SCSI = 0x30,
  GVP_A1291_SCSI      = 0x40,
  GVP_COMBO_R4        = 0x60,
  GVP_COMBO_R4_SCSI   = 0x70,
  GVP_PHONEPAK        = 0x78,
  GVP_IOEXT           = 0x98,
  GVP_GFORCE_030      = 0xa0,
  GVP_GFORCE_030_SCSI = 0xb0,
  GVP_A530            = 0xc0,
  GVP_A530_SCSI       = 0xd0,
  GVP_COMBO_R3        = 0xe0,
  GVP_COMBO_R3_SCSI   = 0xf0,
  GVP_SERIESII        = 0xf8,
};

enum GVP_flags {
  GVP_IO       = 0x01,
  GVP_ACCEL    = 0x02,
  GVP_SCSI     = 0x04,
  GVP_24BITDMA = 0x08,
  GVP_25BITDMA = 0x10,
  GVP_NOBANK   = 0x20,
  GVP_14MHZ    = 0x40,
};


struct Node {
    struct  Node *ln_Succ;	/* Pointer to next (successor) */
    struct  Node *ln_Pred;	/* Pointer to previous (predecessor) */
    u_char  ln_Type;
    char    ln_Pri;		/* Priority, for sorting */
    char    *ln_Name;		/* ID string, null terminated */
};

struct ExpansionRom {
    /* -First 16 bytes of the expansion ROM */
    u_char	er_Type;	/* Board type, size and flags */
    u_char	er_Product;	/* Product number, assigned by manufacturer */
    u_char	er_Flags;	/* Flags */
    u_char	er_Reserved03;	/* Must be zero ($ff inverted) */
    u_short	er_Manufacturer; /* Unique ID,ASSIGNED BY COMMODORE-AMIGA! */
    u_long	er_SerialNumber; /* Available for use by manufacturer */
    u_short	er_InitDiagVec;	/* Offset to optional "DiagArea" structure */
    u_char	er_Reserved0c;
    u_char	er_Reserved0d;
    u_char	er_Reserved0e;
    u_char	er_Reserved0f;
};

/* er_Type board type bits */
#define ERT_TYPEMASK	0xc0
#define ERT_ZORROII	0xc0
#define ERT_ZORROIII	0x80

/* other bits defined in er_Type */
#define ERTB_MEMLIST	5		/* Link RAM into free memory list */
#define ERTF_MEMLIST	(1<<5)

struct ConfigDev {
    struct Node 	cd_Node;
    u_char		cd_Flags;	/* (read/write) */
    u_char		cd_Pad; 	/* reserved */
    struct ExpansionRom cd_Rom; 	/* copy of board's expansion ROM */
    void		*cd_BoardAddr;	/* where in memory the board was placed */
    u_long		cd_BoardSize;	/* size of board in bytes */
    u_short		cd_SlotAddr;	/* which slot number (PRIVATE) */
    u_short		cd_SlotSize;	/* number of slots (PRIVATE) */
    void		*cd_Driver;	/* pointer to node of driver */
    struct ConfigDev	*cd_NextCD;	/* linked list of drivers to config */
    u_long		cd_Unused[4];	/* for whatever the driver wants */
};

#else	/* __ASSEMBLY__ */

LN_Succ		= 0
LN_Pred		= LN_Succ+4
LN_Type		= LN_Pred+4
LN_Pri		= LN_Type+1
LN_Name		= LN_Pri+1
LN_sizeof	= LN_Name+4

ER_Type		= 0
ER_Product	= ER_Type+1
ER_Flags	= ER_Product+1
ER_Reserved03	= ER_Flags+1
ER_Manufacturer	= ER_Reserved03+1
ER_SerialNumber	= ER_Manufacturer+2
ER_InitDiagVec	= ER_SerialNumber+4
ER_Reserved0c	= ER_InitDiagVec+2
ER_Reserved0d	= ER_Reserved0c+1
ER_Reserved0e	= ER_Reserved0d+1
ER_Reserved0f	= ER_Reserved0e+1
ER_sizeof	= ER_Reserved0f+1

CD_Node		= 0
CD_Flags	= CD_Node+LN_sizeof
CD_Pad		= CD_Flags+1
CD_Rom		= CD_Pad+1
CD_BoardAddr	= CD_Rom+ER_sizeof
CD_BoardSize	= CD_BoardAddr+4
CD_SlotAddr	= CD_BoardSize+4
CD_SlotSize	= CD_SlotAddr+2
CD_Driver	= CD_SlotSize+2
CD_NextCD	= CD_Driver+4
CD_Unused	= CD_NextCD+4
CD_sizeof	= CD_Unused+(4*4)

#endif	/* __ASSEMBLY__ */

#ifndef __ASSEMBLY__

/*
 * Zorro Functions
 */

extern int zorro_find(int manuf, int prod, int part, int index);
extern struct ConfigDev *zorro_get_board(int key);
extern void zorro_config_board(int key, int part);
extern void zorro_unconfig_board(int key, int part);


/*
 * Bitmask indicating portions of available Zorro II RAM that are unused
 * by the system. Every bit represents a 64K chunk, for a maximum of 8MB
 * (128 chunks, physical 0x00200000-0x009fffff).
 *
 * If you want to use (= allocate) portions of this RAM, you should clear
 * the corresponding bits.
 */

extern u_long zorro_unused_z2ram[4];

#define Z2RAM_START		(0x00200000)
#define Z2RAM_END		(0x00a00000)
#define Z2RAM_SIZE		(0x00800000)
#define Z2RAM_CHUNKSIZE		(0x00010000)
#define Z2RAM_CHUNKMASK		(0x0000ffff)
#define Z2RAM_CHUNKSHIFT	(16)


/*
 * Verbose Board Identification
 */

#ifdef CONFIG_ZORRO
extern void zorro_identify(void);
extern int zorro_get_list(char *buffer);
#endif CONFIG_ZORRO

#endif	/* __ASSEMBLY__ */

#endif /* _ASM_M68K_ZORRO_H_ */
