/*
 * $Id: pci.c,v 1.44 1997/09/03 05:08:22 richard Exp $
 *
 * PCI services that are built on top of the BIOS32 service.
 *
 * Copyright 1993, 1994, 1995 Drew Eckhardt, Frederic Potter,
 *	David Mosberger-Tang
 */
#include <linux/config.h>
#include <linux/ptrace.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/page.h>

struct pci_bus pci_root;
struct pci_dev *pci_devices = 0;

/*
 * The bridge_id field is an offset of an item into the array
 * BRIDGE_MAPPING_TYPE. 0xff indicates that the device is not a PCI
 * bridge, or that we don't know for the moment how to configure it.
 * I'm trying to do my best so that the kernel stays small.  Different
 * chipset can have same optimization structure. i486 and pentium
 * chipsets from the same manufacturer usually have the same
 * structure.
 */
#define DEVICE(vid,did,name) \
  {PCI_VENDOR_ID_##vid, PCI_DEVICE_ID_##did, (name), 0xff}

#define BRIDGE(vid,did,name,bridge) \
  {PCI_VENDOR_ID_##vid, PCI_DEVICE_ID_##did, (name), (bridge)}

/*
 * Sorted in ascending order by vendor and device.
 * Use binary search for lookup. If you add a device make sure
 * it is sequential by both vendor and device id.
 */
struct pci_dev_info dev_info[] = {
	DEVICE( COMPAQ,		COMPAQ_1280,	"QVision 1280/p"),
	DEVICE( COMPAQ,		COMPAQ_SMART2P,	"Smart-2/P RAID Controller"),
	DEVICE( COMPAQ,		COMPAQ_NETEL100,"Netelligent 10/100"),
	DEVICE( COMPAQ,		COMPAQ_NETEL10,	"Netelligent 10"),
	DEVICE( COMPAQ,		COMPAQ_NETFLEX3I,"NetFlex 3"),
	DEVICE( COMPAQ,		COMPAQ_NETEL100D,"Netelligent 10/100 Dual"),
	DEVICE( COMPAQ,		COMPAQ_NETEL100PI,"Netelligent 10/100 ProLiant"),
	DEVICE( COMPAQ,		COMPAQ_NETEL100I,"Netelligent 10/100 Integrated"),
	DEVICE( COMPAQ,		COMPAQ_THUNDER,	"ThunderLAN"),
	DEVICE( COMPAQ,		COMPAQ_NETFLEX3B,"NetFlex 3 BNC"),
	DEVICE( NCR,		NCR_53C810,	"53c810"),
	DEVICE( NCR,		NCR_53C820,	"53c820"),
	DEVICE( NCR,		NCR_53C825,	"53c825"),
	DEVICE( NCR,		NCR_53C815,	"53c815"),
	DEVICE( NCR,		NCR_53C860,	"53c860"),
	DEVICE( NCR,		NCR_53C896,	"53c896"),
	DEVICE( NCR,		NCR_53C895,	"53c895"),
	DEVICE( NCR,		NCR_53C885,	"53c885"),
	DEVICE( NCR,		NCR_53C875,	"53c875"),
	DEVICE( ATI,		ATI_68800,      "68800AX"),
	DEVICE( ATI,		ATI_215CT222,   "215CT222"),
	DEVICE( ATI,		ATI_210888CX,   "210888CX"),
	DEVICE( ATI,		ATI_215GP,	"Mach64 GP (Rage Pro)"),
	DEVICE( ATI,		ATI_215GT,	"Mach64 GT (Rage II)"),
	DEVICE( ATI,		ATI_215GTB,	"Mach64 GT (Rage II)"),
	DEVICE( ATI,		ATI_210888GX,   "210888GX"),
	DEVICE( ATI,		ATI_264VT,	"Mach64 VT"),
	DEVICE( VLSI,		VLSI_82C592,	"82C592-FC1"),
	DEVICE( VLSI,		VLSI_82C593,	"82C593-FC1"),
	DEVICE( VLSI,		VLSI_82C594,	"82C594-AFC2"),
	DEVICE( VLSI,		VLSI_82C597,	"82C597-AFC2"),
	DEVICE( VLSI,		VLSI_VAS96011,	"VAS96011 PowerPC"),
	DEVICE( ADL,		ADL_2301,	"2301"),
	DEVICE( NS,		NS_87415,	"87415"),
	DEVICE( NS,		NS_87410,	"87410"),
	DEVICE( TSENG,		TSENG_W32P_2,	"ET4000W32P"),
	DEVICE( TSENG,		TSENG_W32P_b,	"ET4000W32P rev B"),
	DEVICE( TSENG,		TSENG_W32P_c,	"ET4000W32P rev C"),
	DEVICE( TSENG,		TSENG_W32P_d,	"ET4000W32P rev D"),
	DEVICE( TSENG,		TSENG_ET6000,	"ET6000"),
	DEVICE( WEITEK,		WEITEK_P9000,	"P9000"),
	DEVICE( WEITEK,		WEITEK_P9100,	"P9100"),
	BRIDGE( DEC,		DEC_BRD,	"DC21050", 		0x00),
	DEVICE( DEC,		DEC_TULIP,	"DC21040"),
	DEVICE( DEC,		DEC_TGA,	"TGA"),
	DEVICE( DEC,		DEC_TULIP_FAST,	"DC21140"),
	DEVICE( DEC,		DEC_TGA2,	"TGA2"),
	DEVICE( DEC,		DEC_FDDI,	"DEFPA"),
	DEVICE( DEC,		DEC_TULIP_PLUS,	"DC21041"),
	DEVICE( DEC,		DEC_21142,	"DC21142"),
	DEVICE( DEC,		DEC_21052,	"DC21052"),
	DEVICE( DEC,		DEC_21152,	"DC21152"),
	DEVICE( CIRRUS,		CIRRUS_7548,	"GD 7548"),
	DEVICE( CIRRUS,		CIRRUS_5430,	"GD 5430"),
	DEVICE( CIRRUS,		CIRRUS_5434_4,	"GD 5434"),
	DEVICE( CIRRUS,		CIRRUS_5434_8,	"GD 5434"),
	DEVICE( CIRRUS,		CIRRUS_5436,	"GD 5436"),
	DEVICE( CIRRUS,		CIRRUS_5446,	"GD 5446"),
	DEVICE( CIRRUS,		CIRRUS_5480,	"GD 5480"),
	DEVICE( CIRRUS,		CIRRUS_5464,	"GD 5464"),
	DEVICE( CIRRUS,		CIRRUS_5465,	"GD 5465"),
	DEVICE( CIRRUS,		CIRRUS_6729,	"CL 6729"),
	DEVICE( CIRRUS,		CIRRUS_6832,	"PD 6832"),
	DEVICE( CIRRUS,		CIRRUS_7542,	"CL 7542"),
	DEVICE( CIRRUS,		CIRRUS_7543,	"CL 7543"),
	DEVICE( CIRRUS,		CIRRUS_7541,	"CL 7541"),
	DEVICE( IBM,		IBM_FIRE_CORAL,	"Fire Coral"),
	DEVICE( IBM,		IBM_82G2675,	"82G2675"),
	DEVICE( IBM,		IBM_82351,	"82351"),
	DEVICE( WD,		WD_7197,	"WD 7197"),
	DEVICE( AMD,		AMD_LANCE,	"79C970"),
	DEVICE( AMD,		AMD_SCSI,	"53C974"),
	DEVICE( TRIDENT,	TRIDENT_9420,	"TG 9420"),
	DEVICE( TRIDENT,	TRIDENT_9440,	"TG 9440"),
	DEVICE( TRIDENT,	TRIDENT_9660,	"TG 9660"),
	DEVICE( AI,		AI_M1435,	"M1435"),
	DEVICE( MATROX,		MATROX_MGA_2,	"Atlas PX2085"),
	DEVICE( MATROX,		MATROX_MIL,	"Millennium"),
	DEVICE( MATROX,		MATROX_MYS,	"Mystique"),
	DEVICE( MATROX,		MATROX_MIL_2,	"Millennium II"),
	DEVICE( MATROX,		MATROX_MGA_IMP,	"MGA Impression"),
	DEVICE( CT,		CT_65545,	"65545"),
	DEVICE( CT,		CT_65548,	"65548"),
	DEVICE(	CT,		CT_65550,	"65550"),
	DEVICE( CT,		CT_65554,	"65554"),
	DEVICE( MIRO,		MIRO_36050,	"ZR36050"),
	DEVICE( FD,		FD_36C70,	"TMC-18C30"),
	DEVICE( SI,		SI_6201,	"6201"),
	DEVICE( SI,		SI_6202,	"6202"),
	DEVICE( SI,		SI_503,		"85C503"),
	DEVICE( SI,		SI_6205,	"6205"),
	DEVICE( SI,		SI_501,		"85C501"),
	DEVICE( SI,		SI_496,		"85C496"),
	DEVICE( SI,		SI_601,		"85C601"),
	DEVICE( SI,		SI_5107,	"5107"),
	DEVICE( SI,		SI_5511,		"85C5511"),
	DEVICE( SI,		SI_5513,		"85C5513"),
	DEVICE( SI,		SI_5571,	"5571"),
	DEVICE( SI,		SI_7001,	"7001"),
	DEVICE( HP,		HP_J2585A,	"J2585A"),
	DEVICE( HP,		HP_J2585B,	"J2585B (Lassen)"),
	DEVICE( PCTECH,		PCTECH_RZ1000,  "RZ1000 (buggy)"),
	DEVICE( PCTECH,		PCTECH_RZ1001,  "RZ1001 (buggy?)"),
	DEVICE( DPT,		DPT,		"SmartCache/Raid"),
	DEVICE( OPTI,		OPTI_92C178,	"92C178"),
	DEVICE( OPTI,		OPTI_82C557,	"82C557"),
	DEVICE( OPTI,		OPTI_82C558,	"82C558"),
	DEVICE( OPTI,		OPTI_82C621,	"82C621"),
	DEVICE( OPTI,		OPTI_82C822,	"82C822"),
	DEVICE( SGS,		SGS_2000,	"STG 2000X"),
	DEVICE( SGS,		SGS_1764,	"STG 1764X"),
	DEVICE( BUSLOGIC,	BUSLOGIC_MULTIMASTER_NC, "MultiMaster NC"),
	DEVICE( BUSLOGIC,	BUSLOGIC_MULTIMASTER,    "MultiMaster"),
	DEVICE( BUSLOGIC,	BUSLOGIC_FLASHPOINT,     "FlashPoint"),
	DEVICE( TI,		TI_PCI1130,	"PCI1130"),
	DEVICE( TI,		TI_PCI1131,	"PCI1131"),
	DEVICE( OAK,		OAK_OTI107,	"OTI107"),
	DEVICE( WINBOND2,	WINBOND2_89C940,"NE2000-PCI"),
	DEVICE( MOTOROLA,	MOTOROLA_MPC105,"MPC105 Eagle"),
	DEVICE( MOTOROLA,	MOTOROLA_MPC106,"MPC106 Grackle"),
	DEVICE( MOTOROLA,	MOTOROLA_RAVEN,	"Raven"),
	DEVICE( PROMISE,        PROMISE_IDE_UDMA,"IDE Ultra DMA/33"),
	DEVICE( PROMISE,	PROMISE_5300,	"DC5030"),
	DEVICE( N9,		N9_I128,	"Imagine 128"),
	DEVICE( N9,		N9_I128_2,	"Imagine 128v2"),
	DEVICE( UMC,		UMC_UM8673F,	"UM8673F"),
	BRIDGE( UMC,		UMC_UM8891A,	"UM8891A", 		0x01),
	DEVICE( UMC,		UMC_UM8886BF,	"UM8886BF"),
	DEVICE( UMC,		UMC_UM8886A,	"UM8886A"),
	BRIDGE( UMC,		UMC_UM8881F,	"UM8881F",		0x02),
	DEVICE( UMC,		UMC_UM8886F,	"UM8886F"),
	DEVICE( UMC,		UMC_UM9017F,	"UM9017F"),
	DEVICE( UMC,		UMC_UM8886N,	"UM8886N"),
	DEVICE( UMC,		UMC_UM8891N,	"UM8891N"),
	DEVICE( X,		X_AGX016,	"ITT AGX016"),
	DEVICE( PICOP,		PICOP_PT86C52X,	"PT86C52x Vesuvius"),
	DEVICE( APPLE,		APPLE_BANDIT,	"Bandit"),
	DEVICE( APPLE,		APPLE_GC,	"Grand Central"),
	DEVICE( APPLE,		APPLE_HYDRA,	"Hydra"),
	DEVICE( NEXGEN,		NEXGEN_82C501,	"82C501"),
	DEVICE( QLOGIC,		QLOGIC_ISP1020,	"ISP1020"),
	DEVICE( QLOGIC,		QLOGIC_ISP1022,	"ISP1022"),
	DEVICE( LEADTEK,	LEADTEK_805,	"S3 805"),
	DEVICE( CONTAQ,		CONTAQ_82C599,	"82C599"),
	DEVICE( OLICOM,		OLICOM_OC3136,	"OC-3136/3137"),
	DEVICE( OLICOM,		OLICOM_OC2315,	"OC-2315"),
	DEVICE( OLICOM,		OLICOM_OC2325,	"OC-2325"),
	DEVICE( OLICOM,		OLICOM_OC2183,	"OC-2183/2185"),
	DEVICE( OLICOM,		OLICOM_OC2326,	"OC-2326"),
	DEVICE( OLICOM,		OLICOM_OC6151,	"OC-6151/6152"),
	DEVICE( SUN,		SUN_EBUS,	"EBUS"),
	DEVICE( SUN,		SUN_HAPPYMEAL,	"Happy Meal"),
	BRIDGE( SUN,		SUN_PBM,	"PCI Bus Module",	0x02),
	DEVICE( CMD,		CMD_640,	"640 (buggy)"),
	DEVICE( CMD,		CMD_643,	"643"),
	DEVICE( CMD,		CMD_646,	"646"),
	DEVICE( CMD,		CMD_670,	"670"),
	DEVICE( VISION,		VISION_QD8500,	"QD-8500"),
	DEVICE( VISION,		VISION_QD8580,	"QD-8580"),
	DEVICE( BROOKTREE,	BROOKTREE_848,	"Bt848"),
	DEVICE( SIERRA,		SIERRA_STB,	"STB Horizon 64"),
	DEVICE( ACC,		ACC_2056,	"2056"),
	DEVICE( WINBOND,	WINBOND_83769,	"W83769F"),
	DEVICE( WINBOND,	WINBOND_82C105,	"SL82C105"),
	DEVICE( WINBOND,	WINBOND_83C553,	"W83C553"),
	DEVICE( DATABOOK,      	DATABOOK_87144,	"DB87144"),
	DEVICE( 3COM,		3COM_3C590,	"3C590 10bT"),
	DEVICE( 3COM,		3COM_3C595TX,	"3C595 100bTX"),
	DEVICE( 3COM,		3COM_3C595T4,	"3C595 100bT4"),
	DEVICE( 3COM,		3COM_3C595MII,	"3C595 100b-MII"),
	DEVICE( 3COM,		3COM_3C900TPO,	"3C900 10bTPO"),
	DEVICE( 3COM,		3COM_3C900COMBO,"3C900 10b Combo"),
	DEVICE( 3COM,		3COM_3C905TX,	"3C905 100bTX"),
	DEVICE( SMC,		SMC_EPIC100,	"9432 TX"),
	DEVICE( AL,		AL_M1445,	"M1445"),
	DEVICE( AL,		AL_M1449,	"M1449"),
	DEVICE( AL,		AL_M1451,	"M1451"),
	DEVICE( AL,		AL_M1461,	"M1461"),
	DEVICE( AL,		AL_M1489,	"M1489"),
	DEVICE( AL,		AL_M1511,	"M1511"),
	DEVICE( AL,		AL_M1513,	"M1513"),
	DEVICE( AL,		AL_M1521,	"M1521"),
	DEVICE( AL,		AL_M1523,	"M1523"),
	DEVICE( AL,		AL_M1531,	"M1531 Aladdin IV"),
	DEVICE( AL,		AL_M1533,	"M1533 Aladdin IV"),
	DEVICE( AL,		AL_M4803,	"M4803"),
	DEVICE( AL,		AL_M5219,	"M5219"),
	DEVICE( AL,		AL_M5229,	"M5229 TXpro"),
	DEVICE( NEOMAGIC,       NEOMAGIC_MAGICGRAPH_NM2070,     "Magicgraph NM2070"),
	DEVICE( NEOMAGIC,	NEOMAGIC_MAGICGRAPH_128V, "MagicGraph 128V"),
	DEVICE( NEOMAGIC,	NEOMAGIC_MAGICGRAPH_128ZV, "MagicGraph 128ZV"),
	DEVICE( ASP,		ASP_ABP940,	"ABP940"),
	DEVICE( ASP,		ASP_ABP940U,	"ABP940U"),
	DEVICE( CERN,		CERN_SPSB_PMC,	"STAR/RD24 SCI-PCI (PMC)"),
	DEVICE( CERN,		CERN_SPSB_PCI,	"STAR/RD24 SCI-PCI (PMC)"),
	DEVICE( IMS,		IMS_8849,	"8849"),
	DEVICE( TEKRAM2,	TEKRAM2_690c,	"DC690c"),
	DEVICE( TUNDRA,		TUNDRA_CA91C042,"CA91C042 Universe"),
	DEVICE( AMCC,		AMCC_MYRINET,	"Myrinet PCI (M2-PCI-32)"),
	DEVICE( AMCC,		AMCC_S5933,	"S5933"),
	DEVICE( INTERG,		INTERG_1680,	"IGA-1680"),
	DEVICE( INTERG,         INTERG_1682,    "IGA-1682"),
	DEVICE( REALTEK,	REALTEK_8029,	"8029"),
	DEVICE( REALTEK,	REALTEK_8129,	"8129"),
	DEVICE( TRUEVISION,	TRUEVISION_T1000,"TARGA 1000"),
	DEVICE( INIT,		INIT_320P,	"320 P"),
	DEVICE( VIA,		VIA_82C505,	"VT 82C505"),
	DEVICE( VIA,		VIA_82C561,	"VT 82C561"),
	DEVICE( VIA,		VIA_82C586_1,	"VT 82C586 Apollo IDE"),
	DEVICE( VIA,		VIA_82C576,	"VT 82C576 3V"),
	DEVICE( VIA,		VIA_82C585,	"VT 82C585 Apollo VP1/VPX"),
	DEVICE( VIA,		VIA_82C586_0,	"VT 82C586 Apollo ISA"),
	DEVICE( VIA,		VIA_82C595,	"VT 82C595 Apollo VP2"),
	DEVICE( VIA,		VIA_82C926,	"VT 82C926 Amazon"),
	DEVICE( VIA,		VIA_82C416,	"VT 82C416MV"),
	DEVICE( VIA,		VIA_82C595_97,	"VT 82C595 Apollo VP2/97"),
	DEVICE( VIA,		VIA_82C586_2,	"VT 82C586 Apollo USB"),
	DEVICE( VIA,		VIA_82C586_3,	"VT 82C586B Apollo ACPI"),
	DEVICE( VORTEX,		VORTEX_GDT60x0,	"GDT 60x0"),
	DEVICE( VORTEX,		VORTEX_GDT6000B,"GDT 6000b"),
	DEVICE( VORTEX,		VORTEX_GDT6x10,	"GDT 6110/6510"),
	DEVICE( VORTEX,		VORTEX_GDT6x20,	"GDT 6120/6520"),
	DEVICE( VORTEX,		VORTEX_GDT6530,	"GDT 6530"),
	DEVICE( VORTEX,		VORTEX_GDT6550,	"GDT 6550"),
	DEVICE( VORTEX,		VORTEX_GDT6x17,	"GDT 6117/6517"),
	DEVICE( VORTEX,		VORTEX_GDT6x27,	"GDT 6127/6527"),
	DEVICE( VORTEX,		VORTEX_GDT6537,	"GDT 6537"),
	DEVICE( VORTEX,		VORTEX_GDT6557,	"GDT 6557"),
	DEVICE( VORTEX,		VORTEX_GDT6x15,	"GDT 6115/6515"),
	DEVICE( VORTEX,		VORTEX_GDT6x25,	"GDT 6125/6525"),
	DEVICE( VORTEX,		VORTEX_GDT6535,	"GDT 6535"),
	DEVICE( VORTEX,		VORTEX_GDT6555,	"GDT 6555"),
	DEVICE( VORTEX,		VORTEX_GDT6x17RP,"GDT 6117RP/6517RP"),
	DEVICE( VORTEX,		VORTEX_GDT6x27RP,"GDT 6127RP/6527RP"),
	DEVICE( VORTEX,		VORTEX_GDT6537RP,"GDT 6537RP"),
	DEVICE( VORTEX,		VORTEX_GDT6557RP,"GDT 6557RP"),
	DEVICE( VORTEX,		VORTEX_GDT6x11RP,"GDT 6111RP/6511RP"),
	DEVICE( VORTEX,		VORTEX_GDT6x21RP,"GDT 6121RP/6521RP"),
	DEVICE( VORTEX,		VORTEX_GDT6x17RP1,"GDT 6117RP1/6517RP1"),
	DEVICE( VORTEX,		VORTEX_GDT6x27RP1,"GDT 6127RP1/6527RP1"),
	DEVICE( VORTEX,		VORTEX_GDT6537RP1,"GDT 6537RP1"),
	DEVICE( VORTEX,		VORTEX_GDT6557RP1,"GDT 6557RP1"),
	DEVICE( VORTEX,		VORTEX_GDT6x11RP1,"GDT 6111RP1/6511RP1"),
	DEVICE( VORTEX,		VORTEX_GDT6x21RP1,"GDT 6121RP1/6521RP1"),
	DEVICE( VORTEX,		VORTEX_GDT6x17RP2,"GDT 6117RP2/6517RP2"),
	DEVICE( VORTEX,		VORTEX_GDT6x27RP2,"GDT 6127RP2/6527RP2"),
	DEVICE( VORTEX,		VORTEX_GDT6537RP2,"GDT 6537RP2"),
	DEVICE( VORTEX,		VORTEX_GDT6557RP2,"GDT 6557RP2"),
	DEVICE( VORTEX,		VORTEX_GDT6x11RP2,"GDT 6111RP2/6511RP2"),
	DEVICE( VORTEX,		VORTEX_GDT6x21RP2,"GDT 6121RP2/6521RP2"),
	DEVICE( EF,		EF_ATM_FPGA,		"155P-MF1 (FPGA)"),
	DEVICE( EF,		EF_ATM_ASIC,	"155P-MF1 (ASIC)"),
	DEVICE( FORE,		FORE_PCA200PC, "PCA-200PC"),
	DEVICE( FORE,		FORE_PCA200E,	 "PCA-200E"),
	DEVICE( IMAGINGTECH,	IMAGINGTECH_ICPCI, "MVC IC-PCI"),
	DEVICE( PHILIPS,	PHILIPS_SAA7146,"SAA7146"),
	DEVICE( PLX,		PLX_9060,	"PCI9060 i960 bridge"),
	DEVICE( ALLIANCE,	ALLIANCE_PROMOTIO, "Promotion-6410"),
	DEVICE( ALLIANCE,	ALLIANCE_PROVIDEO, "Provideo"),
	DEVICE( ALLIANCE,	ALLIANCE_AT24,	"AT24"),
	DEVICE( VMIC,		VMIC_VME,	"VMIVME-7587"),
 	DEVICE( DIGI,		DIGI_RIGHTSWITCH, "RightSwitch SE-6"),
	DEVICE( MUTECH,		MUTECH_MV1000,	"MV-1000"),
	DEVICE( RENDITION,	RENDITION_VERITE,"Verite 1000"),
	DEVICE( TOSHIBA,	TOSHIBA_601,	"Laptop"),
	DEVICE( RICOH,		RICOH_RL5C466,	"RL5C466"),
	DEVICE( ZEITNET,	ZEITNET_1221,	"1221"),
	DEVICE( ZEITNET,	ZEITNET_1225,	"1225"),
	DEVICE( OMEGA,		OMEGA_82C092G,	"82C092G"),
	DEVICE( NP,		NP_PCI_FDDI,	"NP-PCI"),       
	DEVICE( SPECIALIX,	SPECIALIX_XIO,	"XIO/SIO host"),
	DEVICE( SPECIALIX,	SPECIALIX_RIO,	"RIO host"),
	DEVICE( IKON,		IKON_10115,	"10115 Greensheet"),
	DEVICE( IKON,		IKON_10117,	"10117 Greensheet"),
	DEVICE( ZORAN,		ZORAN_36057,	"ZR36057"),
	DEVICE( ZORAN,		ZORAN_36120,	"ZR36120"),
	DEVICE( COMPEX,		COMPEX_ENET100VG4, "Readylink ENET100-VG4"),
	DEVICE( COMPEX,		COMPEX_RL2000,	"ReadyLink 2000"),
	DEVICE( RP,             RP8OCTA,        "RocketPort 8 Oct"),
	DEVICE( RP,             RP8INTF,        "RocketPort 8 Intf"),
	DEVICE( RP,             RP16INTF,       "RocketPort 16 Intf"),
	DEVICE( RP,             RP32INTF,       "RocketPort 32 Intf"),
	DEVICE( CYCLADES,	CYCLOM_Y_Lo,	"Cyclom-Y below 1Mbyte"),
	DEVICE( CYCLADES,	CYCLOM_Y_Hi,	"Cyclom-Y above 1Mbyte"),
	DEVICE( CYCLADES,	CYCLOM_Z_Lo,	"Cyclom-Z below 1Mbyte"),
	DEVICE( CYCLADES,	CYCLOM_Z_Hi,	"Cyclom-Z above 1Mbyte"),
	DEVICE( 3DFX,		3DFX_VOODOO,	"Voodoo"),
	DEVICE( SIGMADES,	SIGMADES_6425,	"REALmagic64/GX"),
	DEVICE( OPTIBASE,	OPTIBASE_FORGE,	"MPEG Forge"),
	DEVICE( OPTIBASE,	OPTIBASE_FUSION,"MPEG Fusion"),
	DEVICE( OPTIBASE,	OPTIBASE_VPLEX,	"VideoPlex"),
	DEVICE( OPTIBASE,	OPTIBASE_VPLEXCC,"VideoPlex CC"),
	DEVICE( OPTIBASE,	OPTIBASE_VQUEST,"VideoQuest"),
	DEVICE( ENSONIQ,	ENSONIQ_AUDIOPCI,"AudioPCI"),
	DEVICE( PICTUREL,	PICTUREL_PCIVST,"PCIVST"),
	DEVICE( NVIDIA,		NVIDIA_RIVA128,	"Riva 128"),
	DEVICE( SYMPHONY,	SYMPHONY_101,	"82C101"),
	DEVICE( TEKRAM,		TEKRAM_DC290,	"DC-290"),
	DEVICE( 3DLABS,		3DLABS_300SX,	"GLINT 300SX"),
	DEVICE( 3DLABS,		3DLABS_500TX,	"GLINT 500TX"),
	DEVICE( 3DLABS,		3DLABS_DELTA,	"GLINT Delta"),
	DEVICE( 3DLABS,		3DLABS_PERMEDIA,"PERMEDIA"),
	DEVICE( AVANCE,		AVANCE_ALG2064,	"ALG2064i"),
	DEVICE( AVANCE,		AVANCE_2302,	"ALG-2302"),
	DEVICE( NETVIN,		NETVIN_NV5000SC,"NV5000"),
	DEVICE( S3,		S3_PLATO_PXS,	"PLATO/PX (system)"),
	DEVICE( S3,		S3_ViRGE,	"ViRGE"),
	DEVICE( S3,		S3_TRIO,	"Trio32/Trio64"),
	DEVICE( S3,		S3_AURORA64VP,	"Aurora64V+"),
	DEVICE( S3,		S3_TRIO64UVP,	"Trio64UV+"),
	DEVICE( S3,		S3_ViRGE_VX,	"ViRGE/VX"),
	DEVICE( S3,		S3_868,	"Vision 868"),
	DEVICE( S3,		S3_928,		"Vision 928-P"),
	DEVICE( S3,		S3_864_1,	"Vision 864-P"),
	DEVICE( S3,		S3_864_2,	"Vision 864-P"),
	DEVICE( S3,		S3_964_1,	"Vision 964-P"),
	DEVICE( S3,		S3_964_2,	"Vision 964-P"),
	DEVICE( S3,		S3_968,		"Vision 968"),
	DEVICE( S3,		S3_TRIO64V2,	"Trio64V2/DX or /GX"),
	DEVICE( S3,		S3_PLATO_PXG,	"PLATO/PX (graphics)"),
	DEVICE( S3,		S3_ViRGE_DXGX,	"ViRGE/DX or /GX"),
	DEVICE( S3,		S3_ViRGE_GX2,	"ViRGE/GX2"),
	DEVICE( INTEL,		INTEL_82375,	"82375EB"),
	BRIDGE( INTEL,		INTEL_82424,	"82424ZX Saturn",	0x00),
	DEVICE( INTEL,		INTEL_82378,	"82378IB"),
	DEVICE( INTEL,		INTEL_82430,	"82430ZX Aries"),
	BRIDGE( INTEL,		INTEL_82434,	"82434LX Mercury/Neptune", 0x00),
	DEVICE( INTEL,		INTEL_82092AA_0,"82092AA PCMCIA bridge"),
	DEVICE( INTEL,		INTEL_82092AA_1,"82092AA EIDE"),
	DEVICE( INTEL,		INTEL_7116,	"SAA7116"),
	DEVICE( INTEL,		INTEL_82596,	"82596"),
	DEVICE( INTEL,		INTEL_82865,	"82865"),
	DEVICE( INTEL,		INTEL_82557,	"82557"),
	DEVICE( INTEL,		INTEL_82437,	"82437"),
	DEVICE( INTEL,		INTEL_82371_0,	"82371 Triton PIIX"),
	DEVICE( INTEL,		INTEL_82371_1,	"82371 Triton PIIX"),
	DEVICE( INTEL,		INTEL_82371MX,	"430MX - 82371MX MPIIX"),
	DEVICE( INTEL,		INTEL_82437MX,	"430MX - 82437MX MTSC"),
	DEVICE( INTEL,		INTEL_82441,	"82441FX Natoma"),
	DEVICE( INTEL,		INTEL_82439,	"82439HX Triton II"),
	DEVICE(	INTEL,		INTEL_82371SB_0,"82371SB Natoma/Triton II PIIX3"),
	DEVICE(	INTEL,		INTEL_82371SB_1,"82371SB Natoma/Triton II PIIX3"),
	DEVICE( INTEL,		INTEL_82371SB_2,"82371SB Natoma/Triton II PIIX3"),
	DEVICE( INTEL,		INTEL_82437VX,	"82437VX Triton II"),
	DEVICE( INTEL,		INTEL_82439TX,	"82439TX"),
	DEVICE( INTEL,		INTEL_82371AB_0,"82371AB PIIX4 ISA"),
	DEVICE( INTEL,		INTEL_82371AB,	"82371AB PIIX4 IDE"),
	DEVICE( INTEL,		INTEL_82371AB_2,"82371AB PIIX4 USB"),
	DEVICE( INTEL,		INTEL_82371AB_3,"82371AB PIIX4 ACPI"),
	DEVICE( INTEL,		INTEL_P6,	"Orion P6"),
 	DEVICE( INTEL,		INTEL_82450GX,	"82450GX Orion P6"),
	DEVICE(	KTI,		KTI_ET32P2,	"ET32P2"),
	DEVICE( ADAPTEC,	ADAPTEC_7850,	"AIC-7850"),
	DEVICE( ADAPTEC,	ADAPTEC_7855,	"AIC-7855"),
	DEVICE( ADAPTEC,	ADAPTEC_7860,	"AIC-7860"),
	DEVICE( ADAPTEC,	ADAPTEC_7861,	"AIC-7861"),
	DEVICE( ADAPTEC,	ADAPTEC_7870,	"AIC-7870"),
	DEVICE( ADAPTEC,	ADAPTEC_7871,	"AIC-7871"),
	DEVICE( ADAPTEC,	ADAPTEC_7872,	"AIC-7872"),
	DEVICE( ADAPTEC,	ADAPTEC_7873,	"AIC-7873"),
	DEVICE( ADAPTEC,	ADAPTEC_7874,	"AIC-7874"),
	DEVICE( ADAPTEC,	ADAPTEC_7880,	"AIC-7880U"),
	DEVICE( ADAPTEC,	ADAPTEC_7881,	"AIC-7881U"),
	DEVICE( ADAPTEC,	ADAPTEC_7882,	"AIC-7882U"),
	DEVICE( ADAPTEC,	ADAPTEC_7883,	"AIC-7883U"),
	DEVICE( ADAPTEC,	ADAPTEC_7884,	"AIC-7884U"),
  	DEVICE( ATRONICS,	ATRONICS_2015,	"IDE-2015PL"),
	DEVICE( ARK,		ARK_STING,	"Stingray"),
	DEVICE( ARK,		ARK_STINGARK,	"Stingray ARK 2000PV"),
	DEVICE( ARK,		ARK_2000MT,	"2000MT")
};


#ifdef CONFIG_PCI_OPTIMIZE

/*
 * An item of this structure has the following meaning:
 * for each optimization, the register address, the mask
 * and value to write to turn it on.
 * There are 5 optimizations for the moment:
 * Cache L2 write back best than write through
 * Posted Write for CPU to PCI enable
 * Posted Write for CPU to MEMORY enable
 * Posted Write for PCI to MEMORY enable
 * PCI Burst enable
 *
 * Half of the bios I've meet don't allow you to turn that on, and you
 * can gain more than 15% on graphic accesses using those
 * optimizations...
 */
struct optimization_type {
	const char	*type;
	const char	*off;
	const char	*on;
} bridge_optimization[] = {
	{"Cache L2",			"write through",	"write back"},
	{"CPU-PCI posted write",	"off",		"on"},
	{"CPU-Memory posted write",	"off",		"on"},
	{"PCI-Memory posted write",	"off",		"on"},
	{"PCI burst",			"off",		"on"}
};

#define NUM_OPTIMIZATIONS \
	(sizeof(bridge_optimization) / sizeof(bridge_optimization[0]))

struct bridge_mapping_type {
	unsigned char	addr;	/* config space address */
	unsigned char	mask;
	unsigned char	value;
} bridge_mapping[] = {
	/*
	 * Intel Neptune/Mercury/Saturn:
	 *	If the internal cache is write back,
	 *	the L2 cache must be write through!
	 *	I've to check out how to control that
	 *	for the moment, we won't touch the cache
	 */
	{0x0	,0x02	,0x02	},
	{0x53	,0x02	,0x02	},
	{0x53	,0x01	,0x01	},
	{0x54	,0x01	,0x01	},
	{0x54	,0x02	,0x02	},

	/*
	 * UMC 8891A Pentium chipset:
	 *	Why did you think UMC was cheaper ??
	 */
	{0x50	,0x10	,0x00	},
	{0x51	,0x40	,0x40	},
	{0x0	,0x0	,0x0	},
	{0x0	,0x0	,0x0	},
	{0x0	,0x0	,0x0	},

	/*
	 * UMC UM8881F
	 *	This is a dummy entry for my tests.
	 *	I have this chipset and no docs....
	 */
	{0x0	,0x1	,0x1	},
	{0x0	,0x2	,0x0	},
	{0x0	,0x0	,0x0	},
	{0x0	,0x0	,0x0	},
	{0x0	,0x0	,0x0	}
};

#endif /* CONFIG_PCI_OPTIMIZE */


/*
 * device_info[] is sorted so we can use binary search
 */
struct pci_dev_info *pci_lookup_dev(unsigned int vendor, unsigned int dev)
{
	int min = 0,
	    max = sizeof(dev_info)/sizeof(dev_info[0]) - 1;

	for ( ; ; )
	{
	    int i = (min + max) >> 1;
	    long order;

	    order = dev_info[i].vendor - (long) vendor;
	    if (!order)
		order = dev_info[i].device - (long) dev;
	
	    if (order < 0)
	    {
		    min = i + 1;
		    if ( min > max )
		       return 0;
		    continue;
	    }

	    if (order > 0)
	    {
		    max = i - 1;
		    if ( min > max )
		       return 0;
		    continue;
	    }

	    return & dev_info[ i ];
	}
}

const char *pci_strclass (unsigned int class)
{
	switch (class >> 8) {
	      case PCI_CLASS_NOT_DEFINED:		return "Non-VGA device";
	      case PCI_CLASS_NOT_DEFINED_VGA:		return "VGA compatible device";

	      case PCI_CLASS_STORAGE_SCSI:		return "SCSI storage controller";
	      case PCI_CLASS_STORAGE_IDE:		return "IDE interface";
	      case PCI_CLASS_STORAGE_FLOPPY:		return "Floppy disk controller";
	      case PCI_CLASS_STORAGE_IPI:		return "IPI bus controller";
	      case PCI_CLASS_STORAGE_RAID:		return "RAID bus controller";
	      case PCI_CLASS_STORAGE_OTHER:		return "Unknown mass storage controller";

	      case PCI_CLASS_NETWORK_ETHERNET:		return "Ethernet controller";
	      case PCI_CLASS_NETWORK_TOKEN_RING:	return "Token ring network controller";
	      case PCI_CLASS_NETWORK_FDDI:		return "FDDI network controller";
	      case PCI_CLASS_NETWORK_ATM:		return "ATM network controller";
	      case PCI_CLASS_NETWORK_OTHER:		return "Network controller";

	      case PCI_CLASS_DISPLAY_VGA:		return "VGA compatible controller";
	      case PCI_CLASS_DISPLAY_XGA:		return "XGA compatible controller";
	      case PCI_CLASS_DISPLAY_OTHER:		return "Display controller";

	      case PCI_CLASS_MULTIMEDIA_VIDEO:		return "Multimedia video controller";
	      case PCI_CLASS_MULTIMEDIA_AUDIO:		return "Multimedia audio controller";
	      case PCI_CLASS_MULTIMEDIA_OTHER:		return "Multimedia controller";

	      case PCI_CLASS_MEMORY_RAM:		return "RAM memory";
	      case PCI_CLASS_MEMORY_FLASH:		return "FLASH memory";
	      case PCI_CLASS_MEMORY_OTHER:		return "Memory";

	      case PCI_CLASS_BRIDGE_HOST:		return "Host bridge";
	      case PCI_CLASS_BRIDGE_ISA:		return "ISA bridge";
	      case PCI_CLASS_BRIDGE_EISA:		return "EISA bridge";
	      case PCI_CLASS_BRIDGE_MC:			return "MicroChannel bridge";
	      case PCI_CLASS_BRIDGE_PCI:		return "PCI bridge";
	      case PCI_CLASS_BRIDGE_PCMCIA:		return "PCMCIA bridge";
	      case PCI_CLASS_BRIDGE_NUBUS:		return "NuBus bridge";
	      case PCI_CLASS_BRIDGE_CARDBUS:		return "CardBus bridge";
	      case PCI_CLASS_BRIDGE_OTHER:		return "Bridge";

	      case PCI_CLASS_COMMUNICATION_SERIAL:	return "Serial controller";
	      case PCI_CLASS_COMMUNICATION_PARALLEL:	return "Parallel controller";
	      case PCI_CLASS_COMMUNICATION_OTHER:	return "Communication controller";

	      case PCI_CLASS_SYSTEM_PIC:		return "PIC";
	      case PCI_CLASS_SYSTEM_DMA:		return "DMA controller";
	      case PCI_CLASS_SYSTEM_TIMER:		return "Timer";
	      case PCI_CLASS_SYSTEM_RTC:		return "RTC";
	      case PCI_CLASS_SYSTEM_OTHER:		return "System peripheral";

	      case PCI_CLASS_INPUT_KEYBOARD:		return "Keyboard controller";
	      case PCI_CLASS_INPUT_PEN:			return "Digitizer Pen";
	      case PCI_CLASS_INPUT_MOUSE:		return "Mouse controller";
	      case PCI_CLASS_INPUT_OTHER:		return "Input device controller";

	      case PCI_CLASS_DOCKING_GENERIC:		return "Generic Docking Station";
	      case PCI_CLASS_DOCKING_OTHER:		return "Docking Station";

	      case PCI_CLASS_PROCESSOR_386:		return "386";
	      case PCI_CLASS_PROCESSOR_486:		return "486";
	      case PCI_CLASS_PROCESSOR_PENTIUM:		return "Pentium";
	      case PCI_CLASS_PROCESSOR_ALPHA:		return "Alpha";
	      case PCI_CLASS_PROCESSOR_POWERPC:		return "Power PC";
	      case PCI_CLASS_PROCESSOR_CO:		return "Co-processor";

	      case PCI_CLASS_SERIAL_FIREWIRE:		return "FireWire (IEEE 1394)";
	      case PCI_CLASS_SERIAL_ACCESS:		return "ACCESS Bus";
	      case PCI_CLASS_SERIAL_SSA:		return "SSA";
	      case PCI_CLASS_SERIAL_USB:		return "USB Controller";
	      case PCI_CLASS_SERIAL_FIBER:		return "Fiber Channel";

	      default:					return "Unknown class";
	}
}


const char *pci_strvendor(unsigned int vendor)
{
	switch (vendor) {
	      case PCI_VENDOR_ID_COMPAQ:	return "Compaq";
	      case PCI_VENDOR_ID_NCR:		return "NCR";
	      case PCI_VENDOR_ID_ATI:		return "ATI";
	      case PCI_VENDOR_ID_VLSI:		return "VLSI";
	      case PCI_VENDOR_ID_ADL:		return "Advance Logic";
	      case PCI_VENDOR_ID_NS:		return "NS";
	      case PCI_VENDOR_ID_TSENG:		return "Tseng'Lab";
	      case PCI_VENDOR_ID_WEITEK:	return "Weitek";
	      case PCI_VENDOR_ID_DEC:		return "DEC";
	      case PCI_VENDOR_ID_CIRRUS:	return "Cirrus Logic";
	      case PCI_VENDOR_ID_IBM:		return "IBM";
	      case PCI_VENDOR_ID_WD:		return "Western Digital";
	      case PCI_VENDOR_ID_AMD:		return "AMD";
	      case PCI_VENDOR_ID_TRIDENT:	return "Trident";
	      case PCI_VENDOR_ID_AI:		return "Acer Incorporated";
	      case PCI_VENDOR_ID_MATROX:	return "Matrox";
	      case PCI_VENDOR_ID_CT:		return "Chips & Technologies";
	      case PCI_VENDOR_ID_MIRO:		return "Miro";
	      case PCI_VENDOR_ID_NEC:		return "NEC";
	      case PCI_VENDOR_ID_FD:		return "Future Domain";
	      case PCI_VENDOR_ID_SI:		return "Silicon Integrated Systems";
	      case PCI_VENDOR_ID_HP:		return "Hewlett Packard";
	      case PCI_VENDOR_ID_PCTECH:	return "PCTECH";
	      case PCI_VENDOR_ID_DPT:		return "DPT";
	      case PCI_VENDOR_ID_OPTI:		return "OPTi";
	      case PCI_VENDOR_ID_SGS:		return "SGS Thomson";
	      case PCI_VENDOR_ID_BUSLOGIC:	return "BusLogic";
	      case PCI_VENDOR_ID_TI:		return "Texas Instruments";
	      case PCI_VENDOR_ID_OAK: 		return "OAK";
	      case PCI_VENDOR_ID_WINBOND2:	return "Winbond";
	      case PCI_VENDOR_ID_MOTOROLA:	return "Motorola";
	      case PCI_VENDOR_ID_PROMISE:	return "Promise Technology";
	      case PCI_VENDOR_ID_N9:		return "Number Nine";
	      case PCI_VENDOR_ID_UMC:		return "UMC";
	      case PCI_VENDOR_ID_X:		return "X TECHNOLOGY";
	      case PCI_VENDOR_ID_PICOP:		return "PicoPower";
	      case PCI_VENDOR_ID_APPLE:		return "Apple";
	      case PCI_VENDOR_ID_NEXGEN:	return "Nexgen";
	      case PCI_VENDOR_ID_QLOGIC:	return "Q Logic";
	      case PCI_VENDOR_ID_LEADTEK:	return "Leadtek Research";
	      case PCI_VENDOR_ID_CONTAQ:	return "Contaq";
	      case PCI_VENDOR_ID_FOREX:		return "Forex";
	      case PCI_VENDOR_ID_OLICOM:	return "Olicom";
	      case PCI_VENDOR_ID_SUN:		return "Sun Microsystems";
	      case PCI_VENDOR_ID_CMD:		return "CMD";
	      case PCI_VENDOR_ID_VISION:	return "Vision";
	      case PCI_VENDOR_ID_BROOKTREE:	return "Brooktree";
	      case PCI_VENDOR_ID_SIERRA:	return "Sierra";
	      case PCI_VENDOR_ID_ACC:		return "ACC MICROELECTRONICS";
	      case PCI_VENDOR_ID_WINBOND:	return "Winbond";
	      case PCI_VENDOR_ID_DATABOOK:	return "Databook";
	      case PCI_VENDOR_ID_3COM:		return "3Com";
	      case PCI_VENDOR_ID_SMC:		return "SMC";
	      case PCI_VENDOR_ID_AL:		return "Acer Labs";
	      case PCI_VENDOR_ID_MITSUBISHI:	return "Mitsubishi";
	      case PCI_VENDOR_ID_NEOMAGIC:	return "Neomagic";
	      case PCI_VENDOR_ID_ASP:		return "Advanced System Products";
	      case PCI_VENDOR_ID_CERN:		return "CERN";
	      case PCI_VENDOR_ID_IMS:		return "IMS";
	      case PCI_VENDOR_ID_TEKRAM2:	return "Tekram";
	      case PCI_VENDOR_ID_TUNDRA:	return "Tundra";
	      case PCI_VENDOR_ID_AMCC:		return "AMCC";
	      case PCI_VENDOR_ID_INTERG:	return "Intergraphics";
	      case PCI_VENDOR_ID_REALTEK:	return "Realtek";
	      case PCI_VENDOR_ID_TRUEVISION:	return "Truevision";
	      case PCI_VENDOR_ID_INIT:		return "Initio Corp";
	      case PCI_VENDOR_ID_VIA:		return "VIA Technologies";
	      case PCI_VENDOR_ID_VORTEX:	return "VORTEX";
	      case PCI_VENDOR_ID_EF:		return "Efficient Networks";
	      case PCI_VENDOR_ID_FORE:		return "Fore Systems";
	      case PCI_VENDOR_ID_IMAGINGTECH:	return "Imaging Technology";
	      case PCI_VENDOR_ID_PHILIPS:	return "Philips";
	      case PCI_VENDOR_ID_PLX:		return "PLX";
	      case PCI_VENDOR_ID_ALLIANCE:	return "Alliance";
	      case PCI_VENDOR_ID_VMIC:		return "VMIC";
	      case PCI_VENDOR_ID_DIGI:		return "Digi Intl.";
	      case PCI_VENDOR_ID_MUTECH:	return "Mutech";
	      case PCI_VENDOR_ID_RENDITION:	return "Rendition";
	      case PCI_VENDOR_ID_TOSHIBA:	return "Toshiba";
	      case PCI_VENDOR_ID_RICOH:		return "Ricoh";
	      case PCI_VENDOR_ID_ZEITNET:	return "ZeitNet";
	      case PCI_VENDOR_ID_OMEGA:		return "Omega Micro";
	      case PCI_VENDOR_ID_NP:		return "Network Peripherals";
	      case PCI_VENDOR_ID_SPECIALIX:	return "Specialix";
	      case PCI_VENDOR_ID_IKON:		return "Ikon";
	      case PCI_VENDOR_ID_ZORAN:		return "Zoran";
	      case PCI_VENDOR_ID_COMPEX:	return "Compex";
	      case PCI_VENDOR_ID_RP:		return "Comtrol";
	      case PCI_VENDOR_ID_CYCLADES:	return "Cyclades";
	      case PCI_VENDOR_ID_3DFX:		return "3Dfx";
	      case PCI_VENDOR_ID_SIGMADES:	return "Sigma Designs";
	      case PCI_VENDOR_ID_OPTIBASE:	return "Optibase";
	      case PCI_VENDOR_ID_ENSONIQ:	return "Ensoniq";
	      case PCI_VENDOR_ID_PICTUREL:	return "Picture Elements";
	      case PCI_VENDOR_ID_NVIDIA:	return "NVidia/SGS Thomson";
	      case PCI_VENDOR_ID_SYMPHONY:	return "Symphony";
	      case PCI_VENDOR_ID_TEKRAM:	return "Tekram";
	      case PCI_VENDOR_ID_3DLABS:	return "3Dlabs";
	      case PCI_VENDOR_ID_AVANCE:	return "Avance";
	      case PCI_VENDOR_ID_NETVIN:	return "NetVin";
	      case PCI_VENDOR_ID_S3:		return "S3 Inc.";
	      case PCI_VENDOR_ID_INTEL:		return "Intel";
	      case PCI_VENDOR_ID_KTI:		return "KTI";
	      case PCI_VENDOR_ID_ADAPTEC:	return "Adaptec";
	      case PCI_VENDOR_ID_ATRONICS:	return "Atronics";
	      case PCI_VENDOR_ID_ARK:		return "ARK Logic";
	      default:				return "Unknown vendor";
	}
}


const char *pci_strdev(unsigned int vendor, unsigned int device)
{
	struct pci_dev_info *info;

	info = 	pci_lookup_dev(vendor, device);
	return info ? info->name : "Unknown device";
}


const char *pcibios_strerror(int error)
{
	static char buf[32];

	switch (error) {
		case PCIBIOS_SUCCESSFUL:
		case PCIBIOS_BAD_VENDOR_ID:
			return "SUCCESSFUL";

		case PCIBIOS_FUNC_NOT_SUPPORTED:
			return "FUNC_NOT_SUPPORTED";

		case PCIBIOS_DEVICE_NOT_FOUND:
			return "DEVICE_NOT_FOUND";

		case PCIBIOS_BAD_REGISTER_NUMBER:
			return "BAD_REGISTER_NUMBER";

		case PCIBIOS_SET_FAILED:
			return "SET_FAILED";

		case PCIBIOS_BUFFER_TOO_SMALL:
			return "BUFFER_TOO_SMALL";

		default:
			sprintf (buf, "PCI ERROR 0x%x", error);
			return buf;
	}
}


/*
 * Turn on/off PCI bridge optimization. This should allow benchmarking.
 */
__initfunc(static void burst_bridge(unsigned char bus, unsigned char devfn,
				    unsigned char pos, int turn_on))
{
#ifdef CONFIG_PCI_OPTIMIZE
	struct bridge_mapping_type *bmap;
	unsigned char val;
	int i;

	pos *= NUM_OPTIMIZATIONS;
	printk("PCI bridge optimization.\n");
	for (i = 0; i < NUM_OPTIMIZATIONS; i++) {
		printk("    %s: ", bridge_optimization[i].type);
		bmap = &bridge_mapping[pos + i];
		if (!bmap->addr) {
			printk("Not supported.");
		} else {
			pcibios_read_config_byte(bus, devfn, bmap->addr, &val);
			if ((val & bmap->mask) == bmap->value) {
				printk("%s.", bridge_optimization[i].on);
				if (!turn_on) {
					pcibios_write_config_byte(bus, devfn,
								  bmap->addr,
								  (val | bmap->mask)
								  - bmap->value);
					printk("Changed!  Now %s.", bridge_optimization[i].off);
				}
			} else {
				printk("%s.", bridge_optimization[i].off);
				if (turn_on) {
					pcibios_write_config_byte(bus, devfn,
								  bmap->addr,
								  (val & (0xff - bmap->mask))
								  + bmap->value);
					printk("Changed!  Now %s.", bridge_optimization[i].on);
				}
			}
		}
		printk("\n");
	}
#endif /* CONFIG_PCI_OPTIMIZE */
}


/*
 * Convert some of the configuration space registers of the device at
 * address (bus,devfn) into a string (possibly several lines each).
 * The configuration string is stored starting at buf[len].  If the
 * string would exceed the size of the buffer (SIZE), 0 is returned.
 */
static int sprint_dev_config(struct pci_dev *dev, char *buf, int size)
{
	unsigned long base;
	unsigned int l, class_rev, bus, devfn;
	unsigned short vendor, device, status;
	unsigned char bist, latency, min_gnt, max_lat;
	int reg, len = 0;
	const char *str;

	bus   = dev->bus->number;
	devfn = dev->devfn;

	pcibios_read_config_dword(bus, devfn, PCI_CLASS_REVISION, &class_rev);
	pcibios_read_config_word (bus, devfn, PCI_VENDOR_ID, &vendor);
	pcibios_read_config_word (bus, devfn, PCI_DEVICE_ID, &device);
	pcibios_read_config_word (bus, devfn, PCI_STATUS, &status);
	pcibios_read_config_byte (bus, devfn, PCI_BIST, &bist);
	pcibios_read_config_byte (bus, devfn, PCI_LATENCY_TIMER, &latency);
	pcibios_read_config_byte (bus, devfn, PCI_MIN_GNT, &min_gnt);
	pcibios_read_config_byte (bus, devfn, PCI_MAX_LAT, &max_lat);
	if (len + 80 > size) {
		return -1;
	}
	len += sprintf(buf + len, "  Bus %2d, device %3d, function %2d:\n",
		       bus, PCI_SLOT(devfn), PCI_FUNC(devfn));

	if (len + 80 > size) {
		return -1;
	}
	len += sprintf(buf + len, "    %s: %s %s (rev %d).\n      ",
		       pci_strclass(class_rev >> 8), pci_strvendor(vendor),
		       pci_strdev(vendor, device), class_rev & 0xff);

	if (!pci_lookup_dev(vendor, device)) {
		len += sprintf(buf + len,
			       "Vendor id=%x. Device id=%x.\n      ",
			       vendor, device);
	}

	str = 0;	/* to keep gcc shut... */
	switch (status & PCI_STATUS_DEVSEL_MASK) {
	      case PCI_STATUS_DEVSEL_FAST:   str = "Fast devsel.  "; break;
	      case PCI_STATUS_DEVSEL_MEDIUM: str = "Medium devsel.  "; break;
	      case PCI_STATUS_DEVSEL_SLOW:   str = "Slow devsel.  "; break;
	}
	if (len + strlen(str) > size) {
		return -1;
	}
	len += sprintf(buf + len, str);

	if (status & PCI_STATUS_FAST_BACK) {
#		define fast_b2b_capable	"Fast back-to-back capable.  "
		if (len + strlen(fast_b2b_capable) > size) {
			return -1;
		}
		len += sprintf(buf + len, fast_b2b_capable);
#		undef fast_b2b_capable
	}

	if (bist & PCI_BIST_CAPABLE) {
#		define BIST_capable	"BIST capable.  "
		if (len + strlen(BIST_capable) > size) {
			return -1;
		}
		len += sprintf(buf + len, BIST_capable);
#		undef BIST_capable
	}

	if (dev->irq) {
		if (len + 40 > size) {
			return -1;
		}
		len += sprintf(buf + len, "IRQ %x.  ", dev->irq);
	}

	if (dev->master) {
		if (len + 80 > size) {
			return -1;
		}
		len += sprintf(buf + len, "Master Capable.  ");
		if (latency)
		  len += sprintf(buf + len, "Latency=%d.  ", latency);
		else
		  len += sprintf(buf + len, "No bursts.  ");
		if (min_gnt)
		  len += sprintf(buf + len, "Min Gnt=%d.", min_gnt);
		if (max_lat)
		  len += sprintf(buf + len, "Max Lat=%d.", max_lat);
	}

	for (reg = 0; reg < 6; reg++) {
		if (len + 40 > size) {
			return -1;
		}
		pcibios_read_config_dword(bus, devfn,
				PCI_BASE_ADDRESS_0 + (reg << 2), &l);
		if (l == 0xffffffff)
			base = 0;
		else
			base = l;
		if (!base)
			continue;

		if (base & PCI_BASE_ADDRESS_SPACE_IO) {
			len += sprintf(buf + len,
				       "\n      I/O at 0x%lx [0x%lx].",
				       base & PCI_BASE_ADDRESS_IO_MASK,
				       dev->base_address[reg]);
		} else {
			const char *pref, *type = "unknown";

			if (base & PCI_BASE_ADDRESS_MEM_PREFETCH) {
				pref = "P";
			} else {
				pref = "Non-p";
			}
			switch (base & PCI_BASE_ADDRESS_MEM_TYPE_MASK) {
			      case PCI_BASE_ADDRESS_MEM_TYPE_32:
				type = "32 bit"; break;
			      case PCI_BASE_ADDRESS_MEM_TYPE_1M:
				type = "20 bit"; break;
			      case PCI_BASE_ADDRESS_MEM_TYPE_64:
				type = "64 bit";
				/* read top 32 bit address of base addr: */
				reg += 4;
				pcibios_read_config_dword(bus, devfn, reg, &l);
				base |= ((u64) l) << 32;
				break;
			}
			len += sprintf(buf + len,
				       "\n      %srefetchable %s memory at "
				       "0x%lx [0x%lx].", pref, type,
				       base & PCI_BASE_ADDRESS_MEM_MASK,
				       dev->base_address[reg]);
		}
	}

	len += sprintf(buf + len, "\n");
	return len;
}


/*
 * Return list of PCI devices as a character string for /proc/pci.
 * BUF is a buffer that is PAGE_SIZE bytes long.
 */
int get_pci_list(char *buf)
{
	int nprinted, len, size;
	struct pci_dev *dev;
#	define MSG "\nwarning: page-size limit reached!\n"

	/* reserve same for truncation warning message: */
	size  = PAGE_SIZE - (strlen(MSG) + 1);
	len   = sprintf(buf, "PCI devices found:\n");

	for (dev = pci_devices; dev; dev = dev->next) {
		nprinted = sprint_dev_config(dev, buf + len, size - len);
		if (nprinted < 0) {
			return len + sprintf(buf + len, MSG);
		}
		len += nprinted;
	}
	return len;
}


/*
 * pci_malloc() returns initialized memory of size SIZE.  Can be
 * used only while pci_init() is active.
 */
__initfunc(static void *pci_malloc(long size, unsigned long *mem_startp))
{
	void *mem;

#ifdef DEBUG
	printk("...pci_malloc(size=%ld,mem=%p)", size, (void *)*mem_startp);
#endif
	mem = (void*) *mem_startp;
	*mem_startp += (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
	memset(mem, 0, size);
	return mem;
}


unsigned int pci_scan_bus(struct pci_bus *bus, unsigned long *mem_startp)
{
	unsigned int devfn, l, max;
	unsigned char cmd, tmp, irq, hdr_type = 0;
	struct pci_dev_info *info;
	struct pci_dev *dev;
	struct pci_bus *child;
	int reg;

#ifdef DEBUG
	printk("...pci_scan_bus(busno=%d,mem=%p)\n", bus->number,
	       (void *)*mem_startp);
#endif

	max = bus->secondary;
	for (devfn = 0; devfn < 0xff; ++devfn) {
		if (PCI_FUNC(devfn) == 0) {
			pcibios_read_config_byte(bus->number, devfn,
						 PCI_HEADER_TYPE, &hdr_type);
		} else if (!(hdr_type & 0x80)) {
			/* not a multi-function device */
			continue;
		}

		pcibios_read_config_dword(bus->number, devfn, PCI_VENDOR_ID,
					  &l);
		/* some broken boards return 0 if a slot is empty: */
		if (l == 0xffffffff || l == 0x00000000) {
			hdr_type = 0;
			continue;
		}

		dev = pci_malloc(sizeof(*dev), mem_startp);
		dev->bus = bus;
		/*
		 * Put it into the simple chain of devices on this
		 * bus.  It is used to find devices once everything is
		 * set up.
		 */
		dev->next = pci_devices;
		pci_devices = dev;

		dev->devfn  = devfn;
		dev->vendor = l & 0xffff;
		dev->device = (l >> 16) & 0xffff;

		/*
		 * Check to see if we know about this device and report
		 * a message at boot time.  This is the only way to
		 * learn about new hardware...
		 */
		info = pci_lookup_dev(dev->vendor, dev->device);
		if (!info) {
			printk("PCI: Warning: Unknown PCI device (%x:%x).  Please read include/linux/pci.h\n",
				dev->vendor, dev->device);
		} else {
			/* Some BIOS' are lazy. Let's do their job: */
			if (info->bridge_type != 0xff) {
				burst_bridge(bus->number, devfn,
					     info->bridge_type, 1);
			}
		}

		/* non-destructively determine if device can be a master: */
		pcibios_read_config_byte(bus->number, devfn, PCI_COMMAND,
					 &cmd);
		pcibios_write_config_byte(bus->number, devfn, PCI_COMMAND,
					  cmd | PCI_COMMAND_MASTER);
		pcibios_read_config_byte(bus->number, devfn, PCI_COMMAND,
					 &tmp);
		dev->master = ((tmp & PCI_COMMAND_MASTER) != 0);
		pcibios_write_config_byte(bus->number, devfn, PCI_COMMAND,
					  cmd);

		/* read irq level (may be changed during pcibios_fixup()): */
		pcibios_read_config_byte(bus->number, devfn,
					 PCI_INTERRUPT_LINE, &irq);
		dev->irq = irq;

		/* read base address registers, again pcibios_fixup() can
		 * tweak these
		 */
		for (reg = 0; reg < 6; reg++) {
			pcibios_read_config_dword(bus->number, devfn,
					PCI_BASE_ADDRESS_0 + (reg << 2), &l);
			if (l == 0xffffffff)
				dev->base_address[reg] = 0;
			else
				dev->base_address[reg] = l;
		}

		/* check to see if this device is a PCI-PCI bridge: */
		pcibios_read_config_dword(bus->number, devfn,
					  PCI_CLASS_REVISION, &l);
		l = l >> 8;			/* upper 3 bytes */
		dev->class = l;
		/*
		 * Now insert it into the list of devices held
		 * by the parent bus.
		 */
		dev->sibling = bus->devices;
		bus->devices = dev;

		if (dev->class >> 8 == PCI_CLASS_BRIDGE_PCI) {
			unsigned int buses;
			unsigned short cr;

			/*
			 * Insert it into the tree of buses.
			 */
			child = pci_malloc(sizeof(*child), mem_startp);
			child->next   = bus->children;
			bus->children = child;
			child->self = dev;
			child->parent = bus;

			/*
			 * Set up the primary, secondary and subordinate
			 * bus numbers.
			 */
			child->number = child->secondary = ++max;
			child->primary = bus->secondary;
			child->subordinate = 0xff;
			/*
			 * Clear all status bits and turn off memory,
			 * I/O and master enables.
			 */
			pcibios_read_config_word(bus->number, devfn,
						  PCI_COMMAND, &cr);
			pcibios_write_config_word(bus->number, devfn,
						  PCI_COMMAND, 0x0000);
			pcibios_write_config_word(bus->number, devfn,
						  PCI_STATUS, 0xffff);
			/*
			 * Read the existing primary/secondary/subordinate bus
			 * number configuration to determine if the PCI bridge
			 * has already been configured by the system.  If so,
			 * do not modify the configuration, merely note it.
			 */
			pcibios_read_config_dword(bus->number, devfn, 0x18,
						  &buses);
			if ((buses & 0xFFFFFF) != 0)
			  {
			    child->primary = buses & 0xFF;
			    child->secondary = (buses >> 8) & 0xFF;
			    child->subordinate = (buses >> 16) & 0xFF;
			    child->number = child->secondary;
			    max = pci_scan_bus(child, mem_startp);
			  }
			else
			  {
			    /*
			     * Configure the bus numbers for this bridge:
			     */
			    buses &= 0xff000000;
			    buses |=
			      (((unsigned int)(child->primary)     <<  0) |
			       ((unsigned int)(child->secondary)   <<  8) |
			       ((unsigned int)(child->subordinate) << 16));
			    pcibios_write_config_dword(bus->number, devfn, 0x18,
						       buses);
			    /*
			     * Now we can scan all subordinate buses:
			     */
			    max = pci_scan_bus(child, mem_startp);
			    /*
			     * Set the subordinate bus number to its real
			     * value:
			     */
			    child->subordinate = max;
			    buses = (buses & 0xff00ffff)
			      | ((unsigned int)(child->subordinate) << 16);
			    pcibios_write_config_dword(bus->number, devfn, 0x18,
						       buses);
			  }
			pcibios_write_config_word(bus->number, devfn,
						  PCI_COMMAND, cr);
		}
	}
	/*
	 * We've scanned the bus and so we know all about what's on
	 * the other side of any bridges that may be on this bus plus
	 * any devices.
	 *
	 * Return how far we've got finding sub-buses.
	 */
	return max;
}


__initfunc(unsigned long pci_init (unsigned long mem_start, unsigned long mem_end))
{
	mem_start = pcibios_init(mem_start, mem_end);

	if (!pcibios_present()) {
		printk("PCI: No PCI bus detected\n");
		return mem_start;
	}

	printk("Probing PCI hardware.\n");

	memset(&pci_root, 0, sizeof(pci_root));
	pci_root.subordinate = pci_scan_bus(&pci_root, &mem_start);

	/* give BIOS a chance to apply platform specific fixes: */
	mem_start = pcibios_fixup(mem_start, mem_end);

#ifdef DEBUG
	{
		int len = get_pci_list((char*)mem_start);
		if (len) {
			((char *) mem_start)[len] = '\0';
			printk("%s\n", (char *) mem_start);
		}
	}
#endif
	return mem_start;
}
