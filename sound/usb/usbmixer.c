/*
 *   (Tentative) USB Audio Driver for ALSA
 *
 *   Mixer control part
 *
 *   Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 *
 *   Many codes borrowed from audio.c by 
 *	    Alan Cox (alan@lxorguk.ukuu.org.uk)
 *	    Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <sound/core.h>
#include <sound/control.h>

#include "usbaudio.h"


/*
 */

typedef struct usb_mixer_build mixer_build_t;
typedef struct usb_audio_term usb_audio_term_t;
typedef struct usb_mixer_elem_info usb_mixer_elem_info_t;


struct usb_audio_term {
	int id;
	int type;
	int channels;
	unsigned int chconfig;
	int name;
};

struct usb_mixer_build {
	snd_usb_audio_t *chip;
	unsigned char *buffer;
	unsigned int buflen;
	unsigned int ctrlif;
	unsigned long unitbitmap[32/sizeof(unsigned long)];
	usb_audio_term_t oterm;
};

struct usb_mixer_elem_info {
	snd_usb_audio_t *chip;
	unsigned int ctrlif;
	unsigned int id;
	unsigned int control;
	unsigned int cmask; /* channel mask bitmap: 0 = master */ 
	int channels;
	int val_type;
	int min, max;
};


enum {
	USB_FEATURE_MUTE = 0,
	USB_FEATURE_VOLUME,
	USB_FEATURE_BASS,
	USB_FEATURE_MID,
	USB_FEATURE_TREBLE,
	USB_FEATURE_GEQ,
	USB_FEATURE_AGC,
	USB_FEATURE_DELAY,
	USB_FEATURE_BASSBOOST,
	FSB_FEATURE_LOUDNESS
};

enum {
	USB_MIXER_BOOLEAN,
	USB_MIXER_INV_BOOLEAN,
	USB_MIXER_S8,
	USB_MIXER_U8,
	USB_MIXER_S16,
	USB_MIXER_U16,
};

#define MAX_CHANNELS	10	/* max logical channels */


/*
 * find an audio control unit with the given unit id
 */
static void *find_audio_control_unit(mixer_build_t *state, unsigned char unit)
{
	unsigned char *p;

	p = NULL;
	while ((p = snd_usb_find_desc(state->buffer, state->buflen, p,
				      USB_DT_CS_INTERFACE, state->ctrlif, -1)) != NULL) {
		if (p[0] >= 4 && p[2] >= INPUT_TERMINAL && p[2] <= EXTENSION_UNIT && p[3] == unit)
			return p;
	}
	return NULL;
}


/*
 * copy a string with the given id
 */
static int snd_usb_copy_string_desc(mixer_build_t *state, int index, char *buf, int maxlen)
{
	int len = usb_string(state->chip->dev, index, buf, maxlen - 1);
	buf[len] = 0;
	return len;
}

/*
 * convert from the byte/word on usb descriptor to the zero-based integer
 */
static int convert_signed_value(usb_mixer_elem_info_t *cval, int val)
{
	switch (cval->val_type) {
	case USB_MIXER_BOOLEAN:
		return !!val;
	case USB_MIXER_INV_BOOLEAN:
		return !val;
	case USB_MIXER_U8:
		val &= 0xff;
		break;
	case USB_MIXER_S8:
		val &= 0xff;
		if (val >= 0x80)
			val -= 0x100;
		break;
	case USB_MIXER_U16:
		val &= 0xffff;
		break;
	case USB_MIXER_S16:
		val &= 0xffff;
		if (val >= 0x8000)
			val -= 0x10000;
		break;
	}
	return val;
}

/*
 * convert from the zero-based int to the byte/word for usb descriptor
 */
static int convert_bytes_value(usb_mixer_elem_info_t *cval, int val)
{
	switch (cval->val_type) {
	case USB_MIXER_BOOLEAN:
		return !!val;
	case USB_MIXER_INV_BOOLEAN:
		return !val;
	case USB_MIXER_S8:
	case USB_MIXER_U8:
		return val & 0xff;
	case USB_MIXER_S16:
	case USB_MIXER_U16:
		return val & 0xffff;
	}
	return 0; /* not reached */
}

static int get_relative_value(usb_mixer_elem_info_t *cval, int val)
{
	if (val < cval->min)
		return 0;
	else if (val > cval->max)
		return cval->max - cval->min;
	else
		return val - cval->min;
}

static int get_abs_value(usb_mixer_elem_info_t *cval, int val)
{
	if (val < 0)
		return cval->min;
	val += cval->min;
	if (val > cval->max)
		return cval->max;
	return val;
}


/*
 * retrieve a mixer value
 */

static int get_ctl_value(usb_mixer_elem_info_t *cval, int request, int validx, int *value_ret)
{
	unsigned char buf[2];
	int val_len = cval->val_type >= USB_MIXER_S16 ? 2 : 1;
 
	if (usb_control_msg(cval->chip->dev, usb_rcvctrlpipe(cval->chip->dev, 0),
			    request,
			    USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
			    validx, cval->ctrlif | (cval->id << 8),
			    buf, val_len, HZ) < 0)
		return -EINVAL;
	*value_ret = convert_signed_value(cval, snd_usb_combine_bytes(buf, val_len));
	return 0;
}

