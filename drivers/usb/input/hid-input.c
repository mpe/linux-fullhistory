/*
 * $Id: hid-input.c,v 1.2 2002/04/23 00:59:25 rdamazio Exp $
 *
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *
 *  USB HID to Linux Input mapping
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/usb.h>

#undef DEBUG

#include "hid.h"

#define unk	KEY_UNKNOWN

static unsigned char hid_keyboard[256] = {
	  0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
	 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
	  4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
	 27, 43, 43, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
	 65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
	105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
	 72, 73, 82, 83, 86,127,116,117,183,184,185,186,187,188,189,190,
	191,192,193,194,134,138,130,132,128,129,131,137,133,135,136,113,
	115,114,unk,unk,unk,121,unk, 89, 93,124, 92, 94, 95,unk,unk,unk,
	122,123, 90, 91, 85,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,unk,
	 29, 42, 56,125, 97, 54,100,126,164,166,165,163,161,115,114,113,
	150,158,159,128,136,177,178,176,142,152,173,140,unk,unk,unk,unk
};

static struct {
	__s32 x;
	__s32 y;
}  hid_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

#define map_abs(c)	do { usage->code = c; usage->type = EV_ABS; bit = input->absbit; max = ABS_MAX; } while (0)
#define map_rel(c)	do { usage->code = c; usage->type = EV_REL; bit = input->relbit; max = REL_MAX; } while (0)
#define map_key(c)	do { usage->code = c; usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX; } while (0)
#define map_led(c)	do { usage->code = c; usage->type = EV_LED; bit = input->ledbit; max = LED_MAX; } while (0)
#define map_ff(c)	do { usage->code = c; usage->type = EV_FF;  bit = input->ffbit;  max =  FF_MAX; } while (0)

#define map_abs_clear(c)	do { map_abs(c); clear_bit(c, bit); } while (0)
#define map_key_clear(c)	do { map_key(c); clear_bit(c, bit); } while (0)
#define map_ff_effect(c)	do { set_bit(c, input->ffbit); } while (0)

static void hidinput_configure_usage(struct hid_input *hidinput, struct hid_field *field,
				     struct hid_usage *usage)
{
	struct input_dev *input = &hidinput->input;
	struct hid_device *device = hidinput->input.private;
	int max, code;
	unsigned long *bit;

	field->hidinput = hidinput;

#ifdef DEBUG
	printk(KERN_DEBUG "Mapping: ");
	resolv_usage(usage->hid);
	printk(" ---> ");
#endif

	if (field->flags & HID_MAIN_ITEM_CONSTANT)
		goto ignore;

	switch (usage->hid & HID_USAGE_PAGE) {

		case HID_UP_UNDEFINED:
			goto ignore;

		case HID_UP_KEYBOARD:

			set_bit(EV_REP, input->evbit);

			if ((usage->hid & HID_USAGE) < 256) {
				if (!hid_keyboard[usage->hid & HID_USAGE]) goto ignore;
				map_key_clear(hid_keyboard[usage->hid & HID_USAGE]);
			} else
				map_key(KEY_UNKNOWN);

			break;

		case HID_UP_BUTTON:

			code = ((usage->hid - 1) & 0xf);

			switch (field->application) {
				case HID_GD_MOUSE:
				case HID_GD_POINTER:  code += 0x110; break;
				case HID_GD_JOYSTICK: code += 0x120; break;
				case HID_GD_GAMEPAD:  code += 0x130; break;
				default:
					switch (field->physical) {
						case HID_GD_MOUSE:
						case HID_GD_POINTER:  code += 0x110; break;
						case HID_GD_JOYSTICK: code += 0x120; break;
						case HID_GD_GAMEPAD:  code += 0x130; break;
						default:              code += 0x100;
					}
			}

			map_key(code);
			break;

		case HID_UP_GENDESK:

			if ((usage->hid & 0xf0) == 0x80) {	/* SystemControl */
				switch (usage->hid & 0xf) {
					case 0x1: map_key_clear(KEY_POWER);  break;
					case 0x2: map_key_clear(KEY_SLEEP);  break;
					case 0x3: map_key_clear(KEY_WAKEUP); break;
					default: goto unknown;
				}
				break;
			}

			if ((usage->hid & 0xf0) == 0x90) {	/* D-pad */
				switch (usage->hid) {
					case HID_GD_UP:	   usage->hat_dir = 1; break;
					case HID_GD_DOWN:  usage->hat_dir = 5; break;
					case HID_GD_RIGHT: usage->hat_dir = 3; break;
					case HID_GD_LEFT:  usage->hat_dir = 7; break;
					default: goto unknown;
				}
				if (field->dpad) {
					map_abs(field->dpad);
					goto ignore;
				}
				map_abs(ABS_HAT0X);
				break;
			}

			switch (usage->hid) {

				/* These usage IDs map directly to the usage codes. */
				case HID_GD_X: case HID_GD_Y: case HID_GD_Z:
				case HID_GD_RX: case HID_GD_RY: case HID_GD_RZ:
				case HID_GD_SLIDER: case HID_GD_DIAL: case HID_GD_WHEEL:
					if (field->flags & HID_MAIN_ITEM_RELATIVE) 
						map_rel(usage->hid & 0xf);
					else
						map_abs(usage->hid & 0xf);
					break;

				case HID_GD_HATSWITCH:
					usage->hat_min = field->logical_minimum;
					usage->hat_max = field->logical_maximum;
					map_abs(ABS_HAT0X);
					break;

				case HID_GD_START:	map_key_clear(BTN_START);	break;
				case HID_GD_SELECT:	map_key_clear(BTN_SELECT);	break;

				default: goto unknown;
			}

			break;

		case HID_UP_LED:
			if (((usage->hid - 1) & 0xffff) >= LED_MAX)
				goto ignore;
			map_led((usage->hid - 1) & 0xffff);
			break;

		case HID_UP_DIGITIZER:

			switch (usage->hid & 0xff) {

				case 0x30: /* TipPressure */
					if (!test_bit(BTN_TOUCH, input->keybit)) {
						device->quirks |= HID_QUIRK_NOTOUCH;
						set_bit(EV_KEY, input->evbit);
						set_bit(BTN_TOUCH, input->keybit);
					}

					map_abs_clear(ABS_PRESSURE);
					break;

				case 0x32: /* InRange */
					switch (field->physical & 0xff) {
						case 0x21: map_key(BTN_TOOL_MOUSE); break;
						case 0x22: map_key(BTN_TOOL_FINGER); break;
						default: map_key(BTN_TOOL_PEN); break;
					}
					break;

				case 0x3c: /* Invert */
					map_key_clear(BTN_TOOL_RUBBER);
					break;

				case 0x33: /* Touch */
				case 0x42: /* TipSwitch */
				case 0x43: /* TipSwitch2 */
					device->quirks &= ~HID_QUIRK_NOTOUCH;
					map_key_clear(BTN_TOUCH);
					break;

				case 0x44: /* BarrelSwitch */
					map_key_clear(BTN_STYLUS);
					break;

				default:  goto unknown;
			}
			break;

		case HID_UP_CONSUMER:	/* USB HUT v1.1, pages 56-62 */

			switch (usage->hid & HID_USAGE) {
				case 0x000: goto ignore;
				case 0x034: map_key_clear(KEY_SLEEP);		break;
				case 0x036: map_key_clear(BTN_MISC);		break;
				case 0x08a: map_key_clear(KEY_WWW);		break;
				case 0x095: map_key_clear(KEY_HELP);		break;
				case 0x0b0: map_key_clear(KEY_PLAY);		break;
				case 0x0b1: map_key_clear(KEY_PAUSE);		break;
				case 0x0b2: map_key_clear(KEY_RECORD);		break;
				case 0x0b3: map_key_clear(KEY_FASTFORWARD);	break;
				case 0x0b4: map_key_clear(KEY_REWIND);		break;
				case 0x0b5: map_key_clear(KEY_NEXTSONG);	break;
				case 0x0b6: map_key_clear(KEY_PREVIOUSSONG);	break;
				case 0x0b7: map_key_clear(KEY_STOPCD);		break;
				case 0x0b8: map_key_clear(KEY_EJECTCD);		break;
				case 0x0cd: map_key_clear(KEY_PLAYPAUSE);	break;
			        case 0x0e0: map_abs_clear(ABS_VOLUME);		break;
				case 0x0e2: map_key_clear(KEY_MUTE);		break;
				case 0x0e5: map_key_clear(KEY_BASSBOOST);	break;
				case 0x0e9: map_key_clear(KEY_VOLUMEUP);	break;
				case 0x0ea: map_key_clear(KEY_VOLUMEDOWN);	break;
				case 0x183: map_key_clear(KEY_CONFIG);		break;
				case 0x18a: map_key_clear(KEY_MAIL);		break;
				case 0x192: map_key_clear(KEY_CALC);		break;
				case 0x194: map_key_clear(KEY_FILE);		break;
				case 0x21a: map_key_clear(KEY_UNDO);		break;
				case 0x21b: map_key_clear(KEY_COPY);		break;
				case 0x21c: map_key_clear(KEY_CUT);		break;
				case 0x21d: map_key_clear(KEY_PASTE);		break;
				case 0x221: map_key_clear(KEY_FIND);		break;
				case 0x223: map_key_clear(KEY_HOMEPAGE);	break;
				case 0x224: map_key_clear(KEY_BACK);		break;
				case 0x225: map_key_clear(KEY_FORWARD);		break;
				case 0x226: map_key_clear(KEY_STOP);		break;
				case 0x227: map_key_clear(KEY_REFRESH);		break;
				case 0x22a: map_key_clear(KEY_BOOKMARKS);	break;
				case 0x238: map_rel(REL_HWHEEL);		break;
				default:    goto unknown;
			}
			break;

		case HID_UP_HPVENDOR:	/* Reported on a Dutch layout HP5308 */

			set_bit(EV_REP, input->evbit);
			switch (usage->hid & HID_USAGE) {
			        case 0x021: map_key_clear(KEY_PRINT);           break;
				case 0x070: map_key_clear(KEY_HP);		break;
				case 0x071: map_key_clear(KEY_CAMERA);		break;
				case 0x072: map_key_clear(KEY_SOUND);		break;
				case 0x073: map_key_clear(KEY_QUESTION);	break;
				case 0x080: map_key_clear(KEY_EMAIL);		break;
				case 0x081: map_key_clear(KEY_CHAT);		break;
				case 0x082: map_key_clear(KEY_SEARCH);		break;
				case 0x083: map_key_clear(KEY_CONNECT);	        break;
				case 0x084: map_key_clear(KEY_FINANCE);		break;
				case 0x085: map_key_clear(KEY_SPORT);		break;
				case 0x086: map_key_clear(KEY_SHOP);	        break;
				default:    goto ignore;
			}
			break;

		case HID_UP_MSVENDOR:

			goto ignore;
			
		case HID_UP_PID:

			set_bit(EV_FF, input->evbit);
			switch(usage->hid & HID_USAGE) {
				case 0x26: map_ff_effect(FF_CONSTANT);	goto ignore;
				case 0x27: map_ff_effect(FF_RAMP);	goto ignore;
				case 0x28: map_ff_effect(FF_CUSTOM);	goto ignore;
				case 0x30: map_ff_effect(FF_SQUARE);	map_ff_effect(FF_PERIODIC); goto ignore;
				case 0x31: map_ff_effect(FF_SINE);	map_ff_effect(FF_PERIODIC); goto ignore;
				case 0x32: map_ff_effect(FF_TRIANGLE);	map_ff_effect(FF_PERIODIC); goto ignore;
				case 0x33: map_ff_effect(FF_SAW_UP);	map_ff_effect(FF_PERIODIC); goto ignore;
				case 0x34: map_ff_effect(FF_SAW_DOWN);	map_ff_effect(FF_PERIODIC); goto ignore;
				case 0x40: map_ff_effect(FF_SPRING);	goto ignore;
				case 0x41: map_ff_effect(FF_DAMPER);	goto ignore;
				case 0x42: map_ff_effect(FF_INERTIA);	goto ignore;
				case 0x43: map_ff_effect(FF_FRICTION);	goto ignore;
				case 0x7e: map_ff(FF_GAIN);		break;
				case 0x83: input->ff_effects_max = field->value[0]; goto ignore;
				case 0x98: map_ff(FF_AUTOCENTER);	break;
				case 0xa4: map_key_clear(BTN_DEAD);	break;
				default: goto ignore;
			}
			break;

		default:
		unknown:
			if (field->report_size == 1) {
				if (field->report->type == HID_OUTPUT_REPORT) {
					map_led(LED_MISC);
					break;
				}
				map_key(BTN_MISC);
				break;
			}
			if (field->flags & HID_MAIN_ITEM_RELATIVE) {
				map_rel(REL_MISC);
				break;
			}
			map_abs(ABS_MISC);
			break;
	}

	set_bit(usage->type, input->evbit);

	while (usage->code <= max && test_and_set_bit(usage->code, bit))
		usage->code = find_next_zero_bit(bit, max + 1, usage->code);

	if (usage->code > max)
		goto ignore;

	if ((device->quirks & (HID_QUIRK_2WHEEL_MOUSE_HACK_7 | HID_QUIRK_2WHEEL_MOUSE_HACK_5)) &&
		 (usage->type == EV_REL) && (usage->code == REL_WHEEL)) 
			set_bit(REL_HWHEEL, bit);

	if (((device->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_5) && (usage->hid == 0x00090005))
		|| ((device->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_7) && (usage->hid == 0x00090007)))
		goto ignore;

	if (usage->type == EV_ABS) {

		int a = field->logical_minimum;
		int b = field->logical_maximum;

		if ((device->quirks & HID_QUIRK_BADPAD) && (usage->code == ABS_X || usage->code == ABS_Y)) {
			a = field->logical_minimum = 0;
			b = field->logical_maximum = 255;
		}
		
		if (field->application == HID_GD_GAMEPAD || field->application == HID_GD_JOYSTICK)
			input_set_abs_params(input, usage->code, a, b, (b - a) >> 8, (b - a) >> 4);
		else	input_set_abs_params(input, usage->code, a, b, 0, 0);
		
	}

	if (usage->hat_min < usage->hat_max || usage->hat_dir) {
		int i;
		for (i = usage->code; i < usage->code + 2 && i <= max; i++) {
			input_set_abs_params(input, i, -1, 1, 0, 0);
			set_bit(i, input->absbit);
		}
		if (usage->hat_dir && !field->dpad)
			field->dpad = usage->code;
	}

