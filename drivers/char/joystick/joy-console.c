/*
 *  joy-console.c  Version 0.14V
 *
 *  Copyright (c) 1998 Andree Borrmann
 *  Copyright (c) 1999 John Dahlstrom
 *  Copyright (c) 1999 David Kuder
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * console (NES, SNES, N64, Multi1, Multi2, PSX) gamepads 
 * connected via parallel port. Up to five such controllers 
 * can be connected to one parallel port.
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
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <asm/io.h>
#include <asm/system.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>


MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_PARM(js_console, "2-6i");
MODULE_PARM(js_console_2,"2-6i");
MODULE_PARM(js_console_3,"2-6i");


#define JS_NO_PAD	0
#define JS_SNES_PAD	1
#define JS_NES_PAD	2
#define JS_NES4_PAD	3
#define JS_MULTI_STICK	4
#define JS_MULTI2_STICK	5
#define JS_PSX_PAD	6
#define JS_N64_PAD	7	
#define JS_N64_PAD_DPP	8	/* DirectPad Pro compatible layout */
 
#define JS_MAX_PAD	JS_N64_PAD_DPP

struct js_console_info {
	struct pardevice *port;	/* parport device */
	int pads;		/* total number of pads */
	int pad_to_device[5];   /* pad to js device mapping (js0, js1, etc.) */
	int snes;		/* SNES pads */
	int nes;		/* NES pads */
	int n64;		/* N64 pads */
	int n64_dpp;		/* bits indicate N64 pads treated 14 button, 2 axis */
	int multi;		/* Multi joysticks */
	int multi2;		/* Multi joysticks with 2 buttons */
	int psx;		/* PSX controllers */
};

static struct js_port* js_console_port = NULL;

static int js_console[] __initdata = { -1, 0, 0, 0, 0, 0 };
static int js_console_2[] __initdata = { -1, 0, 0, 0, 0, 0 };
static int js_console_3[] __initdata = { -1, 0, 0, 0, 0, 0 };

static int status_bit[] = { 0x40, 0x80, 0x20, 0x10, 0x08 };

/*
 * NES/SNES support.
 */

#define JS_NES_DELAY	6	/* Delay between bits - 6us */

#define JS_NES_LENGTH	8	/* The NES pads use 8 bits of data */

#define JS_NES_A	0
#define JS_NES_B	1
#define JS_NES_START	2
#define JS_NES_SELECT	3
#define JS_NES_UP	4
#define JS_NES_DOWN	5
#define JS_NES_LEFT	6
#define JS_NES_RIGHT	7

#define JS_SNES_LENGTH	12	/* The SNES true length is 16, but the last 4 bits are unused */

#define JS_SNES_B	0
#define JS_SNES_Y	1
#define JS_SNES_START	2
#define JS_SNES_SELECT	3
#define JS_SNES_UP	4
#define JS_SNES_DOWN	5
#define JS_SNES_LEFT	6
#define JS_SNES_RIGHT	7
#define JS_SNES_A	8
#define JS_SNES_X	9
#define JS_SNES_L	10
#define JS_SNES_R	11

#define JS_NES_POWER	0xfc
#define JS_NES_CLOCK	0x01
#define JS_NES_LATCH	0x02

/*
 * js_nes_read_packet() reads a NES/SNES packet.
 * Each pad uses one bit per byte. So all pads connected to
 * this port are read in parallel.
 */

static void js_nes_read_packet(struct js_console_info *info, int length, unsigned char *data)
{
	int i;

	JS_PAR_DATA_OUT(JS_NES_POWER | JS_NES_CLOCK | JS_NES_LATCH, info->port);
	udelay(JS_NES_DELAY * 2);
	JS_PAR_DATA_OUT(JS_NES_POWER | JS_NES_CLOCK, info->port);

	for (i = 0; i < length; i++) {
		udelay(JS_NES_DELAY);
		JS_PAR_DATA_OUT(JS_NES_POWER, info->port);
		data[i] = JS_PAR_STATUS(info->port) ^ ~JS_PAR_STATUS_INVERT;
		udelay(JS_NES_DELAY);
		JS_PAR_DATA_OUT(JS_NES_POWER | JS_NES_CLOCK, info->port);
	}
}