static int get_cur_ctl_value(usb_mixer_elem_info_t *cval, int validx, int *value)
{
	return get_ctl_value(cval, GET_CUR, validx, value);
}

/* channel = 0: master, 1 = first channel */
inline static int get_cur_mix_value(usb_mixer_elem_info_t *cval, int channel, int *value)
{
	return get_ctl_value(cval, GET_CUR, ((cval->control + 1) << 8) | channel, value);
}

/*
 * set a mixer value
 */

static int set_ctl_value(usb_mixer_elem_info_t *cval, int request, int validx, int value_set)
{
	unsigned char buf[2];
	int val_len = cval->val_type >= USB_MIXER_S16 ? 2 : 1;
 
	value_set = convert_bytes_value(cval, value_set);
	buf[0] = value_set & 0xff;
	buf[1] = (value_set >> 8) & 0xff;
	return usb_control_msg(cval->chip->dev, usb_sndctrlpipe(cval->chip->dev, 0),
			       request,
			       USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
			       validx, cval->ctrlif | (cval->id << 8),
			       buf, val_len, HZ);
}

static int set_cur_ctl_value(usb_mixer_elem_info_t *cval, int validx, int value)
{
	return set_ctl_value(cval, SET_CUR, validx, value);
}

inline static int set_cur_mix_value(usb_mixer_elem_info_t *cval, int channel, int value)
{
	return set_ctl_value(cval, SET_CUR, ((cval->control + 1) << 8) | channel, value);
}


/*
 * parser routines begin here... 
 */

static int parse_audio_unit(mixer_build_t *state, int unitid);


/*
 * check if the input/output channel routing is enabled on the given bitmap.
 * used for mixer unit parser
 */
static int check_matrix_bitmap(unsigned char *bmap, int ich, int och, int num_outs)
{
	int idx = ich * num_outs + och;
	return bmap[-(idx >> 3)] & (0x80 >> (idx & 7));
}


/*
 * add an alsa control element
 * search and increment the index until an empty slot is found.
 *
 * if failed, give up and free the control instance.
 */

static int add_control_to_empty(snd_card_t *card, snd_kcontrol_t *kctl)
{
	int err;
	while (snd_ctl_find_id(card, &kctl->id))
		kctl->id.index++;
	if ((err = snd_ctl_add(card, kctl)) < 0) {
		snd_printk(KERN_ERR "cannot add control\n");
		snd_ctl_free_one(kctl);
	}
	return err;
}


/*
 * get a terminal name string
 */

static struct iterm_name_combo {
	int type;
	char *name;
} iterm_names[] = {
	{ 0x0300, "Output" },
	{ 0x0301, "Speaker" },
	{ 0x0302, "Headphone" },
	{ 0x0303, "HMD Audio" },
	{ 0x0304, "Desktop Speaker" },
	{ 0x0305, "Room Speaker" },
	{ 0x0306, "Com Speaker" },
	{ 0x0307, "LFE" },
	{ 0x0600, "External In" },
	{ 0x0601, "Analog In" },
	{ 0x0602, "Digital In" },
	{ 0x0603, "Line" },
	{ 0x0604, "Legacy In" },
	{ 0x0605, "IEC958 In" },
	{ 0x0606, "1394 DA Stream" },
	{ 0x0607, "1394 DV Stream" },
	{ 0x0700, "Embedded" },
	{ 0x0701, "Noise Source" },
	{ 0x0702, "Equalization Noise" },
	{ 0x0703, "CD" },
	{ 0x0704, "DAT" },
	{ 0x0705, "DCC" },
	{ 0x0706, "MiniDisk" },
	{ 0x0707, "Analog Tape" },
	{ 0x0708, "Phonograph" },
	{ 0x0709, "VCR Audio" },
	{ 0x070a, "Video Disk Audio" },
	{ 0x070b, "DVD Audio" },
	{ 0x070c, "TV Tuner Audio" },
	{ 0x070d, "Satellite Rec Audio" },
	{ 0x070e, "Cable Tuner Audio" },
	{ 0x070f, "DSS Audio" },
	{ 0x0710, "Radio Receiver" },
	{ 0x0711, "Radio Transmitter" },
	{ 0x0712, "Multi-Track Recorder" },
	{ 0x0713, "Synthesizer" },
	{ 0 },
};	

static int get_term_name(mixer_build_t *state, usb_audio_term_t *iterm,
			 unsigned char *name, int maxlen, int term_only)
{
	struct iterm_name_combo *names;

	if (iterm->name)
		return snd_usb_copy_string_desc(state, iterm->name, name, maxlen);

	/* virtual type - not a real terminal */
	if (iterm->type >> 16) {
		if (term_only)
			return 0;
		switch (iterm->type >> 16) {
		case SELECTOR_UNIT:
			strcpy(name, "Selector"); return 8;
		case PROCESSING_UNIT:
			strcpy(name, "Process Unit"); return 12;
		case EXTENSION_UNIT:
			strcpy(name, "Ext Unit"); return 8;
		case MIXER_UNIT:
			strcpy(name, "Mixer"); return 5;
		default:
			return sprintf(name, "Unit %d", iterm->id);
		}
	}

	switch (iterm->type & 0xff00) {
	case 0x0100:
		strcpy(name, "PCM"); return 3;
	case 0x0200:
		strcpy(name, "Mic"); return 3;
	case 0x0400:
		strcpy(name, "Headset"); return 7;
	case 0x0500:
		strcpy(name, "Phone"); return 5;
	}

	for (names = iterm_names; names->type; names++)
		if (names->type == iterm->type) {
			strcpy(name, names->name);
			return strlen(names->name);
		}
	return 0;
}


