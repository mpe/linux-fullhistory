/*
 *  linux/drivers/block/q40ide.c -- Q40 I/O port IDE Driver
 *
 *     original file created 12 Jul 1997 by Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 * RZ:
 *  almost identical with pcide.c, maybe we can merge it later. 
 *  Differences:
 *       max 2 HWIFS for now
 *       translate portaddresses to q40 native addresses (not yet...) instead rely on in/out[bw]
 *         address translation
 *
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#include <linux/ide.h>

    /*
     *  Bases of the IDE interfaces
     */

#define PCIDE_NUM_HWIFS	2

#define PCIDE_BASE1	0x1f0
#define PCIDE_BASE2	0x170
#define PCIDE_BASE3	0x1e8
#define PCIDE_BASE4	0x168
#define PCIDE_BASE5	0x1e0
#define PCIDE_BASE6	0x160

static const q40ide_ioreg_t pcide_bases[PCIDE_NUM_HWIFS] = {
    PCIDE_BASE1, PCIDE_BASE2, /* PCIDE_BASE3, PCIDE_BASE4  , PCIDE_BASE5,
    PCIDE_BASE6 */
};


    /*
     *  Offsets from one of the above bases
     */

#undef HD_DATA
#define HD_DATA  0x1f0

#define PCIDE_REG(x)	((q40ide_ioreg_t)(HD_##x-PCIDE_BASE1))

static const int pcide_offsets[IDE_NR_PORTS] = {
    PCIDE_REG(DATA), PCIDE_REG(ERROR), PCIDE_REG(NSECTOR), PCIDE_REG(SECTOR),
    PCIDE_REG(LCYL), PCIDE_REG(HCYL), PCIDE_REG(CURRENT), PCIDE_REG(STATUS),
    PCIDE_REG(CMD)
};

int q40ide_default_irq(q40ide_ioreg_t base)
{
           switch (base) { 
	            case 0x1f0: return 14;
		    case 0x170: return 15;
		    case 0x1e8: return 11;
		    default:
			return 0;
	   }
}

void q40_ide_init_hwif_ports (q40ide_ioreg_t *p, q40ide_ioreg_t base, int *irq)
{
	q40ide_ioreg_t port = base;
	int i = 8;

	while (i--)
		*p++ = port++;
	*p++ = base + 0x206;
	if (irq != NULL)
		*irq = 0;
}


    /*
     *  Probe for PC IDE interfaces
     */

int q40ide_probe_hwif(int index, ide_hwif_t *hwif)
{
    static int pcide_index[PCIDE_NUM_HWIFS] = { 0, };
    int i;

    if (!MACH_IS_Q40)
      return 0;

    for (i = 0; i < PCIDE_NUM_HWIFS; i++) {
	if (!pcide_index[i]) {
	  /*printk("ide%d: Q40 IDE interface\n", index);*/
	    pcide_index[i] = index+1;
	}
	if (pcide_index[i] == index+1) {
	    ide_setup_ports(hwif,(ide_ioreg_t) pcide_bases[i], pcide_offsets, 0, /*q40_ack_intr???*/ NULL);
	    hwif->irq = ide_default_irq((ide_ioreg_t)pcide_bases[i]); /*q40_ide_irq[i];  */ /* 14 */
	    return 1;
	}
    }
    return 0;
}