/*
 * N64 support.
 */

#define JS_N64_A	0
#define JS_N64_B	1
#define JS_N64_Z	2
#define JS_N64_START	3
#define JS_N64_UP	4
#define JS_N64_DOWN	5
#define JS_N64_LEFT	6
#define JS_N64_RIGHT	7
#define JS_N64_UNUSED1	8
#define JS_N64_UNUSED2	9
#define JS_N64_L	10
#define JS_N64_R	11
#define JS_N64_CU	12
#define JS_N64_CD	13
#define JS_N64_CL	14
#define JS_N64_CR	15
#define JS_N64_X	23			/* 16 - 23, signed 8-bit int */
#define JS_N64_Y	31			/* 24 - 31, signed 8-bit int */

#define JS_N64_LENGTH		32		/* N64 bit length, not including stop bit */
#define JS_N64_REQUEST_LENGTH	37		/* transmit request sequence is 9 bits long */
#define JS_N64_DELAY		133		/* delay between transmit request, and response ready (us) */
#define JS_N64_REQUEST		0x1dd1111111ULL /* the request data command (encoded for 000000011) */
#define JS_N64_DWS		3		/* delay between write segments (required for sound playback because of ISA DMA) */
						/* JS_N64_DWS > 24 is known to fail */ 
#define JS_N64_POWER_W		0xe2		/* power during write (transmit request) */
#define JS_N64_POWER_R		0xfd		/* power during read */
#define JS_N64_OUT		0x1d		/* output bits to the 4 pads */
						/* Reading the main axes of any N64 pad is known to fail if the corresponding bit */
						/* in JS_N64_OUT is pulled low on the output port (by any routine) for more */
						/* than 0.123 consecutive ms */
#define JS_N64_CLOCK		0x02		/* clock bits for read */

/* 
 * js_n64_read_packet() reads an N64 packet. 
 * Each pad uses one bit per byte. So all pads connected to this port are read in parallel.
 */

static void js_n64_read_packet(struct js_console_info *info, unsigned char *data)
{
	int i;
	unsigned long flags;

/*
 * Request the pad to transmit data
 */

	save_flags(flags);
	cli();
	for (i = 0; i < JS_N64_REQUEST_LENGTH; i++) {
		JS_PAR_DATA_OUT(JS_N64_POWER_W | ((JS_N64_REQUEST >> i) & 1 ? JS_N64_OUT : 0), info->port);
		udelay(JS_N64_DWS);
	}
	restore_flags(flags);

/*
 * Wait for the pad response to be loaded into the 33-bit register of the adapter
 */

	udelay(JS_N64_DELAY);

/*
 * Grab data (ignoring the last bit, which is a stop bit)
 */

	for (i = 0; i < JS_N64_LENGTH; i++) {
		JS_PAR_DATA_OUT(JS_N64_POWER_R, info->port);
		data[i] = JS_PAR_STATUS(info->port);
		JS_PAR_DATA_OUT(JS_N64_POWER_R | JS_N64_CLOCK, info->port);
	 }

/*
 * We must wait ~0.2 ms here for the controller to reinitialize before the next read request.
 * No worries as long as js_console_read is polled less frequently than this.
 */

}

/*
 * Multisystem joystick support
 */

#define JS_MULTI_LENGTH		5	/* Multi system joystick packet lenght is 5 */
#define JS_MULTI2_LENGTH	6	/* One more bit for one more button */

#define JS_MULTI_UP		0
#define JS_MULTI_DOWN		1
#define JS_MULTI_LEFT		2
#define JS_MULTI_RIGHT		3
#define JS_MULTI_BUTTON		4
#define JS_MULTI_BUTTON2	5