/*
 * parse the source unit recursively until it reaches to a terminal
 * or a branched unit.
 */
static int check_input_term(mixer_build_t *state, int id, usb_audio_term_t *term)
{
	unsigned char *p1;

	memset(term, 0, sizeof(*term));
	while ((p1 = find_audio_control_unit(state, id)) != NULL) {
		term->id = id;
		switch (p1[2]) {
		case INPUT_TERMINAL:
			term->type = combine_word(p1 + 4);
			term->channels = p1[7];
			term->chconfig = combine_word(p1 + 8);
			term->name = p1[11];
			return 0;
		case FEATURE_UNIT:
			id = p1[4];
			break; /* continue to parse */
		case MIXER_UNIT:
			term->type = p1[2] << 16; /* virtual type */
			term->channels = p1[5 + p1[4]];
			term->chconfig = combine_word(p1 + 6 + p1[4]);
			term->name = p1[p1[0] - 1];
			return 0;
		case SELECTOR_UNIT:
			/* call recursively to retrieve the channel info */
			if (check_input_term(state, p1[5], term) < 0)
				return -ENODEV;
			term->type = p1[2] << 16; /* virtual type */
			term->id = id;
			term->name = p1[9 + p1[0] - 1];
			return 0;
		case PROCESSING_UNIT:
		case EXTENSION_UNIT:
			if (p1[6] == 1) {
				id = p1[7];
				break; /* continue to parse */
			}
			term->type = p1[2] << 16; /* virtual type */
			term->channels = p1[7 + p1[6]];
			term->chconfig = combine_word(p1 + 8 + p1[6]);
			term->name = p1[12 + p1[6] + p1[11 + p1[6]]];
			return 0;
		default:
			return -ENODEV;
		}
	}
	return -ENODEV;
}


/*
 * Feature Unit
 */

/* feature unit control information */
struct usb_feature_control_info {
	const char *name;
	unsigned int type;	/* control type (mute, volume, etc.) */
};

static struct usb_feature_control_info audio_feature_info[] = {
	{ "Mute",		USB_MIXER_INV_BOOLEAN },
	{ "Volume",		USB_MIXER_S16 },
	{ "Tone Control - Bass",	USB_MIXER_S8 },
	{ "Tone Control - Mid",		USB_MIXER_S8 },
	{ "Tone Control - Treble",	USB_MIXER_S8 },
	{ "Graphic Equalizer",		USB_MIXER_S8 }, /* FIXME: not implemeted yet */
	{ "Auto Gain Control",	USB_MIXER_BOOLEAN },
	{ "Delay Control",	USB_MIXER_U16 },
	{ "Bass Boost",		USB_MIXER_BOOLEAN },
	{ "Loudness",		USB_MIXER_BOOLEAN },
};


/* private_free callback */
static void usb_mixer_elem_free(snd_kcontrol_t *kctl)
{
	if (kctl->private_data) {
		snd_magic_kfree((void *)kctl->private_data);
		kctl->private_data = 0;
	}
}


/*
 * interface to ALSA control for feature/mixer units
 */

/* get a feature/mixer unit info */
static int mixer_ctl_feature_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{	
	usb_mixer_elem_info_t *cval = snd_magic_cast(usb_mixer_elem_info_t, kcontrol->private_data, return -EINVAL);

	if (cval->val_type == USB_MIXER_BOOLEAN ||
	    cval->val_type == USB_MIXER_INV_BOOLEAN)
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = cval->channels;
	if (cval->val_type == USB_MIXER_BOOLEAN ||
	    cval->val_type == USB_MIXER_INV_BOOLEAN) {
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = 1;
	} else {
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = cval->max - cval->min;
	}
	return 0;
}

/* get the current value from feature/mixer unit */
static int mixer_ctl_feature_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	usb_mixer_elem_info_t *cval = snd_magic_cast(usb_mixer_elem_info_t, kcontrol->private_data, return -EINVAL);
	int c, cnt, val, err;

	if (cval->cmask) {
		cnt = 0;
		for (c = 0; c < MAX_CHANNELS; c++) {
			if (cval->cmask & (1 << c)) {
				err = get_cur_mix_value(cval, c + 1, &val);
				if (err < 0)
					return err;
				val = get_relative_value(cval, val);
				ucontrol->value.integer.value[cnt] = val;
				cnt++;
			}
		}
	} else {
		/* master channel */
		err = get_cur_mix_value(cval, 0, &val);
		if (err < 0)
			return err;
		val = get_relative_value(cval, val);
		ucontrol->value.integer.value[0] = val;
	}
	return 0;
}

