/*
 *    linux/arch/m68k/amiga/zorro.c
 *
 *    Copyright (C) 1995 Geert Uytterhoeven
 *
 *    This file is subject to the terms and conditions of the GNU General Public
 *    License.  See the file README.legal in the main directory of this archive
 *    for more details.
 */


#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/bitops.h>
#include <asm/amigahw.h>
#include <asm/bootinfo.h>
#include <asm/zorro.h>


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

BEGIN_PROD(MEMPHIS)
   PROD("Stormbringer", STORMBRINGER)
END

BEGIN_PROD(COMMODORE2)
   PROD("A2088 Bridgeboard", A2088)
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
   PROD("Romulator Card", ROMULATOR)
   PROD("A3000 Test Fixture", A3000TESTFIX)
   PROD("A2065 Ethernet Card", A2065)
END

BEGIN_PROD(CARDCO)
   PROD("Cardco A2410 Hires Graphics card", CC_A2410)
END

BEGIN_PROD(MICROBOTICS)
   PROD("VXL-30 Turbo Board", VXL_30)
END

BEGIN_PROD(ASDG)
   PROD("Lan Rover Ethernet", LAN_ROVER)
   PROD("Dual Serial Card", ASDG_DUAL_SERIAL)
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
   PROD("Wordsync SCSI Controller", WORDSYNC)
   PROD("Wordsync II SCSI Controller", WORDSYNC_II)
   PROD("2400 Modem", SUPRA_2400MODEM)
END

BEGIN_PROD(CSA)
   PROD("Magnum 40 SCSI Controller", MAGNUM)
   PROD("12 Gauge SCSI Controller", 12GAUGE)
END

BEGIN_PROD(POWER_COMPUTING)
   PROD("Viper II Turbo Board (DKB 1240)", DKB_1240)
END

BEGIN_PROD(GVP)
   PROD("Generic GVP product", GVP)
   PROD("Series II SCSI Controller", GVPIISCSI)
   PROD("Series II SCSI Controller", GVPIISCSI_2)
   PROD("Series II RAM", GVPIIRAM)
   PROD("GFORCE 040 with SCSI Controller", GFORCE_040_SCSI)
   PROD("IV-24 Graphics Board", GVPIV_24)
/*
   PROD("I/O Extender", GVPIO_EXT)
*/
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

BEGIN_PROD(PPI)
   PROD("Mercury Turbo Board", MERCURY)
   PROD("PP&S A3000 68040 Turbo Board", PPS_A3000_040)
   PROD("PP&S A2000 68040 Turbo Board", PPS_A2000_040)
   PROD("Zeus SCSI Controller", ZEUS)
   PROD("PP&S A500 68040 Turbo Board", PPS_A500_040)
END

BEGIN_PROD(BSC)
   PROD("ALF 3 SCSI Controller", ALF_3_SCSI)
END

BEGIN_PROD(C_LTD)
   PROD("Kronos SCSI Controller", KRONOS_SCSI)
END

BEGIN_PROD(JOCHHEIM)
   PROD("Jochheim RAM", JOCHHEIM_RAM)
END

BEGIN_PROD(CHECKPOINT)
   PROD("Serial Solution", SERIAL_SOLUTION)
END

BEGIN_PROD(GOLEM)
   PROD("Golem SCSI-II Controller", GOLEM_SCSI_II)
END

BEGIN_PROD(HARDITAL_SYNTHES)
   PROD("SCSI Controller", HARDITAL_SCSI)
END

BEGIN_PROD(BSC2)
   PROD("Oktagon 2008 SCSI Controller", OKTAGON_SCSI)
   PROD("Tandem", TANDEM)
   PROD("Oktagon 2008 RAM", OKTAGON_RAM)
   PROD("Alfa Data MultiFace I", MULTIFACE_I)
   PROD("Alfa Data MultiFace II", MULTIFACE_II)
   PROD("Alfa Data MultiFace III", MULTIFACE_III)
   PROD("ISDN Master", ISDN_MASTER)
END

BEGIN_PROD(ADV_SYS_SOFT)
   PROD("Nexus SCSI Controller", NEXUS_SCSI)
   PROD("Nexus RAM", NEXUS_RAM)
END

