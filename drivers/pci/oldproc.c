/*
 *	$Id: oldproc.c,v 1.24 1998/10/11 15:13:04 mj Exp $
 *
 *	Backward-compatible procfs interface for PCI.
 *
 *	Copyright 1993, 1994, 1995, 1997 Drew Eckhardt, Frederic Potter,
 *	David Mosberger-Tang, Martin Mares
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <asm/page.h>

#ifdef CONFIG_PROC_FS

struct pci_dev_info {
	unsigned short	vendor;		/* vendor id */
	unsigned short	device;		/* device id */

	const char	*name;		/* device name */
};

#define DEVICE(vid,did,name) \
  {PCI_VENDOR_ID_##vid, PCI_DEVICE_ID_##did, (name)}

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
	DEVICE( NCR,		NCR_53C875J,	"53c875J"),
	DEVICE( ATI,		ATI_68800,      "68800AX"),
	DEVICE( ATI,		ATI_215CT222,   "215CT222"),
	DEVICE( ATI,		ATI_210888CX,   "210888CX"),
	DEVICE( ATI,		ATI_215GB,	"Mach64 GB"),
	DEVICE( ATI,		ATI_215GD,	"Mach64 GD (Rage Pro)"),
	DEVICE( ATI,		ATI_215GI,	"Mach64 GI (Rage Pro)"),
	DEVICE( ATI,		ATI_215GP,	"Mach64 GP (Rage Pro)"),
	DEVICE( ATI,		ATI_215GQ,	"Mach64 GQ (Rage Pro)"),
	DEVICE( ATI,		ATI_215GT,	"Mach64 GT (Rage II)"),
	DEVICE( ATI,		ATI_215GTB,	"Mach64 GT (Rage II)"),
	DEVICE( ATI,		ATI_210888GX,   "210888GX"),
	DEVICE( ATI,		ATI_215LG,	"Mach64 LG (Rage Pro)"),
	DEVICE( ATI,		ATI_264LT,	"Mach64 LT"),
	DEVICE( ATI,		ATI_264VT,	"Mach64 VT"),
	DEVICE( VLSI,		VLSI_82C592,	"82C592-FC1"),
	DEVICE( VLSI,		VLSI_82C593,	"82C593-FC1"),
	DEVICE( VLSI,		VLSI_82C594,	"82C594-AFC2"),
	DEVICE( VLSI,		VLSI_82C597,	"82C597-AFC2"),
	DEVICE( VLSI,		VLSI_82C541,	"82C541 Lynx"),
	DEVICE( VLSI,		VLSI_82C543,	"82C543 Lynx ISA"),
	DEVICE( VLSI,		VLSI_82C532,	"82C532"),
	DEVICE( VLSI,		VLSI_82C534,	"82C534"),
	DEVICE( VLSI,		VLSI_82C535,	"82C535"),
	DEVICE( VLSI,		VLSI_82C147,	"82C147"),
	DEVICE( VLSI,		VLSI_VAS96011,	"VAS96011 (Golden Gate II)"),
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
	DEVICE( DEC,		DEC_BRD,	"DC21050"),
	DEVICE( DEC,		DEC_TULIP,	"DC21040"),
	DEVICE( DEC,		DEC_TGA,	"TGA"),
	DEVICE( DEC,		DEC_TULIP_FAST,	"DC21140"),
	DEVICE( DEC,		DEC_TGA2,	"TGA2"),
	DEVICE( DEC,		DEC_FDDI,	"DEFPA"),
	DEVICE( DEC,		DEC_TULIP_PLUS,	"DC21041"),
	DEVICE( DEC,		DEC_21142,	"DC21142"),
	DEVICE( DEC,		DEC_21052,	"DC21052"),
	DEVICE( DEC,		DEC_21150,	"DC21150"),
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
	DEVICE( IBM,		IBM_TR,		"Token Ring"),
	DEVICE( IBM,		IBM_82G2675,	"82G2675"),
	DEVICE( IBM,		IBM_MCA,	"MicroChannel"),
	DEVICE( IBM,		IBM_82351,	"82351"),
	DEVICE( IBM,		IBM_SERVERAID,	"ServeRAID"),
	DEVICE( IBM,		IBM_TR_WAKE,	"Wake On LAN Token Ring"),
	DEVICE( IBM,		IBM_MPIC,	"MPIC-2 Interrupt Controller"),
	DEVICE( IBM,		IBM_3780IDSP,	"MWave DSP"),
	DEVICE( IBM,		IBM_MPIC_2,	"MPIC-2 ASIC Interrupt Controller"),
	DEVICE( WD,		WD_7197,	"WD 7197"),
	DEVICE( AMD,		AMD_LANCE,	"79C970"),
	DEVICE( AMD,		AMD_SCSI,	"53C974"),
	DEVICE( TRIDENT,	TRIDENT_9397,	"Cyber9397"),
	DEVICE( TRIDENT,	TRIDENT_9420,	"TG 9420"),
	DEVICE( TRIDENT,	TRIDENT_9440,	"TG 9440"),
	DEVICE( TRIDENT,	TRIDENT_9660,	"TG 9660 / Cyber9385"),
	DEVICE( TRIDENT,	TRIDENT_9750,	"Image 975"),
	DEVICE( AI,		AI_M1435,	"M1435"),
	DEVICE( MATROX,		MATROX_MGA_2,	"Atlas PX2085"),
	DEVICE( MATROX,		MATROX_MIL,	"Millennium"),
	DEVICE( MATROX,		MATROX_MYS,	"Mystique"),
	DEVICE( MATROX,		MATROX_MIL_2,	"Millennium II"),
	DEVICE( MATROX,		MATROX_MIL_2_AGP,"Millennium II AGP"),
	DEVICE( MATROX,         MATROX_G200_PCI,"Matrox G200 PCI"),
	DEVICE( MATROX,         MATROX_G200_AGP,"Matrox G200 AGP"),
	DEVICE( MATROX,		MATROX_MGA_IMP,	"MGA Impression"),
	DEVICE( MATROX,         MATROX_G100_MM, "Matrox G100 multi monitor"),
	DEVICE( MATROX,         MATROX_G100_AGP,"Matrox G100 AGP"),
	DEVICE( CT,		CT_65545,	"65545"),
	DEVICE( CT,		CT_65548,	"65548"),
	DEVICE(	CT,		CT_65550,	"65550"),
	DEVICE( CT,		CT_65554,	"65554"),
	DEVICE( CT,		CT_65555,	"65555"),
	DEVICE( MIRO,		MIRO_36050,	"ZR36050"),
	DEVICE( NEC,		NEC_PCX2,	"PowerVR PCX2"),
	DEVICE( FD,		FD_36C70,	"TMC-18C30"),
	DEVICE( SI,		SI_5591_AGP,	"5591/5592 AGP"),
	DEVICE( SI,		SI_6202,	"6202"),
	DEVICE( SI,		SI_503,		"85C503"),
	DEVICE( SI,		SI_ACPI,	"ACPI"),
	DEVICE( SI,		SI_5597_VGA,	"5597/5598 VGA"),
	DEVICE( SI,		SI_6205,	"6205"),
	DEVICE( SI,		SI_501,		"85C501"),
	DEVICE( SI,		SI_496,		"85C496"),
	DEVICE( SI,		SI_601,		"85C601"),
	DEVICE( SI,		SI_5107,	"5107"),
	DEVICE( SI,		SI_5511,       	"85C5511"),
	DEVICE( SI,		SI_5513,	"85C5513"),
	DEVICE( SI,		SI_5571,	"5571"),
	DEVICE( SI,		SI_5591,	"5591/5592 Host"),
	DEVICE( SI,		SI_5597,	"5597/5598 Host"),
	DEVICE( SI,		SI_7001,	"7001 USB"),
	DEVICE( HP,		HP_J2585A,	"J2585A"),
	DEVICE( HP,		HP_J2585B,	"J2585B (Lassen)"),
	DEVICE( PCTECH,		PCTECH_RZ1000,  "RZ1000 (buggy)"),
	DEVICE( PCTECH,		PCTECH_RZ1001,  "RZ1001 (buggy?)"),
	DEVICE( PCTECH,		PCTECH_SAMURAI_0,"Samurai 0"),
	DEVICE( PCTECH,		PCTECH_SAMURAI_1,"Samurai 1"),
	DEVICE( PCTECH,		PCTECH_SAMURAI_IDE,"Samurai IDE"),
	DEVICE( DPT,		DPT,		"SmartCache/Raid"),
	DEVICE( OPTI,		OPTI_92C178,	"92C178"),
	DEVICE( OPTI,		OPTI_82C557,	"82C557 Viper-M"),
	DEVICE( OPTI,		OPTI_82C558,	"82C558 Viper-M ISA+IDE"),
	DEVICE( OPTI,		OPTI_82C621,	"82C621"),
	DEVICE( OPTI,		OPTI_82C700,	"82C700"),
	DEVICE( OPTI,		OPTI_82C701,	"82C701 FireStar Plus"),
	DEVICE( OPTI,		OPTI_82C814,	"82C814 Firebridge 1"),
	DEVICE( OPTI,		OPTI_82C822,	"82C822"),
	DEVICE( OPTI,		OPTI_82C825,	"82C825 Firebridge 2"),
	DEVICE( SGS,		SGS_2000,	"STG 2000X"),
	DEVICE( SGS,		SGS_1764,	"STG 1764X"),
	DEVICE( BUSLOGIC,	BUSLOGIC_MULTIMASTER_NC, "MultiMaster NC"),
	DEVICE( BUSLOGIC,	BUSLOGIC_MULTIMASTER,    "MultiMaster"),
	DEVICE( BUSLOGIC,	BUSLOGIC_FLASHPOINT,     "FlashPoint"),
	DEVICE( TI,		TI_TVP4010,	"TVP4010 Permedia"),
	DEVICE( TI,		TI_TVP4020,	"TVP4020 Permedia 2"),
	DEVICE( TI,		TI_PCI1130,	"PCI1130"),
	DEVICE( TI,		TI_PCI1131,	"PCI1131"),
	DEVICE( TI,		TI_PCI1250,	"PCI1250"),
	DEVICE( OAK,		OAK_OTI107,	"OTI107"),
	DEVICE( WINBOND2,	WINBOND2_89C940,"NE2000-PCI"),
	DEVICE( MOTOROLA,	MOTOROLA_MPC105,"MPC105 Eagle"),
	DEVICE( MOTOROLA,	MOTOROLA_MPC106,"MPC106 Grackle"),
	DEVICE( MOTOROLA,	MOTOROLA_RAVEN,	"Raven"),
	DEVICE( PROMISE,        PROMISE_20246,	"IDE UltraDMA/33"),
	DEVICE( PROMISE,	PROMISE_5300,	"DC5030"),
	DEVICE( N9,		N9_I128,	"Imagine 128"),
	DEVICE( N9,		N9_I128_2,	"Imagine 128v2"),
	DEVICE( N9,		N9_I128_T2R,	"Revolution 3D"),
	DEVICE( UMC,		UMC_UM8673F,	"UM8673F"),
	DEVICE( UMC,		UMC_UM8891A,	"UM8891A"),
	DEVICE( UMC,		UMC_UM8886BF,	"UM8886BF"),
	DEVICE( UMC,		UMC_UM8886A,	"UM8886A"),
	DEVICE( UMC,		UMC_UM8881F,	"UM8881F"),
	DEVICE( UMC,		UMC_UM8886F,	"UM8886F"),
	DEVICE( UMC,		UMC_UM9017F,	"UM9017F"),
	DEVICE( UMC,		UMC_UM8886N,	"UM8886N"),
	DEVICE( UMC,		UMC_UM8891N,	"UM8891N"),
	DEVICE( X,		X_AGX016,	"ITT AGX016"),
	DEVICE( PICOP,		PICOP_PT86C52X,	"PT86C52x Vesuvius"),
	DEVICE( PICOP,		PICOP_PT80C524,	"PT80C524 Nile"),
	DEVICE( APPLE,		APPLE_BANDIT,	"Bandit"),
	DEVICE( APPLE,		APPLE_GC,	"Grand Central"),
	DEVICE( APPLE,		APPLE_HYDRA,	"Hydra"),
	DEVICE( NEXGEN,		NEXGEN_82C501,	"82C501"),
	DEVICE( QLOGIC,		QLOGIC_ISP1020,	"ISP1020"),
	DEVICE( QLOGIC,		QLOGIC_ISP1022,	"ISP1022"),
	DEVICE( CYRIX,		CYRIX_5510,	"5510"),
	DEVICE( CYRIX,		CYRIX_PCI_MASTER,"PCI Master"),
	DEVICE( CYRIX,		CYRIX_5520,	"5520"),
	DEVICE( CYRIX,		CYRIX_5530_LEGACY,"5530 Kahlua Legacy"),
	DEVICE( CYRIX,		CYRIX_5530_SMI,	"5530 Kahlua SMI"),
	DEVICE( CYRIX,		CYRIX_5530_IDE,	"5530 Kahlua IDE"),
	DEVICE( CYRIX,		CYRIX_5530_AUDIO,"5530 Kahlua Audio"),
	DEVICE( CYRIX,		CYRIX_5530_VIDEO,"5530 Kahlua Video"),
	DEVICE( LEADTEK,	LEADTEK_805,	"S3 805"),
	DEVICE( CONTAQ,		CONTAQ_82C599,	"82C599"),
	DEVICE( CONTAQ,		CONTAQ_82C693,	"82C693"),
	DEVICE( OLICOM,		OLICOM_OC3136,	"OC-3136/3137"),
	DEVICE( OLICOM,		OLICOM_OC2315,	"OC-2315"),
	DEVICE( OLICOM,		OLICOM_OC2325,	"OC-2325"),
	DEVICE( OLICOM,		OLICOM_OC2183,	"OC-2183/2185"),
	DEVICE( OLICOM,		OLICOM_OC2326,	"OC-2326"),
	DEVICE( OLICOM,		OLICOM_OC6151,	"OC-6151/6152"),
	DEVICE( SUN,		SUN_EBUS,	"PCI-EBus Bridge"),
	DEVICE( SUN,		SUN_HAPPYMEAL,	"Happy Meal Ethernet"),
	DEVICE( SUN,		SUN_SIMBA,	"Advanced PCI Bridge"),
	DEVICE( SUN,		SUN_PBM,	"PCI Bus Module"),
	DEVICE( SUN,		SUN_SABRE,	"Ultra IIi PCI"),
	DEVICE( CMD,		CMD_640,	"640 (buggy)"),
	DEVICE( CMD,		CMD_643,	"643"),
	DEVICE( CMD,		CMD_646,	"646"),
	DEVICE( CMD,		CMD_670,	"670"),
	DEVICE( VISION,		VISION_QD8500,	"QD-8500"),
	DEVICE( VISION,		VISION_QD8580,	"QD-8580"),
	DEVICE( BROOKTREE,	BROOKTREE_848,	"Bt848"),
	DEVICE( BROOKTREE,	BROOKTREE_849A,	"Bt849"),
	DEVICE( BROOKTREE,      BROOKTREE_878_1,"Bt878 2nd Contr. (?)"),
	DEVICE( BROOKTREE,      BROOKTREE_878,  "Bt878"),
	DEVICE( BROOKTREE,	BROOKTREE_8474,	"Bt8474"),
	DEVICE( SIERRA,		SIERRA_STB,	"STB Horizon 64"),
	DEVICE( ACC,		ACC_2056,	"2056"),
	DEVICE( WINBOND,	WINBOND_83769,	"W83769F"),
	DEVICE( WINBOND,	WINBOND_82C105,	"SL82C105"),
	DEVICE( WINBOND,	WINBOND_83C553,	"W83C553"),
	DEVICE( DATABOOK,      	DATABOOK_87144,	"DB87144"),
	DEVICE(	PLX,		PLX_9050,	"PCI9050 I2O"),
	DEVICE( PLX,		PLX_9080,	"PCI9080 I2O"),
	DEVICE( MADGE,		MADGE_MK2,	"Smart 16/4 BM Mk2 Ringnode"),
	DEVICE( MADGE,		MADGE_C155S,	"Collage 155 Server"),
	DEVICE( 3COM,		3COM_3C339,	"3C339 TokenRing"),
	DEVICE( 3COM,		3COM_3C590,	"3C590 10bT"),
	DEVICE( 3COM,		3COM_3C595TX,	"3C595 100bTX"),
	DEVICE( 3COM,		3COM_3C595T4,	"3C595 100bT4"),
	DEVICE( 3COM,		3COM_3C595MII,	"3C595 100b-MII"),
	DEVICE( 3COM,		3COM_3C900TPO,	"3C900 10bTPO"),
	DEVICE( 3COM,		3COM_3C900COMBO,"3C900 10b Combo"),
	DEVICE( 3COM,		3COM_3C905TX,	"3C905 100bTX"),
	DEVICE( 3COM,		3COM_3C905T4,	"3C905 100bT4"),
	DEVICE( 3COM,		3COM_3C905B_TX,	"3C905B 100bTX"),
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
	DEVICE( AL,		AL_M3307,	"M3307 MPEG-1 decoder"),
	DEVICE( AL,		AL_M4803,	"M4803"),
	DEVICE( AL,		AL_M5219,	"M5219"),
	DEVICE( AL,		AL_M5229,	"M5229 TXpro"),
	DEVICE( AL,		AL_M5237,	"M5237 USB"),
	DEVICE( SURECOM,	SURECOM_NE34,	"NE-34PCI LAN"),
	DEVICE( NEOMAGIC,       NEOMAGIC_MAGICGRAPH_NM2070,     "Magicgraph NM2070"),
	DEVICE( NEOMAGIC,	NEOMAGIC_MAGICGRAPH_128V, "MagicGraph 128V"),
	DEVICE( NEOMAGIC,	NEOMAGIC_MAGICGRAPH_128ZV, "MagicGraph 128ZV"),
	DEVICE( NEOMAGIC,	NEOMAGIC_MAGICGRAPH_NM2160, "MagicGraph NM2160"),
	DEVICE( ASP,		ASP_ABP940,	"ABP940"),
	DEVICE( ASP,		ASP_ABP940U,	"ABP940U"),
	DEVICE( ASP,		ASP_ABP940UW,	"ABP940UW"),
	DEVICE( MACRONIX,	MACRONIX_MX98713,"MX98713"),
	DEVICE( MACRONIX,	MACRONIX_MX987x5,"MX98715 / MX98725"),
	DEVICE( CERN,		CERN_SPSB_PMC,	"STAR/RD24 SCI-PCI (PMC)"),
	DEVICE( CERN,		CERN_SPSB_PCI,	"STAR/RD24 SCI-PCI (PMC)"),
	DEVICE( CERN,		CERN_HIPPI_DST,	"HIPPI destination"),
	DEVICE( CERN,		CERN_HIPPI_SRC,	"HIPPI source"),
	DEVICE( IMS,		IMS_8849,	"8849"),
	DEVICE( TEKRAM2,	TEKRAM2_690c,	"DC690c"),
	DEVICE( TUNDRA,		TUNDRA_CA91C042,"CA91C042 Universe"),
	DEVICE( AMCC,		AMCC_MYRINET,	"Myrinet PCI (M2-PCI-32)"),
	DEVICE( AMCC,		AMCC_PARASTATION,"ParaStation Interface"),
	DEVICE( AMCC,		AMCC_S5933,	"S5933 PCI44"),
	DEVICE( AMCC,		AMCC_S5933_HEPC3,"S5933 Traquair HEPC3"),
	DEVICE( INTERG,		INTERG_1680,	"IGA-1680"),
	DEVICE( INTERG,         INTERG_1682,    "IGA-1682"),
	DEVICE( REALTEK,	REALTEK_8029,	"8029"),
	DEVICE( REALTEK,	REALTEK_8129,	"8129"),
	DEVICE( REALTEK,	REALTEK_8139,	"8139"),
	DEVICE( TRUEVISION,	TRUEVISION_T1000,"TARGA 1000"),
	DEVICE( INIT,		INIT_320P,	"320 P"),
	DEVICE( INIT,		INIT_360P,	"360 P"),
	DEVICE(	TTI,		TTI_HPT343,	"HPT343"),
	DEVICE( VIA,		VIA_82C505,	"VT 82C505"),
	DEVICE( VIA,		VIA_82C561,	"VT 82C561"),
	DEVICE( VIA,		VIA_82C586_1,	"VT 82C586 Apollo IDE"),
	DEVICE( VIA,		VIA_82C576,	"VT 82C576 3V"),
	DEVICE( VIA,		VIA_82C585,	"VT 82C585 Apollo VP1/VPX"),
	DEVICE( VIA,		VIA_82C586_0,	"VT 82C586 Apollo ISA"),
	DEVICE( VIA,		VIA_82C595,	"VT 82C595 Apollo VP2"),
	DEVICE( VIA,		VIA_82C597_0,	"VT 82C597 Apollo VP3"),
	DEVICE( VIA,		VIA_82C598_0,	"VT 82C598 Apollo MVP3"),
	DEVICE( VIA,		VIA_82C926,	"VT 82C926 Amazon"),
	DEVICE( VIA,		VIA_82C416,	"VT 82C416MV"),
	DEVICE( VIA,		VIA_82C595_97,	"VT 82C595 Apollo VP2/97"),
	DEVICE( VIA,		VIA_82C586_2,	"VT 82C586 Apollo USB"),
	DEVICE( VIA,		VIA_82C586_3,	"VT 82C586B Apollo ACPI"),
	DEVICE( VIA,		VIA_86C100A,	"VT 86C100A"),
	DEVICE( VIA,		VIA_82C597_1,	"VT 82C597 Apollo VP3 AGP"),
	DEVICE( VIA,		VIA_82C598_1,	"VT 82C598 Apollo MVP3 AGP"),
	DEVICE( SMC2,		SMC2_1211TX,	"1211 TX"),
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
	DEVICE( EF,		EF_ATM_FPGA,   	"155P-MF1 (FPGA)"),
	DEVICE( EF,		EF_ATM_ASIC,    "155P-MF1 (ASIC)"),
	DEVICE( FORE,		FORE_PCA200PC,  "PCA-200PC"),
	DEVICE( FORE,		FORE_PCA200E,	 "PCA-200E"),
	DEVICE( IMAGINGTECH,	IMAGINGTECH_ICPCI, "MVC IC-PCI"),
	DEVICE( PHILIPS,	PHILIPS_SAA7145,"SAA7145"),
	DEVICE( PHILIPS,	PHILIPS_SAA7146,"SAA7146"),
	DEVICE( CYCLONE,	CYCLONE_SDK,	"SDK"),
	DEVICE( ALLIANCE,	ALLIANCE_PROMOTIO, "Promotion-6410"),
	DEVICE( ALLIANCE,	ALLIANCE_PROVIDEO, "Provideo"),
	DEVICE( ALLIANCE,	ALLIANCE_AT24,	"AT24"),
	DEVICE( ALLIANCE,	ALLIANCE_AT3D,	"AT3D"),
	DEVICE( VMIC,		VMIC_VME,	"VMIVME-7587"),
	DEVICE( DIGI,		DIGI_EPC,	"AccelPort EPC"),
 	DEVICE( DIGI,		DIGI_RIGHTSWITCH, "RightSwitch SE-6"),
	DEVICE( DIGI,		DIGI_XEM,	"AccelPort Xem"),
	DEVICE( DIGI,		DIGI_XR,	"AccelPort Xr"),
	DEVICE( DIGI,		DIGI_CX,	"AccelPort C/X"),
	DEVICE( DIGI,		DIGI_XRJ,	"AccelPort Xr/J"),
	DEVICE( DIGI,		DIGI_EPCJ,	"AccelPort EPC/J"),
	DEVICE( DIGI,		DIGI_XR_920,	"AccelPort Xr 920"),
	DEVICE( MUTECH,		MUTECH_MV1000,	"MV-1000"),
	DEVICE( RENDITION,	RENDITION_VERITE,"Verite 1000"),
	DEVICE( RENDITION,	RENDITION_VERITE2100,"Verite 2100"),
	DEVICE( TOSHIBA,	TOSHIBA_601,	"Laptop"),
	DEVICE( TOSHIBA,	TOSHIBA_TOPIC95,"ToPIC95"),
	DEVICE( TOSHIBA,	TOSHIBA_TOPIC97,"ToPIC97"),
	DEVICE( RICOH,		RICOH_RL5C466,	"RL5C466"),
	DEVICE(	ARTOP,		ARTOP_ATP8400,	"ATP8400"),
	DEVICE( ARTOP,		ARTOP_ATP850UF,	"ATP850UF"),
	DEVICE( ZEITNET,	ZEITNET_1221,	"1221"),
	DEVICE( ZEITNET,	ZEITNET_1225,	"1225"),
	DEVICE( OMEGA,		OMEGA_82C092G,	"82C092G"),
	DEVICE( LITEON,		LITEON_LNE100TX,"LNE100TX"),
	DEVICE( NP,		NP_PCI_FDDI,	"NP-PCI"),       
	DEVICE( ATT,		ATT_L56XMF,	"L56xMF"),
	DEVICE( SPECIALIX,	SPECIALIX_IO8,	"IO8+/PCI"),
	DEVICE( SPECIALIX,	SPECIALIX_XIO,	"XIO/SIO host"),
	DEVICE( SPECIALIX,	SPECIALIX_RIO,	"RIO host"),
	DEVICE( AURAVISION,	AURAVISION_VXP524,"VXP524"),
	DEVICE( IKON,		IKON_10115,	"10115 Greensheet"),
	DEVICE( IKON,		IKON_10117,	"10117 Greensheet"),
	DEVICE( ZORAN,		ZORAN_36057,	"ZR36057"),
	DEVICE( ZORAN,		ZORAN_36120,	"ZR36120"),
	DEVICE( KINETIC,	KINETIC_2915,	"2915 CAMAC"),
	DEVICE( COMPEX,		COMPEX_ENET100VG4, "Readylink ENET100-VG4"),
	DEVICE( COMPEX,		COMPEX_RL2000,	"ReadyLink 2000"),
	DEVICE( RP,             RP32INTF,       "RocketPort 32 Intf"),
	DEVICE( RP,             RP8INTF,        "RocketPort 8 Intf"),
	DEVICE( RP,             RP16INTF,       "RocketPort 16 Intf"),
	DEVICE( RP, 		RP4QUAD,	"Rocketport 4 Quad"),
	DEVICE( RP,             RP8OCTA,        "RocketPort 8 Oct"),
	DEVICE( RP,             RP8J,	        "RocketPort 8 J"),
	DEVICE( RP,             RPP4,	        "RocketPort Plus 4 Quad"),
	DEVICE( RP,             RPP8,	        "RocketPort Plus 8 Oct"),
	DEVICE( RP,             RP8M,	        "RocketModem 8 J"),
	DEVICE( CYCLADES,	CYCLOM_Y_Lo,	"Cyclom-Y below 1Mbyte"),
	DEVICE( CYCLADES,	CYCLOM_Y_Hi,	"Cyclom-Y above 1Mbyte"),
	DEVICE( CYCLADES,	CYCLOM_Z_Lo,	"Cyclom-Z below 1Mbyte"),
	DEVICE( CYCLADES,	CYCLOM_Z_Hi,	"Cyclom-Z above 1Mbyte"),
	DEVICE( ESSENTIAL,	ESSENTIAL_ROADRUNNER,"Roadrunner serial HIPPI"),
	DEVICE( O2,		O2_6832,	"6832"),
	DEVICE( 3DFX,		3DFX_VOODOO,	"Voodoo"),
	DEVICE( 3DFX,		3DFX_VOODOO2,	"Voodoo2"),
	DEVICE( 3DFX,           3DFX_BANSHEE,   "Banshee"),
	DEVICE( SIGMADES,	SIGMADES_6425,	"REALmagic64/GX"),
	DEVICE( STALLION,	STALLION_ECHPCI832,"EasyConnection 8/32"),
	DEVICE( STALLION,	STALLION_ECHPCI864,"EasyConnection 8/64"),
	DEVICE( STALLION,	STALLION_EIOPCI,"EasyIO"),
	DEVICE( OPTIBASE,	OPTIBASE_FORGE,	"MPEG Forge"),
	DEVICE( OPTIBASE,	OPTIBASE_FUSION,"MPEG Fusion"),
	DEVICE( OPTIBASE,	OPTIBASE_VPLEX,	"VideoPlex"),
	DEVICE( OPTIBASE,	OPTIBASE_VPLEXCC,"VideoPlex CC"),
	DEVICE( OPTIBASE,	OPTIBASE_VQUEST,"VideoQuest"),
	DEVICE( SATSAGEM,	SATSAGEM_PCR2101,"PCR2101 DVB receiver"),
	DEVICE( SATSAGEM,	SATSAGEM_TELSATTURBO,"Telsat Turbo DVB"),
	DEVICE( HUGHES,		HUGHES_DIRECPC,	"DirecPC"),
	DEVICE( ENSONIQ,	ENSONIQ_AUDIOPCI,"AudioPCI"),
	DEVICE( ALTEON,		ALTEON_ACENIC,  "AceNIC"),
	DEVICE( PICTUREL,	PICTUREL_PCIVST,"PCIVST"),
	DEVICE( NVIDIA_SGS,	NVIDIA_SGS_RIVA128,	"Riva 128"),
	DEVICE( CBOARDS,	CBOARDS_DAS1602_16,"DAS1602/16"),
	DEVICE( SYMPHONY,	SYMPHONY_101,	"82C101"),
	DEVICE( TEKRAM,		TEKRAM_DC290,	"DC-290"),
	DEVICE( 3DLABS,		3DLABS_300SX,	"GLINT 300SX"),
	DEVICE( 3DLABS,		3DLABS_500TX,	"GLINT 500TX"),
	DEVICE( 3DLABS,		3DLABS_DELTA,	"GLINT Delta"),
	DEVICE( 3DLABS,		3DLABS_PERMEDIA,"PERMEDIA"),
	DEVICE( 3DLABS,		3DLABS_MX,	"GLINT MX"),
	DEVICE( AVANCE,		AVANCE_ALG2064,	"ALG2064i"),
	DEVICE( AVANCE,		AVANCE_2302,	"ALG-2302"),
	DEVICE( NETVIN,		NETVIN_NV5000SC,"NV5000"),
	DEVICE( S3,		S3_PLATO_PXS,	"PLATO/PX (system)"),
	DEVICE( S3,		S3_ViRGE,	"ViRGE"),
	DEVICE( S3,		S3_TRIO,	"Trio32/Trio64"),
	DEVICE( S3,		S3_AURORA64VP,	"Aurora64V+"),
	DEVICE( S3,		S3_TRIO64UVP,	"Trio64UV+"),
	DEVICE( S3,		S3_ViRGE_VX,	"ViRGE/VX"),
	DEVICE( S3,		S3_868,	        "Vision 868"),
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
	DEVICE( S3,		S3_ViRGE_MX,	"ViRGE/MX"),
	DEVICE( S3,		S3_ViRGE_MXP,	"ViRGE/MX+"),
	DEVICE( S3,		S3_ViRGE_MXPMV,	"ViRGE/MX+MV"),
	DEVICE( S3,		S3_SONICVIBES,	"SonicVibes"),
	DEVICE( DCI,		DCI_PCCOM4,	"PC COM PCI Bus 4 port serial Adapter"),
	DEVICE( INTEL,		INTEL_82375,	"82375EB"),
	DEVICE( INTEL,		INTEL_82424,	"82424ZX Saturn"),
	DEVICE( INTEL,		INTEL_82378,	"82378IB"),
	DEVICE( INTEL,		INTEL_82430,	"82430ZX Aries"),
	DEVICE( INTEL,		INTEL_82434,	"82434LX Mercury/Neptune"),
	DEVICE( INTEL,		INTEL_82092AA_0,"82092AA PCMCIA bridge"),
	DEVICE( INTEL,		INTEL_82092AA_1,"82092AA EIDE"),
	DEVICE( INTEL,		INTEL_7116,	"SAA7116"),
	DEVICE( INTEL,		INTEL_82596,	"82596"),
	DEVICE( INTEL,		INTEL_82865,	"82865"),
	DEVICE( INTEL,		INTEL_82557,	"82557"),
	DEVICE( INTEL,		INTEL_82437,	"82437"),
	DEVICE( INTEL,		INTEL_82371FB_0,"82371FB PIIX ISA"),
	DEVICE( INTEL,		INTEL_82371FB_1,"82371FB PIIX IDE"),
	DEVICE( INTEL,		INTEL_82371MX,	"430MX - 82371MX MPIIX"),
	DEVICE( INTEL,		INTEL_82437MX,	"430MX - 82437MX MTSC"),
	DEVICE( INTEL,		INTEL_82441,	"82441FX Natoma"),
	DEVICE( INTEL,		INTEL_82380FB,	"82380FB Mobile"),
	DEVICE( INTEL,		INTEL_82439,	"82439HX Triton II"),
	DEVICE(	INTEL,		INTEL_82371SB_0,"82371SB PIIX3 ISA"),
	DEVICE(	INTEL,		INTEL_82371SB_1,"82371SB PIIX3 IDE"),
	DEVICE( INTEL,		INTEL_82371SB_2,"82371SB PIIX3 USB"),
	DEVICE( INTEL,		INTEL_82437VX,	"82437VX Triton II"),
	DEVICE( INTEL,		INTEL_82439TX,	"82439TX"),
	DEVICE( INTEL,		INTEL_82371AB_0,"82371AB PIIX4 ISA"),
	DEVICE( INTEL,		INTEL_82371AB,	"82371AB PIIX4 IDE"),
	DEVICE( INTEL,		INTEL_82371AB_2,"82371AB PIIX4 USB"),
	DEVICE( INTEL,		INTEL_82371AB_3,"82371AB PIIX4 ACPI"),
	DEVICE( INTEL,		INTEL_82443LX_0,"440LX - 82443LX PAC Host"),
	DEVICE( INTEL,		INTEL_82443LX_1,"440LX - 82443LX PAC AGP"),
	DEVICE( INTEL,		INTEL_82443BX_0,"440BX - 82443BX Host"),
	DEVICE( INTEL,		INTEL_82443BX_1,"440BX - 82443BX AGP"),
	DEVICE( INTEL,		INTEL_82443BX_2,"440BX - 82443BX Host (no AGP)"),
	DEVICE( INTEL,		INTEL_P6,	"Orion P6"),
 	DEVICE( INTEL,		INTEL_82450GX,	"82450GX Orion P6"),
	DEVICE(	KTI,		KTI_ET32P2,	"ET32P2"),
	DEVICE( ADAPTEC,	ADAPTEC_7810,	"AIC-7810 RAID"),
	DEVICE( ADAPTEC,	ADAPTEC_7850,	"AIC-7850"),
	DEVICE( ADAPTEC,	ADAPTEC_7855,	"AIC-7855"),
	DEVICE( ADAPTEC,	ADAPTEC_5800,	"AIC-5800"),
	DEVICE( ADAPTEC,	ADAPTEC_7860,	"AIC-7860"),
	DEVICE( ADAPTEC,	ADAPTEC_7861,	"AIC-7861"),
	DEVICE( ADAPTEC,	ADAPTEC_7870,	"AIC-7870"),
	DEVICE( ADAPTEC,	ADAPTEC_7871,	"AIC-7871"),
	DEVICE( ADAPTEC,	ADAPTEC_7872,	"AIC-7872"),
	DEVICE( ADAPTEC,	ADAPTEC_7873,	"AIC-7873"),
	DEVICE( ADAPTEC,	ADAPTEC_7874,	"AIC-7874"),
	DEVICE( ADAPTEC,	ADAPTEC_7895,	"AIC-7895U"),
	DEVICE( ADAPTEC,	ADAPTEC_7880,	"AIC-7880U"),
	DEVICE( ADAPTEC,	ADAPTEC_7881,	"AIC-7881U"),
	DEVICE( ADAPTEC,	ADAPTEC_7882,	"AIC-7882U"),
	DEVICE( ADAPTEC,	ADAPTEC_7883,	"AIC-7883U"),
	DEVICE( ADAPTEC,	ADAPTEC_7884,	"AIC-7884U"),
	DEVICE( ADAPTEC,	ADAPTEC_1030,	"ABA-1030 DVB receiver"),
	DEVICE( ADAPTEC2,	ADAPTEC2_2940U2,"AHA-2940U2"),
	DEVICE( ADAPTEC2,	ADAPTEC2_78902,	"AIC-7890/1"),
	DEVICE( ADAPTEC2,	ADAPTEC2_7890,	"AIC-7890/1"),
	DEVICE( ADAPTEC2,	ADAPTEC2_3940U2,"AHA-3940U2"),
	DEVICE( ADAPTEC2,	ADAPTEC2_3950U2D,"AHA-3950U2D"),
	DEVICE( ADAPTEC2,	ADAPTEC2_7896,	"AIC-7896/7"),
  	DEVICE( ATRONICS,	ATRONICS_2015,	"IDE-2015PL"),
	DEVICE( TIGERJET,	TIGERJET_300,	"Tiger300 ISDN"),
	DEVICE( ARK,		ARK_STING,	"Stingray"),
	DEVICE( ARK,		ARK_STINGARK,	"Stingray ARK 2000PV"),
	DEVICE( ARK,		ARK_2000MT,	"2000MT")
};