/* put the current value to feature/mixer unit */
static int mixer_ctl_feature_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	usb_mixer_elem_info_t *cval = snd_magic_cast(usb_mixer_elem_info_t, kcontrol->private_data, return -EINVAL);
	int c, cnt, val, oval, err;
	int changed = 0;

	if (cval->cmask) {
		cnt = 0;
		for (c = 0; c < MAX_CHANNELS; c++) {
			if (cval->cmask & (1 << c)) {
				err = get_cur_mix_value(cval, c + 1, &oval);
				if (err < 0)
					return err;
				val = ucontrol->value.integer.value[cnt];
				val = get_abs_value(cval, val);
				if (oval != val) {
					set_cur_mix_value(cval, c + 1, val);
					changed = 1;
				}
				cnt++;
			}
		}
	} else {
		/* master channel */
		err = get_cur_mix_value(cval, 0, &oval);
		if (err < 0)
			return err;
		val = ucontrol->value.integer.value[0];
		val = get_abs_value(cval, val);
		if (val != oval) {
			set_cur_mix_value(cval, 0, val);
			changed = 1;
		}
	}
	return changed;
}

static snd_kcontrol_new_t usb_feature_unit_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "", /* will be filled later manually */
	.info = mixer_ctl_feature_info,
	.get = mixer_ctl_feature_get,
	.put = mixer_ctl_feature_put,
};


/*
 * build a feature control
 */

static void build_feature_ctl(mixer_build_t *state, unsigned char *desc,
			      unsigned int ctl_mask, int control,
			      usb_audio_term_t *iterm, int unitid)
{
	int len = 0;
	int nameid = desc[desc[0] - 1];
	snd_kcontrol_t *kctl;
	usb_mixer_elem_info_t *cval;

	if (control == USB_FEATURE_GEQ) {
		/* FIXME: not supported yet */
		return;
	}

	cval = snd_magic_kcalloc(usb_mixer_elem_info_t, 0, GFP_KERNEL);
	if (! cval) {
		snd_printk(KERN_ERR "cannot malloc kcontrol");
		return;
	}
	cval->chip = state->chip;
	cval->ctrlif = state->ctrlif;
	cval->id = unitid;
	cval->control = control;
	cval->cmask = ctl_mask;
	cval->val_type = audio_feature_info[control].type;
	if (ctl_mask == 0)
		cval->channels = 1;	/* master channel */
	else {
		int i, c = 0;
		for (i = 0; i < 16; i++)
			if (ctl_mask & (1 << i))
				c++;
		cval->channels = c;
	}

	/* get min/max values */
	if (cval->val_type == USB_MIXER_BOOLEAN ||
	    cval->val_type == USB_MIXER_INV_BOOLEAN)
		cval->max = 1;
	else {
		if (get_ctl_value(cval, GET_MAX, ((cval->control+1) << 8) | (ctl_mask ? 1 : 0), &cval->max) < 0 ||
		    get_ctl_value(cval, GET_MIN, ((cval->control+1) << 8) | (ctl_mask ? 1 : 0), &cval->min) < 0) {
			snd_printk(KERN_ERR "%d:%d: cannot get min/max values for control %d\n", cval->id, cval->ctrlif, control);
			snd_magic_kfree(cval);
			return;
		}
	}

	kctl = snd_ctl_new1(&usb_feature_unit_ctl, cval);
	if (! kctl) {
		snd_printk(KERN_ERR "cannot malloc kcontrol");
		snd_magic_kfree(cval);
		return;
	}
	kctl->private_free = usb_mixer_elem_free;

	if (nameid)
		len = snd_usb_copy_string_desc(state, nameid, kctl->id.name, sizeof(kctl->id.name));

	switch (control) {
	case USB_FEATURE_MUTE:
	case USB_FEATURE_VOLUME:
		/* determine the control name.  the rule is:
		 * - if a name id is given in descriptor, use it.
		 * - if the connected input can be determined, then use the name
		 *   of terminal type.
		 * - if the connected output can be determined, use it.
		 * - otherwise, anonymous name.
		 */
		if (! nameid) {
			len = get_term_name(state, iterm, kctl->id.name, sizeof(kctl->id.name), 1);
			if (! len)
				len = get_term_name(state, &state->oterm, kctl->id.name, sizeof(kctl->id.name), 1);
			if (! len)
				len = sprintf(kctl->id.name, "Feature %d", unitid);
		}
		/* determine the stream direction:
		 * if the connected output is USB stream, then it's likely a
		 * capture stream.  otherwise it should be playback (hopefully :)
		 */
		if (! (state->oterm.type >> 16)) {
			if ((state->oterm.type & 0xff00) == 0x0100) {
				strcpy(kctl->id.name + len, " Capture");
				len += 8;
			} else {
				strcpy(kctl->id.name + len, " Playback");
				len += 9;
			}
		}
		strcpy(kctl->id.name + len, control == USB_FEATURE_MUTE ? " Switch" : " Volume");
		break;

	default:
		if (! nameid)
			strcpy(kctl->id.name, audio_feature_info[control].name);
		break;
	}

	snd_printdd(KERN_INFO "[%d] FU [%s] ch = %d, val = %d/%d\n",
		    cval->id, kctl->id.name, cval->channels, cval->min, cval->max);
	add_control_to_empty(state->chip->card, kctl);
}



