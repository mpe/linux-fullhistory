/*
 * linux/zorro.h -- Amiga AutoConfig (Zorro) Expansion Device Definitions
 *
 * Copyright (C) 1995 Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef __ZORRO_H
#define __ZORRO_H

#ifndef __ASSEMBLY__

/*
 * Defined Board Manufacturers
 *
 * Please update arch/m68k/amiga/zorro.c if you make changes here
 * Many IDs were obtained from ExpName/Identify ((C) Richard Körber)
 * and by looking at the NetBSD-Amiga kernel sources
 */

#define MANUF_PACIFIC          (0x00D3)	/* Pacific Peripherals */
#define PROD_SE_2000_A500      (0x00)	/* SE 2000 A500 */
#define PROD_PACIFIC_HD        (0x0A)	/* HD Controller */

#define MANUF_KUPKE            (0x00DD)	/* Kupke */
#define PROD_GOLEM_BOX_2       (0x00)	/* Golem RAM Box 2MB */

#define MANUF_MEMPHIS          (0x0100)	/* Memphis */
#define PROD_STORMBRINGER      (0x00)	/* Stormbringer */

#define MANUF_3_STATE          (0x0200)	/* 3-State */
#define PROD_MEGAMIX_2000      (0x02)	/* Megamix 2000 RAM */

#define MANUF_COMMODORE2       (0x0201)	/* Commodore Braunschweig */
#define PROD_A2088             (0x01)	/* CBM A2088 XT Bridgeboard */
#define PROD_A2286             (0x02)	/* CBM A2286 AT Bridgeboard */
#define PROD_A4091_2           (0x54)	/* CBM A4091 SCSI Controller */
#define PROD_A2386SX           (0x67)	/* CBM A2386-SX Bridgeboard */

#define MANUF_COMMODORE        (0x0202)	/* Commodore West Chester */
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
#define PROD_A2065_2           (0x5A)	/* A2065 Ethernet Card */
#define PROD_ROMULATOR         (0x60)	/* CBM Romulator Card */
#define PROD_A3000TESTFIX      (0x61)	/* CBM A3000 Test Fixture */
#define PROD_A2386SX_2         (0x67)	/* A2386-SX Bridgeboard */
#define PROD_A2065             (0x70)	/* CBM A2065 Ethernet Card */

#define MANUF_COMMODORE3       (0x0203)	/* Commodore West Chester */
#define PROD_A2090A_CM         (0x03)	/* A2090A Combitec/MacroSystem */

#define MANUF_KCS              (0x02FF)	/* Kolff Computer Supplies */
#define PROD_POWER_BOARD       (0x00)	/* KCS Power PC Board */

#define MANUF_CARDCO           (0x03EC)	/* Cardco */
#define PROD_KRONOS_2000_SCSI  (0x04)	/* Kronos 2000 SCSI Controller */
#define PROD_A1000_SCSI        (0x0C)	/* A1000 SCSI Controller */
#define PROD_ESCORT_SCSI       (0x0E)	/* Escort SCSI Controller */
#define PROD_CC_A2410          (0xF5)	/* Cardco A2410 Hires Graphics Card */

#define MANUF_A_SQUARED        (0x03ED)	/* A-Squared */
#define PROD_LIVE_2000         (0x01)	/* Live! 2000 */

#define MANUF_COMSPEC          (0x03EE)	/* ComSpec Communications */
#define PROD_AX2000            (0x01)	/* AX2000 */

#define MANUF_ANAKIN           (0x03F1)	/* Anakin */
#define PROD_EASYL             (0x01)	/* Easyl Tablet */

#define MANUF_MICROBOTICS      (0x03F2)	/* MicroBotics */
#define PROD_STARBOARD_II      (0x00)	/* StarBoard II */
#define PROD_STARDRIVE         (0x02)	/* StarDrive */
#define PROD_8_UP_A            (0x03)	/* 8-Up (Rev A) */
#define PROD_8_UP_Z            (0x04)	/* 8-Up (Rev Z) */
#define PROD_DELTA_RAM         (0x20)	/* Delta Card RAM */
#define PROD_8_STAR_RAM        (0x40)	/* 8-Star RAM */
#define PROD_8_STAR            (0x41)	/* 8-Star */
#define PROD_VXL_RAM           (0x44)	/* VXL RAM */
#define PROD_VXL_30            (0x45)	/* VXL-30 Turbo Board */
#define PROD_DELTA             (0x60)	/* Delta Card */
#define PROD_MBX_1200          (0x81)	/* MBX 1200 */
#define PROD_HARDFRAME_2000    (0x9E)	/* Hardframe 2000 */
#define PROD_MBX_1200_2        (0xC1)	/* MBX 1200 */

