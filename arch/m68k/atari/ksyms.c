#include <linux/config.h>
#include <linux/module.h>
#include <asm/ptrace.h>
#include <asm/traps.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atarikb.h>
#include <asm/atari_joystick.h>
#include <asm/atari_stdma.h>

static struct symbol_table mach_atari_symbol_table = {
#include <linux/symtab_begin.h>

	X(is_medusa),
	X(atari_register_vme_int),
	X(stdma_lock),
	X(stdma_release),
	X(stdma_others_waiting),
	X(stdma_islocked),

	X(atari_mouse_buttons),
	X(atari_mouse_interrupt_hook),
	X(atari_MIDI_interrupt_hook),
	X(ikbd_write),
	X(ikbd_mouse_y0_top),
	X(ikbd_mouse_thresh),
	X(ikbd_mouse_rel_pos),
	X(ikbd_mouse_disable),

#include <linux/symtab_end.h>
};

void mach_atari_syms_export(void)
{
	register_symtab(&mach_atari_symbol_table);
}