#ifdef DEBUG
	resolv_event(usage->type, usage->code);
	printk("\n");
#endif
	return;

ignore:
#ifdef DEBUG
	printk("IGNORED\n");
#endif
	return;
}

void hidinput_hid_event(struct hid_device *hid, struct hid_field *field, struct hid_usage *usage, __s32 value, struct pt_regs *regs)
{
	struct input_dev *input = &field->hidinput->input;
	int *quirks = &hid->quirks;

	if (!input)
		return;

	input_regs(input, regs);

	if (!usage->type)
		return;

	if (((hid->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_5) && (usage->hid == 0x00090005))
		|| ((hid->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_7) && (usage->hid == 0x00090007))) {
		if (value) hid->quirks |=  HID_QUIRK_2WHEEL_MOUSE_HACK_ON;
		else       hid->quirks &= ~HID_QUIRK_2WHEEL_MOUSE_HACK_ON;
		return;
	}

	if ((hid->quirks & HID_QUIRK_2WHEEL_MOUSE_HACK_ON) && (usage->code == REL_WHEEL)) {
		input_event(input, usage->type, REL_HWHEEL, value);
		return;
	}

	if (usage->hat_min < usage->hat_max || usage->hat_dir) { 
		int hat_dir = usage->hat_dir;
		if (!hat_dir)
			hat_dir = (value - usage->hat_min) * 8 / (usage->hat_max - usage->hat_min + 1) + 1;
		if (hat_dir < 0 || hat_dir > 8) hat_dir = 0;
		input_event(input, usage->type, usage->code    , hid_hat_to_axis[hat_dir].x);
                input_event(input, usage->type, usage->code + 1, hid_hat_to_axis[hat_dir].y);
                return;
        }

	if (usage->hid == (HID_UP_DIGITIZER | 0x003c)) { /* Invert */
		*quirks = value ? (*quirks | HID_QUIRK_INVERT) : (*quirks & ~HID_QUIRK_INVERT);
		return;
	}

	if (usage->hid == (HID_UP_DIGITIZER | 0x0032)) { /* InRange */
		if (value) {
			input_event(input, usage->type, (*quirks & HID_QUIRK_INVERT) ? BTN_TOOL_RUBBER : usage->code, 1);
			return;
		}
		input_event(input, usage->type, usage->code, 0);
		input_event(input, usage->type, BTN_TOOL_RUBBER, 0);
		return;
	}

	if (usage->hid == (HID_UP_DIGITIZER | 0x0030) && (*quirks & HID_QUIRK_NOTOUCH)) { /* Pressure */
		int a = field->logical_minimum;
		int b = field->logical_maximum;
		input_event(input, EV_KEY, BTN_TOUCH, value > a + ((b - a) >> 3));
	}

	if (usage->hid == (HID_UP_PID | 0x83UL)) { /* Simultaneous Effects Max */
		input->ff_effects_max = value;
		dbg("Maximum Effects - %d",input->ff_effects_max);
		return;
	}

	if (usage->hid == (HID_UP_PID | 0x7fUL)) {
		dbg("PID Pool Report\n");
		return;
	}

	if((usage->type == EV_KEY) && (usage->code == 0)) /* Key 0 is "unassigned", not KEY_UNKNOWN */
		return;

	input_event(input, usage->type, usage->code, value);

	if ((field->flags & HID_MAIN_ITEM_RELATIVE) && (usage->type == EV_KEY))
		input_event(input, usage->type, usage->code, 0);
}