/*
 * parse a feature unit
 *
 * most of controlls are defined here.
 */
static int parse_audio_feature_unit(mixer_build_t *state, int unitid, unsigned char *ftr)
{
	int channels, i, j;
	usb_audio_term_t iterm;
	unsigned int master_bits, first_ch_bits;
	int err, csize;

	if (ftr[0] < 7 || ! (csize = ftr[5]) || ftr[0] < 7 + csize) {
		snd_printk(KERN_ERR "usbaudio: unit %u: invalid FEATURE_UNIT descriptor\n", unitid);
		return -EINVAL;
	}

	/* parse the source unit */
	if ((err = parse_audio_unit(state, ftr[4])) < 0)
		return err;

	/* determine the input source type and name */
	if (check_input_term(state, ftr[4], &iterm) < 0)
		return -EINVAL;

	channels = (ftr[0] - 7) / csize - 1;

	master_bits = snd_usb_combine_bytes(ftr + 6, csize);
	if (channels > 0)
		first_ch_bits = snd_usb_combine_bytes(ftr + 6 + csize, csize);
	else
		first_ch_bits = 0;
	/* check all control types */
	for (i = 0; i < 10; i++) {
		unsigned int ch_bits = 0;
		for (j = 0; j < channels; j++) {
			unsigned int mask = snd_usb_combine_bytes(ftr + 6 + csize * (j+1), csize);
			if (mask & (1 << i))
				ch_bits |= (1 << j);
		}
		if (ch_bits & 1) /* the first channel must be set (for ease of programming) */
			build_feature_ctl(state, ftr, ch_bits, i, &iterm, unitid);
		if (master_bits & (1 << i))
			build_feature_ctl(state, ftr, 0, i, &iterm, unitid);
	}
	
	return 0;
}


/*
 * Mixer Unit
 */

/*
 * build a mixer unit control
 *
 * the callbacks are identical with feature unit.
 * input channel number (zero based) is given in control field instead.
 */

static void build_mixer_unit_ctl(mixer_build_t *state, unsigned char *desc,
				 int in_ch, int unitid)
{
	usb_mixer_elem_info_t *cval;
	int num_ins = desc[4];
	int num_outs = desc[5 + num_ins];
	int i, len;
	snd_kcontrol_t *kctl;
	usb_audio_term_t iterm;

	cval = snd_magic_kcalloc(usb_mixer_elem_info_t, 0, GFP_KERNEL);
	if (! cval)
		return;

	if (check_input_term(state, desc[5 + in_ch], &iterm) < 0)
		return;

	cval->chip = state->chip;
	cval->ctrlif = state->ctrlif;
	cval->id = unitid;
	cval->control = in_ch;
	cval->val_type = USB_MIXER_S16;
	for (i = 0; i < num_outs; i++) {
		if (check_matrix_bitmap(desc + 9 + num_ins, in_ch, i, num_outs)) {
			cval->cmask |= (1 << i);
			cval->channels++;
		}
	}

	/* get min/max values */
	if (get_ctl_value(cval, GET_MAX, ((in_ch+1) << 8) | 1, &cval->max) < 0 ||
	    get_ctl_value(cval, GET_MIN, ((in_ch+1) << 8) | 1, &cval->min) < 0) {
		snd_printk(KERN_ERR "cannot get min/max values for mixer\n");
		snd_magic_kfree(cval);
		return;
	}

	kctl = snd_ctl_new1(&usb_feature_unit_ctl, cval);
	if (! kctl) {
		snd_printk(KERN_ERR "cannot malloc kcontrol");
		snd_magic_kfree(cval);
		return;
	}
	kctl->private_free = usb_mixer_elem_free;

	len = get_term_name(state, &iterm, kctl->id.name, sizeof(kctl->id.name), 0);
	if (! len)
		len = sprintf(kctl->id.name, "Mixer Source %d", in_ch);
	strcpy(kctl->id.name + len, " Volume");

	snd_printdd(KERN_INFO "[%d] MU [%s] ch = %d, val = %d/%d\n",
		    cval->id, kctl->id.name, cval->channels, cval->min, cval->max);
	add_control_to_empty(state->chip->card, kctl);
}


/*
 * parse a mixer unit
 */
static int parse_audio_mixer_unit(mixer_build_t *state, int unitid, unsigned char *desc)
{
	int num_ins, num_outs;
	int i, err;
	if (desc[0] < 12 || ! (num_ins = desc[4]) || ! (num_outs = desc[5 + num_ins]))
		return -EINVAL;

	for (i = 0; i < num_ins; i++) {
		err = parse_audio_unit(state, desc[5 + i]);
		if (err < 0)
			return err;
		if (check_matrix_bitmap(desc + 9 + num_ins, i, 0, num_outs))
			build_mixer_unit_ctl(state, desc, i, unitid);
	}
	return 0;
}


/*
 * Processing Unit / Extension Unit
 */

/* get callback for processing/extension unit */
static int mixer_ctl_procunit_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	usb_mixer_elem_info_t *cval = snd_magic_cast(usb_mixer_elem_info_t, kcontrol->private_data, return -EINVAL);
	int err, val;

	err = get_cur_ctl_value(cval, cval->control, &val);
	if (err < 0)
		return err;
	val = get_relative_value(cval, val);
	ucontrol->value.integer.value[0] = val;
	return 0;
}

