/*
 *    $Id: zorro.c,v 1.1.2.1 1998/06/07 23:21:02 geert Exp $
 *
 *    Zorro Expansion Device Names
 *
 *    Copyright (C) 1999-2000 Geert Uytterhoeven
 *
 *    This file is subject to the terms and conditions of the GNU General Public
 *    License.  See the file COPYING in the main directory of this archive
 *    for more details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/zorro.h>


    /*
     *  Just for reference, these are the boards we have a driver for in the
     *  kernel:
     *
     *  ZORRO_PROD_AMERISTAR_A2065
     *  ZORRO_PROD_BSC_FRAMEMASTER_II
     *  ZORRO_PROD_BSC_MULTIFACE_III
     *  ZORRO_PROD_BSC_OKTAGON_2008
     *  ZORRO_PROD_CBM_A2065_1
     *  ZORRO_PROD_CBM_A2065_2
     *  ZORRO_PROD_CBM_A4091_1
     *  ZORRO_PROD_CBM_A4091_2
     *  ZORRO_PROD_CBM_A590_A2091_1
     *  ZORRO_PROD_CBM_A590_A2091_2
     *  ZORRO_PROD_GVP_A1291
     *  ZORRO_PROD_GVP_A530_SCSI
     *  ZORRO_PROD_GVP_COMBO_030_R3_SCSI
     *  ZORRO_PROD_GVP_COMBO_030_R4_SCSI
     *  ZORRO_PROD_GVP_EGS_28_24_SPECTRUM_RAM
     *  ZORRO_PROD_GVP_EGS_28_24_SPECTRUM_REG
     *  ZORRO_PROD_GVP_GFORCE_030_SCSI
     *  ZORRO_PROD_GVP_GFORCE_040_060
     *  ZORRO_PROD_GVP_GFORCE_040_1
     *  ZORRO_PROD_GVP_GFORCE_040_SCSI_1
     *  ZORRO_PROD_GVP_IO_EXTENDER
     *  ZORRO_PROD_GVP_SERIES_II
     *  ZORRO_PROD_HELFRICH_PICCOLO_RAM
     *  ZORRO_PROD_HELFRICH_PICCOLO_REG
     *  ZORRO_PROD_HELFRICH_RAINBOW_II
     *  ZORRO_PROD_HELFRICH_SD64_RAM
     *  ZORRO_PROD_HELFRICH_SD64_REG
     *  ZORRO_PROD_HYDRA_SYSTEMS_AMIGANET
     *  ZORRO_PROD_INDIVIDUAL_COMPUTERS_BUDDHA
     *  ZORRO_PROD_INDIVIDUAL_COMPUTERS_CATWEASEL
     *  ZORRO_PROD_MACROSYSTEMS_RETINA_Z3
     *  ZORRO_PROD_MACROSYSTEMS_WARP_ENGINE_40xx
     *  ZORRO_PROD_PHASE5_BLIZZARD_1220_CYBERSTORM
     *  ZORRO_PROD_PHASE5_BLIZZARD_1230_II_FASTLANE_Z3_CYBERSCSI_CYBERSTORM060
     *  ZORRO_PROD_PHASE5_BLIZZARD_1230_IV_1260
     *  ZORRO_PROD_PHASE5_BLIZZARD_2060
     *  ZORRO_PROD_PHASE5_BLIZZARD_603E_PLUS
     *  ZORRO_PROD_PHASE5_CYBERSTORM_MK_II
     *  ZORRO_PROD_PHASE5_CYBERVISION64
     *  ZORRO_PROD_PHASE5_CYBERVISION64_3D
     *  ZORRO_PROD_VILLAGE_TRONIC_ARIADNE
     *  ZORRO_PROD_VILLAGE_TRONIC_ARIADNE2
     *  ZORRO_PROD_VILLAGE_TRONIC_PICASSO_II_II_PLUS_RAM
     *  ZORRO_PROD_VILLAGE_TRONIC_PICASSO_II_II_PLUS_REG
     *  ZORRO_PROD_VILLAGE_TRONIC_PICASSO_IV_Z3
     *
     *  And I guess these are automagically supported as well :-)
     *
     *  ZORRO_PROD_CBM_A560_RAM
     *  ZORRO_PROD_CBM_A590_A2052_A2058_A2091
     */

void __init zorro_namedevice(struct zorro_dev *dev)
{
    /*
     *  Nah, we're not that stupid to put name databases in the kernel ;-)
     *  That's why we have zorroutils...
     */
    sprintf(dev->name, "Zorro device %08x", dev->id);
}