/*
 * js_multi_read_packet() reads a Multisystem joystick packet.
 */

static void js_multi_read_packet(struct js_console_info *info, int length, unsigned char *data)
{
	int i;

	for (i = 0; i < length; i++) {
		JS_PAR_DATA_OUT(~(1 << i), info->port);
		data[i] = JS_PAR_STATUS(info->port) ^ ~JS_PAR_STATUS_INVERT;
		printk(" %d", data[i]);
	}
	printk("\n");
}

/*
 * PSX support
 */

#define JS_PSX_DELAY	10
#define JS_PSX_LENGTH	8    /* talk to the controller in bytes */

#define JS_PSX_NORMAL	0x41 /* Standard Digital controller */
#define JS_PSX_NEGCON	0x23 /* NegCon pad */
#define JS_PSX_MOUSE	0x12 /* PSX Mouse */
#define JS_PSX_ANALOGR	0x73 /* Analog controller in Red mode */
#define JS_PSX_ANALOGG	0x53 /* Analog controller in Green mode */

#define JS_PSX_JOYR	0x02 /* These are for the Analog/Dual Shock controller in RED mode */
#define JS_PSX_JOYL	0x04 /* I'm not sure the exact purpose of these but its in the docs */
#define JS_PSX_SELBUT	0x01 /* Standard buttons on almost all PSX controllers. */
#define JS_PSX_START	0x08
#define JS_PSX_UP	0x10 /* Digital direction pad */
#define JS_PSX_RIGHT	0x20
#define JS_PSX_DOWN	0x40
#define JS_PSX_LEFT	0x80

#define JS_PSX_CLOCK	0x04 /* Pin 3 */
#define JS_PSX_COMMAND	0x01 /* Pin 1 */
#define JS_PSX_POWER	0xf8 /* Pins 5-9 */
#define JS_PSX_SELECT	0x02 /* Pin 2 */
#define JS_PSX_NOPOWER  0x04

/*
 * js_psx_command() writes 8bit command and reads 8bit data from
 * the psx pad.
 */

static int js_psx_command(struct js_console_info *info, int b)
{
	int i, cmd, ret=0;

	cmd = (b&1)?JS_PSX_COMMAND:0;
	for (i=0; i<8; i++) {
		JS_PAR_DATA_OUT(cmd | JS_PSX_POWER, info->port);
		udelay(JS_PSX_DELAY);
		ret |= ((JS_PAR_STATUS(info->port) ^ JS_PAR_STATUS_INVERT ) & info->psx) ? (1<<i) : 0;
		cmd = (b&1)?JS_PSX_COMMAND:0;
		JS_PAR_DATA_OUT(cmd | JS_PSX_CLOCK | JS_PSX_POWER, info->port);
		udelay(JS_PSX_DELAY);
		b >>= 1;
	}
	return ret;
}

/*
 * js_psx_read_packet() reads a whole psx packet and returns
 * device identifier code.
 */

static int js_psx_read_packet(struct js_console_info *info, int length, unsigned char *data)
{
	int i, ret;
	unsigned long flags;

	__save_flags(flags);
	__cli();

	JS_PAR_DATA_OUT(JS_PSX_POWER, info->port);

	JS_PAR_DATA_OUT(JS_PSX_CLOCK | JS_PSX_SELECT | JS_PSX_POWER, info->port);	/* Select pad */
	udelay(JS_PSX_DELAY*2);
	js_psx_command(info, 0x01);					/* Access pad */
	ret = js_psx_command(info, 0x42);				/* Get device id */
	if (js_psx_command(info, 0)=='Z')				/* okay? */
		for (i=0; i<length; i++)
			data[i]=js_psx_command(info, 0);
	else ret = -1;

	JS_PAR_DATA_OUT(JS_PSX_SELECT | JS_PSX_CLOCK | JS_PSX_POWER, info->port);
	__restore_flags(flags);

	return ret;
}


/*
 * js_console_read() reads and analyzes console pads data.
 */