/* put callback for processing/extension unit */
static int mixer_ctl_procunit_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	usb_mixer_elem_info_t *cval = snd_magic_cast(usb_mixer_elem_info_t, kcontrol->private_data, return -EINVAL);
	int val, oval, err;

	err = get_cur_ctl_value(cval, cval->control, &oval);
	if (err < 0)
		return err;
	val = ucontrol->value.integer.value[0];
	val = get_abs_value(cval, val);
	if (val != oval) {
		set_cur_ctl_value(cval, cval->control, val);
		return 1;
	}
	return 0;
}

/* alsa control interface for processing/extension unit */
static snd_kcontrol_new_t mixer_procunit_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "", /* will be filled later */
	.info = mixer_ctl_feature_info,
	.get = mixer_ctl_procunit_get,
	.put = mixer_ctl_procunit_put,
};


/*
 * predefined data for processing units
 */
struct procunit_value_info {
	int control;
	char *suffix;
	int val_type;
};

struct procunit_info {
	int type;
	char *name;
	struct procunit_value_info *values;
};

static struct procunit_value_info updown_proc_info[] = {
	{ 0x01, "Switch", USB_MIXER_BOOLEAN },
	{ 0x02, "Mode Select", USB_MIXER_U8 },
	{ 0 }
};
static struct procunit_value_info prologic_proc_info[] = {
	{ 0x01, "Switch", USB_MIXER_BOOLEAN },
	{ 0x02, "Mode Select", USB_MIXER_U8 },
	{ 0 }
};
static struct procunit_value_info threed_enh_proc_info[] = {
	{ 0x01, "Switch", USB_MIXER_BOOLEAN },
	{ 0x02, "Spaciousness", USB_MIXER_U8 },
	{ 0 }
};
static struct procunit_value_info reverb_proc_info[] = {
	{ 0x01, "Switch", USB_MIXER_BOOLEAN },
	{ 0x02, "Level", USB_MIXER_U8 },
	{ 0x03, "Time", USB_MIXER_U16 },
	{ 0x04, "Delay", USB_MIXER_U8 },
	{ 0 }
};
static struct procunit_value_info chorus_proc_info[] = {
	{ 0x01, "Switch", USB_MIXER_BOOLEAN },
	{ 0x02, "Level", USB_MIXER_U8 },
	{ 0x03, "Rate", USB_MIXER_U16 },
	{ 0x04, "Depth", USB_MIXER_U16 },
	{ 0 }
};
static struct procunit_value_info dcr_proc_info[] = {
	{ 0x01, "Switch", USB_MIXER_BOOLEAN },
	{ 0x02, "Ratio", USB_MIXER_U16 },
	{ 0x03, "Max Amp", USB_MIXER_S16 },
	{ 0x04, "Threshold", USB_MIXER_S16 },
	{ 0x05, "Attack Time", USB_MIXER_U16 },
	{ 0x06, "Release Time", USB_MIXER_U16 },
	{ 0 }
};

static struct procunit_info procunits[] = {
	{ 0x01, "Up Down", updown_proc_info },
	{ 0x02, "Dolby Prologic", prologic_proc_info },
	{ 0x03, "3D Stereo Extender", threed_enh_proc_info },
	{ 0x04, "Reverb", reverb_proc_info },
	{ 0x05, "Chorus", chorus_proc_info },
	{ 0x06, "DCR", dcr_proc_info },
	{ 0 },
};

/*
 * build a processing/extension unit
 */
