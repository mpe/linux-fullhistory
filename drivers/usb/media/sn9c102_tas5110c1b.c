/***************************************************************************
 * Plug-in for TAS5110C1B image sensor connected to the SN9C10x PC Camera  *
 * Controllers                                                             *
 *                                                                         *
 * Copyright (C) 2004-2005 by Luca Risolia <luca.risolia@studio.unibo.it>  *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 2 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program; if not, write to the Free Software             *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.               *
 ***************************************************************************/

#include "sn9c102_sensor.h"


static struct sn9c102_sensor tas5110c1b;

static struct v4l2_control tas5110c1b_gain;


static int tas5110c1b_init(struct sn9c102_device* cam)
{
	int err = 0;

	err += sn9c102_write_reg(cam, 0x01, 0x01);
	err += sn9c102_write_reg(cam, 0x44, 0x01);
	err += sn9c102_write_reg(cam, 0x00, 0x10);
	err += sn9c102_write_reg(cam, 0x00, 0x11);
	err += sn9c102_write_reg(cam, 0x0a, 0x14);
	err += sn9c102_write_reg(cam, 0x60, 0x17);
	err += sn9c102_write_reg(cam, 0x06, 0x18);
	err += sn9c102_write_reg(cam, 0xfb, 0x19);

	err += sn9c102_i2c_write(cam, 0xc0, 0x80);

	return err;
}


static int tas5110c1b_get_ctrl(struct sn9c102_device* cam, 
                               struct v4l2_control* ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		ctrl->value = tas5110c1b_gain.value;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}


static int tas5110c1b_set_ctrl(struct sn9c102_device* cam, 
                               const struct v4l2_control* ctrl)
{
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		if (!(err += sn9c102_i2c_write(cam, 0x20, 0xf6 - ctrl->value)))
			tas5110c1b_gain.value = ctrl->value;
		break;
	default:
		return -EINVAL;
	}

	return err ? -EIO : 0;
}


static int tas5110c1b_set_crop(struct sn9c102_device* cam, 
                               const struct v4l2_rect* rect)
{
	struct sn9c102_sensor* s = &tas5110c1b;
	int err = 0;
	u8 h_start = (u8)(rect->left - s->cropcap.bounds.left) + 69,
	   v_start = (u8)(rect->top - s->cropcap.bounds.top) + 9;

	err += sn9c102_write_reg(cam, h_start, 0x12);
	err += sn9c102_write_reg(cam, v_start, 0x13);

	/* Don't change ! */
	err += sn9c102_write_reg(cam, 0x14, 0x1a);
	err += sn9c102_write_reg(cam, 0x0a, 0x1b);
	err += sn9c102_write_reg(cam, sn9c102_pread_reg(cam, 0x19), 0x19);

	return err;
}


static int tas5110c1b_set_pix_format(struct sn9c102_device* cam, 
                                     const struct v4l2_pix_format* pix)
{
	int err = 0;

	if (pix->pixelformat == V4L2_PIX_FMT_SN9C10X)
		err += sn9c102_write_reg(cam, 0x2b, 0x19);
	else
		err += sn9c102_write_reg(cam, 0xfb, 0x19);

	return err;
}


static struct sn9c102_sensor tas5110c1b = {
	.name = "TAS5110C1B",
	.maintainer = "Luca Risolia <luca.risolia@studio.unibo.it>",
	.sysfs_ops = SN9C102_I2C_WRITE,
	.frequency = SN9C102_I2C_100KHZ,
	.interface = SN9C102_I2C_3WIRES,
	.init = &tas5110c1b_init,
	.qctrl = {
		{
			.id = V4L2_CID_GAIN,
			.type = V4L2_CTRL_TYPE_INTEGER,
			.name = "global gain",
			.minimum = 0x00,
			.maximum = 0xf6,
			.step = 0x01,
			.default_value = 0x40,
			.flags = 0,
		},
	},
	.set_ctrl = &tas5110c1b_set_ctrl,
	.cropcap = {
		.bounds = {
			.left = 0,
			.top = 0,
			.width = 352,
			.height = 288,
		},
		.defrect = {
			.left = 0,
			.top = 0,
			.width = 352,
			.height = 288,
		},
	},
	.get_ctrl = &tas5110c1b_get_ctrl,
	.set_crop = &tas5110c1b_set_crop,
	.pix_format = {
		.width = 352,
		.height = 288,
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.priv = 8,
	},
	.set_pix_format = &tas5110c1b_set_pix_format
};


int sn9c102_probe_tas5110c1b(struct sn9c102_device* cam)
{
	/* This sensor has no identifiers, so let's attach it anyway */
	sn9c102_attach_sensor(cam, &tas5110c1b);

	/* Sensor detection is based on USB pid/vid */
	if (le16_to_cpu(tas5110c1b.usbdev->descriptor.idProduct) != 0x6001 &&
	    le16_to_cpu(tas5110c1b.usbdev->descriptor.idProduct) != 0x6005 &&
	    le16_to_cpu(tas5110c1b.usbdev->descriptor.idProduct) != 0x60ab)
		return -ENODEV;

	return 0;
}
