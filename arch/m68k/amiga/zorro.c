/*
 *    linux/arch/m68k/amiga/zorro.c
 *
 *    Copyright (C) 1995 Geert Uytterhoeven
 *
 *    This file is subject to the terms and conditions of the GNU General Public
 *    License.  See the file COPYING in the main directory of this archive
 *    for more details.
 */


#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/zorro.h>
#include <asm/setup.h>
#include <asm/bitops.h>
#include <asm/amigahw.h>


#ifdef CONFIG_ZORRO

   /*
    *    Zorro Expansion Device Manufacturers and Products
    */

struct Manufacturer {
   char *Name;
   u_short ID;
   u_short NumProd;
   struct Product *Products;
};

struct Product {
   char *Name;
   u_char ID;
};

struct GVP_Product {
   char *Name;
   enum GVP_ident ID;
};


   /*
    *    Macro's to make life easier
    */

#define BEGIN_PROD(id) static struct Product Prod_##id[] = {
#define PROD(name, id) \
   { name, PROD_##id },

#define BEGIN_GVP_PROD static struct GVP_Product Ext_Prod_GVP[] = {
#define GVP_PROD(name, id) \
   { name, GVP_##id },

#define BEGIN_MANUF static struct Manufacturer Manufacturers[] = {
#define MANUF(name, id) \
   { name, MANUF_##id, sizeof(Prod_##id)/sizeof(struct Product), Prod_##id },

#define END };


   /*
    *    Known Zorro Expansion Devices
    *
    *    Warning: Make sure the Manufacturer and Product names are not too
    *             long (max. 80 characters per board identification line)
    */

BEGIN_PROD(PACIFIC)
   PROD("SE 2000 A500", SE_2000_A500)
   PROD("HD Controller", PACIFIC_HD)
END

BEGIN_PROD(KUPKE)
   PROD("Golem RAM Box 2MB", GOLEM_BOX_2)
END

BEGIN_PROD(MEMPHIS)
   PROD("Stormbringer", STORMBRINGER)
END

BEGIN_PROD(3_STATE)
   PROD("Megamix 2000 RAM", MEGAMIX_2000)
END

BEGIN_PROD(COMMODORE2)
   PROD("A2088 XT Bridgeboard", A2088)
   PROD("A2286 AT Bridgeboard", A2286)
   PROD("A4091 SCSI Controller", A4091_2)
   PROD("A2386-SX Bridgeboard", A2386SX)
END

BEGIN_PROD(COMMODORE)
   PROD("A2090/A2090A HD Controller", A2090A)
   PROD("A590 SCSI Controller", A590)
   PROD("A2091 SCSI Controller", A2091)
   PROD("A2090B 2090 Autoboot Card", A2090B)
   PROD("A2060 Arcnet Card", ARCNET)
   PROD("A2052/58.RAM | 590/2091.RAM", CBMRAM)
   PROD("A560 Memory Module", A560RAM)
   PROD("A2232 Serial Prototype", A2232PROTO)
   PROD("A2232 Serial Production", A2232)
   PROD("A2620 68020/RAM Card", A2620)
   PROD("A2630 68030/RAM Card", A2630)
   PROD("A4091 SCSI Controller", A4091)
   PROD("A2065 Ethernet Card", A2065_2)
   PROD("Romulator Card", ROMULATOR)
   PROD("A3000 Test Fixture", A3000TESTFIX)
   PROD("A2386-SX Bridgeboard", A2386SX_2)
   PROD("A2065 Ethernet Card", A2065)
END

BEGIN_PROD(COMMODORE3)
   PROD("A2090A Combitec/MacroSystem", A2090A_CM)
END

BEGIN_PROD(KCS)
   PROD("KCS Power PC Board", POWER_BOARD)
END

BEGIN_PROD(CARDCO)
   PROD("Kronos 2000 SCSI Controller", KRONOS_2000_SCSI)
   PROD("A1000 SCSI Controller", A1000_SCSI)
   PROD("Escort SCSI Controller", ESCORT_SCSI)
   PROD("Cardco A2410 Hires Graphics card", CC_A2410)
END

BEGIN_PROD(A_SQUARED)
   PROD("Live! 2000", LIVE_2000)
END

BEGIN_PROD(COMSPEC)
   PROD("AX2000", AX2000)
END

BEGIN_PROD(ANAKIN)
   PROD("Easyl Tablet", EASYL)
END

BEGIN_PROD(MICROBOTICS)
   PROD("StarBoard II", STARBOARD_II)
   PROD("StarDrive", STARDRIVE)
   PROD("8-Up (Rev A)", 8_UP_A)
   PROD("8-Up (Rev Z)", 8_UP_Z)
   PROD("Delta Card RAM", DELTA_RAM)
   PROD("8-Star RAM", 8_STAR_RAM)
   PROD("8-Star", 8_STAR)
   PROD("VXL RAM", VXL_RAM)
   PROD("VXL-30 Turbo Board", VXL_30)
   PROD("Delta Card", DELTA)
   PROD("MBX 1200", MBX_1200)
   PROD("Hardframe 2000", HARDFRAME_2000)
   PROD("MBX 1200", MBX_1200_2)
END

BEGIN_PROD(ACCESS)
END

BEGIN_PROD(EXPANSION_TECH)
END

BEGIN_PROD(ASDG)
   PROD("Memory Expansion", ASDG_MEMORY)
   PROD("Memory Expansion", ASDG_MEMORY_2)
   PROD("Lan Rover Ethernet", LAN_ROVER)
   PROD("Twin-X Serial Card", TWIN_X)
END

BEGIN_PROD(IMTRONICS)
   PROD("Hurricane 2800 68030", HURRICANE_2800)
   PROD("Hurricane 2800 68030", HURRICANE_2800_2)
END

BEGIN_PROD(UNIV_OF_LOWELL)
   PROD("A2410 Hires Graphics Card", A2410)
END

BEGIN_PROD(AMERISTAR)
   PROD("A2065 Ethernet Card", AMERISTAR2065)
   PROD("A560 Arcnet Card", A560)
   PROD("A4066 Ethernet Card", A4066)
END

BEGIN_PROD(SUPRA)
   PROD("SupraDrive 4x4 SCSI Controller", SUPRADRIVE_4x4)
   PROD("2000 DMA HD", SUPRA_2000)
   PROD("500 HD/RAM", SUPRA_500)
   PROD("500XP/2000 RAM", SUPRA_500XP)
   PROD("500RX/2000 RAM", SUPRA_500RX)
   PROD("2400zi Modem", SUPRA_2400ZI)
   PROD("Wordsync SCSI Controller", WORDSYNC)
   PROD("Wordsync II SCSI Controller", WORDSYNC_II)
   PROD("2400zi+ Modem", SUPRA_2400ZIPLUS)
END

BEGIN_PROD(CSA)
   PROD("Magnum 40 SCSI Controller", MAGNUM)
   PROD("12 Gauge SCSI Controller", 12GAUGE)
END

BEGIN_PROD(MTEC2)
   PROD("AT500 RAM", AT500_2)
END

BEGIN_PROD(GVP3)
   PROD("Impact SCSI/Memory", IMPACT)
END

BEGIN_PROD(BYTEBOX)
   PROD("A500", BYTEBOX_A500)
END

BEGIN_PROD(POWER_COMPUTING)
   PROD("DKB 3128 RAM", DKB_3128)
   PROD("Rapid Fire SCSI Controller", RAPID_FIRE)
   PROD("DKB 1202 RAM", DKB_1202)
   PROD("DKB Cobra / Viper II Turbo Board", VIPER_II_COBRA)
   PROD("WildFire 060 Turbo Board", WILDFIRE_060)
   PROD("WildFire 060 Turbo Board", WILDFIRE_060_2)
END

BEGIN_PROD(GVP)
   PROD("Impact Series-I SCSI 4K", IMPACT_I_4K)
   PROD("Impact Series-I SCSI 16K/2", IMPACT_I_16K_2)
   PROD("Impact Series-I SCSI 16K/3", IMPACT_I_16K_3)
   PROD("Impact 3001 IDE", IMPACT_3001_IDE)
/* PROD("Impact 3001 RAM", IMPACT_3001_RAM) */
   PROD("Generic GVP product", GVP)
   PROD("Series II SCSI Controller", GVPIISCSI)
   PROD("Series II SCSI Controller", GVPIISCSI_2)
   PROD("Series II RAM", GVPIIRAM)
   PROD("A2000 68030 Turbo Board", GVP_A2000_030)
/* PROD("Impact 3001 IDE", IMPACT_3001_IDE_2) */
   PROD("GFORCE 040 with SCSI Controller", GFORCE_040_SCSI)
   PROD("IV-24 Graphics Board", GVPIV_24)
   PROD("GFORCE 040 Turbo Board", GFORCE_040)
/* PROD("I/O Extender", GVPIO_EXT) */
END

BEGIN_GVP_PROD
   GVP_PROD("GFORCE 040", GFORCE_040)
   GVP_PROD("GFORCE 040 with SCSI controller", GFORCE_040_SCSI)
   GVP_PROD("A1291 SCSI controller", A1291_SCSI)
   GVP_PROD("COMBO 030 R4", COMBO_R4)
   GVP_PROD("COMBO 030 R4 with SCSI controller", COMBO_R4_SCSI)
   GVP_PROD("Phone Pak", PHONEPAK)
   GVP_PROD("IO-Extender", IOEXT)
   GVP_PROD("GFORCE 030", GFORCE_030)
   GVP_PROD("GFORCE 030 with SCSI controller", GFORCE_030_SCSI)
   GVP_PROD("A530", A530)
   GVP_PROD("A530 with SCSI", A530_SCSI)
   GVP_PROD("COMBO 030 R3", COMBO_R3)
   GVP_PROD("COMBO 030 R3 with SCSI controller", COMBO_R3_SCSI)
   GVP_PROD("SERIES-II SCSI controller", SERIESII)
END

BEGIN_PROD(SYNERGY)
END

BEGIN_PROD(XETEC)
   PROD("FastCard SCSI Controller", FASTCARD_SCSI)
   PROD("FastCard RAM", FASTCARD_RAM)
END

BEGIN_PROD(PPI)
   PROD("Mercury Turbo Board", MERCURY)
   PROD("PP&S A3000 68040 Turbo Board", PPS_A3000_040)
   PROD("PP&S A2000 68040 Turbo Board", PPS_A2000_040)
   PROD("Zeus SCSI Controller", ZEUS)
   PROD("PP&S A500 68040 Turbo Board", PPS_A500_040)
END

BEGIN_PROD(XEBEC)
END

BEGIN_PROD(SPIRIT)
   PROD("HDA 506 Harddisk", HDA_506)
   PROD("OctaByte RAM", OCTABYTE_RAM)
END

BEGIN_PROD(BSC)
   PROD("ALF 3 SCSI Controller", ALF_3_SCSI)
END

BEGIN_PROD(BSC3)
   PROD("ALF 2 SCSI Controller", ALF_2_SCSI)
   PROD("ALF 2 SCSI Controller", ALF_2_SCSI_2)
   PROD("ALF 3 SCSI Controller", ALF_3_SCSI_2)
END

BEGIN_PROD(C_LTD)
   PROD("Kronos SCSI Controller", KRONOS_SCSI)
   PROD("A1000 SCSI Controller", A1000_SCSI_2)
END

BEGIN_PROD(JOCHHEIM)
   PROD("Jochheim RAM", JOCHHEIM_RAM)
END

BEGIN_PROD(CHECKPOINT)
   PROD("Serial Solution", SERIAL_SOLUTION)
END

BEGIN_PROD(ICD)
   PROD("Advantage 2000 SCSI Controller", ADVANTAGE_2000)
END

BEGIN_PROD(KUPKE2)
   PROD("Golem SCSI-II Controller", KUPKE_SCSI_II)
   PROD("Golem Box", GOLEM_BOX)
   PROD("030/882 Turbo Board", KUPKE_TURBO)
   PROD("Golem SCSI/AT Controller", KUPKE_SCSI_AT)
END

BEGIN_PROD(GVP4)
   PROD("A2000-RAM8/2", A2000_RAM8)
END

BEGIN_PROD(INTERWORKS_NET)
END

BEGIN_PROD(HARDITAL)
   PROD("TQM 68030+68882 Turbo Board", TQM)
END

BEGIN_PROD(BSC2)
   PROD("Oktagon 2008 SCSI Controller", OKTAGON_SCSI)
   PROD("Tandem AT-2008/508 IDE Controller", TANDEM)
   PROD("Alpha RAM 1200", ALPHA_RAM_1200)
   PROD("Oktagon 2008 RAM", OKTAGON_RAM)
   PROD("Alfa Data MultiFace I", MULTIFACE_I)
   PROD("Alfa Data MultiFace II", MULTIFACE_II)
   PROD("Alfa Data MultiFace III", MULTIFACE_III)
   PROD("Framebuffer", BSC_FRAEMBUFFER)
   PROD("Graffiti Graphics Board (RAM)", GRAFFITI_RAM)
   PROD("Graffiti Graphics Board (REG)", GRAFFITI_REG)
   PROD("ISDN MasterCard", ISDN_MASTERCARD)
   PROD("ISDN MasterCard II", ISDN_MASTERCARD_2)
END

BEGIN_PROD(ADV_SYS_SOFT)
   PROD("Nexus SCSI Controller", NEXUS_SCSI)
   PROD("Nexus RAM", NEXUS_RAM)
END

BEGIN_PROD(IMPULSE)
   PROD("FireCracker 24", FIRECRACKER_24)
END

BEGIN_PROD(IVS)
   PROD("GrandSlam PIC 2 RAM", GRANDSLAM_PIC_2)
   PROD("GrandSlam PIC 1 RAM", GRANDSLAM_PIC_1)
   PROD("OverDrive HD", IVS_OVERDRIVE)
   PROD("Trumpcard Classic SCSI Controller", TRUMPCARD_CLASSIC)
   PROD("Trumpcard Pro SCSI Controller", TRUMPCARD_PRO)
   PROD("Meta-4 RAM", META_4)
   PROD("Wavetools Sound Board", WAVETOOLS)
   PROD("Vector SCSI Controller", VECTOR)
   PROD("Vector SCSI Controller", VECTOR_2)
END

BEGIN_PROD(VECTOR)
   PROD("Connection Serial IO", CONNECTION)
END

BEGIN_PROD(XPERT_PRODEV)
   PROD("Visiona Graphics Board (RAM)", VISIONA_RAM)
   PROD("Visiona Graphics Board (REG)", VISIONA_REG)
   PROD("Merlin Graphics Board (RAM)", MERLIN_RAM)
   PROD("Merlin Graphics Board (REG)", MERLIN_REG)
   PROD("Merlin Graphics Board (REG)", MERLIN_REG_2)
END

BEGIN_PROD(HYDRA_SYSTEMS)
   PROD("Amiganet Board", AMIGANET)
END

BEGIN_PROD(SUNRIZE)
   PROD("AD516 Audio", AD516)
END

BEGIN_PROD(TRICERATOPS)
   PROD("Multi I/O Board", TRICERATOPS)
END

BEGIN_PROD(APPLIED_MAGIC)
   PROD("DMI Resolver Graphics Board", DMI_RESOLVER)
   PROD("Digital Broadcaster", DIGITAL_BCASTER)
END

BEGIN_PROD(GFX_BASE)
   PROD("GDA-1 Graphics Board (RAM)", GDA_1_RAM)
   PROD("GDA-1 Graphics Board (REG)", GDA_1_REG)
END

BEGIN_PROD(ROCTEC)
   PROD("RH 800C Hard Disk Controller", RH_800C)
   PROD("RH 800C RAM", RH_800C_RAM)
END

BEGIN_PROD(HELFRICH1)
   PROD("Rainbow3 Graphics Board", RAINBOW3)
END

BEGIN_PROD(SW_RESULT_ENTS)
   PROD("GG2+ Bus Converter", GG2PLUS)
END

BEGIN_PROD(MASOBOSHI)
   PROD("Master Card RAM", MASTER_CARD_RAM)
   PROD("Master Card SCSI Controller", MASTER_CARD_SCSI)
   PROD("MVD 819", MVD_819)
END

BEGIN_PROD(VILLAGE_TRONIC)
   PROD("Domino Graphics Board (RAM)", DOMINO_RAM)
   PROD("Domino Graphics Board (REG)", DOMINO_REG)
   PROD("Picasso II Graphics Board (RAM)", PICASSO_II_RAM)
   PROD("Picasso II Graphics Board (REG)", PICASSO_II_REG)
   PROD("Picasso II/II+ Graphics Board (Segmented Mode)", PICASSO_II_SEGM)
   PROD("Picassio IV Graphics Board", PICASSO_IV)
   PROD("Picassio IV Graphics Board", PICASSO_IV_2)
   PROD("Picassio IV Graphics Board", PICASSO_IV_3)
   PROD("Picassio IV Graphics Board", PICASSO_IV_4)
   PROD("Ariadne Ethernet Card", ARIADNE)
END

BEGIN_PROD(UTILITIES_ULTD)
   PROD("Emplant Deluxe SCSI Controller", EMPLANT_DELUXE)
   PROD("Emplant Deluxe SCSI Controller", EMPLANT_DELUXE2)
END

BEGIN_PROD(AMITRIX)
   PROD("Multi-IO", AMITRIX_MULTI_IO)
   PROD("CD-RAM Memory", AMITRIX_CD_RAM)
END

BEGIN_PROD(ARMAX)
   PROD("OmniBus Graphics Board", OMNIBUS)
END

BEGIN_PROD(NEWTEK)
   PROD("VideoToaster", VIDEOTOASTER)
END

BEGIN_PROD(MTEC)
   PROD("AT500 IDE Controller", AT500)
   PROD("68030 Turbo Board", MTEC_68030)
   PROD("68020i Turbo Board", MTEC_68020I)
   PROD("A1200 T68030/42 RTC Turbo Board", MTEC_T1230)
   PROD("8MB RAM", MTEC_RAM)
END

BEGIN_PROD(GVP2)
   PROD("EGS 28/24 Spectrum Graphics Board (RAM)", SPECTRUM_RAM)
   PROD("EGS 28/24 Spectrum Graphics Board (REG)", SPECTRUM_REG)
END

BEGIN_PROD(HELFRICH2)
   PROD("Piccolo Graphics Board (RAM)", PICCOLO_RAM)
   PROD("Piccolo Graphics Board (REG)", PICCOLO_REG)
   PROD("PeggyPlus MPEG Decoder Board", PEGGY_PLUS)
   PROD("VideoCruncher", VIDEOCRUNCHER)
   PROD("SD64 Graphics Board (RAM)", SD64_RAM)
   PROD("SD64 Graphics Board (REG)", SD64_REG)
END

BEGIN_PROD(MACROSYSTEMS)
   PROD("Warp Engine 40xx SCSI Controller", WARP_ENGINE)
END

BEGIN_PROD(ELBOX)
   PROD("Elbox 1200/4 RAM", ELBOX_1200)
END

BEGIN_PROD(HARMS_PROF)
   PROD("030 plus", HARMS_030_PLUS)
   PROD("3500 Turbo board", 3500_TURBO)
END

BEGIN_PROD(MICRONIK)
   PROD("RCA 120 RAM", RCA_120)
END

BEGIN_PROD(MEGA_MICRO)
   PROD("SCRAM 500 SCSI Controller", SCRAM_500_SCSI)
   PROD("SCRAM 500 RAM", SCRAM_500_RAM)
END

BEGIN_PROD(IMTRONICS2)
   PROD("Hurricane 2800 68030", HURRICANE_2800_3)
   PROD("Hurricane 2800 68030", HURRICANE_2800_4)
END

BEGIN_PROD(KUPKE3)
   PROD("Golem HD 3000", GOLEM_3000)
END

BEGIN_PROD(ITH)
   PROD("ISDN-Master II", ISDN_MASTER_II)
END

BEGIN_PROD(VMC)
   PROD("ISDN Blaster Z2", ISDN_BLASTER_Z2)
   PROD("HyperCom 4", HYPERCOM_4)
END

BEGIN_PROD(INFORMATION)
   PROD("ISDN Engine I", ISDN_ENGINE_I)
END

BEGIN_PROD(VORTEX)
   PROD("Golden Gate 80386SX Board", GOLDEN_GATE_386SX)
   PROD("Golden Gate RAM", GOLDEN_GATE_RAM)
   PROD("Golden Gate 80486 Board", GOLDEN_GATE_486)
END

BEGIN_PROD(DATAFLYER)
   PROD("4000SX SCSI Controller", DATAFLYER_4000SXS)
   PROD("4000SX RAM", DATAFLYER_4000SXR)
END

BEGIN_PROD(READYSOFT)
   PROD("AMax II/IV", AMAX)
END

BEGIN_PROD(PHASE5)
   PROD("Blizzard RAM", BLIZZARD_RAM)
   PROD("Blizzard", BLIZZARD)
   PROD("Blizzard 1220-IV Turbo Board", BLIZZARD_1220_IV)
   PROD("FastLane RAM", FASTLANE_RAM)
   PROD("FastLane/Blizzard 1230-II/CyberSCSI", FASTLANE_SCSI)
   PROD("Blizzard 1220/CyberStorm", CYBERSTORM_SCSI)
   PROD("Blizzard 1230-III Turbo Board", BLIZZARD_1230_III)
   PROD("Blizzard 1230-IV/1260 Turbo Board", BLIZZARD_1230_IV)
   PROD("Blizzard 2060 SCSI Controller", BLIZZARD_2060SCSI)
   PROD("CyberStorm Mk II", CYBERSTORM_II)
   PROD("CyberVision64 Graphics Board", CYBERVISION)
   PROD("CyberVision64-3D Graphics Board Prototype)", CYBERVISION3D_PRT)
   PROD("CyberVision64-3D Graphics Board", CYBERVISION3D)
END

BEGIN_PROD(DPS)
   PROD("Personal Animation Recorder", DPS_PAR)
END

BEGIN_PROD(APOLLO2)
   PROD("A620 68020 Accelerator", A620)
   PROD("A620 68020 Accelerator", A620_2)
END

BEGIN_PROD(APOLLO)
   PROD("AT-Apollo", AT_APOLLO)
   PROD("Turbo Board", APOLLO_TURBO)
END

BEGIN_PROD(PETSOFF)
   PROD("Delfina DSP", DELFINA)
END

BEGIN_PROD(UWE_GERLACH)
   PROD("RAM/ROM", UG_RAM_ROM)
END

BEGIN_PROD(MACROSYSTEMS2)
   PROD("Maestro", MAESTRO)
   PROD("VLab", VLAB)
   PROD("Maestro Pro", MAESTRO_PRO)
   PROD("Retina Z2 Graphics Board", RETINA_Z2)
   PROD("MultiEvolution", MULTI_EVOLUTION)
   PROD("Toccata Sound Board", TOCCATA)
   PROD("Retina Z3 Graphics Board", RETINA_Z3)
   PROD("VLab Motion", VLAB_MOTION)
   PROD("Altais Graphics Board", ALTAIS)
   PROD("Falcon '040 Turbo Board", FALCON_040)
END

BEGIN_PROD(COMBITEC)
END

BEGIN_PROD(SKI)
   PROD("MAST Fireball SCSI Controller", MAST_FIREBALL)
   PROD("SCSI / Dual Serial", SKI_SCSI_SERIAL)
END

BEGIN_PROD(CAMERON)
   PROD("Personal A4", PERSONAL_A4)
END

BEGIN_PROD(REIS_WARE)
   PROD("Handyscanner", RW_HANDYSCANNER)
END


BEGIN_MANUF
   MANUF("Pacific Peripherals", PACIFIC)
   MANUF("Kupke", KUPKE)
   MANUF("Memphis", MEMPHIS)
   MANUF("3-State", 3_STATE)
   MANUF("Commodore", COMMODORE2)
   MANUF("Commodore", COMMODORE)
   MANUF("Commodore", COMMODORE3)
   MANUF("Kolff Computer Supplies", KCS)
   MANUF("Cardco", CARDCO)
   MANUF("A-Squared", A_SQUARED)
   MANUF("ComSpec Communications", COMSPEC)
   MANUF("Anakin", ANAKIN)
   MANUF("MicroBotics", MICROBOTICS)
   MANUF("Access Associates", ACCESS)
   MANUF("Expansion Technologies", EXPANSION_TECH)
   MANUF("ASDG", ASDG)
   MANUF("Imtronics", IMTRONICS)
   MANUF("University of Lowell", UNIV_OF_LOWELL)
   MANUF("Ameristar", AMERISTAR)
   MANUF("Supra", SUPRA)
   MANUF("Computer Systems Ass.", CSA)
   MANUF("M-Tech", MTEC2)
   MANUF("Great Valley Products", GVP3)
   MANUF("ByteBox", BYTEBOX)
   MANUF("Power Computing", POWER_COMPUTING)
   MANUF("Great Valley Products", GVP)
   MANUF("Synergy", SYNERGY)
   MANUF("Xetec", XETEC)
   MANUF("Progressive Peripherals", PPI)
   MANUF("Xebec", XEBEC)
   MANUF("Spirit", SPIRIT)
   MANUF("BSC", BSC)
   MANUF("BSC", BSC3)
   MANUF("C Ltd.", C_LTD)
   MANUF("Jochheim", JOCHHEIM)
   MANUF("Checkpoint Technologies", CHECKPOINT)
   MANUF("ICD", ICD)
   MANUF("Kupke", KUPKE2)
   MANUF("Great Valley Products", GVP4)
   MANUF("Interworks Network", INTERWORKS_NET)
   MANUF("Hardital Synthesis", HARDITAL)
   MANUF("BSC", BSC2)
   MANUF("Advanced Systems & Software", ADV_SYS_SOFT)
   MANUF("Impulse", IMPULSE)
   MANUF("IVS", IVS)
   MANUF("Vector", VECTOR)
   MANUF("XPert/ProDev", XPERT_PRODEV)
   MANUF("Hydra Systems", HYDRA_SYSTEMS)
   MANUF("Sunrize Industries", SUNRIZE)
   MANUF("Triceratops", TRICERATOPS)
   MANUF("Applied Magic", APPLIED_MAGIC)
   MANUF("GFX-Base", GFX_BASE)
   MANUF("RocTec", ROCTEC)
   MANUF("Helfrich", HELFRICH1)
   MANUF("Software Result Enterprises", SW_RESULT_ENTS)
   MANUF("Masoboshi", MASOBOSHI)
   MANUF("Village Tronic", VILLAGE_TRONIC)
   MANUF("Utilities Unlimited", UTILITIES_ULTD)
   MANUF("Amitrix", AMITRIX)
   MANUF("ArMax", ARMAX)
   MANUF("NewTek", NEWTEK)
   MANUF("M-Tech", MTEC)
   MANUF("Great Valley Products", GVP2)
   MANUF("Helfrich", HELFRICH2)
   MANUF("MacroSystems", MACROSYSTEMS)
   MANUF("ElBox Computer", ELBOX)
   MANUF("Harms Professional", HARMS_PROF)
   MANUF("Micronik", MICRONIK)
   MANUF("MegaMicro", MEGA_MICRO)
   MANUF("Imtronics", IMTRONICS2)
   MANUF("Kupke", KUPKE3)
   MANUF("ITH", ITH)
   MANUF("VMC", VMC)
   MANUF("Information", INFORMATION)
   MANUF("Vortex", VORTEX)
   MANUF("DataFlyer", DATAFLYER)
   MANUF("ReadySoft", READYSOFT)
   MANUF("Phase5", PHASE5)
   MANUF("DPS", DPS)
   MANUF("Apollo", APOLLO2)
   MANUF("Apollo", APOLLO)
   MANUF("Petsoff LP", PETSOFF)
   MANUF("Uwe Gerlach", UWE_GERLACH)
   MANUF("MacroSystems", MACROSYSTEMS2)
   MANUF("Combitec", COMBITEC)
   MANUF("SKI Peripherals", SKI)
   MANUF("Cameron", CAMERON)
   MANUF("Reis-Ware", REIS_WARE)
END

#define NUM_MANUF (sizeof(Manufacturers)/sizeof(struct Manufacturer))
#define NUM_GVP_PROD (sizeof(Ext_Prod_GVP)/sizeof(struct GVP_Product))

#endif /* CONFIG_ZORRO */


   /*
    *    Expansion Devices
    */

int zorro_num_autocon;
struct ConfigDev zorro_autocon[ZORRO_NUM_AUTO];
static u_long BoardPartFlags[ZORRO_NUM_AUTO] = { 0, };

   
   /*
    *    Find the key for the next unconfigured expansion device of a specific
    *    type.
    *
    *    Part is a device specific number (0 <= part <= 31) to allow for the
    *    independent configuration of independent parts of an expansion board.
    *    Thanks to Jes Soerensen for this idea!
    *
    *    Index is used to specify the first board in the autocon list
    *    to be tested. It was inserted in order to solve the problem
    *    with the GVP boards that uses the same product code, but
    *    it should help if there are other companies uses the same
    *    method as GVP. Drivers for boards which are not using this
    *    method does not need to think of this - just set index = 0.
    *    
    *    Example:
    *
    *       while ((key = zorro_find(MY_MANUF, MY_PROD, MY_PART, 0))) {
    *          cd = zorro_get_board(key);
    *          initialise_this_board;
    *          zorro_config_board(key, MY_PART);
    *       }
    */

int zorro_find(int manuf, int prod, int part, int index)
{
   int key;
   struct ConfigDev *cd;
  
   if (!MACH_IS_AMIGA || !AMIGAHW_PRESENT(ZORRO))
      return(0);

   if ((part < 0) || (part > 31)) {
      printk("zorro_find: bad part %d\n", part);
      return(0);
   }

   for (key = index + 1; key <= zorro_num_autocon; key++) {
      cd = &zorro_autocon[key-1];
      if ((cd->cd_Rom.er_Manufacturer == manuf) &&
          (cd->cd_Rom.er_Product == prod) &&
          !(BoardPartFlags[key-1] & (1<<part)))
         break;
   }
   return(key <= zorro_num_autocon ? key : 0);
}


   /*
    *    Get the board for a specified key
    */

struct ConfigDev *zorro_get_board(int key)
{
   struct ConfigDev *cd = NULL;

   if ((key < 1) || (key > zorro_num_autocon))
      printk("zorro_get_board: bad key %d\n", key);
   else
      cd = &zorro_autocon[key-1];

   return(cd);
}


   /*
    *    Mark a part of a board as configured
    */

void zorro_config_board(int key, int part)
{
   if ((key < 1) || (key > zorro_num_autocon))
      printk("zorro_config_board: bad key %d\n", key);
   else if ((part < 0) || (part > 31))
      printk("zorro_config_board: bad part %d\n", part);
   else
      BoardPartFlags[key-1] |= 1<<part;
}


   /*
    *    Mark a part of a board as unconfigured
    *
    *    This function is mainly intended for the unloading of LKMs
    */

void zorro_unconfig_board(int key, int part)
{
   if ((key < 1) || (key > zorro_num_autocon))
      printk("zorro_unconfig_board: bad key %d\n", key);
   else if ((part < 0) || (part > 31))
      printk("zorro_unconfig_board: bad part %d\n", part);
   else
      BoardPartFlags[key-1] &= ~(1<<part);
}


#ifdef CONFIG_ZORRO

   /*
    *    Identify an AutoConfig Expansion Device
    *
    *    If the board was configured by a Linux/m68k driver, an asterisk will
    *    be printed before the board address (except for unknown and `Hacker
    *    Test' boards).
    */

static int identify(int devnum, char *buf)
{
   struct ConfigDev *cd;
   int manuf, prod;
   u_long addr, size;
   char *manufname, *prodname, *is_mem;
   char zorro, mag, configured;
   int identified = 0;
   int i, j, k, len = 0;
   enum GVP_ident epc;

   cd = &zorro_autocon[devnum];
   manuf = cd->cd_Rom.er_Manufacturer;
   prod = cd->cd_Rom.er_Product;
   addr = (u_long)cd->cd_BoardAddr;
   size = cd->cd_BoardSize;
   configured = BoardPartFlags[devnum] ? '*' : ' ';
   manufname = prodname = "<UNKNOWN>";

   for (i = 0; i < NUM_MANUF; i++)
      if (Manufacturers[i].ID == manuf) {
         manufname = Manufacturers[i].Name;
         for (j = 0; j < Manufacturers[i].NumProd; j++)
            if (Manufacturers[i].Products[j].ID == prod)
               if ((manuf != MANUF_GVP) || (prod != PROD_GVP)) {
                  prodname = Manufacturers[i].Products[j].Name;
                  identified = 1;
                  break;
               } else {
		       /*
			* The epc must be read as a short from the
			* hardware.
			*/
                  epc = *(unsigned short *)ZTWO_VADDR(addr+0x8000) &
                        GVP_PRODMASK;
                  for (k = 0; k < NUM_GVP_PROD; k++)
                     if (Ext_Prod_GVP[k].ID == epc) {
                        prodname = Ext_Prod_GVP[k].Name;
                        identified = 1;
                        break;
                     }
               }
         break;
      }

   switch (cd->cd_Rom.er_Type & ERT_TYPEMASK) {
      case ERT_ZORROII:
         zorro = '2';
         break;
      case ERT_ZORROIII:
         zorro = '3';
         break;
      default:
         zorro = '?';
         break;
   }
   if (size & 0xfffff) {
      size >>= 10;
      mag = 'K';
   } else {
      size >>= 20;
      mag = 'M';
   }
   if (cd->cd_Rom.er_Type & ERTF_MEMLIST)
      is_mem = " MEM";
   else
      is_mem = "";

   if (identified)
      len = sprintf(buf, " %c0x%08lx: %s %s (Z%c, %ld%c%s)\n", configured, addr,
                    manufname, prodname, zorro, size, mag, is_mem);
   else if (manuf == MANUF_HACKER)
      len = sprintf(buf, "  0x%08lx: Hacker Test Board 0x%02x (Z%c, %ld%c%s)\n",
                    addr, prod, zorro, size, mag, is_mem);
   else {
      len = sprintf(buf, "  0x%08lx: [%04x:%02x] made by %s (Z%c, %ld%c%s)\n",
                    addr, manuf, prod, manufname, zorro, size, mag, is_mem);
      len += sprintf(buf+len, "  Please report this unknown device to "
                     "Geert.Uytterhoeven@cs.kuleuven.ac.be\n");
   }
   return(len);
}


   /*
    *    Identify all known AutoConfig Expansion Devices
    */

void zorro_identify(void)
{
   int i;
   char tmp[160];

   if (!AMIGAHW_PRESENT(ZORRO))
      return;

   printk("Probing AutoConfig expansion device(s):\n");
   for (i = 0; i < zorro_num_autocon; i++) {
      identify(i, tmp);
      printk(tmp);
   }
   if (!zorro_num_autocon)
      printk("No AutoConfig expansion devices present.\n");
}


   /*
    *    Get the list of all AutoConfig Expansion Devices
    */

int zorro_get_list(char *buffer)
{
   int i, j, len = 0;
   char tmp[160];

   if (MACH_IS_AMIGA && AMIGAHW_PRESENT(ZORRO)) {
      len = sprintf(buffer, "AutoConfig expansion devices:\n");
      for (i = 0; i < zorro_num_autocon; i++) {
         j = identify(i, tmp);
         if (len+j >= 4075) {
            len += sprintf(buffer+len, "4K limit reached!\n");
            break;
         }
         strcpy(buffer+len, tmp);
         len += j;
      }
   }
   return(len);
}

#endif /* CONFIG_ZORRO */


   /*
    *    Bitmask indicating portions of available Zorro II RAM that are unused
    *    by the system. Every bit represents a 64K chunk, for a maximum of 8MB
    *    (128 chunks, physical 0x00200000-0x009fffff).
    *
    *    If you want to use (= allocate) portions of this RAM, you should clear
    *    the corresponding bits.
    *
    *    Possible uses:
    *       - z2ram device
    *       - SCSI DMA bounce buffers
    */

u_long zorro_unused_z2ram[4] = { 0, 0, 0, 0 };


static void mark_region(u_long addr, u_long size, int flag)
{
   u_long start, end, chunk;

   if (flag) {
      start = (addr+Z2RAM_CHUNKMASK) & ~Z2RAM_CHUNKMASK;
      end = (addr+size) & ~Z2RAM_CHUNKMASK;
   } else {
      start = addr & ~Z2RAM_CHUNKMASK;
      end = (addr+size+Z2RAM_CHUNKMASK) & ~Z2RAM_CHUNKMASK;
   }
   if (end <= Z2RAM_START || start >= Z2RAM_END)
      return;
   start = start < Z2RAM_START ? 0x00000000 : start-Z2RAM_START;
   end = end > Z2RAM_END ? Z2RAM_SIZE : end-Z2RAM_START;
   while (start < end) {
      chunk = start>>Z2RAM_CHUNKSHIFT;
      if (flag)
         set_bit( chunk, zorro_unused_z2ram );
      else
         clear_bit( chunk, zorro_unused_z2ram );
      start += Z2RAM_CHUNKSIZE;
   }
}


   /*
    *    Initialization
    */

__initfunc(void zorro_init(void))
{
   int i;
   struct ConfigDev *cd;

   if (!AMIGAHW_PRESENT(ZORRO))
      return;

   /* Mark all available Zorro II memory */
   for (i = 0; i < zorro_num_autocon; i++) {
      cd = &zorro_autocon[i];
      if (cd->cd_Rom.er_Type & ERTF_MEMLIST)
         mark_region((u_long)cd->cd_BoardAddr, cd->cd_BoardSize, 1);
   }
   /* Unmark all used Zorro II memory */
   for (i = 0; i < m68k_num_memory; i++)
      mark_region(m68k_memory[i].addr, m68k_memory[i].size, 0);
}
