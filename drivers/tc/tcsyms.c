/*
 *	Turbo Channel Services -- Exported Symbols
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/dec/tc.h>

EXPORT_SYMBOL(get_tc_irq_nr);
EXPORT_SYMBOL(claim_tc_card);
EXPORT_SYMBOL(search_tc_card);
EXPORT_SYMBOL(get_tc_speed);
EXPORT_SYMBOL(get_tc_base_addr);
