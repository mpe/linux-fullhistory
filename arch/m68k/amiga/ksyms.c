#include <linux/types.h>
#include <linux/module.h>
#include <asm/zorro.h>
#include <asm/amigahw.h>

extern volatile u_short amiga_audio_min_period;
extern u_short amiga_audio_period;

/*
 * Add things here when you find the need for it.
 */
EXPORT_SYMBOL(amiga_model);
EXPORT_SYMBOL(amiga_hw_present);
EXPORT_SYMBOL(amiga_eclock);
EXPORT_SYMBOL(amiga_colorclock);
EXPORT_SYMBOL(amiga_chip_alloc);
EXPORT_SYMBOL(amiga_chip_free);
EXPORT_SYMBOL(amiga_chip_avail);
EXPORT_SYMBOL(amiga_audio_period);
EXPORT_SYMBOL(amiga_audio_min_period);

EXPORT_SYMBOL(zorro_find);
EXPORT_SYMBOL(zorro_get_board);
EXPORT_SYMBOL(zorro_config_board);
EXPORT_SYMBOL(zorro_unconfig_board);
EXPORT_SYMBOL(zorro_unused_z2ram);
