#include <linux/module.h>
#include <asm/ptrace.h>
#include <asm/traps.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atarikb.h>
#include <asm/atari_joystick.h>
#include <asm/atari_stdma.h>

extern void atari_microwire_cmd( int cmd );


EXPORT_SYMBOL(atari_mch_cookie);
EXPORT_SYMBOL(atari_hw_present);
EXPORT_SYMBOL(is_medusa);
EXPORT_SYMBOL(atari_register_vme_int);
EXPORT_SYMBOL(atari_unregister_vme_int);
EXPORT_SYMBOL(stdma_lock);
EXPORT_SYMBOL(stdma_release);
EXPORT_SYMBOL(stdma_others_waiting);
EXPORT_SYMBOL(stdma_islocked);

EXPORT_SYMBOL(atari_mouse_buttons);
EXPORT_SYMBOL(atari_mouse_interrupt_hook);
EXPORT_SYMBOL(atari_MIDI_interrupt_hook);
EXPORT_SYMBOL(atari_mch_cookie);
EXPORT_SYMBOL(ikbd_write);
EXPORT_SYMBOL(ikbd_mouse_y0_top);
EXPORT_SYMBOL(ikbd_mouse_thresh);
EXPORT_SYMBOL(ikbd_mouse_rel_pos);
EXPORT_SYMBOL(ikbd_mouse_disable);

EXPORT_SYMBOL(atari_microwire_cmd);