BEGIN_PROD(IVS)
   PROD("Trumpcard 500 SCSI Controller", TRUMPCARD_500)
   PROD("Trumpcard SCSI Controller", TRUMPCARD)
   PROD("Vector SCSI Controller", VECTOR)
END

BEGIN_PROD(XPERT_PRODEV)
   PROD("Merlin Graphics Board (RAM)", MERLIN_RAM)
   PROD("Merlin Graphics Board (REG)", MERLIN_REG)
END

BEGIN_PROD(HYDRA_SYSTEMS)
   PROD("Amiganet Board", AMIGANET)
END

BEGIN_PROD(DIG_MICRONICS)
   PROD("DMI Resolver Graphics Board", DMI_RESOLVER)
END

BEGIN_PROD(HELFRICH1)
   PROD("Rainbow3 Graphics Board", RAINBOW3)
END

BEGIN_PROD(SW_RESULT_ENTS)
   PROD("GG2+ Bus Converter", GG2PLUS)
END

BEGIN_PROD(VILLAGE_TRONIC)
   PROD("Ariadne Ethernet Card", ARIADNE)
   PROD("Picasso II Graphics Board (RAM)", PICASSO_II_RAM)
   PROD("Picasso II Graphics Board (REG)", PICASSO_II_REG)
END

BEGIN_PROD(UTILITIES_ULTD)
   PROD("Emplant Deluxe SCSI Controller", EMPLANT_DELUXE)
   PROD("Emplant Deluxe SCSI Controller", EMPLANT_DELUXE2)
END

BEGIN_PROD(MTEC)
   PROD("68030 Turbo Board", MTEC_68030)
   PROD("T1230/28 Turbo Board", MTEC_T1230)
END

BEGIN_PROD(GVP2)
   PROD("Spectrum Graphics Board (RAM)", SPECTRUM_RAM)
   PROD("Spectrum Graphics Board (REG)", SPECTRUM_REG)
END

BEGIN_PROD(HELFRICH2)
   PROD("Piccolo Graphics Board (RAM)", PICCOLO_RAM)
   PROD("Piccolo Graphics Board (REG)", PICCOLO_REG)
   PROD("PeggyPlus MPEG Decoder Board", PEGGY_PLUS)
   PROD("SD64 Graphics Board (RAM)", SD64_RAM)
   PROD("SD64 Graphics Board (REG)", SD64_REG)
END

BEGIN_PROD(MACROSYSTEMS)
   PROD("Warp Engine SCSI Controller", WARP_ENGINE)
END

BEGIN_PROD(HARMS_PROF)
   PROD("3500 Turbo board", 3500_TURBO)
END

BEGIN_PROD(VORTEX)
   PROD("Golden Gate 80386 Board", GOLDEN_GATE_386)
   PROD("Golden Gate RAM", GOLDEN_GATE_RAM)
   PROD("Golden Gate 80486 Board", GOLDEN_GATE_486)
END

BEGIN_PROD(DATAFLYER)
   PROD("4000SX SCSI Controller", DATAFLYER_4000SX)
END

BEGIN_PROD(PHASE5)
   PROD("FastLane RAM", FASTLANE_RAM)
   PROD("FastLane/Blizzard 1230-II SCSI Controller", FASTLANE_SCSI)
   PROD("Blizzard 1230-III Turbo Board", BLIZZARD_1230_III)
   PROD("Blizzard 1230-IV Turbo Board", BLIZZARD_1230_IV)
   PROD("CyberVision64 Graphics Board", CYBERVISION)
END

BEGIN_PROD(APOLLO)
   PROD("AT-Apollo", AT_APOLLO)
   PROD("Turbo Board", APOLLO_TURBO)
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
   PROD("Retina Z3 Graphics Board", RETINA_Z3)
END

