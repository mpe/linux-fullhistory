/*
 * lowlevel/init.c - Calls initialization code for configured drivers.
 */

#include <linux/config.h>

#ifdef CONFIG_LOWLEVEL_SOUND
extern int attach_aci(void);
extern void unload_aci(void);

void
sound_init_lowlevel_drivers(void)
{
#ifdef CONFIG_ACI_MIXER
   attach_aci();
#endif
}

void
sound_unload_lowlevel_drivers(void)
{
#ifdef CONFIG_ACI_MIXER
   unload_aci();
#endif
}
#endif
