/*
 *    The sound core exports the following symbols to the rest of
 *      modulespace.
 *
 *      (C) Copyright 1997      Alan Cox, Licensed under the GNU GPL
 */

#include <linux/config.h>
#include <linux/module.h>
#include "sound_config.h"
#define _MIDI_SYNTH_C_
#include "midi_synth.h"
#include <linux/notifier.h>
#include "sound_firmware.h"

extern struct notifier_block *sound_locker;

EXPORT_SYMBOL(mixer_devs);
EXPORT_SYMBOL(audio_devs);
EXPORT_SYMBOL(num_audiodevs);

EXPORT_SYMBOL(note_to_freq);
EXPORT_SYMBOL(compute_finetune);
EXPORT_SYMBOL(seq_copy_to_input);
EXPORT_SYMBOL(sequencer_timer);

EXPORT_SYMBOL(sound_install_audiodrv);
EXPORT_SYMBOL(sound_install_mixer);
EXPORT_SYMBOL(sound_alloc_dma);
EXPORT_SYMBOL(sound_free_dma);
EXPORT_SYMBOL(snd_set_irq_handler);
EXPORT_SYMBOL(snd_release_irq);
EXPORT_SYMBOL(sound_alloc_audiodev);
EXPORT_SYMBOL(sound_alloc_mididev);
EXPORT_SYMBOL(sound_alloc_mixerdev);
EXPORT_SYMBOL(sound_alloc_timerdev);
EXPORT_SYMBOL(sound_alloc_synthdev);
EXPORT_SYMBOL(sound_unload_audiodev);
EXPORT_SYMBOL(sound_unload_mididev);
EXPORT_SYMBOL(sound_unload_mixerdev);
EXPORT_SYMBOL(sound_unload_timerdev);
EXPORT_SYMBOL(sound_unload_synthdev);

EXPORT_SYMBOL(DMAbuf_start_dma);
EXPORT_SYMBOL(DMAbuf_inputintr);
EXPORT_SYMBOL(DMAbuf_outputintr);
EXPORT_SYMBOL(dma_ioctl);

EXPORT_SYMBOL(conf_printf2);

EXPORT_SYMBOL(sound_timer_init);
EXPORT_SYMBOL(sound_timer_interrupt);
EXPORT_SYMBOL(sound_timer_syncinterval);

/* Locking */
EXPORT_SYMBOL(sound_locker);

/* MIDI symbols */
EXPORT_SYMBOL(midi_devs);
EXPORT_SYMBOL(num_midis);
EXPORT_SYMBOL(synth_devs);
EXPORT_SYMBOL(num_synths);

EXPORT_SYMBOL(do_midi_msg);
EXPORT_SYMBOL(midi_synth_open);
EXPORT_SYMBOL(midi_synth_close);
EXPORT_SYMBOL(midi_synth_ioctl);
EXPORT_SYMBOL(midi_synth_kill_note);
EXPORT_SYMBOL(midi_synth_start_note);
EXPORT_SYMBOL(midi_synth_set_instr);
EXPORT_SYMBOL(midi_synth_reset);
EXPORT_SYMBOL(midi_synth_hw_control);
EXPORT_SYMBOL(midi_synth_aftertouch);
EXPORT_SYMBOL(midi_synth_controller);
EXPORT_SYMBOL(midi_synth_panning);
EXPORT_SYMBOL(midi_synth_setup_voice);
EXPORT_SYMBOL(midi_synth_send_sysex);
EXPORT_SYMBOL(midi_synth_bender);