#define JS_MAX_LENGTH JS_N64_LENGTH

static int js_console_read(void *xinfo, int **axes, int **buttons)
{
	struct js_console_info *info = xinfo;
	unsigned char data[JS_MAX_LENGTH];

	int i, j, s;
	int n = 0;

/*
 * NES and SNES pads
 */

	if (info->nes || info->snes) {

		js_nes_read_packet(info, info->snes ? JS_SNES_LENGTH : JS_NES_LENGTH, data);

		for (i = 0; i < 5; i++) {
			s = status_bit[i];
			n = info->pad_to_device[i];
			if (info->nes & s) {
				axes[n][0] = (data[JS_SNES_RIGHT]&s?1:0) - (data[JS_SNES_LEFT]&s?1:0);
				axes[n][1] = (data[JS_SNES_DOWN] &s?1:0) - (data[JS_SNES_UP]  &s?1:0);

				buttons[n][0] = (data[JS_NES_A]    &s?1:0) | (data[JS_NES_B]     &s?2:0)
					      | (data[JS_NES_START]&s?4:0) | (data[JS_NES_SELECT]&s?8:0);
			} else
			if (info->snes & s) {
				axes[n][0] = (data[JS_SNES_RIGHT]&s?1:0) - (data[JS_SNES_LEFT]&s?1:0);
				axes[n][1] = (data[JS_SNES_DOWN] &s?1:0) - (data[JS_SNES_UP]  &s?1:0);

				buttons[n][0] = (data[JS_SNES_A]    &s?0x01:0) | (data[JS_SNES_B]     &s?0x02:0)
					      | (data[JS_SNES_X]    &s?0x04:0) | (data[JS_SNES_Y]     &s?0x08:0)
					      | (data[JS_SNES_L]    &s?0x10:0) | (data[JS_SNES_R]     &s?0x20:0)
					      | (data[JS_SNES_START]&s?0x40:0) | (data[JS_SNES_SELECT]&s?0x80:0);
			}
		}
	}

/*
 * N64 pads
 */

	if (info->n64) {
		if ( (info->nes || info->snes) && (info->n64 & status_bit[0]) ) {
					/* SNES/NES compatibility */
			udelay(240);	/* 200 us delay + 20% tolerance */
		}

		js_n64_read_packet(info, data);

		for (i = 0; i < 5; i++) {
			s = status_bit[i];
			n = info->pad_to_device[i];
			if (info->n64 & s & ~(data[JS_N64_UNUSED1] | data[JS_N64_UNUSED2])) {

				buttons[n][0] = ( ((data[JS_N64_A]&s) ? 0x01:0) | ((data[JS_N64_B] & s ) ? 0x02:0) 
						| ((data[JS_N64_Z]&s) ? 0x04:0) | ((data[JS_N64_L] & s ) ? 0x08:0) 
						| ((data[JS_N64_R]&s) ? 0x10:0) | ((data[JS_N64_START]&s)? 0x20:0)
						| ((data[JS_N64_CU]&s)? 0x40:0) | ((data[JS_N64_CR]&s)   ? 0x80:0)
						| ((data[JS_N64_CD]&s)?0x100:0) | ((data[JS_N64_CL]&s)   ?0x200:0) );

				if (info->n64_dpp & s) { 
					buttons[n][0] |= ((data[JS_N64_LEFT]&s) ? 0x400:0) | ((data[JS_N64_UP] & s)? 0x800:0)
							|((data[JS_N64_RIGHT]&s)?0x1000:0) | ((data[JS_N64_DOWN]&s)?0x2000:0);
				} else {
					axes[n][2] = (data[JS_N64_RIGHT]&s?1:0) - (data[JS_N64_LEFT]&s?1:0);
					axes[n][3] = (data[JS_N64_DOWN] &s?1:0) - (data[JS_N64_UP]  &s?1:0);
				}

						/* build int from bits of signed 8-bit int's */
				j = 7;
				axes[n][0] = (data[JS_N64_X - j] & s) ? ~0x7f : 0;
				axes[n][1] = (data[JS_N64_Y - j] & s) ? ~0x7f : 0;
				while ( j-- > 0 ) {
					axes[n][0] |= (data[JS_N64_X - j] & s) ? (1 << j) : 0; 
					axes[n][1] |= (data[JS_N64_Y - j] & s) ? (1 << j) : 0; 
				}
						/* flip Y-axis for conformity */
				axes[n][1] = -axes[n][1];

			}
		}
	}

/*
 * Multi and Multi2 joysticks
 */

	if (info->multi || info->multi2) {

		js_multi_read_packet(info, info->multi2 ? JS_MULTI2_LENGTH : JS_MULTI_LENGTH, data);

		for (i = 0; i < 5; i++) {
			s = status_bit[i];
			n = info->pad_to_device[i];
			if (info->multi & s) {
				axes[n][0] = (data[JS_MULTI_RIGHT]&s?1:0) - (data[JS_MULTI_LEFT]&s?1:0);
				axes[n][1] = (data[JS_MULTI_DOWN] &s?1:0) - (data[JS_MULTI_UP]  &s?1:0);

				buttons[n][0] = (data[JS_MULTI_BUTTON]&s)?1:0;
			} else
			if (info->multi2 & s) {
				axes[n][0] = (data[JS_MULTI_RIGHT]&s?1:0) - (data[JS_MULTI_LEFT]&s?1:0);
				axes[n][1] = (data[JS_MULTI_DOWN] &s?1:0) - (data[JS_MULTI_UP]  &s?1:0);

				buttons[n][0] = (data[JS_MULTI_BUTTON]&s)?1:0 | (data[JS_MULTI_BUTTON2]&s)?2:0;
			}
		}
	}

/*
 * PSX controllers
 */

	if (info->psx) {

		for ( i = 0; i < 5; i++ )
	       		if ( info->psx & status_bit[i] ) {
				n = info->pad_to_device[i];
				break;
			}

		buttons[n][0] = 0;

 		switch (js_psx_read_packet(info, 6, data)) {

			case JS_PSX_ANALOGR:

				buttons[n][0] |= (data[0]&JS_PSX_JOYL?0:0x800) | (data[0]&JS_PSX_JOYR?0:0x400);

			case JS_PSX_ANALOGG:
	
				axes[n][2] = data[2];
				axes[n][3] = data[3];
				axes[n][4] = data[4];
				axes[n][5] = data[5];

			case JS_PSX_NORMAL:
			case JS_PSX_NEGCON:

				axes[n][0] = (data[0]&JS_PSX_RIGHT?0:1) - (data[0]&JS_PSX_LEFT?0:1);
				axes[n][1] = (data[0]&JS_PSX_DOWN ?0:1) - (data[0]&JS_PSX_UP  ?0:1);

				buttons[n][0] |= ((~data[1]&0xf)<<4) | ((~data[1]&0xf0)>>4) |
					(data[0]&JS_PSX_START?0:0x200) | (data[0]&JS_PSX_SELBUT?0:0x100);
		
				break;

		}
	}

	return 0;
}

