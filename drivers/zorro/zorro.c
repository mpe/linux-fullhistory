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
#include <asm/setup.h>
#include <asm/bitops.h>
#include <asm/amigahw.h>
#include <linux/zorro.h>


#ifdef CONFIG_ZORRO

    /*
     *  Zorro Expansion Device Manufacturers and Products
     */

struct Manufacturer {
    const char *Name;
    u_short Manuf;
    u_short NumProd;
    const struct Product *Products;
};

struct Product {
    const char *Name;
    u_char Class;
    u_char Prod;
};

struct GVP_Product {
    const char *Name;
    u_char Class;
    u_char EPC;
};


    /*
     *  Macro's to make life easier
     */

#define ARRAYSIZE(x)		(sizeof(x)/sizeof(*(x)))

#define BEGIN_PROD(id) \
    static struct Product Prod_##id[] = {
#define PROD(name, class, id) \
    { name, ZORRO_CLASS_##class, ZORRO_PROD(ZORRO_PROD_##id) },

#define BEGIN_GVP_PROD \
    static struct GVP_Product Ext_Prod_GVP[] = {
#define GVP_PROD(name, class, id) \
    { name, ZORRO_CLASS_##class, ZORRO_EPC(ZORRO_PROD_##id) },

#define BEGIN_MANUF \
    static struct Manufacturer Manufacturers[] = {
#define MANUF(name, id) \
    { name, ZORRO_MANUF_##id, ARRAYSIZE(Prod_##id), Prod_##id },

#define END \
    };


    /*
     *  Recognized Zorro Expansion Devices
     */

BEGIN_PROD(PACIFIC_PERIPHERALS)
    PROD("SE 2000 A500", HD, PACIFIC_PERIPHERALS_SE_2000_A500)
    PROD(NULL, SCSI, PACIFIC_PERIPHERALS_SCSI)
END

BEGIN_PROD(MACROSYSTEMS_USA_2)
    PROD("Warp Engine", TURBO_SCSI_RAM, MACROSYSTEMS_WARP_ENGINE)
END

BEGIN_PROD(KUPKE_1)
    PROD("Golem RAM Box 2MB", RAM, KUPKE_GOLEM_RAM_BOX_2MB)
END

BEGIN_PROD(MEMPHIS)
    PROD("Stormbringer", TURBO, MEMPHIS_STORMBRINGER)
END

BEGIN_PROD(3_STATE)
    PROD("Megamix 2000", RAM, 3_STATE_MEGAMIX_2000)
END

BEGIN_PROD(COMMODORE_BRAUNSCHWEIG)
    PROD("A2088 XT/A2286 AT", BRIDGE, CBM_A2088_A2286)
    PROD("A2286 AT", BRIDGE, CBM_A2286)
    PROD("A4091", SCSI, CBM_A4091_1)
    PROD("A2386-SX", BRIDGE, CBM_A2386SX_1)
END

BEGIN_PROD(COMMODORE_WEST_CHESTER_1)
    PROD("A2090/A2090A", SCSI, CBM_A2090A)
    PROD("A590/A2091", SCSI, CBM_A590_A2091_1)
    PROD("A590/A2091", SCSI, CBM_A590_A2091_2)
    PROD("A2090B 2090 Autoboot", SCSI, CBM_A2090B)
    PROD("A2060", ARCNET, CBM_A2060)
    PROD("A590/A2052/A2058/A2091", RAM, CBM_A590_A2052_A2058_A2091)
    PROD("A560", RAM, CBM_A560_RAM)
    PROD("A2232 Prototype", MULTIIO, CBM_A2232_PROTOTYPE)
    PROD("A2232", MULTIIO, CBM_A2232)
    PROD("A2620 68020/RAM", TURBO_RAM, CBM_A2620)
    PROD("A2630 68030/RAM", TURBO_RAM, CBM_A2630)
    PROD("A4091", SCSI, CBM_A4091_2)
    PROD("A2065", ETHERNET, CBM_A2065_1)
    PROD("Romulator Card", UNKNOWN, CBM_ROMULATOR)
    PROD("A3000 Test Fixture", MISC, CBM_A3000_TEST_FIXTURE)
    PROD("A2386-SX", BRIDGE, CBM_A2386SX_2)
    PROD("A2065", ETHERNET, CBM_A2065_2)
END

BEGIN_PROD(COMMODORE_WEST_CHESTER_2)
    PROD("A2090/A2090A Combitec/MacroSystem", SCSI, CBM_A2090A_CM)
END

BEGIN_PROD(PROGRESSIVE_PERIPHERALS_AND_SYSTEMS_2)
    PROD("EXP8000", RAM, PPS_EXP8000)
END

BEGIN_PROD(KOLFF_COMPUTER_SUPPLIES)
    PROD("KCS Power PC Board", BRIDGE, KCS_POWER_PC_BOARD)
END

BEGIN_PROD(CARDCO_1)
    PROD("Kronos 2000", SCSI, CARDCO_KRONOS_2000_1)
    PROD("A1000", SCSI, CARDCO_A1000_1)
    PROD("Escort", SCSI, CARDCO_ESCORT)
    PROD("A2410 HiRes", GFX, CARDCO_A2410)
END

BEGIN_PROD(A_SQUARED)
    PROD("Live! 2000", VIDEO, A_SQUARED_LIVE_2000)
END

BEGIN_PROD(COMSPEC_COMMUNICATIONS)
    PROD("AX2000", RAM, COMSPEC_COMMUNICATIONS_AX2000)
END

BEGIN_PROD(ANAKIN_RESEARCH)
    PROD("Easyl", TABLET, ANAKIN_RESEARCH_EASYL)
END

BEGIN_PROD(MICROBOTICS)
    PROD("StarBoard II", RAM, MICROBOTICS_STARBOARD_II)
    PROD("StarDrive", SCSI, MICROBOTICS_STARDRIVE)
    PROD("8-Up (Rev A)", RAM, MICROBOTICS_8_UP_A)
    PROD("8-Up (Rev Z)", RAM, MICROBOTICS_8_UP_Z)
    PROD("Delta", RAM, MICROBOTICS_DELTA_RAM)
    PROD("8-Star", RAM, MICROBOTICS_8_STAR_RAM)
    PROD("8-Star", MISC, MICROBOTICS_8_STAR)
    PROD("VXL RAM*32", RAM, MICROBOTICS_VXL_RAM_32)
    PROD("VXL-30", TURBO, MICROBOTICS_VXL_68030)
    PROD("Delta", MISC, MICROBOTICS_DELTA)
    PROD("MBX 1200/1200z", RAM, MICROBOTICS_MBX_1200_1200Z_RAM)
    PROD("Hardframe 2000", SCSI, MICROBOTICS_HARDFRAME_2000_1)
    PROD("Hardframe 2000", SCSI, MICROBOTICS_HARDFRAME_2000_2)
    PROD("MBX 1200/1200z", MISC, MICROBOTICS_MBX_1200_1200Z)
END

BEGIN_PROD(ACCESS_ASSOCIATES_ALEGRA)
END

BEGIN_PROD(EXPANSION_TECHNOLOGIES)
END

BEGIN_PROD(ASDG)
    PROD(NULL, RAM, ASDG_MEMORY_1)
    PROD(NULL, RAM, ASDG_MEMORY_2)
    PROD("EB-920 Lan Rover", ETHERNET, ASDG_EB920_LAN_ROVER)
    PROD("GPIB/Dual IEEE-488/Twin-X", MULTIIO, ASDG_GPIB_DUALIEEE488_TWIN_X)
END

BEGIN_PROD(IMTRONICS_1)
    PROD("Hurricane 2800", TURBO_RAM, IMTRONICS_HURRICANE_2800_1)
    PROD("Hurricane 2800", TURBO_RAM, IMTRONICS_HURRICANE_2800_2)
END

BEGIN_PROD(CBM_UNIVERSITY_OF_LOWELL)
    PROD("A2410 HiRes", GFX, CBM_A2410)
END

BEGIN_PROD(AMERISTAR)
    PROD("A2065", ETHERNET, AMERISTAR_A2065)
    PROD("A560", ARCNET, AMERISTAR_A560)
    PROD("A4066", ETHERNET, AMERISTAR_A4066)
END

BEGIN_PROD(SUPRA)
    PROD("SupraDrive 4x4", SCSI, SUPRA_SUPRADRIVE_4x4)
    PROD("1000", RAM, SUPRA_1000_RAM)
    PROD("2000 DMA", SCSI, SUPRA_2000_DMA)
    PROD("500", SCSI_RAM, SUPRA_500)
    PROD("500", SCSI, SUPRA_500_SCSI)
    PROD("500XP/2000", RAM, SUPRA_500XP_2000_RAM)
    PROD("500RX/2000", RAM, SUPRA_500RX_2000_RAM)
    PROD("2400zi", MODEM, SUPRA_2400ZI)
    PROD("500XP/SupraDrive WordSync", SCSI, SUPRA_500XP_SUPRADRIVE_WORDSYNC)
    PROD("SupraDrive WordSync II", SCSI, SUPRA_SUPRADRIVE_WORDSYNC_II)
    PROD("2400zi+", MODEM, SUPRA_2400ZIPLUS)
END

BEGIN_PROD(COMPUTER_SYSTEMS_ASSOCIATES)
    PROD("Magnum 40", TURBO_SCSI, CSA_MAGNUM)
    PROD("12 Gauge", SCSI, CSA_12_GAUGE)
END

BEGIN_PROD(MARC_MICHAEL_GROTH)
END

BEGIN_PROD(M_TECH)
    PROD("AT500", RAM, MTEC_AT500_1)
END

BEGIN_PROD(GREAT_VALLEY_PRODUCTS_1)
    PROD("Impact Series I", SCSI_RAM, GVP_IMPACT_SERIES_I)
END

BEGIN_PROD(BYTEBOX)
    PROD("A500", UNKNOWN, BYTEBOX_A500)
END

BEGIN_PROD(DKB_POWER_COMPUTING)
    PROD("SecureKey", UNKNOWN, DKB_POWER_COMPUTING_SECUREKEY)
    PROD("DKM 3128", RAM, DKB_POWER_COMPUTING_DKM_3128)
    PROD("Rapid Fire", SCSI, DKB_POWER_COMPUTING_RAPID_FIRE)
    PROD("DKM 1202", FPU_RAM, DKB_POWER_COMPUTING_DKM_1202)
    PROD("Cobra/Viper II 68EC030", TURBO, DKB_POWER_COMPUTING_COBRA_VIPER_II_68EC030)
    PROD("WildFire 060", TURBO, DKB_POWER_COMPUTING_WILDFIRE_060_1)
    PROD("WildFire 060", TURBO, DKB_POWER_COMPUTING_WILDFIRE_060_2)
END

BEGIN_PROD(GREAT_VALLEY_PRODUCTS_2)
    PROD("Impact Series I (4K)", SCSI, GVP_IMPACT_SERIES_I_4K)
    PROD("Impact Series I (16K/2)", SCSI, GVP_IMPACT_SERIES_I_16K_2)
    PROD("Impact Series I (16K/2)", SCSI, GVP_IMPACT_SERIES_I_16K_3)
    PROD("Impact 3001", IDE, GVP_IMPACT_3001_IDE_1)
    PROD("Impact 3001", RAM, GVP_IMPACT_3001_RAM)
    PROD("Impact Series II", RAM, GVP_IMPACT_SERIES_II_RAM_1)
/*  PROD(NULL, UNKNOWN, GVP_EPC_BASE) */
    PROD("Impact 3001", IDE, GVP_IMPACT_3001_IDE_2)
/*  PROD("A2000 030", TURBO, GVP_A2000_030) */
/*  PROD("GForce 040", TURBO_SCSI, GFORCE_040_SCSI_2) */
    PROD("GForce 040/060", TURBO_SCSI, GVP_GFORCE_040_060)
    PROD("Impact Vision 24", GFX, GVP_IMPACT_VISION_24)
    PROD("GForce 040", TURBO, GVP_GFORCE_040_2)
END

BEGIN_GVP_PROD					/* ZORRO_PROD_GVP_EPC_BASE */
    GVP_PROD("GForce 040", TURBO, GVP_GFORCE_040_1)
    GVP_PROD("GForce 040", TURBO_SCSI, GVP_GFORCE_040_SCSI_1)
    GVP_PROD("A1291", SCSI, GVP_A1291)
    GVP_PROD("Combo 030 R4", TURBO, GVP_COMBO_030_R4)
    GVP_PROD("Combo 030 R4", TURBO_SCSI, GVP_COMBO_030_R4_SCSI)
    GVP_PROD("Phone Pak", UNKNOWN, GVP_PHONEPAK)
    GVP_PROD("IO-Extender", MULTIIO, GVP_IO_EXTENDER)
    GVP_PROD("GForce 030", TURBO, GVP_GFORCE_030)
    GVP_PROD("GForce 030", TURBO_SCSI, GVP_GFORCE_030_SCSI)
    GVP_PROD("A530", TURBO, GVP_A530)
    GVP_PROD("A530", TURBO_SCSI, GVP_A530_SCSI)
    GVP_PROD("Combo 030 R3", TURBO, GVP_COMBO_030_R3)
    GVP_PROD("Combo 030 R3", TURBO_SCSI, GVP_COMBO_030_R3_SCSI)
    GVP_PROD("Series-II", SCSI, GVP_SERIES_II)
END

BEGIN_PROD(CALIFORNIA_ACCESS_SYNERGY)
    PROD("Malibu", SCSI, CALIFORNIA_ACCESS_SYNERGY_MALIBU)
END

BEGIN_PROD(XETEC)
    PROD("FastCard", SCSI, XETEC_FASTCARD)
    PROD("FastCard", RAM, XETEC_FASTCARD_RAM)
    PROD("FastCard Plus", SCSI, XETEC_FASTCARD_PLUS)
END

BEGIN_PROD(PROGRESSIVE_PERIPHERALS_AND_SYSTEMS)
    PROD("Mercury", TURBO, PPS_MERCURY)
    PROD("A3000 68040", TURBO, PPS_A3000_68040)
    PROD("A2000 68040", TURBO, PPS_A2000_68040)
    PROD("Zeus", TURBO_SCSI_RAM, PPS_ZEUS)
    PROD("A500 68040", TURBO, PPS_A500_68040)
END

BEGIN_PROD(XEBEC)
END

BEGIN_PROD(SPIRIT_TECHNOLOGY)
    PROD("Insider IN1000", RAM, SPIRIT_TECHNOLOGY_INSIDER_IN1000)
    PROD("Insider IN500", RAM, SPIRIT_TECHNOLOGY_INSIDER_IN500)
    PROD("SIN500", RAM, SPIRIT_TECHNOLOGY_SIN500)
    PROD("HDA 506", HD, SPIRIT_TECHNOLOGY_HDA_506)
    PROD("AX-S", MISC, SPIRIT_TECHNOLOGY_AX_S)
    PROD("OctaByte", RAM, SPIRIT_TECHNOLOGY_OCTABYTE)
    PROD("Inmate", SCSI_RAM, SPIRIT_TECHNOLOGY_INMATE)
END

BEGIN_PROD(SPIRIT_TECHNOLOGY_2)
END

BEGIN_PROD(BSC_ALFADATA_1)
    PROD("ALF 3", SCSI, BSC_ALF_3_1)
END

BEGIN_PROD(BSC_ALFADATA_2)
    PROD("ALF 2", SCSI, BSC_ALF_2_1)
    PROD("ALF 2", SCSI, BSC_ALF_2_2)
    PROD("ALF 3", SCSI, BSC_ALF_3_2)
END

BEGIN_PROD(CARDCO_2)
    PROD("Kronos", SCSI, CARDCO_KRONOS_2000_2)
    PROD("A1000", SCSI, CARDCO_A1000_2)
END

BEGIN_PROD(JOCHHEIM)
    PROD(NULL, RAM, JOCHHEIM_RAM)
END

BEGIN_PROD(CHECKPOINT_TECHNOLOGIES)
    PROD("Serial Solution", SERIAL, CHECKPOINT_TECHNOLOGIES_SERIAL_SOLUTION)
END

BEGIN_PROD(EDOTRONIK)
    PROD("IEEE-488 Interface Board", UNKNOWN, EDOTRONIK_IEEE_488)
    PROD("CBM-8032 Board", UNKNOWN, EDOTRONIK_8032)
    PROD(NULL, SERIAL, EDOTRONIK_MULTISERIAL)
    PROD("24Bit Realtime Video Digitizer", UNKNOWN, EDOTRONIK_VIDEODIGITIZER)
    PROD("32Bit Parallel I/O Interface", UNKNOWN, EDOTRONIK_PARALLEL_IO)
    PROD("PIC Prototyping Board", UNKNOWN, EDOTRONIK_PIC_PROTOYPING)
    PROD("16 Channel ADC Interface", UNKNOWN, EDOTRONIK_ADC)
    PROD("VME-Bus Controller", UNKNOWN, EDOTRONIK_VME)
    PROD("DSP96000 Realtime Data Acquisition", DSP, EDOTRONIK_DSP96000)
END

BEGIN_PROD(NES_INC)
    PROD(NULL, RAM, NES_INC_RAM)
END

BEGIN_PROD(ICD)
    PROD("Advantage 2000", SCSI, ICD_ADVANTAGE_2000_SCSI)
    PROD("Advantage", IDE, ICD_ADVANTAGE_2000_SCSI)
    PROD("Advantage 2080", RAM, ICD_ADVANTAGE_2080_RAM)
END

BEGIN_PROD(KUPKE_2)
    PROD("Omti", HD, KUPKE_OMTI)
    PROD("Golem SCSI-II", SCSI, KUPKE_SCSI_II)
    PROD("Golem Box", UNKNOWN, KUPKE_GOLEM_BOX)
    PROD("030/882", TURBO, KUPKE_030_882)
    PROD("Golem", SCSI, KUPKE_SCSI_AT)
END

BEGIN_PROD(GREAT_VALLEY_PRODUCTS_3)
    PROD("A2000-RAM8/2", MISC, GVP_A2000_RAM8)
    PROD("Impact Series II", RAM, GVP_IMPACT_SERIES_II_RAM_2)
END

BEGIN_PROD(INTERWORKS_NETWORK)
END

BEGIN_PROD(HARDITAL_SYNTHESIS)
    PROD("TQM 68030+68882", TURBO, HARDITAL_SYNTHESIS_TQM_68030_68882)
END

BEGIN_PROD(APPLIED_ENGINEERING)
    PROD("DL2000", MODEM, APPLIED_ENGINEERING_DL2000)
    PROD("RAM Works", RAM, APPLIED_ENGINEERING_RAM_WORKS)
END

BEGIN_PROD(BSC_ALFADATA_3)
    PROD("Oktagon 2008", SCSI, BSC_OKTAGON_2008)
    PROD("Tandem AT-2008/508", IDE, BSC_TANDEM_AT_2008_508)
    PROD("Alpha RAM 1200", RAM, BSC_ALFA_RAM_1200)
    PROD("Oktagon 2008", RAM, BSC_OKTAGON_2008_RAM)
    PROD("MultiFace I", MULTIIO, BSC_MULTIFACE_I)
    PROD("MultiFace II", MULTIIO, BSC_MULTIFACE_II)
    PROD("MultiFace III", MULTIIO, BSC_MULTIFACE_III)
    PROD("Framebuffer", MISC, BSC_FRAMEBUFFER)
    PROD("Graffiti", GFXRAM, BSC_GRAFFITI_RAM)
    PROD("Graffiti", GFX, BSC_GRAFFITI_REG)
    PROD("ISDN MasterCard", ISDN, BSC_ISDN_MASTERCARD)
    PROD("ISDN MasterCard II", ISDN, BSC_ISDN_MASTERCARD_II)
END

BEGIN_PROD(PHOENIX)
    PROD("ST506", HD, PHOENIX_ST506)
    PROD(NULL, SCSI, PHOENIX_SCSI)
    PROD(NULL, RAM, PHOENIX_RAM)
END

BEGIN_PROD(ADVANCED_STORAGE_SYSTEMS)
    PROD("Nexus", SCSI, ADVANCED_STORAGE_SYSTEMS_NEXUS)
    PROD("Nexus", RAM, ADVANCED_STORAGE_SYSTEMS_NEXUS_RAM)
END

BEGIN_PROD(IMPULSE)
    PROD("FireCracker 24", GFX, IMPULSE_FIRECRACKER_24)
END

BEGIN_PROD(IVS)
    PROD("GrandSlam PIC 2", RAM, IVS_GRANDSLAM_PIC_2)
    PROD("GrandSlam PIC 1", RAM, IVS_GRANDSLAM_PIC_1)
    PROD("OverDrive", HD, IVS_OVERDRIVE)
    PROD("TrumpCard Classic", SCSI, IVS_TRUMPCARD_CLASSIC)
    PROD("TrumpCard Pro/GrandSlam", SCSI, IVS_TRUMPCARD_PRO_GRANDSLAM)
    PROD("Meta-4", RAM, IVS_META_4)
    PROD("Wavetools", AUDIO, IVS_WAVETOOLS)
    PROD("Vector", SCSI, IVS_VECTOR_1)
    PROD("Vector", SCSI, IVS_VECTOR_2)
END

BEGIN_PROD(VECTOR_1)
    PROD("Connection", MULTIIO, VECTOR_CONNECTION_1)
END

BEGIN_PROD(XPERT_PRODEV)
    PROD("Visiona", GFXRAM, XPERT_PRODEV_VISIONA_RAM)
    PROD("Visiona", GFX, XPERT_PRODEV_VISIONA_REG)
    PROD("Merlin", GFXRAM, XPERT_PRODEV_MERLIN_RAM)
    PROD("Merlin", GFX, XPERT_PRODEV_MERLIN_REG_1)
    PROD("Merlin", GFX, XPERT_PRODEV_MERLIN_REG_2)
END

BEGIN_PROD(HYDRA_SYSTEMS)
    PROD("Amiganet", ETHERNET, HYDRA_SYSTEMS_AMIGANET)
END

BEGIN_PROD(SUNRIZE_INDUSTRIES)
    PROD("AD1012", AUDIO, SUNRIZE_INDUSTRIES_AD1012)
    PROD("AD516", AUDIO, SUNRIZE_INDUSTRIES_AD516)
    PROD("DD512", AUDIO, SUNRIZE_INDUSTRIES_DD512)
END

BEGIN_PROD(TRICERATOPS)
    PROD(NULL, MULTIIO, TRICERATOPS_MULTI_IO)
END

BEGIN_PROD(APPLIED_MAGIC)
    PROD("DMI Resolver", GFX, APPLIED_MAGIC_DMI_RESOLVER)
    PROD("Digital Broadcaster", VIDEO, APPLIED_MAGIC_DIGITAL_BROADCASTER)
END

BEGIN_PROD(GFX_BASE)
    PROD("GDA-1 VRAM", GFX, GFX_BASE_GDA_1_VRAM)
    PROD("GDA-1", GFX, GFX_BASE_GDA_1)
END

BEGIN_PROD(ROCTEC)
    PROD("RH 800C", HD, ROCTEC_RH_800C)
    PROD("RH 800C", RAM, ROCTEC_RH_800C_RAM)
END

BEGIN_PROD(KATO)
    PROD("Melody MPEG", AUDIO, KATO_MELODY)
    PROD("Rainbow II", GFX, HELFRICH_RAINBOW_II)	/* ID clash!! */
    PROD("Rainbow III", GFX, HELFRICH_RAINBOW_III)	/* ID clash!! */
END

BEGIN_PROD(ATLANTIS)
END

BEGIN_PROD(PROTAR)
END

BEGIN_PROD(ACS)
END

BEGIN_PROD(SOFTWARE_RESULTS_ENTERPRISES)
    PROD("Golden Gate 2 Bus+", BRIDGE, SOFTWARE_RESULTS_ENTERPRISES_GOLDEN_GATE_2_BUS_PLUS)
END

BEGIN_PROD(MASOBOSHI)
    PROD("MasterCard SC201", RAM, MASOBOSHI_MASTER_CARD_SC201)
    PROD("MasterCard MC702", SCSI_IDE, MASOBOSHI_MASTER_CARD_MC702)
    PROD("MVD 819", UNKNOWN, MASOBOSHI_MVD_819)
END

BEGIN_PROD(MAINHATTAN_DATA)
    PROD(NULL, IDE, MAINHATTAN_DATA_IDE)
END

BEGIN_PROD(VILLAGE_TRONIC)
    PROD("Domino", GFXRAM, VILLAGE_TRONIC_DOMINO_RAM)
    PROD("Domino", GFX, VILLAGE_TRONIC_DOMINO_REG)
    PROD("Domino 16M Prototype", GFX, VILLAGE_TRONIC_DOMINO_16M_PROTOTYPE)
    PROD("Picasso II/II+", GFXRAM, VILLAGE_TRONIC_PICASSO_II_II_PLUS_RAM)
    PROD("Picasso II/II+", GFX, VILLAGE_TRONIC_PICASSO_II_II_PLUS_REG)
    PROD("Picasso II/II+ (Segmented Mode)", GFX, VILLAGE_TRONIC_PICASSO_II_II_PLUS_SEGMENTED_MODE)
    PROD("Picasso IV Z2", GFXRAM, VILLAGE_TRONIC_PICASSO_IV_Z2_MEM1)
    PROD("Picasso IV Z2", GFXRAM, VILLAGE_TRONIC_PICASSO_IV_Z2_MEM2)
    PROD("Picasso IV Z2", GFX, VILLAGE_TRONIC_PICASSO_IV_Z2_REG)
    PROD("Picasso IV Z3", GFX, VILLAGE_TRONIC_PICASSO_IV_Z3)
    PROD("Ariadne", ETHERNET_PARALLEL, VILLAGE_TRONIC_ARIADNE)
END

BEGIN_PROD(UTILITIES_UNLIMITED)
    PROD("Emplant Deluxe", MACEMU, UTILITIES_UNLIMITED_EMPLANT_DELUXE)
    PROD("Emplant Deluxe", MACEMU, UTILITIES_UNLIMITED_EMPLANT_DELUXE2)
END

BEGIN_PROD(AMITRIX)
    PROD(NULL, MULTIIO, AMITRIX_MULTI_IO)
    PROD("CD-RAM", RAM, AMITRIX_CD_RAM)
END

BEGIN_PROD(ARMAX)
    PROD("OmniBus", GFX, ARMAX_OMNIBUS)
END

BEGIN_PROD(ZEUS)
    PROD("Spider", VIDEO, ZEUS_SPIDER)
END

BEGIN_PROD(NEWTEK)
    PROD("VideoToaster", VIDEO, NEWTEK_VIDEOTOASTER)
END

BEGIN_PROD(M_TECH_GERMANY)
    PROD("AT500", IDE, MTEC_AT500_2)
    PROD("68030", TURBO, MTEC_68030)
    PROD("68020i", TURBO, MTEC_68020I)
    PROD("A1200 T68030 RTC", TURBO, MTEC_A1200_T68030_RTC)
    PROD("Viper Mk V/E-Matrix 530", TURBO_RAM, MTEC_VIPER_MK_V_E_MATRIX_530)
    PROD("8MB", RAM, MTEC_8_MB_RAM)
    PROD("Viper Mk V/E-Matrix 530 SCSI/IDE", SCSI_IDE, MTEC_VIPER_MK_V_E_MATRIX_530_SCSI_IDE)
END

BEGIN_PROD(GREAT_VALLEY_PRODUCTS_4)
    PROD("EGS 28/24 Spectrum", GFX, GVP_EGS_28_24_SPECTRUM_REG)
    PROD("EGS 28/24 Spectrum", GFXRAM, GVP_EGS_28_24_SPECTRUM_RAM)
END

BEGIN_PROD(APOLLO_1)
    PROD("A1200", FPU_RAM, APOLLO_A1200)
END

BEGIN_PROD(HELFRICH_2)
    PROD("Piccolo", GFXRAM, HELFRICH_PICCOLO_RAM)
    PROD("Piccolo", GFX, HELFRICH_PICCOLO_REG)
    PROD("PeggyPlus MPEG", VIDEO, HELFRICH_PEGGY_PLUS_MPEG)
    PROD("VideoCruncher", VIDEO, HELFRICH_VIDEOCRUNCHER)
    PROD("Piccolo SD64", GFXRAM, HELFRICH_SD64_RAM)
    PROD("Piccolo SD64", GFX, HELFRICH_SD64_REG)
END

BEGIN_PROD(MACROSYSTEMS_USA)
    PROD("Warp Engine 40xx", TURBO_SCSI_RAM, MACROSYSTEMS_WARP_ENGINE_40xx)
END

BEGIN_PROD(ELBOX_COMPUTER)
    PROD("1200/4", RAM, ELBOX_COMPUTER_1200_4)
END

BEGIN_PROD(HARMS_PROFESSIONAL)
    PROD("030 Plus", TURBO, HARMS_PROFESSIONAL_030_PLUS)
    PROD("3500 Professional", TURBO_RAM, HARMS_PROFESSIONAL_3500)
END

BEGIN_PROD(MICRONIK)
    PROD("RCA 120", RAM, MICRONIK_RCA_120)
END

BEGIN_PROD(MICRONIK2)
    PROD("Z3i A1200 Zorro III + SCSI", SCSI, MICRONIK2_Z3I)
END

BEGIN_PROD(MEGAMICRO)
    PROD("SCRAM 500", SCSI, MEGAMICRO_SCRAM_500)
    PROD("SCRAM 500", RAM, MEGAMICRO_SCRAM_500_RAM)
END

BEGIN_PROD(IMTRONICS_2)
    PROD("Hurricane 2800", TURBO_RAM, IMTRONICS_HURRICANE_2800_3)
    PROD("Hurricane 2800", TURBO_RAM, IMTRONICS_HURRICANE_2800_4)
END

BEGIN_PROD(INDIVIDUAL_COMPUTERS)
    PROD("Buddha", IDE, INDIVIDUAL_COMPUTERS_BUDDHA)
    PROD("Catweasel", IDE_FLOPPY, INDIVIDUAL_COMPUTERS_CATWEASEL)
END

BEGIN_PROD(KUPKE_3)
    PROD("Golem HD 3000", HD, KUPKE_GOLEM_HD_3000)
END

BEGIN_PROD(ITH)
    PROD("ISDN-Master II", ISDN, ITH_ISDN_MASTER_II)
END

BEGIN_PROD(VMC)
    PROD("ISDN Blaster Z2", ISDN, VMC_ISDN_BLASTER_Z2)
    PROD("HyperCom 4", MULTIIO, VMC_HYPERCOM_4)
END

BEGIN_PROD(INFORMATION)
    PROD("ISDN Engine I", ISDN, INFORMATION_ISDN_ENGINE_I)
END

BEGIN_PROD(VORTEX)
    PROD("Golden Gate 80386SX", BRIDGE, VORTEX_GOLDEN_GATE_80386SX)
    PROD("Golden Gate", RAM, VORTEX_GOLDEN_GATE_RAM)
    PROD("Golden Gate 80486", BRIDGE, VORTEX_GOLDEN_GATE_80486)
END

BEGIN_PROD(EXPANSION_SYSTEMS)
    PROD("DataFlyer 4000SX", SCSI, EXPANSION_SYSTEMS_DATAFLYER_4000SX)
    PROD("DataFlyer 4000SX", RAM, EXPANSION_SYSTEMS_DATAFLYER_4000SX_RAM)
END

BEGIN_PROD(READYSOFT)
    PROD("AMax II/IV", MACEMU, READYSOFT_AMAX_II_IV)
END

BEGIN_PROD(PHASE5)
    PROD("Blizzard", RAM, PHASE5_BLIZZARD_RAM)
    PROD("Blizzard", TURBO, PHASE5_BLIZZARD)
    PROD("Blizzard 1220-IV", TURBO, PHASE5_BLIZZARD_1220_IV)
    PROD("FastLane Z3", RAM, PHASE5_FASTLANE_Z3_RAM)
    PROD("Blizzard 1230-II/Fastlane Z3/CyberSCSI/CyberStorm060", TURBO_SCSI, PHASE5_BLIZZARD_1230_II_FASTLANE_Z3_CYBERSCSI_CYBERSTORM060)
    PROD("Blizzard 1220/CyberStorm", TURBO_SCSI, PHASE5_BLIZZARD_1220_CYBERSTORM)
    PROD("Blizzard 1230", TURBO, PHASE5_BLIZZARD_1230)
    PROD("Blizzard 1230-IV/1260", TURBO, PHASE5_BLIZZARD_1230_IV_1260)
    PROD("Blizzard 2060", TURBO, PHASE5_BLIZZARD_2060)
    PROD("CyberStorm Mk II", FLASHROM, PHASE5_CYBERSTORM_MK_II)
    PROD("CyberVision64", GFX, PHASE5_CYBERVISION64)
    PROD("CyberVision64-3D Prototype", GFX, PHASE5_CYBERVISION64_3D_PROTOTYPE)
    PROD("CyberVision64-3D", GFX, PHASE5_CYBERVISION64_3D)
    PROD("CyberStorm Mk III", TURBO_SCSI, PHASE5_CYBERSTORM_MK_III)
    PROD("Blizzard 603e+", TURBO_SCSI, PHASE5_BLIZZARD_603E_PLUS)
END

BEGIN_PROD(DPS)
    PROD("Personal Animation Recorder", VIDEO, DPS_PERSONAL_ANIMATION_RECORDER)
END

BEGIN_PROD(APOLLO_2)
    PROD("A620 68020", TURBO, APOLLO_A620_68020_1)
    PROD("A620 68020", TURBO, APOLLO_A620_68020_2)
END

BEGIN_PROD(APOLLO_3)
    PROD("AT-Apollo", UNKNOWN, APOLLO_AT_APOLLO)
    PROD("1230/1240/1260/2030/4040/4060", TURBO, APOLLO_1230_1240_1260_2030_4040_4060)
END

BEGIN_PROD(PETSOFF_LP)
    PROD("Delfina", AUDIO, PETSOFF_LP_DELFINA)
    PROD("Delfina Lite", AUDIO, PETSOFF_LP_DELFINA_LITE)
END

BEGIN_PROD(UWE_GERLACH)
    PROD("RAM/ROM", MISC, UWE_GERLACH_RAM_ROM)
END

BEGIN_PROD(ACT)
    PROD("Prelude", AUDIO, ACT_PRELUDE)
END

BEGIN_PROD(MACROSYSTEMS_GERMANY)
    PROD("Maestro", AUDIO, MACROSYSTEMS_MAESTRO)
    PROD("VLab", VIDEO, MACROSYSTEMS_VLAB)
    PROD("Maestro Pro", AUDIO, MACROSYSTEMS_MAESTRO_PRO)
    PROD("Retina", GFX, MACROSYSTEMS_RETINA)
    PROD("MultiEvolution", SCSI, MACROSYSTEMS_MULTI_EVOLUTION)
    PROD("Toccata", AUDIO, MACROSYSTEMS_TOCCATA)
    PROD("Retina Z3", GFX, MACROSYSTEMS_RETINA_Z3)
    PROD("VLab Motion", VIDEO, MACROSYSTEMS_VLAB_MOTION)
    PROD("Altais", GFX, MACROSYSTEMS_ALTAIS)
    PROD("Falcon '040", TURBO, MACROSYSTEMS_FALCON_040)
END

BEGIN_PROD(COMBITEC)
END

BEGIN_PROD(SKI_PERIPHERALS)
    PROD("MAST Fireball", SCSI, SKI_PERIPHERALS_MAST_FIREBALL)
    PROD("SCSI/Dual Serial", SCSI_SERIAL, SKI_PERIPHERALS_SCSI_DUAL_SERIAL)
END

BEGIN_PROD(REIS_WARE_2)
    PROD("Scan King", SCANNER, REIS_WARE_SCAN_KING)
END

BEGIN_PROD(CAMERON)
    PROD("Personal A4", SCANNER, CAMERON_PERSONAL_A4)
END

BEGIN_PROD(REIS_WARE)
    PROD("Handyscanner", SCANNER, REIS_WARE_HANDYSCANNER)
END

BEGIN_PROD(PHOENIX_2)
    PROD("ST506", HD, PHOENIX_ST506_2)
    PROD(NULL, SCSI, PHOENIX_SCSI_2)
    PROD(NULL, RAM, PHOENIX_RAM_2)
END

BEGIN_PROD(COMBITEC_2)
    PROD(NULL, HD, COMBITEC_HD)
    PROD("SRAM", RAM, COMBITEC_SRAM)
END

BEGIN_PROD(HACKER)	/* Unused */
END


BEGIN_MANUF
    MANUF("Pacific Peripherals", PACIFIC_PERIPHERALS)
    MANUF("MacroSystems USA", MACROSYSTEMS_USA_2)
    MANUF("Kupke", KUPKE_1)
    MANUF("Memphis", MEMPHIS)
    MANUF("3-State", 3_STATE)
    MANUF("Commodore Braunschweig", COMMODORE_BRAUNSCHWEIG)
    MANUF("Commodore West Chester", COMMODORE_WEST_CHESTER_1)
    MANUF("Commodore West Chester", COMMODORE_WEST_CHESTER_2)
    MANUF("Progressive Peripherals & Systems", PROGRESSIVE_PERIPHERALS_AND_SYSTEMS_2)
    MANUF("Kolff Computer Supplies", KOLFF_COMPUTER_SUPPLIES)
    MANUF("Cardco Ltd.", CARDCO_1)
    MANUF("A-Squared", A_SQUARED)
    MANUF("Comspec Communications", COMSPEC_COMMUNICATIONS)
    MANUF("Anakin Research", ANAKIN_RESEARCH)
    MANUF("Microbotics", MICROBOTICS)
    MANUF("Access Associates Alegra", ACCESS_ASSOCIATES_ALEGRA)
    MANUF("Expansion Technologies (Pacific Cypress)", EXPANSION_TECHNOLOGIES)
    MANUF("ASDG", ASDG)
    MANUF("Ronin/Imtronics", IMTRONICS_1)
    MANUF("Commodore/University of Lowell", CBM_UNIVERSITY_OF_LOWELL)
    MANUF("Ameristar", AMERISTAR)
    MANUF("Supra", SUPRA)
    MANUF("Computer Systems Assosiates", COMPUTER_SYSTEMS_ASSOCIATES)
    MANUF("Marc Michael Groth", MARC_MICHAEL_GROTH)
    MANUF("M-Tech", M_TECH)
    MANUF("Great Valley Products", GREAT_VALLEY_PRODUCTS_1)
    MANUF("ByteBox", BYTEBOX)
    MANUF("DKB/Power Computing", DKB_POWER_COMPUTING)
    MANUF("Great Valley Products", GREAT_VALLEY_PRODUCTS_2)
    MANUF("California Access (Synergy)", CALIFORNIA_ACCESS_SYNERGY)
    MANUF("Xetec", XETEC)
    MANUF("Progressive Peripherals & Systems", PROGRESSIVE_PERIPHERALS_AND_SYSTEMS)
    MANUF("Xebec", XEBEC)
    MANUF("Spirit Technology", SPIRIT_TECHNOLOGY)
    MANUF("Spirit Technology", SPIRIT_TECHNOLOGY_2)
    MANUF("BSC/Alfadata", BSC_ALFADATA_1)
    MANUF("BSC/Alfadata", BSC_ALFADATA_2)
    MANUF("Cardco Ltd.", CARDCO_2)
    MANUF("Jochheim", JOCHHEIM)
    MANUF("Checkpoint Technologies", CHECKPOINT_TECHNOLOGIES)
    MANUF("Edotronik", EDOTRONIK)
    MANUF("NES Inc.", NES_INC)
    MANUF("ICD", ICD)
    MANUF("Kupke", KUPKE_2)
    MANUF("Great Valley Products", GREAT_VALLEY_PRODUCTS_3)
    MANUF("Interworks Network", INTERWORKS_NETWORK)
    MANUF("Hardital Synthesis", HARDITAL_SYNTHESIS)
    MANUF("Applied Engineering", APPLIED_ENGINEERING)
    MANUF("BSC/Alfadata", BSC_ALFADATA_3)
    MANUF("Phoenix", PHOENIX)
    MANUF("Advanced Storage Systems", ADVANCED_STORAGE_SYSTEMS)
    MANUF("Impulse", IMPULSE)
    MANUF("IVS", IVS)
    MANUF("Vector", VECTOR_1)
    MANUF("XPert ProDev", XPERT_PRODEV)
    MANUF("Hydra Systems", HYDRA_SYSTEMS)
    MANUF("Sunrize Industries", SUNRIZE_INDUSTRIES)
    MANUF("Triceratops", TRICERATOPS)
    MANUF("Applied Magic Inc.", APPLIED_MAGIC)
    MANUF("GFX-Base", GFX_BASE)
    MANUF("RocTec", ROCTEC)
    MANUF("Kato", KATO)
    MANUF("Atlantis", ATLANTIS)
    MANUF("Protar", PROTAR)
    MANUF("ACS", ACS)
    MANUF("Software Results Enterprises", SOFTWARE_RESULTS_ENTERPRISES)
    MANUF("Masoboshi", MASOBOSHI)
    MANUF("Mainhattan-Data (A-Team)", MAINHATTAN_DATA)
    MANUF("Village Tronic", VILLAGE_TRONIC)
    MANUF("Utilities Unlimited", UTILITIES_UNLIMITED)
    MANUF("Amitrix", AMITRIX)
    MANUF("ArMax", ARMAX)
    MANUF("ZEUS Electronic Development", ZEUS)
    MANUF("NewTek", NEWTEK)
    MANUF("M-Tech Germany", M_TECH_GERMANY)
    MANUF("Great Valley Products", GREAT_VALLEY_PRODUCTS_4)
    MANUF("Apollo", APOLLO_1)
    MANUF("Ingenieurbüro Helfrich", HELFRICH_2)
    MANUF("MacroSystems USA", MACROSYSTEMS_USA)
    MANUF("ElBox Computer", ELBOX_COMPUTER)
    MANUF("Harms Professional", HARMS_PROFESSIONAL)
    MANUF("Micronik", MICRONIK)
    MANUF("Micronik", MICRONIK2)
    MANUF("MegaMicro", MEGAMICRO)
    MANUF("Ronin/Imtronics", IMTRONICS_2)
    MANUF("Individual Computers", INDIVIDUAL_COMPUTERS)
    MANUF("Kupke", KUPKE_3)
    MANUF("ITH", ITH)
    MANUF("VMC", VMC)
    MANUF("Information", INFORMATION)
    MANUF("Vortex", VORTEX)
    MANUF("Expansion Systems", EXPANSION_SYSTEMS)
    MANUF("ReadySoft", READYSOFT)
    MANUF("Phase 5", PHASE5)
    MANUF("DPS", DPS)
    MANUF("Apollo", APOLLO_2)
    MANUF("Apollo", APOLLO_3)
    MANUF("Petsoff LP", PETSOFF_LP)
    MANUF("Uwe Gerlach", UWE_GERLACH)
    MANUF("ACT", ACT)
    MANUF("MacroSystems Germany", MACROSYSTEMS_GERMANY)
    MANUF("Combitec", COMBITEC)
    MANUF("SKI Peripherals", SKI_PERIPHERALS)
    MANUF("Reis-Ware", REIS_WARE_2)
    MANUF("Cameron", CAMERON)
    MANUF("Reis-Ware", REIS_WARE)
    MANUF("Hacker Test Board", HACKER)	/* Unused */
    MANUF("Phoenix", PHOENIX_2)
    MANUF("Combitec", COMBITEC_2)
END

#define NUM_MANUF		(ARRAYSIZE(Manufacturers))
#define NUM_GVP_PROD		(ARRAYSIZE(Ext_Prod_GVP))


    /*
     *  Zorro product classes
     *
     *  Make sure to keep these in sync with include/linux/zorro.h!
     */

static const char *classnames[] = {
    NULL,				    /* ZORRO_CLASS_UNKNOWN */
    "ArcNet Card",			    /* ZORRO_CLASS_ARCNET */
    "Audio Board",			    /* ZORRO_CLASS_AUDIO */
    "ISA Bus Bridge",			    /* ZORRO_CLASS_BRIDGE */
    "DSP Board",			    /* ZORRO_CLASS_DSP */
    "Ethernet Card",			    /* ZORRO_CLASS_ETHERNET */
    "Ethernet Card and Parallel Ports",	    /* ZORRO_CLASS_ETHERNET_PARALLEL */
    "Flash ROM",			    /* ZORRO_CLASS_FLASHROM */
    "FPU and RAM Expansion",		    /* ZORRO_CLASS_FPU_RAM */
    "Graphics Board",			    /* ZORRO_CLASS_GFX */
    "Graphics Board (RAM)",		    /* ZORRO_CLASS_GFXRAM */
    "HD Controller",			    /* ZORRO_CLASS_HD */
    "HD Controller and RAM Expansion",	    /* ZORRO_CLASS_HD_RAM */
    "IDE Interface",			    /* ZORRO_CLASS_IDE */
    "IDE Interface and RAM Expansion",	    /* ZORRO_CLASS_IDE_RAM */
    "IDE Interface and Floppy Controller",  /* ZORRO_CLASS_IDE_FLOPPY */
    "ISDN Interface",			    /* ZORRO_CLASS_ISDN */
    "Macintosh Emulator",		    /* ZORRO_CLASS_MACEMU */
    "Miscellaneous Expansion Card",	    /* ZORRO_CLASS_MISC */
    "Modem",				    /* ZORRO_CLASS_MODEM */
    "Multi I/O",			    /* ZORRO_CLASS_MULTIIO */
    "RAM Expansion",			    /* ZORRO_CLASS_RAM */
    "Scanner Interface",		    /* ZORRO_CLASS_SCANNER */
    "SCSI Host Adapter",		    /* ZORRO_CLASS_SCSI */
    "SCSI Host Adapter and IDE Interface",  /* ZORRO_CLASS_SCSI_IDE */
    "SCSI Host Adapter and RAM Expansion",  /* ZORRO_CLASS_SCSI_RAM */
    "SCSI Host Adapter and Serial Card",    /* ZORRO_CLASS_SCSI_SERIAL */
    "Multi Serial",			    /* ZORRO_CLASS_SERIAL */
    "Drawing Tablet Interface",		    /* ZORRO_CLASS_TABLET */
    "Accelerator",			    /* ZORRO_CLASS_TURBO */
    "Accelerator and RAM Expansion",	    /* ZORRO_CLASS_TURBO_RAM */
    "Accelerator and HD Controller",	    /* ZORRO_CLASS_TURBO_HD */
    "Accelerator and IDE Interface",	    /* ZORRO_CLASS_TURBO_IDE */
    "Accelerator and SCSI Host Adapter",    /* ZORRO_CLASS_TURBO_SCSI */
    "Accelerator, SCSI Host Adapter and RAM Expansion",    /* ZORRO_CLASS_TURBO_SCSI */
    "Video Board",			    /* ZORRO_CLASS_VIDEO */
};

static inline const char *get_class_name(enum Zorro_Classes class)
{
    if (class < ARRAYSIZE(classnames))
	return(classnames[class]);
    else
	return("(**Illegal**)");
}

#endif /* CONFIG_ZORRO */


    /*
     *  Expansion Devices
     */

u_int zorro_num_autocon;
struct ConfigDev zorro_autocon[ZORRO_NUM_AUTO];
static u32 BoardPartFlags[ZORRO_NUM_AUTO] = { 0, };

   
    /*
     *  Find the key for the next unconfigured expansion device of a specific
     *  type.
     *
     *  Part is a device specific number (0 <= part <= 31) to allow for the
     *  independent configuration of independent parts of an expansion board.
     *  Thanks to Jes Soerensen for this idea!
     *
     *  Index is used to specify the first board in the autocon list
     *  to be tested. It was inserted in order to solve the problem
     *  with the GVP boards that uses the same product code, but
     *  it should help if there are other companies which use the same
     *  method as GVP. Drivers for boards which are not using this
     *  method do not need to think of this - just set index = 0.
     *
     *  Example:
     *
     *      while ((key = zorro_find(ZORRO_PROD_MY_BOARD, MY_PART, 0))) {
     *      	cd = zorro_get_board(key);
     *      	initialise_this_board;
     *      	zorro_config_board(key, MY_PART);
     *      }
     */

u_int zorro_find(zorro_id id, u_int part, u_int index)
{
    u_int manuf = ZORRO_MANUF(id);
    u_int prod = ZORRO_PROD(id);
    u_int epc = ZORRO_EPC(id);
    u_int key;
    const struct ConfigDev *cd;
    u_long addr;

    if (!MACH_IS_AMIGA || !AMIGAHW_PRESENT(ZORRO))
	return(0);

    if (part > 31) {
	printk("zorro_find: bad part %d\n", part);
	return(0);
    }

    for (key = index + 1; key <= zorro_num_autocon; key++) {
	cd = &zorro_autocon[key-1];
	addr = (u_long)cd->cd_BoardAddr;
	if ((cd->cd_Rom.er_Manufacturer == manuf) &&
	    (cd->cd_Rom.er_Product == prod) &&
	    !(BoardPartFlags[key-1] & (1<<part)) &&
	    (manuf != ZORRO_MANUF(ZORRO_PROD_GVP_EPC_BASE) ||
	     prod != ZORRO_PROD(ZORRO_PROD_GVP_EPC_BASE) ||
	     (*(u_short *)ZTWO_VADDR(addr+0x8000) & GVP_PRODMASK) == epc))
	    break;
    }
    return(key <= zorro_num_autocon ? key : 0);
}


    /*
     *  Get the board corresponding to a specific key
     */

const struct ConfigDev *zorro_get_board(u_int key)
{
    const struct ConfigDev *cd = NULL;

    if ((key < 1) || (key > zorro_num_autocon))
	printk("zorro_get_board: bad key %d\n", key);
    else
	cd = &zorro_autocon[key-1];

    return(cd);
}


    /*
     *  Mark a part of a board as configured
     */

void zorro_config_board(u_int key, u_int part)
{
    if ((key < 1) || (key > zorro_num_autocon))
	printk("zorro_config_board: bad key %d\n", key);
    else if (part > 31)
	printk("zorro_config_board: bad part %d\n", part);
    else if (BoardPartFlags[key-1] & (1<<part))
	printk("zorro_config_board: key %d part %d is already configured\n",
	       key, part);
    else
	BoardPartFlags[key-1] |= 1<<part;
}


    /*
     *  Mark a part of a board as unconfigured
     *
     *  This function is mainly intended for the unloading of LKMs
     */

void zorro_unconfig_board(u_int key, u_int part)
{
    if ((key < 1) || (key > zorro_num_autocon))
	printk("zorro_unconfig_board: bad key %d\n", key);
    else if (part > 31)
	printk("zorro_unconfig_board: bad part %d\n", part);
    else if (!(BoardPartFlags[key-1] & (1<<part)))
	printk("zorro_config_board: key %d part %d is not yet configured\n",
	       key, part);
    else
	BoardPartFlags[key-1] &= ~(1<<part);
}


#ifdef CONFIG_ZORRO

    /*
     *  Identify an AutoConfig Expansion Device
     *
     *  If the board was configured by a Linux/m68k driver, an asterisk will
     *  be printed before the board address (except for unknown and `Hacker
     *  Test' boards).
     */

static int identify(u_int devnum, char *buf, int verbose)
{
    const struct ConfigDev *cd = &zorro_autocon[devnum];
    u32 configured = BoardPartFlags[devnum];
    u_int manuf = cd->cd_Rom.er_Manufacturer;
    u_int prod = cd->cd_Rom.er_Product;
    u_int class = ZORRO_CLASS_UNKNOWN;
    u_int epc = 0;
    const char *manufname = "Unknown";
    const char *prodname = "Unknown";
    const char *classname;
    u_int i, j, k, len = 0;
    u_long addr = (u_long)cd->cd_BoardAddr;
    u_long size = cd->cd_BoardSize;
    char mag;
    int identified = 0, gvp = 0;

    if (manuf != ZORRO_MANUF(ZORRO_PROD_GVP_EPC_BASE) ||
	prod != ZORRO_PROD(ZORRO_PROD_GVP_EPC_BASE)) {
	for (i = 0; i < NUM_MANUF; i++)
	    if (Manufacturers[i].Manuf == manuf) {
		manufname = Manufacturers[i].Name;
		for (j = 0; j < Manufacturers[i].NumProd; j++)
		    if (Manufacturers[i].Products[j].Prod == prod) {
			prodname = Manufacturers[i].Products[j].Name;
			class = Manufacturers[i].Products[j].Class;
			identified = 1;
			break;
		    }
	    }
	/* Put workarounds for ID clashes here */
	if (manuf == ZORRO_MANUF(ZORRO_PROD_HELFRICH_RAINBOW_III) &&
	    prod == ZORRO_PROD(ZORRO_PROD_HELFRICH_RAINBOW_III))
	    manufname = "Ingenieurbüro Helfrich";
    } else {
	manufname = "Great Valley Products";
	gvp = 1;
	epc = *(u_short *)ZTWO_VADDR(addr+0x8000) & GVP_PRODMASK;
	for (k = 0; k < NUM_GVP_PROD; k++)
	    if (epc == Ext_Prod_GVP[k].EPC) {
		prodname = Ext_Prod_GVP[k].Name;
		class = Ext_Prod_GVP[k].Class;
		identified = 1;
		break;
	    }
    }
    classname = get_class_name(class);
    if (size & 0xfffff) {
	size >>= 10;
	mag = 'K';
    } else {
	size >>= 20;
	mag = 'M';
    }
    if (verbose) {
	const char *zorro;
	int is_mem = cd->cd_Rom.er_Type & ERTF_MEMLIST;
	switch (cd->cd_Rom.er_Type & ERT_TYPEMASK) {
	    case ERT_ZORROII:
		zorro = "Zorro II";
		break;
	    case ERT_ZORROIII:
		zorro = "Zorro III";
		break;
	    default:
		zorro = "Unknown Zorro";
		break;
	}
	if (!prodname)
	    prodname = "Unknown";
	if (!classname)
	    classname = "Unknown";
	len = sprintf(buf, "  Device %d at 0x%08lx: ID=%04x:%02x", devnum,
		      addr, manuf, prod);
	if (gvp)
	    len += sprintf(buf+len, ":%02x", epc);
	len += sprintf(buf+len, ", %s, %ld%c", zorro, size, mag);
	if (is_mem)
	    len += sprintf(buf+len, ", System RAM");
	else
	    len += sprintf(buf+len, ", Configured=%08x", configured);
	len += sprintf(buf+len, "\n"
				"    Manufacturer: %s\n"
				"    Product Name: %s\n"
				"    Board Class : %s\n",
				manufname, prodname, classname);
    } else {
	len = sprintf(buf, " %c%08lx: ", configured ? '*' : ' ', addr);
	if (identified) {
	    len += sprintf(buf+len, "%s", manufname);
	    if (prodname)
		len += sprintf(buf+len, " %s", prodname);
	    if (classname)
		len += sprintf(buf+len, " %s", classname);
	} else if (manuf == ZORRO_MANUF_HACKER)
	    len += sprintf(buf+len, "Hacker Test Board %02x", prod);
	else if (gvp)
	    len += sprintf(buf+len, "[%04x:%02x:%02x] made by %s", manuf, prod,
			   epc, manufname);
	else
	    len += sprintf(buf+len, "[%04x:%02x] made by %s", manuf, prod,
			   manufname);
	len += sprintf(buf+len, " (%ld%c)\n", size, mag);
	if (!identified && manuf != ZORRO_MANUF_HACKER)
	    len += sprintf(buf+len, "    Please report this unknown device to "
				    "zorro@linux-m68k.org\n");
    }
    return(len);
}


    /*
     *  Identify all known AutoConfig Expansion Devices
     */

void zorro_identify(void)
{
    u_int i;
    char tmp[256];

    if (!AMIGAHW_PRESENT(ZORRO))
	return;

    printk("Probing AutoConfig expansion device(s):\n");
    for (i = 0; i < zorro_num_autocon; i++) {
	identify(i, tmp, 0);
	printk(tmp);
    }
    if (!zorro_num_autocon)
	printk("No AutoConfig expansion devices present.\n");
}


    /*
     *  Get the list of all AutoConfig Expansion Devices
     */

int zorro_get_list(char *buffer)
{
    u_int i, len = 0, len2;
    char tmp[256];

    if (MACH_IS_AMIGA && AMIGAHW_PRESENT(ZORRO)) {
	len = sprintf(buffer, "AutoConfig expansion devices:\n");
	for (i = 0; i < zorro_num_autocon; i++) {
	    len2 = identify(i, tmp, 1);
	    if (len+len2 >= 4075) {
		len += sprintf(buffer+len, "4K limit reached!\n");
		break;
	    }
	    strcpy(buffer+len, tmp);
	    len += len2;
	}
    }
    return(len);
}

#endif /* CONFIG_ZORRO */


    /*
     *  Bitmask indicating portions of available Zorro II RAM that are unused
     *  by the system. Every bit represents a 64K chunk, for a maximum of 8MB
     *  (128 chunks, physical 0x00200000-0x009fffff).
     *
     *  If you want to use (= allocate) portions of this RAM, you should clear
     *  the corresponding bits.
     *
     *  Possible uses:
     *      - z2ram device
     *      - SCSI DMA bounce buffers
     */

u32 zorro_unused_z2ram[4] = { 0, 0, 0, 0 };


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
     *  Initialization
     */

void zorro_init(void)
{
    u_int i;
    const struct ConfigDev *cd;

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
