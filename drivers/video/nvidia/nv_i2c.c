/*
 * linux/drivers/video/nvidia/nvidia-i2c.c - nVidia i2c
 *
 * Copyright 2004 Antonino A. Daplas <adaplas @pol.net>
 *
 * Based on rivafb-i2c.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/fb.h>

#include <asm/io.h>

#include "nv_type.h"
#include "nv_local.h"
#include "nv_proto.h"

#include "../edid.h"

static void nvidia_gpio_setscl(void *data, int state)
{
	struct nvidia_i2c_chan *chan = data;
	struct nvidia_par *par = chan->par;
	u32 val;

	VGA_WR08(par->PCIO, 0x3d4, chan->ddc_base + 1);
	val = VGA_RD08(par->PCIO, 0x3d5) & 0xf0;

	if (state)
		val |= 0x20;
	else
		val &= ~0x20;

	VGA_WR08(par->PCIO, 0x3d4, chan->ddc_base + 1);
	VGA_WR08(par->PCIO, 0x3d5, val | 0x1);
}

static void nvidia_gpio_setsda(void *data, int state)
{
	struct nvidia_i2c_chan *chan = (struct nvidia_i2c_chan *)data;
	struct nvidia_par *par = chan->par;
	u32 val;

	VGA_WR08(par->PCIO, 0x3d4, chan->ddc_base + 1);
	val = VGA_RD08(par->PCIO, 0x3d5) & 0xf0;

	if (state)
		val |= 0x10;
	else
		val &= ~0x10;

	VGA_WR08(par->PCIO, 0x3d4, chan->ddc_base + 1);
	VGA_WR08(par->PCIO, 0x3d5, val | 0x1);
}

static int nvidia_gpio_getscl(void *data)
{
	struct nvidia_i2c_chan *chan = (struct nvidia_i2c_chan *)data;
	struct nvidia_par *par = chan->par;
	u32 val = 0;

	VGA_WR08(par->PCIO, 0x3d4, chan->ddc_base);
	if (VGA_RD08(par->PCIO, 0x3d5) & 0x04)
		val = 1;

	val = VGA_RD08(par->PCIO, 0x3d5);

	return val;
}

static int nvidia_gpio_getsda(void *data)
{
	struct nvidia_i2c_chan *chan = (struct nvidia_i2c_chan *)data;
	struct nvidia_par *par = chan->par;
	u32 val = 0;

	VGA_WR08(par->PCIO, 0x3d4, chan->ddc_base);
	if (VGA_RD08(par->PCIO, 0x3d5) & 0x08)
		val = 1;

	return val;
}

#define I2C_ALGO_NVIDIA   0x0e0000
static int nvidia_setup_i2c_bus(struct nvidia_i2c_chan *chan, const char *name)
{
	int rc;

	strcpy(chan->adapter.name, name);
	chan->adapter.owner = THIS_MODULE;
	chan->adapter.id = I2C_ALGO_NVIDIA;
	chan->adapter.algo_data = &chan->algo;
	chan->adapter.dev.parent = &chan->par->pci_dev->dev;
	chan->algo.setsda = nvidia_gpio_setsda;
	chan->algo.setscl = nvidia_gpio_setscl;
	chan->algo.getsda = nvidia_gpio_getsda;
	chan->algo.getscl = nvidia_gpio_getscl;
	chan->algo.udelay = 40;
	chan->algo.timeout = msecs_to_jiffies(2);
	chan->algo.data = chan;

	i2c_set_adapdata(&chan->adapter, chan);

	/* Raise SCL and SDA */
	nvidia_gpio_setsda(chan, 1);
	nvidia_gpio_setscl(chan, 1);
	udelay(20);

	rc = i2c_bit_add_bus(&chan->adapter);
	if (rc == 0)
		dev_dbg(&chan->par->pci_dev->dev,
			"I2C bus %s registered.\n", name);
	else {
		dev_warn(&chan->par->pci_dev->dev,
			 "Failed to register I2C bus %s.\n", name);
		chan->par = NULL;
	}

	return rc;
}

void nvidia_create_i2c_busses(struct nvidia_par *par)
{
	par->bus = 3;

	par->chan[0].par = par;
	par->chan[1].par = par;
	par->chan[2].par = par;

	par->chan[0].ddc_base = 0x3e;
	nvidia_setup_i2c_bus(&par->chan[0], "BUS1");

	par->chan[1].ddc_base = 0x36;
	nvidia_setup_i2c_bus(&par->chan[1], "BUS2");

	par->chan[2].ddc_base = 0x50;
	nvidia_setup_i2c_bus(&par->chan[2], "BUS3");
}

void nvidia_delete_i2c_busses(struct nvidia_par *par)
{
	if (par->chan[0].par)
		i2c_bit_del_bus(&par->chan[0].adapter);
	par->chan[0].par = NULL;

	if (par->chan[1].par)
		i2c_bit_del_bus(&par->chan[1].adapter);
	par->chan[1].par = NULL;

	if (par->chan[2].par)
		i2c_bit_del_bus(&par->chan[2].adapter);
	par->chan[2].par = NULL;

}

static u8 *nvidia_do_probe_i2c_edid(struct nvidia_i2c_chan *chan)
{
	u8 start = 0x0;
	struct i2c_msg msgs[] = {
		{
		 .addr = 0x50,
		 .len = 1,
		 .buf = &start,
		 }, {
		     .addr = 0x50,
		     .flags = I2C_M_RD,
		     .len = EDID_LENGTH,
		     },
	};
	u8 *buf;

	if (!chan->par)
		return NULL;

	buf = kmalloc(EDID_LENGTH, GFP_KERNEL);
	if (!buf) {
		dev_warn(&chan->par->pci_dev->dev, "Out of memory!\n");
		return NULL;
	}
	msgs[1].buf = buf;

	if (i2c_transfer(&chan->adapter, msgs, 2) == 2)
		return buf;
	dev_dbg(&chan->par->pci_dev->dev, "Unable to read EDID block.\n");
	kfree(buf);
	return NULL;
}

int nvidia_probe_i2c_connector(struct nvidia_par *par, int conn, u8 **out_edid)
{
	u8 *edid = NULL;
	int i;

	for (i = 0; i < 3; i++) {
		/* Do the real work */
		edid = nvidia_do_probe_i2c_edid(&par->chan[conn - 1]);
		if (edid)
			break;
	}
	if (out_edid)
		*out_edid = edid;
	if (!edid)
		return 1;

	return 0;
}
