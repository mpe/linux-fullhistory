/*
 *  joy-pci.c  Version 0.4.0
 *
 *  Copyright (c) 1999 Raymond Ingles
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting the
 * gameports on Trident 4DWave and Aureal Vortex soundcards, and
 * analog joysticks connected to them.
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

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/init.h>

MODULE_AUTHOR("Raymond Ingles <sorceror@tir.com>");
MODULE_PARM(js_pci, "3-32i");

#define NUM_CARDS              8
static int js_pci[NUM_CARDS * 4] __initdata = { -1,0,0,0,-1,0,0,0,-1,0,0,0,-1,0,0,0,
						-1,0,0,0,-1,0,0,0,-1,0,0,0,-1,0,0,0 };

static struct js_port * js_pci_port __initdata = NULL;

#include "joy-analog.h"

struct js_pci_info;
typedef void (*js_pci_func)(struct js_pci_info *);

struct js_pci_data {
	int vendor;	/* PCI Vendor ID */
	int model;	/* PCI Model ID */
	int size;	/* Memory / IO region size */
	int lcr;	/* Aureal Legacy Control Register */
	int gcr;	/* Gameport control register */
	int buttons;	/* Buttons location */
	int axes;	/* Axes start */
	int axsize;	/* Axis field size */
	int axmax;	/* Axis field max value */
	js_pci_func init;	
	js_pci_func cleanup;	
	char *name;
};

struct js_pci_info {
        unsigned char *base;
	struct pci_dev *pci_p;
	__u32 lcr;
	struct js_pci_data *data;
        struct js_an_info an;
};

/*
 * js_pci_*_init() sets the info->base field, disables legacy gameports,
 * and enables the enhanced ones.
 */

static void js_pci_4dwave_init(struct js_pci_info *info)
{
	info->base = ioremap(BASE_ADDRESS(info->pci_p, 1), info->data->size);
	pci_read_config_word(info->pci_p, info->data->lcr, (unsigned short *)&info->lcr);
	pci_write_config_word(info->pci_p, info->data->lcr, info->lcr & ~0x20);
	writeb(0x80, info->base + info->data->gcr);
}

static void js_pci_vortex_init(struct js_pci_info *info)
{
	info->base = ioremap(BASE_ADDRESS(info->pci_p, 0), info->data->size);
	info->lcr = readl(info->base + info->data->lcr);
	writel(info->lcr & ~0x8, info->base + info->data->lcr);
	writel(0x40, info->base + info->data->gcr);
}

/*
 * js_pci_*_cleanup does the opposite of the above functions.
 */

static void js_pci_4dwave_cleanup(struct js_pci_info *info)
{
	pci_write_config_word(info->pci_p, info->data->lcr, info->lcr);
	writeb(0x00, info->base + info->data->gcr);
	iounmap(info->base);
}

static void js_pci_vortex_cleanup(struct js_pci_info *info)
{
	writel(info->lcr, info->base + info->data->lcr);
	writel(0x00, info->base + info->data->gcr);
	iounmap(info->base);
}

static struct js_pci_data js_pci_data[] =
{{ PCI_VENDOR_ID_TRIDENT, 0x2000, 0x10000, 0x00044 ,0x00030, 0x00031, 0x00034, 2, 0xffff,
	js_pci_4dwave_init, js_pci_4dwave_cleanup, "Trident 4DWave DX" },
 { PCI_VENDOR_ID_TRIDENT, 0x2001, 0x10000, 0x00044, 0x00030, 0x00031, 0x00034, 2, 0xffff,
	js_pci_4dwave_init, js_pci_4dwave_cleanup, "Trident 4DWave NX" },
 { PCI_VENDOR_ID_AUREAL,  0x0001, 0x40000, 0x1280c, 0x1100c, 0x11008, 0x11010, 4, 0x1fff,
	js_pci_vortex_init, js_pci_vortex_cleanup, "Aureal Vortex1" },
 { PCI_VENDOR_ID_AUREAL,  0x0002, 0x40000, 0x2a00c, 0x2880c, 0x28808, 0x28810, 4, 0x1fff,
	js_pci_vortex_init, js_pci_vortex_cleanup, "Aureal Vortex2" },
 { 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, NULL }};

