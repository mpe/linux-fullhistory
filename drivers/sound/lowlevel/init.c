/*
 * lowlevel/init.c - Calls initialization code for configured drivers.
 */

#include "lowlevel.h"
#include <linux/config.h>
#include "../soundvers.h"

#ifdef CONFIG_LOWLEVEL_SOUND

#ifdef LOWLEVEL_MODULE
char *lowlevel_version = SOUND_VERSION_STRING;
#endif

extern int attach_aci(void);
extern void unload_aci(void);
extern int attach_awe(void);
extern void unload_awe(void);
extern int init_aedsp16(void);
extern void uninit_aedsp16(void);

/*
 * There are two places where you can insert initialization calls of
 * low level drivers. sound_init_lowlevel_drivers() is called after
 * the sound driver has been initialized (the normal case)
 * while sound_preinit_lowlevel_drivers() is called before that.
 */
void
sound_preinit_lowlevel_drivers(void)
{
#ifdef CONFIG_AEDSP16
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
#endif
