/*
 * lowlevel/init.c - Calls initialization code for configured drivers.
 */

#include "lowlevel.h"
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include "../soundvers.h"

#ifdef LOWLEVEL_MODULE
char *lowlevel_version = SOUND_VERSION_STRING;
#endif

extern int attach_aci(void);
extern void unload_aci(void);
extern int attach_awe(void);
extern void unload_awe(void);
extern int init_aedsp16(void) __init;
extern void uninit_aedsp16(void) __init;

/*
 * There are two places where you can insert initialization calls of
 * low level drivers. sound_init_lowlevel_drivers() is called after
 * the sound driver has been initialized (the normal case)
 * while sound_preinit_lowlevel_drivers() is called before that.
 */
void
sound_preinit_lowlevel_drivers(void)
{
#if defined(CONFIG_AEDSP16) && !defined(MODULE)
   init_aedsp16();
#endif
}

void
sound_init_lowlevel_drivers(void)
{
#ifdef CONFIG_ACI_MIXER
   attach_aci();
#endif

#ifdef CONFIG_AWE32_SYNTH
   attach_awe();
#endif
}

void
sound_unload_lowlevel_drivers(void)
{
#ifdef CONFIG_ACI_MIXER
   unload_aci();
#endif

#ifdef CONFIG_AWE32_SYNTH
   unload_awe();
#endif

#ifdef CONFIG_AEDSP16
   uninit_aedsp16();
#endif

}

EXPORT_SYMBOL(sound_init_lowlevel_drivers);
EXPORT_SYMBOL(sound_unload_lowlevel_drivers);
EXPORT_SYMBOL(sound_preinit_lowlevel_drivers);