/*
 * open callback: claim parport.
 */

int js_console_open(struct js_dev *dev)
{
	struct js_console_info *info = dev->port->info;
	if (!MOD_IN_USE && parport_claim(info->port)) return -EBUSY;
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * close callback: release parport
 */

int js_console_close(struct js_dev *dev)
{
	struct js_console_info *info = dev->port->info;
	MOD_DEC_USE_COUNT;
	if (!MOD_IN_USE) parport_release(info->port);
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	struct js_console_info *info;
	int i;

	while (js_console_port) {
		for (i = 0; i < js_console_port->ndevs; i++)
			if (js_console_port->devs[i])
				js_unregister_device(js_console_port->devs[i]);
		info = js_console_port->info;
		parport_unregister_device(info->port);
		js_console_port = js_unregister_port(js_console_port);
	}
}
#endif

/*
 * js_console_init_corr() initializes correction values of
 * console gamepads.
 */

static void __init js_console_init_corr(int num_axes, int type, struct js_corr *corr)
{
	int i;

	for (i = 0; i < num_axes; i++) {
		corr[i].type = JS_CORR_BROKEN;
		corr[i].prec = 0;
		corr[i].coef[0] = 0;
		corr[i].coef[1] = 0;
		corr[i].coef[2] = (1 << 29);
		corr[i].coef[3] = (1 << 29);
	}

	if (type == JS_N64_PAD || type == JS_N64_PAD_DPP) {
		for (i = 0; i < 2; i++) {
			corr[i].type = JS_CORR_BROKEN;
			corr[i].prec = 0;
			corr[i].coef[0] = 0;
			corr[i].coef[1] = 0;
			corr[i].coef[2] = (1 << 22);
			corr[i].coef[3] = (1 << 22);
		}
	}

	if (type == JS_PSX_ANALOGG || type == JS_PSX_ANALOGR) {
		for (i = 2; i < 6; i++)  {
			corr[i].type = JS_CORR_BROKEN;
			corr[i].prec = 0;
			corr[i].coef[0] = 127 - 2;
			corr[i].coef[1] = 128 + 2;
			corr[i].coef[2] = (1 << 29) / (127 - 4);
			corr[i].coef[3] = (1 << 29) / (127 - 4);
		}
	}
}

/*
 * js_console_probe() probes for console gamepads.
 * Only PSX pads can really be probed for.
 */

static struct js_port __init *js_console_probe(int *config, struct js_port *port)
{
	char *name[5];
	int i, psx, axes[5], buttons[5], type[5];
	unsigned char data[2];			/* used for PSX probe */
	struct js_console_info info;
	struct parport *pp;

	memset(&info, 0, sizeof(struct js_console_info));

	if (config[0] < 0) return port;

	if (config[0] > 0x10)
		for (pp=parport_enumerate(); pp && (pp->base!=config[0]); pp=pp->next);
	else
		for (pp=parport_enumerate(); pp && (config[0]>0); pp=pp->next) config[0]--;

	if (!pp) {
		printk(KERN_ERR "joy-console: no such parport\n");
		return port;
	}

	info.port = parport_register_device(pp, "joystick (console)", NULL, NULL, NULL, PARPORT_DEV_EXCL, NULL);
	if (!info.port)
		return port;

	if (parport_claim(info.port))
	{
		parport_unregister_device(info.port);	/* port currently not available ... */
		return port;
	}

	for (i = 0; i < 5; i++) {

		type[info.pads] = config[i+1];
		info.pad_to_device[i] = info.pads;

		switch(config[i+1]) {

			case JS_NO_PAD:

				break;

			case JS_SNES_PAD:

				axes[info.pads]    = 2;
				buttons[info.pads] = 8;
				name[info.pads]    = "SNES pad";
				info.snes |= status_bit[i];
				info.pads++;
				break;

			case JS_NES_PAD:

				axes[info.pads]    = 2;
				buttons[info.pads] = 4;
				name[info.pads]    = "NES pad";
				info.nes |= status_bit[i];
				info.pads++;
				break;

			case JS_N64_PAD:
				axes[info.pads]    = 4;
				buttons[info.pads] = 10;
				name[info.pads]    = "N64 pad";
				info.n64 |= status_bit[i];
				info.pads++;
				break;

			case JS_N64_PAD_DPP:
				axes[info.pads]    = 2;
				buttons[info.pads] = 14;
				name[info.pads]    = "N64 pad (DPP mode)";
				info.n64 |= status_bit[i];
				info.n64_dpp |= status_bit[i];
				info.pads++;
				break;
				
			case JS_MULTI_STICK:

				axes[info.pads]    = 2;
				buttons[info.pads] = 1;
				name[info.pads]    = "Multisystem joystick";
				info.multi |= status_bit[i];
				info.pads++;
				break;

			case JS_MULTI2_STICK:

				axes[info.pads]    = 2;
				buttons[info.pads] = 2;
				name[info.pads]    = "Multisystem joystick (2 fire)";
				info.multi |= status_bit[i];
				info.pads++;
				break;

			case JS_PSX_PAD:
				
				info.psx |= status_bit[i];
				psx = js_psx_read_packet(&info, 2, data);
				psx = js_psx_read_packet(&info, 2, data);
				info.psx &= ~status_bit[i];

				type[i] = psx;

				switch(psx) {
					case JS_PSX_NORMAL:
						axes[info.pads]    = 2;
						buttons[info.pads] = 10;
						name[info.pads]    = "PSX pad";
						info.psx |= status_bit[i];
						info.pads++;
						break;

					case JS_PSX_ANALOGR:
						axes[info.pads]    = 6;
						buttons[info.pads] = 12;
						name[info.pads]    = "Analog Red PSX pad";
						info.psx |= status_bit[i];
						info.pads++;
						break;

					case JS_PSX_ANALOGG:
						axes[info.pads]    = 6;
						buttons[info.pads] = 10;
						name[info.pads]    = "Analog Green PSX pad";
						info.psx |= status_bit[i];
						info.pads++;
						break;

					case JS_PSX_NEGCON:
						axes[info.pads]    = 2;
						buttons[info.pads] = 10;
						name[info.pads]    = "NegCon PSX pad";
						info.psx |= status_bit[i];
						info.pads++;
						break;

					case JS_PSX_MOUSE:
						printk(KERN_WARNING "joy-psx: PSX mouse not supported...\n");
						break;

					case -1:
						printk(KERN_ERR "joy-psx: no PSX controller found...\n");
						break;

					default:
						printk(KERN_WARNING "joy-psx: PSX controller unknown: 0x%x,"
							" please report to <vojtech@suse.cz>.\n", psx);
				}
				break;

			default:

				printk(KERN_WARNING "joy-console: pad type %d unknown\n", config[i+1]);
		}
	}

	if (!info.pads) {
		parport_release(info.port);
		parport_unregister_device(info.port);
		return port;
	}

	port = js_register_port(port, &info, info.pads, sizeof(struct js_console_info), js_console_read);

	for (i = 0; i < info.pads; i++) {
		printk(KERN_INFO "js%d: %s on %s\n",
			js_register_device(port, i, axes[i], buttons[i], name[i], js_console_open, js_console_close),
			name[i], info.port->port->name);

		js_console_init_corr(axes[i], type[i], port->corr[i]);
	}

	parport_release(info.port);
	return port;
}

#ifndef MODULE
int __init js_console_setup(SETUP_PARAM)
{
	int i;
	SETUP_PARSE(6);
	for (i = 0; i <= ints[0] && i < 6; i++) js_console[i] = ints[i+1];
	return 1;
}
int __init js_console_setup_2(SETUP_PARAM)
{
	int i;
	SETUP_PARSE(6);
	for (i = 0; i <= ints[0] && i < 6; i++) js_console_2[i] = ints[i+1];
	return 1;
}
int __init js_console_setup_3(SETUP_PARAM)
{
	int i;
	SETUP_PARSE(6);
	for (i = 0; i <= ints[0] && i < 6; i++) js_console_3[i] = ints[i+1];
	return 1;
}
__setup("js_console=", js_console_setup);
__setup("js_console_2=", js_console_setup_2);
__setup("js_console_3=", js_console_setup_3);
#endif

#ifdef MODULE
int init_module(void)
#else
int __init js_console_init(void)
#endif
{
	js_console_port = js_console_probe(js_console, js_console_port);
	js_console_port = js_console_probe(js_console_2, js_console_port);
	js_console_port = js_console_probe(js_console_3, js_console_port);

	if (js_console_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-console: no joysticks specified\n");
#endif
	return -ENODEV;
}