static int build_audio_procunit(mixer_build_t *state, int unitid, unsigned char *dsc, struct procunit_info *list, char *name)
{
	int num_ins = dsc[6];
	usb_mixer_elem_info_t *cval;
	snd_kcontrol_t *kctl;
	int i, err, nameid, type;
	struct procunit_info *info;
	struct procunit_value_info *valinfo;
	static struct procunit_value_info default_value_info[] = {
		{ 0x01, "Switch", USB_MIXER_BOOLEAN },
		{ 0 }
	};
	static struct procunit_info default_info = {
		0, NULL, default_value_info
	};

	if (dsc[0] < 13 || dsc[0] < 13 + num_ins || dsc[0] < num_ins + dsc[11 + num_ins]) {
		snd_printk(KERN_ERR "invalid %s descriptor %d\n", name, unitid);
		return -EINVAL;
	}

	for (i = 0; i < num_ins; i++) {
		if ((err = parse_audio_unit(state, dsc[7 + i])) < 0)
			return err;
	}

	type = combine_word(&dsc[4]);
	if (! type)
		return 0; /* undefined? */

	for (info = list; info && info->type; info++)
		if (info->type == type)
			break;
	if (! info || ! info->type)
		info = &default_info;

	for (valinfo = info->values; valinfo->control; valinfo++) {
		/* FIXME: bitmap might be longer than 8bit */
		if (! (dsc[12 + num_ins] & (1 << (valinfo->control - 1))))
			continue;
		cval = snd_magic_kcalloc(usb_mixer_elem_info_t, 0, GFP_KERNEL);
		if (! cval) {
			snd_printk(KERN_ERR "cannot malloc kcontrol");
			return -ENOMEM;
		}
		cval->chip = state->chip;
		cval->ctrlif = state->ctrlif;
		cval->id = unitid;
		cval->control = valinfo->control;
		cval->val_type = valinfo->val_type;
		cval->channels = 1;

		/* get min/max values */
		if (get_ctl_value(cval, GET_MAX, cval->control, &cval->max) < 0 ||
		    get_ctl_value(cval, GET_MIN, cval->control, &cval->min) < 0) {
			snd_printk(KERN_ERR "cannot get min/max values for proc/ext unit\n");
			snd_magic_kfree(cval);
			continue;
		}

		kctl = snd_ctl_new1(&mixer_procunit_ctl, cval);
		if (! kctl) {
			snd_printk(KERN_ERR "cannot malloc kcontrol");
			snd_magic_kfree(cval);
			return -ENOMEM;
		}
		kctl->private_free = usb_mixer_elem_free;

		if (info->name)
			sprintf(kctl->id.name, "%s %s", info->name, valinfo->suffix);
		else {
			nameid = dsc[12 + num_ins + dsc[11 + num_ins]];
			if (nameid) {
				int len = snd_usb_copy_string_desc(state, nameid, kctl->id.name, sizeof(kctl->id.name));
				strcpy(kctl->id.name + len, valinfo->suffix);
			} else
				sprintf(kctl->id.name, "%s %s", name, valinfo->suffix);
		}

		snd_printdd(KERN_INFO "[%d] PU [%s] ch = %d, val = %d/%d\n",
			    cval->id, kctl->id.name, cval->channels, cval->min, cval->max);
		if ((err = add_control_to_empty(state->chip->card, kctl)) < 0)
			return err;
	}
	return 0;
}


static int parse_audio_processing_unit(mixer_build_t *state, int unitid, unsigned char *desc)
{
	return build_audio_procunit(state, unitid, desc, procunits, "Processing Unit");
}

static int parse_audio_extension_unit(mixer_build_t *state, int unitid, unsigned char *desc)
{
	return build_audio_procunit(state, unitid, desc, NULL, "Extension Unit");
}


/*
 * Selector Unit
 */

/* info callback for selector unit
 * use an enumerator type for routing
 */
static int mixer_ctl_selector_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{	
	usb_mixer_elem_info_t *cval = snd_magic_cast(usb_mixer_elem_info_t, kcontrol->private_data, return -EINVAL);
	char **itemlist = (char **)kcontrol->private_value;

	snd_assert(itemlist, return -EINVAL);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = cval->max;
	if (uinfo->value.enumerated.item >= cval->max)
		uinfo->value.enumerated.item = cval->max - 1;
	strcpy(uinfo->value.enumerated.name, itemlist[uinfo->value.enumerated.item]);
	return 0;
}

/* get callback for selector unit */
static int mixer_ctl_selector_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	usb_mixer_elem_info_t *cval = snd_magic_cast(usb_mixer_elem_info_t, kcontrol->private_data, return -EINVAL);
	int val, err;

	err = get_cur_ctl_value(cval, 0, &val);
	if (err < 0)
		return err;
	val = get_relative_value(cval, val);
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

/* put callback for selector unit */
static int mixer_ctl_selector_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	usb_mixer_elem_info_t *cval = snd_magic_cast(usb_mixer_elem_info_t, kcontrol->private_data, return -EINVAL);
	int val, oval, err;

	err = get_cur_ctl_value(cval, 0, &oval);
	if (err < 0)
		return err;
	val = ucontrol->value.enumerated.item[0];
	val = get_abs_value(cval, val);
	if (val != oval) {
		set_cur_ctl_value(cval, 0, val);
		return 1;
	}
	return 0;
}

/* alsa control interface for selector unit */
static snd_kcontrol_new_t mixer_selectunit_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "", /* will be filled later */
	.info = mixer_ctl_selector_info,
	.get = mixer_ctl_selector_get,
	.put = mixer_ctl_selector_put,
};


/* private free callback.
 * free both private_data and private_value
 */
static void usb_mixer_selector_elem_free(snd_kcontrol_t *kctl)
{
	int i, num_ins = 0;

	if (kctl->private_data) {
		usb_mixer_elem_info_t *cval = snd_magic_cast(usb_mixer_elem_info_t, kctl->private_data,);
		num_ins = cval->max;
		snd_magic_kfree(cval);
		kctl->private_data = 0;
	}
	if (kctl->private_value) {
		char **itemlist = (char **)kctl->private_value;
		for (i = 0; i < num_ins; i++)
			kfree(itemlist[i]);
		kfree(itemlist);
		kctl->private_value = 0;
	}
}

/*
 * parse a selector unit
 */