void hidinput_report_event(struct hid_device *hid, struct hid_report *report)
{
	struct list_head *lh;
	struct hid_input *hidinput;

	list_for_each (lh, &hid->inputs) {
		hidinput = list_entry(lh, struct hid_input, list);
		input_sync(&hidinput->input);
	}
}

static int hidinput_find_field(struct hid_device *hid, unsigned int type, unsigned int code, struct hid_field **field)
{
	struct hid_report *report;
	int i, j;

	list_for_each_entry(report, &hid->report_enum[HID_OUTPUT_REPORT].report_list, list) {
		for (i = 0; i < report->maxfield; i++) {
			*field = report->field[i];
			for (j = 0; j < (*field)->maxusage; j++)
				if ((*field)->usage[j].type == type && (*field)->usage[j].code == code)
					return j;
		}
	}
	return -1;
}

static int hidinput_input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct hid_device *hid = dev->private;
	struct hid_field *field;
	int offset;

	if (type == EV_FF)
		return hid_ff_event(hid, dev, type, code, value);

	if (type != EV_LED)
		return -1;

	if ((offset = hidinput_find_field(hid, type, code, &field)) == -1) {
		warn("event field not found");
		return -1;
	}

	hid_set_field(field, offset, value);
	hid_submit_report(hid, field->report, USB_DIR_OUT);

	return 0;
}

