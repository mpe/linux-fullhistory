/*
 * Export MIPS-specific functions needed for loadable modules.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 by Ralf Baechle
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <asm/dma.h>
#include <asm/floppy.h>
#include <asm/io.h>
#include <asm/softirq.h>

EXPORT_SYMBOL(EISA_bus);

/*
 * String functions
 */
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL_NOVERS(memmove);
EXPORT_SYMBOL_NOVERS(bcopy);

EXPORT_SYMBOL(__mips_bh_counter);
EXPORT_SYMBOL(local_irq_count);

/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy);

/*
 * Functions to control caches.
 */
EXPORT_SYMBOL(fd_cacheflush);

/*
 * Base address of ports for Intel style I/O.
 */
EXPORT_SYMBOL(port_base);