static int parse_audio_selector_unit(mixer_build_t *state, int unitid, unsigned char *desc)
{
	int num_ins = desc[4];
	int i, err, nameid;
	usb_mixer_elem_info_t *cval;
	snd_kcontrol_t *kctl;
	char **namelist;

	if (! num_ins || desc[0] < 6 + num_ins) {
		snd_printk(KERN_ERR "invalid SELECTOR UNIT descriptor %d\n", unitid);
		return -EINVAL;
	}

	for (i = 0; i < num_ins; i++) {
		if ((err = parse_audio_unit(state, desc[5 + i])) < 0)
			return err;
	}

	cval = snd_magic_kcalloc(usb_mixer_elem_info_t, 0, GFP_KERNEL);
	if (! cval) {
		snd_printk(KERN_ERR "cannot malloc kcontrol");
		return -ENOMEM;
	}
	cval->chip = state->chip;
	cval->ctrlif = state->ctrlif;
	cval->id = unitid;
	cval->val_type = USB_MIXER_U8;
	cval->channels = 1;
	cval->min = 1;
	cval->max = num_ins;

	namelist = kmalloc(sizeof(char *) * num_ins, GFP_KERNEL);
	if (! namelist) {
		snd_printk(KERN_ERR "cannot malloc\n");
		snd_magic_kfree(cval);
		return -ENOMEM;
	}
#define MAX_ITEM_NAME_LEN	64
	for (i = 0; i < num_ins; i++) {
		usb_audio_term_t iterm;
		int len = 0;
		namelist[i] = kmalloc(MAX_ITEM_NAME_LEN, GFP_KERNEL);
		if (! namelist[i]) {
			snd_printk(KERN_ERR "cannot malloc\n");
			while (--i > 0)
				kfree(namelist[i]);
			kfree(namelist);
			snd_magic_kfree(cval);
			return -ENOMEM;
		}
		if (check_input_term(state, desc[5 + i], &iterm) >= 0)
			len = get_term_name(state, &iterm, namelist[i], MAX_ITEM_NAME_LEN, 0);
		if (! len)
			sprintf(namelist[i], "Input %d", i);
	}

	kctl = snd_ctl_new1(&mixer_selectunit_ctl, cval);
	if (! kctl) {
		snd_printk(KERN_ERR "cannot malloc kcontrol");
		snd_magic_kfree(cval);
		return -ENOMEM;
	}
	kctl->private_value = (unsigned long)namelist;
	kctl->private_free = usb_mixer_selector_elem_free;

	nameid = desc[desc[0] - 1];
	if (nameid)
		snd_usb_copy_string_desc(state, nameid, kctl->id.name, sizeof(kctl->id.name));
	else {
		int len = get_term_name(state, &state->oterm,
					kctl->id.name, sizeof(kctl->id.name), 0);
		if (! len)
			len = sprintf(kctl->id.name, "USB");
		if ((state->oterm.type & 0xff00) == 0x0100)
			strcpy(kctl->id.name + len, " Capture Source");
		else
			strcpy(kctl->id.name + len, " Playback Source");
	}

	snd_printdd(KERN_INFO "[%d] SU [%s] items = %d\n",
		    cval->id, kctl->id.name, num_ins);
	if ((err = add_control_to_empty(state->chip->card, kctl)) < 0)
		return err;

	return 0;
}


/*
 * parse an audio unit recursively
 */

static int parse_audio_unit(mixer_build_t *state, int unitid)
{
	unsigned char *p1;

	if (test_and_set_bit(unitid, &state->unitbitmap))
		return 0; /* the unit already visited */

	p1 = find_audio_control_unit(state, unitid);
	if (!p1) {
		snd_printk(KERN_ERR "usbaudio: unit %d not found!\n", unitid);
		return -EINVAL;
	}

	switch (p1[2]) {
	case INPUT_TERMINAL:
		return 0; /* NOP */
	case MIXER_UNIT:
		return parse_audio_mixer_unit(state, unitid, p1);
	case SELECTOR_UNIT:
		return parse_audio_selector_unit(state, unitid, p1);
	case FEATURE_UNIT:
		return parse_audio_feature_unit(state, unitid, p1);
	case PROCESSING_UNIT:
		return parse_audio_processing_unit(state, unitid, p1);
	case EXTENSION_UNIT:
		return parse_audio_extension_unit(state, unitid, p1);
	default:
		snd_printk(KERN_ERR "usbaudio: unit %u: unexpected type 0x%02x\n", unitid, p1[2]);
		return -EINVAL;
	}
}

/*
 * create mixer controls
 *
 * walk through all OUTPUT_TERMINAL descriptors to search for mixers
 */
int snd_usb_create_mixer(snd_usb_audio_t *chip, int ctrlif, unsigned char *buffer, int buflen)
{
	unsigned char *desc;
	mixer_build_t state;
	int err;

	strcpy(chip->card->mixername, "USB Mixer");

	memset(&state, 0, sizeof(state));
	state.chip = chip;
	state.buffer = buffer;
	state.buflen = buflen;
	state.ctrlif = ctrlif;
	desc = NULL;
	while ((desc = snd_usb_find_csint_desc(buffer, buflen, desc, OUTPUT_TERMINAL, ctrlif, -1)) != NULL) {
		if (desc[0] < 9)
			continue; /* invalid descriptor? */
		set_bit(desc[3], &state.unitbitmap);  /* mark terminal ID as visited */
		state.oterm.id = desc[3];
		state.oterm.type = combine_word(&desc[4]);
		state.oterm.name = desc[8];
		err = parse_audio_unit(&state, desc[7]);
		if (err < 0)
			return err;
	}
	return 0;
}