#define MANUF_ACCESS           (0x03F4)	/* Access Associates */

#define MANUF_EXPANSION_TECH   (0x03F6)	/* Expansion Technologies */

#define MANUF_ASDG             (0x03FF)	/* ASDG */
#define PROD_ASDG_MEMORY       (0x01)	/* Memory Expansion */
#define PROD_ASDG_MEMORY_2     (0x02)	/* Memory Expansion */
#define PROD_LAN_ROVER         (0xFE)	/* Lan Rover Ethernet */
#define PROD_TWIN_X            (0xFF)	/* Twin-X Serial Card */

#define MANUF_IMTRONICS        (0x0404)	/* Imtronics */
#define PROD_HURRICANE_2800    (0x39)	/* Hurricane 2800 68030 */
#define PROD_HURRICANE_2800_2  (0x57)	/* Hurricane 2800 68030 */

#define MANUF_UNIV_OF_LOWELL   (0x0406)	/* University of Lowell */
#define PROD_A2410             (0x00)	/* CBM A2410 Hires Graphics Card */

#define MANUF_AMERISTAR        (0x041D)	/* Ameristar */
#define PROD_AMERISTAR2065     (0x01)	/* A2065 Ethernet Card */
#define PROD_A560              (0x09)	/* Arcnet Card */
#define PROD_A4066             (0x0A)	/* A4066 Ethernet Card */

#define MANUF_SUPRA            (0x0420)	/* Supra */
#define PROD_SUPRADRIVE_4x4    (0x01)	/* SupraDrive 4x4 SCSI Controller */
#define PROD_SUPRA_2000        (0x03)	/* 2000 DMA HD */
#define PROD_SUPRA_500         (0x05)	/* 500 HD/RAM */
#define PROD_SUPRA_500XP       (0x09)	/* 500XP/2000 RAM */
#define PROD_SUPRA_500RX       (0x0A)	/* 500RX/2000 RAM */
#define PROD_SUPRA_2400ZI      (0x0B)	/* 2400zi Modem */
#define PROD_WORDSYNC          (0x0C)	/* Supra Wordsync SCSI Controller */
#define PROD_WORDSYNC_II       (0x0D)	/* Supra Wordsync II SCSI Controller */
#define PROD_SUPRA_2400ZIPLUS  (0x10)	/* 2400zi+ Modem */

#define MANUF_CSA              (0x0422)	/* Computer Systems Ass. */
#define PROD_MAGNUM            (0x11)	/* Magnum 40 SCSI Controller */
#define PROD_12GAUGE           (0x15)	/* 12 Gauge SCSI Controller */

#define MANUF_MTEC2            (0x0502)	/* M-Tech */
#define PROD_AT500_2           (0x03)	/* AT500 RAM */

#define MANUF_GVP3             (0x06E1)	/* Great Valley Products */
#define PROD_IMPACT            (0x08)	/* Impact SCSI/Memory */

#define MANUF_BYTEBOX          (0x07DA)	/* ByteBox */
#define PROD_BYTEBOX_A500      (0x00)	/* A500 */

#define MANUF_HACKER           (0x07DB)	/* Test only: no product definitions */

#define MANUF_POWER_COMPUTING  (0x07DC)	/* Power Computing (DKB) */
#define PROD_DKB_3128          (0x0E)	/* DKB 3128 RAM */
#define PROD_RAPID_FIRE        (0x0F)	/* Rapid Fire SCSI Controller */
#define PROD_DKB_1202          (0x10)	/* DKB 1202 RAM */
#define PROD_VIPER_II_COBRA    (0x12)	/* Viper II Turbo Board (DKB Cobra) */
#define PROD_WILDFIRE_060      (0x17)	/* WildFire 060 Turbo Board */
#define PROD_WILDFIRE_060_2    (0xFF)	/* WildFire 060 Turbo Board */

