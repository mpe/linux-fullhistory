/*
 **********************************************************************
 *     sblive_voice.h -- EMU Voice Resource Manager header file
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#ifndef _VOICEMGR_H
#define _VOICEMGR_H
/* struct emu_voice.usage flags */
#define VOICEMGR_USAGE_FREE         0x00000000
#define VOICEMGR_USAGE_MIDI         0x00000001
#define VOICEMGR_USAGE_PLAYBACK     0x00000002

/* struct emu_voice.flags flags */
#define VOICEMGR_FLAGS_MONO         0x00000002
#define VOICEMGR_FLAGS_16BIT        0x00000004
#define VOICEMGR_FLAGS_STEREOSLAVE  0x00000008
#define VOICEMGR_FLAGS_VOICEMASTER  0x80000000
#define VOICEMGR_FLAGS_FXRT2        0x00000010

struct voice_param
{
	/* Sound engine */
	u32 start;
	u32 startloop;
	u32 endloop;
	u32 end;

	u16 current_pitch;
	u16 pitch_target;

	u16 current_volume;
	u16 volume_target;

	u16 current_FC;
	u16 FC_target;

	u8 pan_target;
	u8 aux_target;

	/* FX bus amount send */

	u32 send_a;
	u32 send_b;
	u32 send_c;
	u32 send_d;

	/* Envelope engine */
	u16 ampl_env_delay;
	u8 byampl_env_attack;
	u8 byampl_env_hold;
	u8 byampl_env_decay;
	u8 byampl_env_sustain;
	u8 byampl_env_release;

	u16 aux_env_delay;
	u8 byaux_env_attack;
	u8 byaux_env_hold;
	u8 byaux_env_decay;
	u8 byaux_env_sustain;
	u8 byaux_env_release;

	u16 mod_LFO_delay;	/* LFO1 */
	u16 vib_LFO_delay;	/* LFO2 */
	u8 mod_LFO_freq;	/* LFO1 */
	u8 vib_LFO_freq;	/* LFO2 */

	s8 aux_env_to_pitch;
	s8 aux_env_to_FC;
	s8 mod_LFO_to_pitch;
	s8 vib_LFO_to_pitch;
	s8 mod_LFO_to_FC;
	s8 mod_LFO_to_volume;

	u16 sample_pitch;
	u16 initial_pitch;
	u8 initial_attn;
	u8 initial_FC;
};

struct voice_allocdesc
{
	u32 usage;			/* playback, Midi */
	u32 flags;			/* stereo/mono rec/playback 8/16 bit*/
};

struct emu_voice
{
	struct list_head list;

	struct emu10k1_card *card;
	u32 usage;		/* Free, MIDI, playback */
	u32 num;		/* Voice ID */
	u32 flags;		/* Stereo/mono, rec/playback, 8/16 bit */

	struct voice_param params;

	struct emu_voice *linked_voice;	/*for stereo voice*/

	u32 sendhandle[NUM_FXSENDS];
};

struct voice_manager
{
	struct emu10k1_card *card;
	spinlock_t lock;

	struct emu_voice voice[NUM_G];
};

struct voice_cntlset
{
	u32 paramID;
	u32 value;
};

struct emu_voice *emu10k1_voice_alloc(struct voice_manager *, struct voice_allocdesc *);
void emu10k1_voice_free(struct voice_manager *, struct emu_voice *);
void emu10k1_voice_playback_setup(struct emu_voice *);
void emu10k1_voice_start(struct emu_voice *);
void emu10k1_voice_stop(struct emu_voice *);
void emu10k1_voice_setcontrol(struct emu_voice *, struct voice_cntlset *, u32);
void emu10k1_voice_getcontrol(struct emu_voice *, u32, u32 *);

#endif /* _VOICEMGR_H */
