/*
 *  joy-analog.h  Version 1.2
 *
 *  Copyright (c) 1996-1998 Vojtech Pavlik
 */

/*
 * This file is designed to be included in any joystick driver
 * that communicates with standard analog joysticks. This currently
 * is: joy-analog.c, joy-assasin.c, and joy-lightning.c
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
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#define JS_AN_AXES_STD		0x0f
#define JS_AN_BUTTONS_STD	0xf0

#define JS_AN_BUTTONS_CHF	0x01
#define JS_AN_HAT1_CHF		0x02
#define JS_AN_HAT2_CHF		0x04
#define JS_AN_ANY_CHF		0x07
#define JS_AN_HAT_FCS		0x08
#define JS_AN_HATS_ALL		0x0e
#define JS_AN_BUTTON_PXY_X	0x10
#define JS_AN_BUTTON_PXY_Y	0x20
#define JS_AN_BUTTON_PXY_U	0x40
#define JS_AN_BUTTON_PXY_V	0x80
#define JS_AN_BUTTONS_PXY_XY	0x30
#define JS_AN_BUTTONS_PXY_UV	0xc0
#define JS_AN_BUTTONS_PXY	0xf0

static struct {
	int x;
	int y;
} js_an_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1, 0}, { 0, 1}, {-1, 0}};

struct js_an_info {
	int io;
	unsigned char mask[2];
	unsigned int extensions;
	int axes[4];
	int initial[4];
	unsigned char buttons;
};

/*
 * js_an_decode() decodes analog joystick data.
 */

static void js_an_decode(struct js_an_info *info, int **axes, int **buttons)
{
	int i, j, k;
	int hat1, hat2, hat3;

	hat1 = hat2 = hat3 = 0;
	if (info->mask[0] & JS_AN_BUTTONS_STD) buttons[0][0] = 0;
	if (info->mask[1] & JS_AN_BUTTONS_STD) buttons[1][0] = 0;

	if (info->extensions & JS_AN_ANY_CHF) {
		switch (info->buttons) {
			case 0x1: buttons[0][0] = 0x01; break;
			case 0x2: buttons[0][0] = 0x02; break;
			case 0x4: buttons[0][0] = 0x04; break;
			case 0x8: buttons[0][0] = 0x08; break;
			case 0x5: buttons[0][0] = 0x10; break;
			case 0x9: buttons[0][0] = 0x20; break;
			case 0xf: hat1 = 1; break;
			case 0xb: hat1 = 2; break;
			case 0x7: hat1 = 3; break;
			case 0x3: hat1 = 4; break;
			case 0xe: hat2 = 1; break;
			case 0xa: hat2 = 2; break;
			case 0x6: hat2 = 3; break;
			case 0xc: hat2 = 4; break;
		}
		k = info->extensions & JS_AN_BUTTONS_CHF ? 6 : 4;
	} else {
		for (i = 1; i >= 0; i--)
			for (j = k = 0; j < 4; j++)
				if (info->mask[i] & (0x10 << j))
					buttons[i][0] |= ((info->buttons >> j) & 1) << k++;
	}

	if (info->extensions & JS_AN_BUTTON_PXY_X)
		buttons[0][0] |= (info->axes[2] < (info->initial[2] >> 1)) << k++;
	if (info->extensions & JS_AN_BUTTON_PXY_Y)
		buttons[0][0] |= (info->axes[3] < (info->initial[3] >> 1)) << k++;
	if (info->extensions & JS_AN_BUTTON_PXY_U)
		buttons[0][0] |= (info->axes[2] > (info->initial[2] + (info->initial[2] >> 1))) << k++;
	if (info->extensions & JS_AN_BUTTON_PXY_V)
		buttons[0][0] |= (info->axes[3] > (info->initial[3] + (info->initial[3] >> 1))) << k++;

	if (info->extensions & JS_AN_HAT_FCS)
		for (j = 0; j < 4; j++)
			if (info->axes[3] < ((info->initial[3] * ((j << 1) + 1)) >> 3)) {
				hat3 = j + 1;
				break;
			}

	for (i = 1; i >= 0; i--)
		for (j = k = 0; j < 4; j++)
			if (info->mask[i] & (1 << j))
				axes[i][k++] = info->axes[j];

	if (info->extensions & JS_AN_HAT1_CHF) {
		axes[0][k++] = js_an_hat_to_axis[hat1].x;
		axes[0][k++] = js_an_hat_to_axis[hat1].y;
	}
	if (info->extensions & JS_AN_HAT2_CHF) {
		axes[0][k++] = js_an_hat_to_axis[hat2].x;
		axes[0][k++] = js_an_hat_to_axis[hat2].y;
	}
	if (info->extensions & JS_AN_HAT_FCS) {
		axes[0][k++] = js_an_hat_to_axis[hat3].x;
		axes[0][k++] = js_an_hat_to_axis[hat3].y;
	}
}

/*
 * js_an_count_bits() counts set bits in a byte.
 */

static inline int js_an_count_bits(unsigned long c)
{
	int i = 0;
	while (c) {
		i += c & 1;
		c >>= 1;
	}
	return i;
}

/*
 * js_an_init_corr() initializes the correction values for
 * analog joysticks.
 */