#define MANUF_GVP              (0x07E1)	/* Great Valley Products */
#define PROD_IMPACT_I_4K       (0x01)	/* Impact Series-I SCSI 4K */
#define PROD_IMPACT_I_16K_2    (0x02)	/* Impact Series-I SCSI 16K/2 */
#define PROD_IMPACT_I_16K_3    (0x03)	/* Impact Series-I SCSI 16K/3 */
#define PROD_IMPACT_3001_IDE   (0x08)	/* Impact 3001 IDE */
#define PROD_IMPACT_3001_RAM   (0x09)	/* Impact 3001 RAM */
#define PROD_GVPIISCSI         (0x0B)	/* GVP Series II SCSI Controller */
#define PROD_GVPIISCSI_2       (0x09)	/* evidence that the driver works
					   for this product code also */
#define PROD_GVPIIRAM          (0x0A)	/* GVP Series II RAM */
#define PROD_GVP               (0x0B)	/* This code is used by a wide range of
					   GVP products - use the epc to
					   identify it correctly */
#define PROD_GVP_A2000_030     (0x0D)	/* GVP A2000 68030 Turbo Board */
#define PROD_IMPACT_3001_IDE_2 (0x0D)	/* Impact 3001 IDE */
#define PROD_GFORCE_040_SCSI   (0x16)	/* GForce 040 with SCSI (new) */
#define PROD_GVPIV_24          (0x20)	/* GVP IV-24 Graphics Board */
#define PROD_GFORCE_040        (0xFF)	/* GForce 040 Turbo Board */
/* #define PROD_GVPIO_EXT      (0xFF)*/	/* GVP I/O Extender */

#define MANUF_SYNERGY          (0x07E5)	/* Synergy */

#define MANUF_XETEC            (0x07E6)	/* Xetec */
#define PROD_FASTCARD_SCSI     (0x01)	/* FastCard SCSI Controller */
#define PROD_FASTCARD_RAM      (0x02)	/* FastCard RAM */

#define MANUF_PPI              (0x07EA)	/* Progressive Peripherals Inc. */
#define PROD_MERCURY           (0x00)	/* Mercury Turbo Board */
#define PROD_PPS_A3000_040     (0x01)	/* PP&S A3000 68040 Turbo Board */
#define PROD_PPS_A2000_040     (0x69)	/* PP&S A2000 68040 Turbo Board */
#define PROD_ZEUS              (0x96)	/* Zeus SCSI Controller */
#define PROD_PPS_A500_040      (0xBB)	/* PP&S A500 68040 Turbo Board */

#define MANUF_XEBEC            (0x07EC)	/* Xebec */

#define MANUF_SPIRIT           (0x07F2)	/* Spirit */
#define PROD_HDA_506           (0x04)	/* HDA 506 Harddisk */
#define PROD_OCTABYTE_RAM      (0x06)	/* OctaByte RAM */

#define MANUF_BSC              (0x07FE)	/* BSC */
#define PROD_ALF_3_SCSI        (0x03)	/* BSC ALF 3 SCSI Controller */

#define MANUF_BSC3             (0x0801)	/* BSC */
#define PROD_ALF_2_SCSI        (0x01)	/* ALF 2 SCSI Controller */
#define PROD_ALF_2_SCSI_2      (0x02)	/* ALF 2 SCSI Controller */
#define PROD_ALF_3_SCSI_2      (0x03)	/* ALF 3 SCSI Controller */

#define MANUF_C_LTD            (0x0802)	/* C Ltd. */
#define PROD_KRONOS_SCSI       (0x04)	/* Kronos SCSI Controller */
#define PROD_A1000_SCSI_2      (0x0C)	/* A1000 SCSI Controller */

#define MANUF_JOCHHEIM         (0x0804)	/* Jochheim */
#define PROD_JOCHHEIM_RAM      (0x01)	/* Jochheim RAM */

#define MANUF_CHECKPOINT       (0x0807)	/* Checkpoint Technologies */
#define PROD_SERIAL_SOLUTION   (0x00)	/* Serial Solution */

#define MANUF_ICD              (0x0817)	/* ICD */
#define PROD_ADVANTAGE_2000    (0x01)	/* Advantage 2000 SCSI Controller */

#define MANUF_KUPKE2           (0x0819)	/* Kupke */
#define PROD_KUPKE_SCSI_II     (0x02)	/* Golem SCSI-II Controller */
#define PROD_GOLEM_BOX         (0x03)	/* Golem Box */
#define PROD_KUPKE_TURBO       (0x04)	/* 030/882 Turbo Board */
#define PROD_KUPKE_SCSI_AT     (0x05)	/* SCSI/AT Controller */