/*
 * js_pci_read() reads data from a PCI gameport.
 */

static int js_pci_read(void *xinfo, int **axes, int **buttons)
{
        struct js_pci_info *info = xinfo;
	int i;

	info->an.buttons = ~readb(info->base + info->data->buttons) >> 4;

	for (i = 0; i < 4; i++)
		info->an.axes[i] = readw(info->base + info->data->axes + i * info->data->axsize);

        js_an_decode(&info->an, axes, buttons);
        
        return 0;
}

static struct js_port * __init js_pci_probe(struct js_port *port, int type, int number,
					struct pci_dev *pci_p, struct js_pci_data *data)
{
	int i;
	unsigned char u;
	int mask0, mask1, numdev;
        struct js_pci_info iniinfo;
        struct js_pci_info *info = &iniinfo;

	mask0 = mask1 = 0;
	
	for (i = 0; i < NUM_CARDS; i++)
		if (js_pci[i * 4] == type && js_pci[i * 4 + 1] == number) {
			mask0 = js_pci[i * 4 + 2];
			mask1 = js_pci[i * 4 + 3];
			if (!mask0 && !mask1) return port;
			break;
		}

	memset(info, 0,  sizeof(struct js_pci_info));

	info->data = data;
	info->pci_p = pci_p;
	data->init(info);

	mdelay(10);
	js_pci_read(info, NULL, NULL);

	for (i = u = 0; i < 4; i++)
		if (info->an.axes[i] < info->data->axmax) u |= 1 << i;

	if ((numdev = js_an_probe_devs(&info->an, u, mask0, mask1, port)) <= 0)
		return port;

	port = js_register_port(port, info, numdev, sizeof(struct js_pci_info), js_pci_read);

	info = port->info;

	for (i = 0; i < numdev; i++)
		printk(KERN_WARNING "js%d: %s on %s #%d\n",
			js_register_device(port, i, js_an_axes(i, &info->an), js_an_buttons(i, &info->an),
			js_an_name(i, &info->an), THIS_MODULE, NULL, NULL), js_an_name(i, &info->an), data->name, number);

	js_pci_read(info, port->axes, port->buttons);
	js_an_init_corr(&info->an, port->axes, port->corr, 32);

	return port;
}

#ifndef MODULE
int __init js_pci_setup(SETUP_PARAM)
{
        int i;
	SETUP_PARSE(NUM_CARDS*4);
        for (i = 0; i <= ints[0] && i < NUM_CARDS*4; i++)
		js_pci[i] = ints[i+1];
	return 1;
}
__setup("js_pci=", js_pci_setup);
#endif

#ifdef MODULE
int init_module(void)
#else
int __init js_pci_init(void)
#endif
{
        struct pci_dev *pci_p = NULL;
        int i, j;

	for (i = 0; js_pci_data[i].vendor; i++)
		for (j = 0; (pci_p = pci_find_device(js_pci_data[i].vendor, js_pci_data[i].model, pci_p)); j++)
			if (pci_enable_device(pci_p) == 0)
				js_pci_port = js_pci_probe(js_pci_port, i, j, pci_p, js_pci_data + i);

        if (!js_pci_port) {
#ifdef MODULE
                printk(KERN_WARNING "joy-pci: no joysticks found\n");
#endif
                return -ENODEV;
        }

        return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
        int i;
        struct js_pci_info *info;

        while (js_pci_port) {
                for (i = 0; i < js_pci_port->ndevs; i++)
                        if (js_pci_port->devs[i])
                                js_unregister_device(js_pci_port->devs[i]);
                info = js_pci_port->info;
		info->data->cleanup(info);
                js_pci_port = js_unregister_port(js_pci_port);
        }
}
#endif