BEGIN_MANUF
   MANUF("Memphis", MEMPHIS)
   MANUF("Commodore", COMMODORE2)
   MANUF("Commodore", COMMODORE)
   MANUF("Cardco", CARDCO)
   MANUF("MicroBotics", MICROBOTICS)
   MANUF("ASDG", ASDG)
   MANUF("University of Lowell", UNIV_OF_LOWELL)
   MANUF("Ameristar", AMERISTAR)
   MANUF("Supra", SUPRA)
   MANUF("CSA", CSA)
   MANUF("Power Computing", POWER_COMPUTING)
   MANUF("Great Valley Products", GVP)
   MANUF("Progressive Peripherals", PPI)
   MANUF("BSC", BSC)
   MANUF("C Ltd.", C_LTD)
   MANUF("Jochheim", JOCHHEIM)
   MANUF("Checkpoint Technologies", CHECKPOINT)
   MANUF("Golem", GOLEM)
   MANUF("Hardital Synthesis", HARDITAL_SYNTHES)
   MANUF("BSC", BSC2)
   MANUF("Advanced Systems & Software", ADV_SYS_SOFT)
   MANUF("IVS", IVS)
   MANUF("XPert/ProDev", XPERT_PRODEV)
   MANUF("Hydra Systems", HYDRA_SYSTEMS)
   MANUF("Digital Micronics", DIG_MICRONICS)
   MANUF("Helfrich", HELFRICH1)
   MANUF("Software Result Enterprises", SW_RESULT_ENTS)
   MANUF("Village Tronic", VILLAGE_TRONIC)
   MANUF("Utilities Unlimited", UTILITIES_ULTD)
   MANUF("MTEC", MTEC)
   MANUF("Great Valley Products", GVP2)
   MANUF("Helfrich", HELFRICH2)
   MANUF("MacroSystems", MACROSYSTEMS)
   MANUF("Harms Professional", HARMS_PROF)
   MANUF("Vortex", VORTEX)
   MANUF("DataFlyer", DATAFLYER)
   MANUF("Phase5", PHASE5)
   MANUF("Apollo", APOLLO)
   MANUF("Uwe Gerlach", UWE_GERLACH)
   MANUF("MacroSystems", MACROSYSTEMS2)
END

#define NUM_MANUF (sizeof(Manufacturers)/sizeof(struct Manufacturer))
#define NUM_GVP_PROD (sizeof(Ext_Prod_GVP)/sizeof(struct GVP_Product))

#endif /* CONFIG_ZORRO */


   /*
    *    Configured Expansion Devices
    */

static u_long BoardPartFlags[NUM_AUTO] = { 0, };

   
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

   for (key = index + 1; key <= boot_info.bi_amiga.num_autocon; key++) {
      cd = &boot_info.bi_amiga.autocon[key-1];
      if ((cd->cd_Rom.er_Manufacturer == manuf) &&
          (cd->cd_Rom.er_Product == prod) &&
          !(BoardPartFlags[key-1] & (1<<part)))
         break;
   }
   return(key <= boot_info.bi_amiga.num_autocon ? key : 0);
}


   /*
    *    Get the board for a specified key
    */

struct ConfigDev *zorro_get_board(int key)
{
   struct ConfigDev *cd = NULL;

   if ((key < 1) || (key > boot_info.bi_amiga.num_autocon))
      printk("zorro_get_board: bad key %d\n", key);
   else
      cd = &boot_info.bi_amiga.autocon[key-1];

   return(cd);
}


   /*
    *    Mark a part of a board as configured
    */

void zorro_config_board(int key, int part)
{
   if ((key < 1) || (key > boot_info.bi_amiga.num_autocon))
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
   if ((key < 1) || (key > boot_info.bi_amiga.num_autocon))
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

   cd = &boot_info.bi_amiga.autocon[devnum];
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
                  epc = *(enum GVP_ident *)ZTWO_VADDR(addr+0x8000) &
                        GVP_EPCMASK;
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
   for (i = 0; i < boot_info.bi_amiga.num_autocon; i++) {
      identify(i, tmp);
      printk(tmp);
   }
   if (!boot_info.bi_amiga.num_autocon)
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
      for (i = 0; i < boot_info.bi_amiga.num_autocon; i++) {
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

void zorro_init(void)
{
   int i;
   struct ConfigDev *cd;

   if (!AMIGAHW_PRESENT(ZORRO))
      return;

   /* Mark all available Zorro II memory */
   for (i = 0; i < boot_info.bi_amiga.num_autocon; i++) {
      cd = &boot_info.bi_amiga.autocon[i];
      if (cd->cd_Rom.er_Type & ERTF_MEMLIST)
         mark_region((u_long)cd->cd_BoardAddr, cd->cd_BoardSize, 1);
   }
   /* Unmark all used Zorro II memory */
   for (i = 0; i < boot_info.num_memory; i++)
      mark_region(boot_info.memory[i].addr, boot_info.memory[i].size, 0);
}