#define MANUF_GVP4             (0x081D)	/* Great Valley Products */
#define PROD_A2000_RAM8        (0x09)	/* A2000-RAM8/2 */

#define MANUF_INTERWORKS_NET   (0x081E)	/* Interworks Network */

#define MANUF_HARDITAL         (0x0820)	/* Hardital Synthesis */
#define PROD_TQM               (0x14)	/* TQM 68030+68882 Turbo Board */

#define MANUF_BSC2             (0x082C)	/* BSC */
#define PROD_OKTAGON_SCSI      (0x05)	/* BSC Oktagon 2008 SCSI Controller */
#define PROD_TANDEM            (0x06)	/* BSC Tandem AT-2008/508 IDE */
#define PROD_ALPHA_RAM_1200    (0x07)	/* Alpha RAM 1200 */
#define PROD_OKTAGON_RAM       (0x08)	/* BSC Oktagon 2008 RAM */
#define PROD_MULTIFACE_I       (0x10)	/* Alfa Data MultiFace I */
#define PROD_MULTIFACE_II      (0x11)	/* Alfa Data MultiFace II */
#define PROD_MULTIFACE_III     (0x12)	/* Alfa Data MultiFace III */
#define PROD_BSC_FRAEMBUFFER   (0x20)	/* Framebuffer */
#define PROD_GRAFFITI_RAM      (0x21)	/* Graffiti Graphics Board */
#define PROD_GRAFFITI_REG      (0x22)
#define PROD_ISDN_MASTERCARD   (0x40)	/* BSC ISDN MasterCard */
#define PROD_ISDN_MASTERCARD_2 (0x41)	/* BSC ISDN MasterCard II */

#define MANUF_ADV_SYS_SOFT     (0x0836)	/* Advanced Systems & Software */
#define PROD_NEXUS_SCSI        (0x01)	/* Nexus SCSI Controller */
#define PROD_NEXUS_RAM         (0x08)	/* Nexus RAM */

#define MANUF_IMPULSE          (0x0838)	/* Impulse */
#define PROD_FIRECRACKER_24    (0x00)	/* FireCracker 24 */

#define MANUF_IVS              (0x0840)	/* IVS */
#define PROD_GRANDSLAM_PIC_2   (0x02)	/* GrandSlam PIC 2 RAM */
#define PROD_GRANDSLAM_PIC_1   (0x04)	/* GrandSlam PIC 1 RAM */
#define PROD_IVS_OVERDRIVE     (0x10)	/* OverDrive HD */
#define PROD_TRUMPCARD_CLASSIC (0x30)	/* Trumpcard Classic SCSI Controller */
#define PROD_TRUMPCARD_PRO     (0x34)	/* Trumpcard Pro SCSI Controller */
#define PROD_META_4            (0x40)	/* Meta-4 RAM */
#define PROD_WAVETOOLS         (0xBF)	/* Wavetools Sound Board */
#define PROD_VECTOR            (0xF3)	/* Vector SCSI Controller */
#define PROD_VECTOR_2          (0xF4)	/* Vector SCSI Controller */

#define MANUF_VECTOR           (0x0841)	/* Vector */
#define PROD_CONNECTION        (0xE3)	/* Connection Serial IO */

#define MANUF_XPERT_PRODEV     (0x0845)	/* XPert/ProDev */
#define PROD_VISIONA_RAM       (0x01)	/* Visiona Graphics Board */
#define PROD_VISIONA_REG       (0x02)
#define PROD_MERLIN_RAM        (0x03)	/* Merlin Graphics Board */
#define PROD_MERLIN_REG        (0x04)
#define PROD_MERLIN_REG_2      (0xC9)

#define MANUF_HYDRA_SYSTEMS    (0x0849)	/* Hydra Systems */
#define PROD_AMIGANET          (0x01)	/* Amiganet Board */

#define MANUF_SUNRIZE          (0x084F)	/* Sunrize Industries */
#define PROD_AD1012            (0x01)	/* AD1012 Sound Board */
#define PROD_AD516             (0x02)	/* AD516 Sound Board */
#define PROD_DD512             (0x03)	/* DD512 Sound Board */

#define MANUF_TRICERATOPS      (0x0850)	/* Triceratops */
#define PROD_TRICERATOPS       (0x01)	/* Triceratops Multi I/O Board */

