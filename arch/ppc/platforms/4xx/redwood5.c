/*
 * arch/ppc/platforms/4xx/redwood5.c
 *
 * Support for the IBM redwood5 eval board file
 *
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2000-2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <asm/io.h>
#include <asm/machdep.h>

void __init
redwood5_setup_arch(void)
{
	bd_t *bip = &__res;

	ppc4xx_setup_arch();

#ifdef CONFIG_DEBUG_BRINGUP
	printk("\n");
	printk("machine\t: %s\n", PPC4xx_MACHINE_NAME);
	printk("\n");
	printk("bi_s_version\t %s\n",      bip->bi_s_version);
	printk("bi_r_version\t %s\n",      bip->bi_r_version);
	printk("bi_memsize\t 0x%8.8x\t %dMBytes\n", bip->bi_memsize,bip->bi_memsize/(1024*1000));
	printk("bi_enetaddr %d\t %2.2x%2.2x%2.2x-%2.2x%2.2x%2.2x\n", 0,
	bip->bi_enetaddr[0], bip->bi_enetaddr[1],
	bip->bi_enetaddr[2], bip->bi_enetaddr[3],
	bip->bi_enetaddr[4], bip->bi_enetaddr[5]);

	printk("bi_intfreq\t 0x%8.8x\t clock:\t %dMhz\n",
	       bip->bi_intfreq, bip->bi_intfreq/ 1000000);

	printk("bi_busfreq\t 0x%8.8x\t plb bus clock:\t %dMHz\n",
		bip->bi_busfreq, bip->bi_busfreq / 1000000 );
	printk("bi_tbfreq\t 0x%8.8x\t TB freq:\t %dMHz\n",
	       bip->bi_tbfreq, bip->bi_tbfreq/1000000);

	printk("\n");
#endif

}

void __init
redwood5_map_io(void)
{
	int i;

	ppc4xx_map_io();
	for (i = 0; i < 16; i++) {
	 unsigned long v, p;

	/* 0x400x0000 -> 0xe00x0000 */
	p = 0x40000000 | (i << 16);
	v = STB04xxx_IO_BASE | (i << 16);

	io_block_mapping(v, p, PAGE_SIZE,
		 _PAGE_NO_CACHE | pgprot_val(PAGE_KERNEL) | _PAGE_GUARDED);
	}


}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	ppc4xx_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = redwood5_setup_arch;
	ppc_md.setup_io_mappings = redwood5_map_io;
}
