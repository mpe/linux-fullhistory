/*
 * Detection routine for the NCR53c710 based Amiga SCSI Controllers for Linux.
 *  		Amiga MacroSystemUS WarpEngine SCSI controller.
 *		Amiga Technologies A4000T SCSI controller.
 *		Amiga Technologies/DKB A4091 SCSI controller.
 *
 * Written 1997 by Alan Hourihane <alanh@fairlite.demon.co.uk>
 * plus modifications of the 53c7xx.c driver to support the Amiga.
 */
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/blk.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/config.h>
#include <linux/zorro.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <asm/irq.h>

#include "scsi.h"
#include "hosts.h"
#include "53c7xx.h"
#include "amiga7xx.h"

#include<linux/stat.h>

struct proc_dir_entry proc_scsi_amiga7xx = {
    PROC_SCSI_AMIGA7XX, 8, "Amiga7xx",
    S_IFDIR | S_IRUGO | S_IXUGO, 2
};

int amiga7xx_detect(Scsi_Host_Template *tpnt)
{
    static unsigned char called = 0;
    int key;
    int num = 0;
    unsigned long address;
    long long options;
    struct ConfigDev *cd;

    if (called || !MACH_IS_AMIGA)
	return 0;

    tpnt->proc_dir = &proc_scsi_amiga7xx;

#ifdef CONFIG_WARPENGINE_SCSI
    if ((key = zorro_find(MANUF_MACROSYSTEMS, PROD_WARP_ENGINE, 0, 0)))
    {
    	cd = zorro_get_board(key);
    	address = (unsigned long)kernel_map((unsigned long)cd->cd_BoardAddr,
		cd->cd_BoardSize, KERNELMAP_NOCACHE_SER, NULL);

    	options = OPTION_MEMORY_MAPPED|OPTION_DEBUG_TEST1|OPTION_INTFLY|OPTION_SYNCHRONOUS|OPTION_ALWAYS_SYNCHRONOUS|OPTION_DISCONNECT;

	clock = 50000000;	/* 50MHz SCSI Clock */

    	ncr53c7xx_init(tpnt, 0, 710, (u32)(unsigned char *)(address + 0x40000),
			0, IRQ_AMIGA_PORTS & ~IRQ_MACHSPEC, DMA_NONE, 
			options, clock);

    	zorro_config_board(key, 0);
    	num++;
    }
#endif

#ifdef CONFIG_A4000T_SCSI
    if (AMIGAHW_PRESENT(A4000_SCSI))
    { 
    	options = OPTION_MEMORY_MAPPED|OPTION_DEBUG_TEST1|OPTION_INTFLY|OPTION_SYNCHRONOUS|OPTION_ALWAYS_SYNCHRONOUS|OPTION_DISCONNECT;

	clock = 50000000;	/* 50MHz SCSI Clock */

    	ncr53c7xx_init(tpnt, 0, 710, (u32)(unsigned char *)ZTWO_VADDR(0xDD0040),
			0, IRQ_AMIGA_PORTS, DMA_NONE,
			options, clock);
    	num++;
    }
#endif

#ifdef CONFIG_A4091_SCSI
    while ( (key = zorro_find(MANUF_COMMODORE, PROD_A4091, 0, 0)) ||
	 (key = zorro_find(MANUF_COMMODORE2, PROD_A4091_2, 0, 0)) )
    {
	cd = zorro_get_board(key);
	address = (unsigned long)kernel_map((unsigned long)cd->cd_BoardAddr,
		cd->cd_BoardSize, KERNELMAP_NOCACHE_SER, NULL);

    	options = OPTION_MEMORY_MAPPED|OPTION_DEBUG_TEST1|OPTION_INTFLY|OPTION_SYNCHRONOUS|OPTION_ALWAYS_SYNCHRONOUS|OPTION_DISCONNECT;

	clock = 50000000;	/* 50MHz SCSI Clock */

    	ncr53c7xx_init(tpnt, 0, 710, (u32)(unsigned char *)(address+0x800000),
			0, IRQ_AMIGA_PORTS & ~IRQ_MACHSPEC, DMA_NONE, 
			options, clock);

	zorro_config_board(key, 0);
	num++;
    }
#endif

    called = 1;
    return num;
}
