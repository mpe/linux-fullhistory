/*
 * arch/mips/sni/pcimt_scache.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1997 by Ralf Baechle
 *
 * $Id: pcimt_scache.c,v 1.2 1998/05/28 03:18:02 ralf Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/bcache.h>
#include <asm/sni.h>

__initfunc(void sni_pcimt_sc_init(void))
{
	unsigned int cacheconf, sc_size;

	cacheconf = *(volatile unsigned int *)PCIMT_CACHECONF;
	if ((cacheconf & 7) == 0) {
		printk("No second level cache detected\n");
		printk("WARNING: not activating second level cache, "
		       "tell ralf@gnu.org\n");
		return;
	}
	if ((cacheconf & 7) >= 6) {
		printk("Invalid second level cache size detected\n");
		return;
	}
	
	sc_size = 128 << (cacheconf & 7);
	printk("%dkb second level cache detected.\n", sc_size);
}