static void __init js_an_init_corr(struct js_an_info *info, int **axes, struct js_corr **corr, int prec)
{
	int i, j, t;

	for (i = 0; i < 2; i++)
	for (j = 0; j < js_an_count_bits(info->mask[i] & 0xf); j++) {

		if ((j == 2 && (info->mask[i] & 0xb) == 0xb) ||
		    (j == 3 && (info->mask[i] & 0xf) == 0xf)) {
			t = (axes[i][0] + axes[i][1]) >> 1;
		} else {
			t = axes[i][j];
		}

		corr[i][j].type = JS_CORR_BROKEN;
		corr[i][j].prec = prec;
		corr[i][j].coef[0] = t - (t >> 3);
		corr[i][j].coef[1] = t + (t >> 3);
		corr[i][j].coef[2] = (1 << 29) / (t - (t >> 2) + 1);
		corr[i][j].coef[3] = (1 << 29) / (t - (t >> 2) + 1);
	}

	i = js_an_count_bits(info->mask[0] & 0xf);

	for (j = i; j < i + (js_an_count_bits(info->extensions & JS_AN_HATS_ALL) << 1); j++) {
		corr[0][j].type = JS_CORR_BROKEN;
		corr[0][j].prec = 0;
		corr[0][j].coef[0] = 0;
		corr[0][j].coef[1] = 0;
		corr[0][j].coef[2] = (1 << 29);
		corr[0][j].coef[3] = (1 << 29);
	}

	for (i = 0; i < 4; i++)
		info->initial[i] = info->axes[i];
}


/*
 * js_an_probe_devs() probes for analog joysticks.
 */

static int __init js_an_probe_devs(struct js_an_info *info, int exist, int mask0, int mask1, struct js_port *port)
{
	info->mask[0] = info->mask[1] = info->extensions = 0;

	if (mask0 || mask1) {
		info->mask[0] = mask0 & (exist | 0xf0);
		info->mask[1] = mask1 & (exist | 0xf0) & ~info->mask[0];
		info->extensions = (mask0 >> 8) & ((exist & JS_AN_HAT_FCS) | ((exist << 2) & JS_AN_BUTTONS_PXY_XY) |
					((exist << 4) & JS_AN_BUTTONS_PXY_UV) | JS_AN_ANY_CHF);
		if (info->extensions & JS_AN_BUTTONS_PXY) {
			info->mask[0] &= ~((info->extensions & JS_AN_BUTTONS_PXY_XY) >> 2);
			info->mask[0] &= ~((info->extensions & JS_AN_BUTTONS_PXY_UV) >> 4);
			info->mask[1] = 0;
		}
		if (info->extensions & JS_AN_HAT_FCS) {
			info->mask[0] &= ~JS_AN_HAT_FCS;
			info->mask[1] = 0;
			info->extensions &= ~(JS_AN_BUTTON_PXY_Y | JS_AN_BUTTON_PXY_U);
		}
		if (info->extensions & JS_AN_ANY_CHF) {
			info->mask[0] |= 0xf0;
			info->mask[1] = 0;
		}
		if (!(info->mask[0] | info->mask[1])) return -1;
	} else {
		switch (exist) {
			case 0x0:
				return -1;
			case 0x3:
				info->mask[0] = 0xf3; /* joystick 0, assuming 4-button */
				break;
			case 0xb:
				info->mask[0] = 0xfb; /* 3-axis, 4-button joystick */
				break;
			case 0xc:
				info->mask[0] = 0xcc; /* joystick 1 */
				break;
			case 0xf:
				info->mask[0] = 0x33; /* joysticks 0 and 1 */
				info->mask[1] = 0xcc;
				break;
			default:
				printk(KERN_WARNING "joy-analog: Unknown joystick device detected "
					"(data=%#x), contact <vojtech@ucw.cz>\n", exist);
				return -1;
		}
	}

	return !!info->mask[0] + !!info->mask[1];
}

/*
 * js_an_axes() returns the number of axes for an analog joystick.
 */

static inline int js_an_axes(int i, struct js_an_info *info)
{
	return js_an_count_bits(info->mask[i] & 0x0f) + js_an_count_bits(info->extensions & JS_AN_HATS_ALL) * 2;
}

/*
 * js_an_buttons() returns the number of buttons for an analog joystick.
 */

static inline int js_an_buttons(int i, struct js_an_info *info)
{
	return js_an_count_bits(info->mask[i] & 0xf0) +
	       (info->extensions & JS_AN_BUTTONS_CHF) * 2 +
	       js_an_count_bits(info->extensions & JS_AN_BUTTONS_PXY);
}

/*
 * js_an_name() constructs a name for an analog joystick.
 */

static char js_an_name_buf[128] __initdata = "";

static char __init *js_an_name(int i, struct js_an_info *info)
{

	sprintf(js_an_name_buf, "Analog %d-axis %d-button",
		js_an_count_bits(info->mask[i] & 0x0f),
		js_an_buttons(i, info));

	if (info->extensions & JS_AN_HATS_ALL)
		sprintf(js_an_name_buf, "%s %d-hat",
			js_an_name_buf,
			js_an_count_bits(info->extensions & JS_AN_HATS_ALL));

	strcat(js_an_name_buf, " joystick");

	if (info->extensions)
		sprintf(js_an_name_buf, "%s with%s%s%s extensions",
			js_an_name_buf,
			info->extensions & JS_AN_ANY_CHF ? " CHF" : "",
			info->extensions & JS_AN_HAT_FCS ? " FCS" : "",
			info->extensions & JS_AN_BUTTONS_PXY ? " XY-button" : "");

	return js_an_name_buf;
}