static int hidinput_open(struct input_dev *dev)
{
	struct hid_device *hid = dev->private;
	return hid_open(hid);
}

static void hidinput_close(struct input_dev *dev)
{
	struct hid_device *hid = dev->private;
	hid_close(hid);
}

/*
 * Register the input device; print a message.
 * Configure the input layer interface
 * Read all reports and initialize the absolute field values.
 */

int hidinput_connect(struct hid_device *hid)
{
	struct usb_device *dev = hid->dev;
	struct hid_report *report;
	struct hid_input *hidinput = NULL;
	int i, j, k;

	INIT_LIST_HEAD(&hid->inputs);

	for (i = 0; i < hid->maxcollection; i++)
		if (hid->collection[i].type == HID_COLLECTION_APPLICATION ||
		    hid->collection[i].type == HID_COLLECTION_PHYSICAL)
		    	if (IS_INPUT_APPLICATION(hid->collection[i].usage))
				break;

	if (i == hid->maxcollection)
		return -1;

	for (k = HID_INPUT_REPORT; k <= HID_OUTPUT_REPORT; k++)
		list_for_each_entry(report, &hid->report_enum[k].report_list, list) {

			if (!report->maxfield)
				continue;

			if (!hidinput) {
				hidinput = kmalloc(sizeof(*hidinput), GFP_KERNEL);
				if (!hidinput) {
					err("Out of memory during hid input probe");
					return -1;
				}
				memset(hidinput, 0, sizeof(*hidinput));

				list_add_tail(&hidinput->list, &hid->inputs);

				hidinput->input.private = hid;
				hidinput->input.event = hidinput_input_event;
				hidinput->input.open = hidinput_open;
				hidinput->input.close = hidinput_close;

				hidinput->input.name = hid->name;
				hidinput->input.phys = hid->phys;
				hidinput->input.uniq = hid->uniq;
				hidinput->input.id.bustype = BUS_USB;
				hidinput->input.id.vendor = le16_to_cpu(dev->descriptor.idVendor);
				hidinput->input.id.product = le16_to_cpu(dev->descriptor.idProduct);
				hidinput->input.id.version = le16_to_cpu(dev->descriptor.bcdDevice);
				hidinput->input.dev = &hid->intf->dev;
			}

			for (i = 0; i < report->maxfield; i++)
				for (j = 0; j < report->field[i]->maxusage; j++)
					hidinput_configure_usage(hidinput, report->field[i],
								 report->field[i]->usage + j);
			
			if (hid->quirks & HID_QUIRK_MULTI_INPUT) {
				/* This will leave hidinput NULL, so that it
				 * allocates another one if we have more inputs on
				 * the same interface. Some devices (e.g. Happ's
				 * UGCI) cram a lot of unrelated inputs into the
				 * same interface. */
				hidinput->report = report;
				input_register_device(&hidinput->input);
				hidinput = NULL;
			}
		}

	/* This only gets called when we are a single-input (most of the
	 * time). IOW, not a HID_QUIRK_MULTI_INPUT. The hid_ff_init() is
	 * only useful in this case, and not for multi-input quirks. */
	if (hidinput) {
		hid_ff_init(hid);
		input_register_device(&hidinput->input);
	}

	return 0;
}

void hidinput_disconnect(struct hid_device *hid)
{
	struct list_head *lh, *next;
	struct hid_input *hidinput;

	list_for_each_safe(lh, next, &hid->inputs) {
		hidinput = list_entry(lh, struct hid_input, list);
		input_unregister_device(&hidinput->input);
		list_del(&hidinput->list);
		kfree(hidinput);
	}
}