#define MANUF_APPLIED_MAGIC    (0x0851)	/* Applied Magic Inc */
#define PROD_DMI_RESOLVER      (0x01)	/* DMI Resolver Graphics Board */
#define PROD_DIGITAL_BCASTER   (0x06)	/* Digital Broadcaster */

#define MANUF_GFX_BASE         (0x085E)	/* GFX-Base */
#define PROD_GDA_1_RAM         (0x00)	/* GDA-1 Graphics Board */
#define PROD_GDA_1_REG         (0x01)

#define MANUF_ROCTEC           (0x0860)	/* RocTec */
#define PROD_RH_800C           (0x01)	/* RH 800C Hard Disk Controller */
#define PROD_RH_800C_RAM       (0x01)	/* RH 800C RAM */

#define MANUF_HELFRICH1        (0x0861)	/* Helfrich */
#define PROD_RAINBOW3          (0x21)	/* Rainbow3 Graphics Board */

#define MANUF_SW_RESULT_ENTS   (0x0866)	/* Software Result Enterprises */
#define PROD_GG2PLUS           (0x01)	/* GG2+ Bus Converter */

#define MANUF_MASOBOSHI        (0x086D)	/* Masoboshi */
#define PROD_MASTER_CARD_RAM   (0x03)	/* Master Card RAM */
#define PROD_MASTER_CARD_SCSI  (0x04)	/* Master Card SCSI Controller */
#define PROD_MVD_819           (0x07)	/* MVD 819 */

#define MANUF_DELACOMP         (0x0873)	/* DelaComp */
#define PROD_DELACOMP_RAM_2000 (0x01)	/* RAM Expansion 2000 */

#define MANUF_VILLAGE_TRONIC   (0x0877)	/* Village Tronic */
#define PROD_DOMINO_RAM        (0x01)	/* Domino Graphics Board */
#define PROD_DOMINO_REG        (0x02)
#define PROD_PICASSO_II_RAM    (0x0B)	/* Picasso II/II+ Graphics Board */
#define PROD_PICASSO_II_REG    (0x0C)
#define PROD_PICASSO_II_SEGM   (0x0D)	/* Picasso II/II+ (Segmented Mode) */
#define PROD_PICASSO_IV        (0x15)	/* Picassio IV Graphics Board */
#define PROD_PICASSO_IV_2      (0x16)
#define PROD_PICASSO_IV_3      (0x17)
#define PROD_PICASSO_IV_4      (0x18)
#define PROD_ARIADNE           (0xC9)	/* Ariadne Ethernet */

#define MANUF_UTILITIES_ULTD   (0x087B)	/* Utilities Unlimited */
#define PROD_EMPLANT_DELUXE    (0x15)	/* Emplant Deluxe SCSI Controller */
#define PROD_EMPLANT_DELUXE2   (0x20)	/* Emplant Deluxe SCSI Controller */

#define MANUF_AMITRIX          (0x0880)	/* Amitrix */
#define PROD_AMITRIX_MULTI_IO  (0x01)	/* Multi-IO */
#define PROD_AMITRIX_CD_RAM    (0x02)	/* CD-RAM Memory */

#define MANUF_ARMAX            (0x0885)	/* ArMax */
#define PROD_OMNIBUS           (0x00)	/* OmniBus Graphics Board */

#define MANUF_NEWTEK           (0x088F)	/* NewTek */
#define PROD_VIDEOTOASTER      (0x00)	/* VideoToaster */

#define MANUF_MTEC             (0x0890)	/* M-Tech Germany */
#define PROD_AT500             (0x01)	/* AT500 IDE Controller */
#define PROD_MTEC_68030        (0x03)	/* 68030 Turbo Board */
#define PROD_MTEC_68020I       (0x06)	/* 68020i Turbo Board */
#define PROD_MTEC_T1230        (0x20)	/* A1200 T68030/42 RTC Turbo Board */
#define PROD_MTEC_RAM          (0x22)	/* MTEC 8MB RAM */

#define MANUF_GVP2             (0x0891)	/* Great Valley Products */
#define PROD_SPECTRUM_RAM      (0x01)	/* EGS 28/24 Spectrum Graphics Board */
#define PROD_SPECTRUM_REG      (0x02)

#define MANUF_HELFRICH2        (0x0893)	/* Helfrich */
#define PROD_PICCOLO_RAM       (0x05)	/* Piccolo Graphics Board */
#define PROD_PICCOLO_REG       (0x06)
#define PROD_PEGGY_PLUS        (0x07)	/* PeggyPlus MPEG Decoder Board */
#define PROD_VIDEOCRUNCHER     (0x08)	/* VideoCruncher */
#define PROD_SD64_RAM          (0x0A)	/* SD64 Graphics Board */
#define PROD_SD64_REG          (0x0B)