/*
 * device_info[] is sorted so we can use binary search
 */
static struct pci_dev_info *pci_lookup_dev(unsigned int vendor, unsigned int dev)
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

static const char *pci_strclass (unsigned int class)
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


static const char *pci_strvendor(unsigned int vendor)
{
	switch (vendor) {
	      case PCI_VENDOR_ID_COMPAQ:	return "Compaq";
	      case PCI_VENDOR_ID_NCR:		return "NCR";
	      case PCI_VENDOR_ID_ATI:		return "ATI";
	      case PCI_VENDOR_ID_VLSI:		return "VLSI";
	      case PCI_VENDOR_ID_ADL:		return "Avance Logic";
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
	      case PCI_VENDOR_ID_CYRIX:		return "Cyrix";
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
	      case PCI_VENDOR_ID_PLX:		return "PLX";
	      case PCI_VENDOR_ID_MADGE:		return "Madge Networks";
	      case PCI_VENDOR_ID_3COM:		return "3Com";
	      case PCI_VENDOR_ID_SMC:		return "SMC";
	      case PCI_VENDOR_ID_AL:		return "Acer Labs";
	      case PCI_VENDOR_ID_MITSUBISHI:	return "Mitsubishi";
	      case PCI_VENDOR_ID_SURECOM:	return "Surecom";
	      case PCI_VENDOR_ID_NEOMAGIC:	return "Neomagic";
	      case PCI_VENDOR_ID_ASP:		return "Advanced System Products";
	      case PCI_VENDOR_ID_MACRONIX:	return "Macronix";
	      case PCI_VENDOR_ID_CERN:		return "CERN";
	      case PCI_VENDOR_ID_NVIDIA:	return "NVidia";
	      case PCI_VENDOR_ID_IMS:		return "IMS";
	      case PCI_VENDOR_ID_TEKRAM2:	return "Tekram";
	      case PCI_VENDOR_ID_TUNDRA:	return "Tundra";
	      case PCI_VENDOR_ID_AMCC:		return "AMCC";
	      case PCI_VENDOR_ID_INTERG:	return "Intergraphics";
	      case PCI_VENDOR_ID_REALTEK:	return "Realtek";
	      case PCI_VENDOR_ID_TRUEVISION:	return "Truevision";
	      case PCI_VENDOR_ID_INIT:		return "Initio Corp";
	      case PCI_VENDOR_ID_TTI:		return "Triones Technologies, Inc.";
	      case PCI_VENDOR_ID_VIA:		return "VIA Technologies";
	      case PCI_VENDOR_ID_SMC2:		return "SMC";
	      case PCI_VENDOR_ID_VORTEX:	return "VORTEX";
	      case PCI_VENDOR_ID_EF:		return "Efficient Networks";
	      case PCI_VENDOR_ID_FORE:		return "Fore Systems";
	      case PCI_VENDOR_ID_IMAGINGTECH:	return "Imaging Technology";
	      case PCI_VENDOR_ID_PHILIPS:	return "Philips";
	      case PCI_VENDOR_ID_CYCLONE:	return "Cyclone";
	      case PCI_VENDOR_ID_ALLIANCE:	return "Alliance";
	      case PCI_VENDOR_ID_VMIC:		return "VMIC";
	      case PCI_VENDOR_ID_DIGI:		return "Digi Intl.";
	      case PCI_VENDOR_ID_MUTECH:	return "Mutech";
	      case PCI_VENDOR_ID_RENDITION:	return "Rendition";
	      case PCI_VENDOR_ID_TOSHIBA:	return "Toshiba";
	      case PCI_VENDOR_ID_RICOH:		return "Ricoh";
	      case PCI_VENDOR_ID_ARTOP:		return "Artop Electronics";
	      case PCI_VENDOR_ID_ZEITNET:	return "ZeitNet";
	      case PCI_VENDOR_ID_OMEGA:		return "Omega Micro";
	      case PCI_VENDOR_ID_LITEON:	return "LiteOn";
	      case PCI_VENDOR_ID_NP:		return "Network Peripherals";
	      case PCI_VENDOR_ID_ATT:		return "Lucent (ex-AT&T) Microelectronics";
	      case PCI_VENDOR_ID_SPECIALIX:	return "Specialix";
	      case PCI_VENDOR_ID_AURAVISION:	return "Auravision";
	      case PCI_VENDOR_ID_IKON:		return "Ikon";
	      case PCI_VENDOR_ID_ZORAN:		return "Zoran";
	      case PCI_VENDOR_ID_KINETIC:	return "Kinetic";
	      case PCI_VENDOR_ID_COMPEX:	return "Compex";
	      case PCI_VENDOR_ID_RP:		return "Comtrol";
	      case PCI_VENDOR_ID_CYCLADES:	return "Cyclades";
	      case PCI_VENDOR_ID_ESSENTIAL:	return "Essential Communications";
	      case PCI_VENDOR_ID_O2:		return "O2 Micro";
	      case PCI_VENDOR_ID_3DFX:		return "3Dfx";
	      case PCI_VENDOR_ID_SIGMADES:	return "Sigma Designs";
	      case PCI_VENDOR_ID_CCUBE:		return "C-Cube";
	      case PCI_VENDOR_ID_DIPIX:		return "Dipix";
	      case PCI_VENDOR_ID_STALLION:	return "Stallion Technologies";
	      case PCI_VENDOR_ID_OPTIBASE:	return "Optibase";
	      case PCI_VENDOR_ID_SATSAGEM:	return "SatSagem";
	      case PCI_VENDOR_ID_HUGHES:	return "Hughes";
	      case PCI_VENDOR_ID_ENSONIQ:	return "Ensoniq";
	      case PCI_VENDOR_ID_ALTEON:	return "Alteon";
	      case PCI_VENDOR_ID_PICTUREL:	return "Picture Elements";
	      case PCI_VENDOR_ID_NVIDIA_SGS:	return "NVidia/SGS Thomson";
	      case PCI_VENDOR_ID_CBOARDS:	return "ComputerBoards";
	      case PCI_VENDOR_ID_SYMPHONY:	return "Symphony";
	      case PCI_VENDOR_ID_TEKRAM:	return "Tekram";
	      case PCI_VENDOR_ID_3DLABS:	return "3Dlabs";
	      case PCI_VENDOR_ID_AVANCE:	return "Avance";
	      case PCI_VENDOR_ID_NETVIN:	return "NetVin";
	      case PCI_VENDOR_ID_S3:		return "S3 Inc.";
	      case PCI_VENDOR_ID_DCI:		return "Decision Computer Int.";
	      case PCI_VENDOR_ID_INTEL:		return "Intel";
	      case PCI_VENDOR_ID_KTI:		return "KTI";
	      case PCI_VENDOR_ID_ADAPTEC:	return "Adaptec";
	      case PCI_VENDOR_ID_ADAPTEC2:	return "Adaptec";
	      case PCI_VENDOR_ID_ATRONICS:	return "Atronics";
	      case PCI_VENDOR_ID_TIGERJET:	return "TigerJet";
	      case PCI_VENDOR_ID_ARK:		return "ARK Logic";
	      default:				return "Unknown vendor";
	}
}


static const char *pci_strdev(unsigned int vendor, unsigned int device)
{
	struct pci_dev_info *info;

	info = 	pci_lookup_dev(vendor, device);
	return info ? info->name : "Unknown device";
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
	unsigned int class_rev, bus, devfn;
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
		len += sprintf(buf + len, "IRQ %d.  ", dev->irq);
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
		base = dev->base_address[reg];
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
				type = "64 bit"; break;
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
	static int complained = 0;
#	define MSG "\nwarning: page-size limit reached!\n"

	if (!complained) {
		complained++;
		printk(KERN_INFO "%s uses obsolete /proc/pci interface\n",
			current->comm);
	}

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

static struct proc_dir_entry proc_old_pci = {
	PROC_PCI, 3, "pci",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_array_inode_operations
};

__initfunc(void proc_old_pci_init(void))
{
	proc_register(&proc_root, &proc_old_pci);
}

#endif /* CONFIG_PROC_FS */
