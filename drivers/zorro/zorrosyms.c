/*
 *	$Id: zorrosyms.c,v 1.1.2.1 1998/06/07 23:21:02 geert Exp $
 *
 *	Zorro Bus Services -- Exported Symbols
 *
 *	Copyright (C) 1998 Geert Uytterhoeven
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/zorro.h>

    /* Board configuration */

EXPORT_SYMBOL(zorro_find);
EXPORT_SYMBOL(zorro_get_board);
EXPORT_SYMBOL(zorro_config_board);
EXPORT_SYMBOL(zorro_unconfig_board);

    /* Z2 memory */

EXPORT_SYMBOL(zorro_unused_z2ram);