#define MANUF_MACROSYSTEMS     (0x089B)	/* MacroSystems USA */
#define PROD_WARP_ENGINE       (0x13)	/* Warp Engine 40xx SCSI Controller */

#define MANUF_ELBOX            (0x089E)	/* ElBox Computer */
#define PROD_ELBOX_1200        (0x06)	/* Elbox 1200/4 RAM */

#define MANUF_HARMS_PROF       (0x0A00)	/* Harms Professional */
#define PROD_HARMS_030_PLUS    (0x10)	/* 030 plus */
#define PROD_3500_TURBO        (0xD0)	/* 3500 Turbo board */

#define MANUF_MICRONIK         (0x0A50)	/* Micronik */
#define PROD_RCA_120           (0x0A)	/* RCA 120 RAM */

#define MANUF_MEGA_MICRO       (0x1000)	/* MegaMicro */
#define PROD_SCRAM_500_SCSI    (0x03)	/* SCRAM 500 SCSI Controller */
#define PROD_SCRAM_500_RAM     (0x04)	/* SCRAM 500 RAM */

#define MANUF_IMTRONICS2       (0x1028)	/* Imtronics */
#define PROD_HURRICANE_2800_3  (0x39)	/* Hurricane 2800 68030 */
#define PROD_HURRICANE_2800_4  (0x57)	/* Hurricane 2800 68030 */

#define MANUF_KUPKE3           (0x1248)	/* Kupke */
#define PROD_GOLEM_3000        (0x01)	/* Golem HD 3000 */

#define MANUF_ITH              (0x1388)	/* ITH */
#define PROD_ISDN_MASTER_II    (0x01)	/* ISDN-Master II */

#define MANUF_VMC              (0x1389)	/* VMC */
#define PROD_ISDN_BLASTER_Z2   (0x01)	/* ISDN Blaster Z2 */
#define PROD_HYPERCOM_4        (0x02)	/* HyperCom 4 */

#define MANUF_INFORMATION      (0x157C)	/* Information */
#define PROD_ISDN_ENGINE_I     (0x64)	/* ISDN Engine I */

#define MANUF_VORTEX           (0x2017)	/* Vortex */
#define PROD_GOLDEN_GATE_386SX (0x07)	/* Golden Gate 80386SX Board */
#define PROD_GOLDEN_GATE_RAM   (0x08)	/* Golden Gate RAM */
#define PROD_GOLDEN_GATE_486   (0x09)	/* Golden Gate 80486 Board */

#define MANUF_DATAFLYER        (0x2062)	/* DataFlyer */
#define PROD_DATAFLYER_4000SXS (0x01)	/* DataFlyer 4000SX SCSI Controller */
#define PROD_DATAFLYER_4000SXR (0x02)	/* DataFlyer 4000SX RAM */

#define MANUF_READYSOFT        (0x2100)	/* ReadySoft */
#define PROD_AMAX              (0x01)	/* AMax II/IV */

#define MANUF_PHASE5           (0x2140)	/* Phase5 */
#define PROD_BLIZZARD_RAM      (0x01)	/* Blizzard RAM */
#define PROD_BLIZZARD          (0x02)	/* Blizzard */
#define PROD_BLIZZARD_1220_IV  (0x06)	/* Blizzard 1220-IV Turbo Board */
#define PROD_FASTLANE_RAM      (0x0A)	/* FastLane RAM */
#define PROD_FASTLANE_SCSI     (0x0B)	/* FastLane/Blizzard 1230-II SCSI/CyberSCSI */
#define PROD_CYBERSTORM_SCSI   (0x0C)	/* Blizzard 1220/CyberStorm */
#define PROD_BLIZZARD_1230_III (0x0D)	/* Blizzard 1230-III Turbo Board */
#define PROD_BLIZZARD_1230_IV  (0x11)	/* Blizzard 1230-IV/1260 Turbo Board */
#define PROD_BLIZZARD_2060SCSI (0x18)	/* Blizzard 2060 SCSI Controller */
#define PROD_CYBERSTORM_II     (0x19)	/* CyberStorm Mk II */
#define PROD_CYBERVISION       (0x22)	/* CyberVision64 Graphics Board */
#define PROD_CYBERVISION3D_PRT (0x32)	/* CyberVision64-3D Prototype */
#define PROD_CYBERVISION3D     (0x43)	/* CyberVision64-3D Graphics Board */

#define MANUF_DPS              (0x2169)	/* DPS */
#define PROD_DPS_PAR           (0x01)	/* Personal Animation Recorder */

#define MANUF_APOLLO2          (0x2200)	/* Apollo */
#define PROD_A620              (0x00)	/* A620 68020 Accelerator */
#define PROD_A620_2            (0x01)	/* A620 68020 Accelerator */

#define MANUF_APOLLO           (0x2222)	/* Apollo */
#define PROD_AT_APOLLO         (0x22)	/* AT-Apollo */
#define PROD_APOLLO_TURBO      (0x23)	/* Apollo Turbo Board */

#define MANUF_PETSOFF          (0x38A5)	/* Petsoff LP */
#define PROD_DELFINA           (0x00)	/* Delfina DSP */

#define MANUF_UWE_GERLACH      (0x3FF7)	/* Uwe Gerlach */
#define PROD_UG_RAM_ROM        (0xd4)	/* RAM/ROM */

#define MANUF_MACROSYSTEMS2    (0x4754)	/* MacroSystems Germany */
#define PROD_MAESTRO           (0x03)	/* Maestro */
#define PROD_VLAB              (0x04)	/* VLab */
#define PROD_MAESTRO_PRO       (0x05)	/* Maestro Pro */
#define PROD_RETINA_Z2         (0x06)	/* Retina Z2 Graphics Board */
#define PROD_MULTI_EVOLUTION   (0x08)	/* MultiEvolution */
#define PROD_TOCCATA           (0x0C)	/* Toccata Sound Board */
#define PROD_RETINA_Z3         (0x10)	/* Retina Z3 Graphics Board */
#define PROD_VLAB_MOTION       (0x12)	/* VLab Motion */
#define PROD_ALTAIS            (0x13)	/* Altais Graphics Board */
#define PROD_FALCON_040        (0xFD)	/* Falcon '040 Turbo Board */

#define MANUF_COMBITEC         (0x6766)	/* Combitec */

#define MANUF_SKI              (0x8000)	/* SKI Peripherals */
#define PROD_MAST_FIREBALL     (0x08)	/* M.A.S.T. Fireball SCSI Controller */
#define PROD_SKI_SCSI_SERIAL   (0x80)	/* SCSI / Dual Serial */

#define MANUF_CAMERON          (0xAA01)	/* Cameron */
#define PROD_PERSONAL_A4       (0x10)	/* Personal A4 */

#define MANUF_REIS_WARE        (0xAA11)	/* Reis-Ware */
#define PROD_RW_HANDYSCANNER   (0x11)	/* Handyscanner */


/* Illegal Manufacturer IDs. These do NOT appear in arch/m68k/amiga/zorro.c! */

#define MANUF_HACKER_INC       (0x07DB)	/* Hacker Inc. */
#define PROD_HACKER_SCSI       (0x01)	/* Hacker Inc. SCSI Controller */

#define MANUF_RES_MNGT_FORCE   (0x07DB)	/* Resource Management Force */
#define PROD_QUICKNET          (0x02)	/* QuickNet Ethernet */

#define MANUF_VECTOR2          (0x07DB)	/* Vector */
#define PROD_CONNECTION_2      (0xE0)	/* Vector Connection */
#define PROD_CONNECTION_3      (0xE1)	/* Vector Connection */
#define PROD_CONNECTION_4      (0xE2)	/* Vector Connection */
#define PROD_CONNECTION_5      (0xE3)	/* Vector Connection */


/*
 * GVP's identifies most of their product through the 'extended
 * product code' (epc). The epc has to be and'ed with the GVP_PRODMASK
 * before the identification.
 */

#define GVP_PRODMASK    (0xf8)
#define GVP_SCSICLKMASK (0x01)

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

#define ZORRO_NUM_AUTO		16

#ifdef __KERNEL__

extern int zorro_num_autocon;		/* # of autoconfig devices found */
extern struct ConfigDev zorro_autocon[ZORRO_NUM_AUTO];


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

extern void zorro_identify(void);
extern int zorro_get_list(char *buffer);

#endif	/* !__ASSEMBLY__ */
#endif	/* __KERNEL__ */

#endif /* __ZORRO_H */
