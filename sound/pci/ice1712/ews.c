/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
 *
 *   Lowlevel functions for Terratec EWS88MT/D, EWX24/96, DMX 6Fire
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@suse.cz>
 *                    2002 Takashi Iwai <tiwai@suse.de>
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
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/cs8427.h>
#include <sound/asoundef.h>

#include "ice1712.h"
#include "ews.h"

#define SND_CS8404
#include <sound/cs8403.h>

/*
 * access via i2c mode (for EWX 24/96, EWS 88MT&D)
 */

/* send SDA and SCL */
static void ewx_i2c_setlines(snd_i2c_bus_t *bus, int clk, int data)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, bus->private_data, return);
	unsigned char tmp = 0;
	if (clk)
		tmp |= ICE1712_EWX2496_SERIAL_CLOCK;
	if (data)
		tmp |= ICE1712_EWX2496_SERIAL_DATA;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, tmp);
	udelay(5);
}

static int ewx_i2c_getclock(snd_i2c_bus_t *bus)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, bus->private_data, return -EIO);
	return snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA) & ICE1712_EWX2496_SERIAL_CLOCK ? 1 : 0;
}

static int ewx_i2c_getdata(snd_i2c_bus_t *bus, int ack)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, bus->private_data, return -EIO);
	int bit;
	/* set RW pin to low */
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ~ICE1712_EWX2496_RW);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, 0);
	if (ack)
		udelay(5);
	bit = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA) & ICE1712_EWX2496_SERIAL_DATA ? 1 : 0;
	/* set RW pin to high */
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, ICE1712_EWX2496_RW);
	/* reset write mask */
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ~ICE1712_EWX2496_SERIAL_CLOCK);
	return bit;
}

static void ewx_i2c_start(snd_i2c_bus_t *bus)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, bus->private_data, return);
	unsigned char mask;

	snd_ice1712_save_gpio_status(ice, (unsigned char *)&bus->private_value);
	/* set RW high */
	mask = ICE1712_EWX2496_RW;
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_EWX2496:
		mask |= ICE1712_EWX2496_AK4524_CS; /* CS high also */
		break;
	case ICE1712_SUBDEVICE_DMX6FIRE:
		mask |= ICE1712_6FIRE_AK4524_CS_MASK; /* CS high also */
		break;
	}
	snd_ice1712_gpio_write_bits(ice, mask, mask);
}

static void ewx_i2c_stop(snd_i2c_bus_t *bus)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, bus->private_data, return);
	snd_ice1712_restore_gpio_status(ice, (unsigned char *)&bus->private_value);
}

static void ewx_i2c_direction(snd_i2c_bus_t *bus, int clock, int data)
{
	ice1712_t *ice = snd_magic_cast(ice1712_t, bus->private_data, return);
	unsigned char mask = 0;

	if (clock)
		mask |= ICE1712_EWX2496_SERIAL_CLOCK; /* write SCL */
	if (data)
		mask |= ICE1712_EWX2496_SERIAL_DATA; /* write SDA */
	ice->gpio_direction &= ~(ICE1712_EWX2496_SERIAL_CLOCK|ICE1712_EWX2496_SERIAL_DATA);
	ice->gpio_direction |= mask;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION, ice->gpio_direction);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ~mask);
}

static snd_i2c_bit_ops_t snd_ice1712_ewx_cs8427_bit_ops = {
	.start = ewx_i2c_start,
	.stop = ewx_i2c_stop,
	.direction = ewx_i2c_direction,
	.setlines = ewx_i2c_setlines,
	.getclock = ewx_i2c_getclock,
	.getdata = ewx_i2c_getdata,
};


/*
 * AK4524 access
 */

/* AK4524 chip select; address 0x48 bit 0-3 */
static int snd_ice1712_ews88mt_chip_select(ice1712_t *ice, int chip_mask)
{
	unsigned char data, ndata;

	snd_assert(chip_mask >= 0 && chip_mask <= 0x0f, return -EINVAL);
	snd_i2c_lock(ice->i2c);
	if (snd_i2c_readbytes(ice->i2cdevs[1], &data, 1) != 1)
		goto __error;
	ndata = (data & 0xf0) | chip_mask;
	if (ndata != data)
		if (snd_i2c_sendbytes(ice->i2cdevs[1], &ndata, 1) != 1)
			goto __error;
	snd_i2c_unlock(ice->i2c);
	return 0;

     __error:
	snd_i2c_unlock(ice->i2c);
	snd_printk(KERN_ERR "AK4524 chip select failed, check cable to the front module\n");
	return -EIO;
}

/* start callback for EWS88MT, needs to select a certain chip mask */
static int ews88mt_ak4524_start(ice1712_t *ice, unsigned char *saved, int chip)
{
	unsigned char tmp;
	/* assert AK4524 CS */
	if (snd_ice1712_ews88mt_chip_select(ice, ~(1 << chip) & 0x0f) < 0)
		return -EINVAL;
	snd_ice1712_save_gpio_status(ice, saved);
	tmp = ICE1712_EWS88_SERIAL_DATA |
		ICE1712_EWS88_SERIAL_CLOCK |
		ICE1712_EWS88_RW;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION,
			  ice->gpio_direction | tmp);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ~tmp);
	return 0;
}

/* stop callback for EWS88MT, needs to deselect chip mask */
static void ews88mt_ak4524_stop(ice1712_t *ice, unsigned char *saved)
{
	snd_ice1712_restore_gpio_status(ice, saved);
	udelay(1);
	snd_ice1712_ews88mt_chip_select(ice, 0x0f);
}

/* start callback for EWX24/96 */
static int ewx2496_ak4524_start(ice1712_t *ice, unsigned char *saved, int chip)
{
	unsigned char tmp;
	snd_ice1712_save_gpio_status(ice, saved);
	tmp =  ICE1712_EWX2496_SERIAL_DATA |
		ICE1712_EWX2496_SERIAL_CLOCK |
		ICE1712_EWX2496_AK4524_CS |
		ICE1712_EWX2496_RW;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION,
			  ice->gpio_direction | tmp);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ~tmp);
	return 0;
}

/* start callback for DMX 6fire */
static int dmx6fire_ak4524_start(ice1712_t *ice, unsigned char *saved, int chip)
{
	unsigned char tmp;
	snd_ice1712_save_gpio_status(ice, saved);
	tmp = ice->ak4524.cs_mask = ice->ak4524.cs_addr = (1 << chip) & ICE1712_6FIRE_AK4524_CS_MASK;
	tmp |= ICE1712_6FIRE_SERIAL_DATA |
		ICE1712_6FIRE_SERIAL_CLOCK |
		ICE1712_6FIRE_RW;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION,
			  ice->gpio_direction | tmp);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ~tmp);
	return 0;
}


/*
 * CS8404 interface on EWS88MT/D
 */

static void snd_ice1712_ews_cs8404_spdif_write(ice1712_t *ice, unsigned char bits)
{
	unsigned char bytes[2];

	snd_i2c_lock(ice->i2c);
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_EWS88MT:
		snd_runtime_check(snd_i2c_sendbytes(ice->cs8404, &bits, 1) == 1, goto _error);
		break;
	case ICE1712_SUBDEVICE_EWS88D:
		snd_runtime_check(snd_i2c_readbytes(ice->i2cdevs[0], bytes, 2) == 2, goto _error);
		if (bits != bytes[1]) {
			bytes[1] = bits;
			snd_runtime_check(snd_i2c_readbytes(ice->i2cdevs[0], bytes, 2) == 2, goto _error);
		}
		break;
	}
 _error:
	snd_i2c_unlock(ice->i2c);
}

/*
 */

static void ews88_spdif_default_get(ice1712_t *ice, snd_ctl_elem_value_t * ucontrol)
{
	snd_cs8404_decode_spdif_bits(&ucontrol->value.iec958, ice->spdif.cs8403_bits);
}

static int ews88_spdif_default_put(ice1712_t *ice, snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	unsigned int val;
	int change;

	val = snd_cs8404_encode_spdif_bits(&ucontrol->value.iec958);
	spin_lock_irqsave(&ice->reg_lock, flags);
	change = ice->spdif.cs8403_bits != val;
	ice->spdif.cs8403_bits = val;
	if (change && ice->playback_pro_substream == NULL) {
		spin_unlock_irqrestore(&ice->reg_lock, flags);
		snd_ice1712_ews_cs8404_spdif_write(ice, val);
	} else {
		spin_unlock_irqrestore(&ice->reg_lock, flags);
	}
	return change;
}

static void ews88_spdif_stream_get(ice1712_t *ice, snd_ctl_elem_value_t * ucontrol)
{
	snd_cs8404_decode_spdif_bits(&ucontrol->value.iec958, ice->spdif.cs8403_stream_bits);
}

static int ews88_spdif_stream_put(ice1712_t *ice, snd_ctl_elem_value_t * ucontrol)
{
	unsigned long flags;
	unsigned int val;
	int change;

	val = snd_cs8404_encode_spdif_bits(&ucontrol->value.iec958);
	spin_lock_irqsave(&ice->reg_lock, flags);
	change = ice->spdif.cs8403_stream_bits != val;
	ice->spdif.cs8403_stream_bits = val;
	if (change && ice->playback_pro_substream != NULL) {
		spin_unlock_irqrestore(&ice->reg_lock, flags);
		snd_ice1712_ews_cs8404_spdif_write(ice, val);
	} else {
		spin_unlock_irqrestore(&ice->reg_lock, flags);
	}
	return change;
}


/* open callback */
static void ews88_open_spdif(ice1712_t *ice, snd_pcm_substream_t * substream)
{
	ice->spdif.cs8403_stream_bits = ice->spdif.cs8403_bits;
}

/* set up SPDIF for EWS88MT / EWS88D */
static void ews88_setup_spdif(ice1712_t *ice, snd_pcm_substream_t * substream)
{
	unsigned long flags;
	unsigned char tmp;
	int change;

	spin_lock_irqsave(&ice->reg_lock, flags);
	tmp = ice->spdif.cs8403_stream_bits;
	if (tmp & 0x10)		/* consumer */
		tmp &= (tmp & 0x01) ? ~0x06 : ~0x60;
	switch (substream->runtime->rate) {
	case 32000: tmp |= (tmp & 0x01) ? 0x02 : 0x00; break;
	case 44100: tmp |= (tmp & 0x01) ? 0x06 : 0x40; break;
	case 48000: tmp |= (tmp & 0x01) ? 0x04 : 0x20; break;
	default: tmp |= (tmp & 0x01) ? 0x06 : 0x40; break;
	}
	change = ice->spdif.cs8403_stream_bits != tmp;
	ice->spdif.cs8403_stream_bits = tmp;
	spin_unlock_irqrestore(&ice->reg_lock, flags);
	if (change)
		snd_ctl_notify(ice->card, SNDRV_CTL_EVENT_MASK_VALUE, &ice->spdif.stream_ctl->id);
	snd_ice1712_ews_cs8404_spdif_write(ice, tmp);
}


/*
 * initialize the chip
 */

static int __devinit snd_ice1712_ews_init(ice1712_t *ice)
{
	int err;
	ak4524_t *ak;

	/* set the analog DACs */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_EWX2496:
		ice->num_total_dacs = 2;
		break;	
	case ICE1712_SUBDEVICE_EWS88MT:
		ice->num_total_dacs = 8;
		break;
	case ICE1712_SUBDEVICE_EWS88D:
		break; /* no analog */
	case ICE1712_SUBDEVICE_DMX6FIRE:
		ice->num_total_dacs = 6;
		break;
	}

	/* create i2c */
	if ((err = snd_i2c_bus_create(ice->card, "ICE1712 GPIO 1", NULL, &ice->i2c)) < 0) {
		snd_printk("unable to create I2C bus\n");
		return err;
	}
	ice->i2c->private_data = ice;
	ice->i2c->hw_ops.bit = &snd_ice1712_ewx_cs8427_bit_ops;

	/* create i2c devices */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_DMX6FIRE:
		if ((err = snd_i2c_device_create(ice->i2c, "PCF9554", ICE1712_6FIRE_PCF9554_ADDR, &ice->i2cdevs[0])) < 0) {
			snd_printk("PCF9554 initialization failed\n");
			return err;
		}
		break;
	case ICE1712_SUBDEVICE_EWS88MT:
		if ((err = snd_i2c_device_create(ice->i2c, "CS8404", ICE1712_EWS88MT_CS8404_ADDR, &ice->cs8404)) < 0)
			return err;
		if ((err = snd_i2c_device_create(ice->i2c, "PCF8574 (1st)", ICE1712_EWS88MT_INPUT_ADDR, &ice->i2cdevs[0])) < 0)
			return err;
		if ((err = snd_i2c_device_create(ice->i2c, "PCF8574 (2nd)", ICE1712_EWS88MT_OUTPUT_ADDR, &ice->i2cdevs[1])) < 0)
			return err;
		/* Check if the front module is connected */
		if ((err = snd_ice1712_ews88mt_chip_select(ice, 0x0f)) < 0)
			return err;
		break;
	case ICE1712_SUBDEVICE_EWS88D:
		if ((err = snd_i2c_device_create(ice->i2c, "PCF8575", ICE1712_EWS88D_PCF_ADDR, &ice->i2cdevs[0])) < 0)
			return err;
		break;
	}

	/* set up SPDIF interface */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_EWX2496:
		if ((err = snd_ice1712_init_cs8427(ice, CS8427_BASE_ADDR)) < 0)
			return err;
		break;
#if 0 // XXX not working...
	case ICE1712_SUBDEVICE_DMX6FIRE:
		if ((err = snd_ice1712_init_cs8427(ice, ICE1712_6FIRE_CS8427_ADDR)) < 0)
			return err;
#endif
	case ICE1712_SUBDEVICE_EWS88MT:
	case ICE1712_SUBDEVICE_EWS88D:
		/* set up CS8404 */
		ice->spdif.ops.open = ews88_open_spdif;
		ice->spdif.ops.setup = ews88_setup_spdif;
		ice->spdif.ops.default_get = ews88_spdif_default_get;
		ice->spdif.ops.default_put = ews88_spdif_default_put;
		ice->spdif.ops.stream_get = ews88_spdif_stream_get;
		ice->spdif.ops.stream_put = ews88_spdif_stream_put;
		/* Set spdif defaults */
		snd_ice1712_ews_cs8404_spdif_write(ice, ice->spdif.cs8403_bits);
		break;
	}

	/* analog section */
	ak = &ice->ak4524;
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_EWS88MT:
		ak->num_adcs = ak->num_dacs = 8;
		ak->type = SND_AK4524;
		ak->cif = 1; /* CIF high */
		ak->data_mask = ICE1712_EWS88_SERIAL_DATA;
		ak->clk_mask = ICE1712_EWS88_SERIAL_CLOCK;
		ak->cs_mask = ak->cs_addr = ak->cs_none = 0; /* no chip select on gpio */
		ak->add_flags = ICE1712_EWS88_RW; /* set rw bit high */
		ak->mask_flags = 0;
		ak->ops.start = ews88mt_ak4524_start;
		ak->ops.stop = ews88mt_ak4524_stop;
		snd_ice1712_ak4524_init(ice);
		break;
	case ICE1712_SUBDEVICE_EWX2496:
		ak->num_adcs = ak->num_dacs = 2;
		ak->type = SND_AK4524;
		ak->cif = 1; /* CIF high */
		ak->data_mask = ICE1712_EWS88_SERIAL_DATA;
		ak->clk_mask = ICE1712_EWS88_SERIAL_CLOCK;
		ak->cs_mask = ak->cs_addr = ICE1712_EWX2496_AK4524_CS;
		ak->cs_none = 0;
		ak->add_flags = ICE1712_EWS88_RW; /* set rw bit high */
		ak->mask_flags = 0;
		ak->ops.start = ewx2496_ak4524_start;
		snd_ice1712_ak4524_init(ice);
		break;
	case ICE1712_SUBDEVICE_DMX6FIRE:
		ak->num_adcs = ak->num_dacs = 6;
		ak->type = SND_AK4524;
		ak->cif = 1; /* CIF high */
		ak->data_mask = ICE1712_6FIRE_SERIAL_DATA;
		ak->clk_mask = ICE1712_6FIRE_SERIAL_CLOCK;
		ak->cs_mask = ak->cs_addr = 0; /* set later */
		ak->cs_none = 0;
		ak->add_flags = ICE1712_6FIRE_RW; /* set rw bit high */
		ak->mask_flags = 0;
		ak->ops.start = dmx6fire_ak4524_start;
		snd_ice1712_ak4524_init(ice);
		break;
	}

	return 0;
}

/*
 * EWX 24/96 specific controls
 */

/* i/o sensitivity - this callback is shared among other devices, too */
static int snd_ice1712_ewx_io_sense_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo){

	static char *texts[2] = {
		"+4dBu", "-10dBV",
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item >= 2)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_ice1712_ewx_io_sense_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char mask = kcontrol->private_value & 0xff;
	unsigned char saved[2];
	
	snd_ice1712_save_gpio_status(ice, saved);
	ucontrol->value.enumerated.item[0] = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA) & mask ? 1 : 0;
	snd_ice1712_restore_gpio_status(ice, saved);
	return 0;
}

static int snd_ice1712_ewx_io_sense_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char mask = kcontrol->private_value & 0xff;
	unsigned char saved[2];
	int val, nval;

	if (kcontrol->private_value & (1 << 31))
		return -EPERM;
	nval = ucontrol->value.enumerated.item[0] ? mask : 0;
	snd_ice1712_save_gpio_status(ice, saved);
	val = snd_ice1712_read(ice, ICE1712_IREG_GPIO_DATA);
	nval |= val & ~mask;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, nval);
	snd_ice1712_restore_gpio_status(ice, saved);
	return val != nval;
}

static snd_kcontrol_new_t snd_ice1712_ewx2496_controls[] __devinitdata = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Input Sensitivity Switch",
		.info = snd_ice1712_ewx_io_sense_info,
		.get = snd_ice1712_ewx_io_sense_get,
		.put = snd_ice1712_ewx_io_sense_put,
		.private_value = ICE1712_EWX2496_AIN_SEL,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Output Sensitivity Switch",
		.info = snd_ice1712_ewx_io_sense_info,
		.get = snd_ice1712_ewx_io_sense_get,
		.put = snd_ice1712_ewx_io_sense_put,
		.private_value = ICE1712_EWX2496_AOUT_SEL,
	},
};


/*
 * EWS88MT specific controls
 */
/* analog output sensitivity;; address 0x48 bit 6 */
static int snd_ice1712_ews88mt_output_sense_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char data;

	snd_i2c_lock(ice->i2c);
	if (snd_i2c_readbytes(ice->i2cdevs[1], &data, 1) != 1) {
		snd_i2c_unlock(ice->i2c);
		return -EIO;
	}
	snd_i2c_unlock(ice->i2c);
	ucontrol->value.enumerated.item[0] = data & ICE1712_EWS88MT_OUTPUT_SENSE ? 1 : 0; /* high = -10dBV, low = +4dBu */
	return 0;
}

/* analog output sensitivity;; address 0x48 bit 6 */
static int snd_ice1712_ews88mt_output_sense_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char data, ndata;

	snd_i2c_lock(ice->i2c);
	if (snd_i2c_readbytes(ice->i2cdevs[1], &data, 1) != 1) {
		snd_i2c_unlock(ice->i2c);
		return -EIO;
	}
	ndata = (data & ~ICE1712_EWS88MT_OUTPUT_SENSE) | (ucontrol->value.enumerated.item[0] ? ICE1712_EWS88MT_OUTPUT_SENSE : 0);
	if (ndata != data && snd_i2c_sendbytes(ice->i2cdevs[1], &ndata, 1) != 1) {
		snd_i2c_unlock(ice->i2c);
		return -EIO;
	}
	snd_i2c_unlock(ice->i2c);
	return ndata != data;
}

/* analog input sensitivity; address 0x46 */
static int snd_ice1712_ews88mt_input_sense_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int channel = kcontrol->id.index;
	unsigned char data;

	snd_assert(channel >= 0 && channel <= 7, return 0);
	snd_i2c_lock(ice->i2c);
	if (snd_i2c_readbytes(ice->i2cdevs[0], &data, 1) != 1) {
		snd_i2c_unlock(ice->i2c);
		return -EIO;
	}
	/* reversed; high = +4dBu, low = -10dBV */
	ucontrol->value.enumerated.item[0] = data & (1 << channel) ? 0 : 1;
	snd_i2c_unlock(ice->i2c);
	return 0;
}

/* analog output sensitivity; address 0x46 */
static int snd_ice1712_ews88mt_input_sense_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int channel = kcontrol->id.index;
	unsigned char data, ndata;

	snd_assert(channel >= 0 && channel <= 7, return 0);
	snd_i2c_lock(ice->i2c);
	if (snd_i2c_readbytes(ice->i2cdevs[0], &data, 1) != 1) {
		snd_i2c_unlock(ice->i2c);
		return -EIO;
	}
	ndata = (data & ~(1 << channel)) | (ucontrol->value.enumerated.item[0] ? 0 : (1 << channel));
	if (ndata != data && snd_i2c_sendbytes(ice->i2cdevs[0], &ndata, 1) != 1) {
		snd_i2c_unlock(ice->i2c);
		return -EIO;
	}
	snd_i2c_unlock(ice->i2c);
	return ndata != data;
}

static snd_kcontrol_new_t snd_ice1712_ews88mt_input_sense __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Input Sensitivity Switch",
	.info = snd_ice1712_ewx_io_sense_info,
	.get = snd_ice1712_ews88mt_input_sense_get,
	.put = snd_ice1712_ews88mt_input_sense_put,
};

static snd_kcontrol_new_t snd_ice1712_ews88mt_output_sense __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Output Sensitivity Switch",
	.info = snd_ice1712_ewx_io_sense_info,
	.get = snd_ice1712_ews88mt_output_sense_get,
	.put = snd_ice1712_ews88mt_output_sense_put,
};


/*
 * EWS88D specific controls
 */

static int snd_ice1712_ews88d_control_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ice1712_ews88d_control_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value & 0xff;
	int invert = (kcontrol->private_value >> 8) & 1;
	unsigned char data[2];
	
	snd_i2c_lock(ice->i2c);
	if (snd_i2c_readbytes(ice->i2cdevs[0], data, 2) != 2) {
		snd_i2c_unlock(ice->i2c);
		return -EIO;
	}
	snd_i2c_unlock(ice->i2c);
	data[0] = (data[shift >> 3] >> (shift & 7)) & 0x01;
	if (invert)
		data[0] ^= 0x01;
	ucontrol->value.integer.value[0] = data[0];
	return 0;
}

static int snd_ice1712_ews88d_control_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value & 0xff;
	int invert = (kcontrol->private_value >> 8) & 1;
	unsigned char data[2], ndata[2];
	int change;

	snd_i2c_lock(ice->i2c);
	if (snd_i2c_readbytes(ice->i2cdevs[0], data, 2) != 2) {
		snd_i2c_unlock(ice->i2c);
		return -EIO;
	}
	ndata[shift >> 3] = data[shift >> 3] & ~(1 << (shift & 7));
	if (invert) {
		if (! ucontrol->value.integer.value[0])
			ndata[shift >> 3] |= (1 << (shift & 7));
	} else {
		if (ucontrol->value.integer.value[0])
			ndata[shift >> 3] |= (1 << (shift & 7));
	}
	change = (data[shift >> 3] != ndata[shift >> 3]);
	if (change && snd_i2c_sendbytes(ice->i2cdevs[0], data, 2) != 2) {
		snd_i2c_unlock(ice->i2c);
		return -EIO;
	}
	snd_i2c_unlock(ice->i2c);
	return change;
}

#define EWS88D_CONTROL(xiface, xname, xshift, xinvert, xaccess) \
{ .iface = xiface,\
  .name = xname,\
  .access = xaccess,\
  .info = snd_ice1712_ews88d_control_info,\
  .get = snd_ice1712_ews88d_control_get,\
  .put = snd_ice1712_ews88d_control_put,\
  .private_value = xshift | (xinvert << 8),\
}

static snd_kcontrol_new_t snd_ice1712_ews88d_controls[] __devinitdata = {
	EWS88D_CONTROL(SNDRV_CTL_ELEM_IFACE_MIXER, "IEC958 Input Optical", 0, 1, 0), /* inverted */
	EWS88D_CONTROL(SNDRV_CTL_ELEM_IFACE_MIXER, "ADAT Output Optical", 1, 0, 0),
	EWS88D_CONTROL(SNDRV_CTL_ELEM_IFACE_MIXER, "ADAT External Master Clock", 2, 0, 0),
	EWS88D_CONTROL(SNDRV_CTL_ELEM_IFACE_MIXER, "Enable ADAT", 3, 0, 0),
	EWS88D_CONTROL(SNDRV_CTL_ELEM_IFACE_MIXER, "ADAT Through", 4, 1, 0),
};


/*
 * DMX 6Fire specific controls
 */

#define PCF9554_REG_INPUT	0
#define PCF9554_REG_OUTPUT	1
#define PCF9554_REG_POLARITY	2
#define PCF9554_REG_CONFIG	3

static int snd_ice1712_6fire_read_pca(ice1712_t *ice, unsigned char reg)
{
	unsigned char byte;
	snd_i2c_lock(ice->i2c);
	byte = reg;
	snd_i2c_sendbytes(ice->i2cdevs[0], &byte, 1);
	byte = 0;
	if (snd_i2c_readbytes(ice->i2cdevs[0], &byte, 1) != 1) {
		snd_i2c_unlock(ice->i2c);
		printk("cannot read pca\n");
		return -EIO;
	}
	snd_i2c_unlock(ice->i2c);
	return byte;
}

static int snd_ice1712_6fire_write_pca(ice1712_t *ice, unsigned char reg, unsigned char data)
{
	unsigned char bytes[2];
	snd_i2c_lock(ice->i2c);
	bytes[0] = reg;
	bytes[1] = data;
	if (snd_i2c_sendbytes(ice->i2cdevs[0], bytes, 2) != 2) {
		snd_i2c_unlock(ice->i2c);
		return -EIO;
	}
	snd_i2c_unlock(ice->i2c);
	return 0;
}

static int snd_ice1712_6fire_control_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_ice1712_6fire_control_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value & 0xff;
	int invert = (kcontrol->private_value >> 8) & 1;
	int data;
	
	if ((data = snd_ice1712_6fire_read_pca(ice, PCF9554_REG_OUTPUT)) < 0)
		return data;
	data = (data >> shift) & 1;
	if (invert)
		data ^= 1;
	ucontrol->value.integer.value[0] = data;
	return 0;
}

static int snd_ice1712_6fire_control_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int shift = kcontrol->private_value & 0xff;
	int invert = (kcontrol->private_value >> 8) & 1;
	int data, ndata;
	
	if ((data = snd_ice1712_6fire_read_pca(ice, PCF9554_REG_OUTPUT)) < 0)
		return data;
	ndata = data & ~(1 << shift);
	if (ucontrol->value.integer.value[0])
		ndata |= (1 << shift);
	if (invert)
		ndata ^= (1 << shift);
	if (data != ndata) {
		snd_ice1712_6fire_write_pca(ice, PCF9554_REG_OUTPUT, (unsigned char)ndata);
		return 1;
	}
	return 0;
}

static int snd_ice1712_6fire_select_input_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[4] = {
		"Internal", "Front Input", "Rear Input", "Wave Table"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item >= 4)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}
     
static int snd_ice1712_6fire_select_input_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int data;
	
	if ((data = snd_ice1712_6fire_read_pca(ice, PCF9554_REG_OUTPUT)) < 0)
		return data;
	ucontrol->value.integer.value[0] = data & 3;
	return 0;
}

static int snd_ice1712_6fire_select_input_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int data, ndata;
	
	if ((data = snd_ice1712_6fire_read_pca(ice, PCF9554_REG_OUTPUT)) < 0)
		return data;
	ndata = data & ~3;
	ndata |= (ucontrol->value.integer.value[0] & 3);
	if (data != ndata) {
		snd_ice1712_6fire_write_pca(ice, PCF9554_REG_OUTPUT, (unsigned char)ndata);
		return 1;
	}
	return 0;
}


#define DMX6FIRE_CONTROL(xname, xshift, xinvert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,\
  .name = xname,\
  .info = snd_ice1712_6fire_control_info,\
  .get = snd_ice1712_6fire_control_get,\
  .put = snd_ice1712_6fire_control_put,\
  .private_value = xshift | (xinvert << 8),\
}

static snd_kcontrol_new_t snd_ice1712_6fire_controls[] __devinitdata = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog Input Select",
		.info = snd_ice1712_6fire_select_input_info,
		.get = snd_ice1712_6fire_select_input_get,
		.put = snd_ice1712_6fire_select_input_put,
	},
	DMX6FIRE_CONTROL("Front Digital Input Switch", 2, 0),
	// DMX6FIRE_CONTROL("Master Clock Select", 3, 0),
	DMX6FIRE_CONTROL("Optical Digital Input Switch", 4, 0),
	DMX6FIRE_CONTROL("Phono Analog Input Switch", 5, 0),
	DMX6FIRE_CONTROL("Breakbox LED", 6, 0),
};


static int __devinit snd_ice1712_ews_add_controls(ice1712_t *ice)
{
	int err, idx;
	snd_kcontrol_t *kctl;
	
	/* all terratec cards have spdif */
	err = snd_ice1712_spdif_build_controls(ice);
	if (err < 0)
		return err;

	/* ak4524 controls */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_EWX2496:
	case ICE1712_SUBDEVICE_EWS88MT:
	case ICE1712_SUBDEVICE_DMX6FIRE:
		err = snd_ice1712_ak4524_build_controls(ice);
		if (err < 0)
			return err;
		break;
	}

	/* card specific controls */
	switch (ice->eeprom.subvendor) {
	case ICE1712_SUBDEVICE_EWX2496:
		for (idx = 0; idx < sizeof(snd_ice1712_ewx2496_controls)/sizeof(snd_ice1712_ewx2496_controls[0]); idx++) {
			err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_ewx2496_controls[idx], ice));
			if (err < 0)
				return err;
		}
		break;
	case ICE1712_SUBDEVICE_EWS88MT:
		for (idx = 0; idx < 8; idx++) {
			kctl = snd_ctl_new1(&snd_ice1712_ews88mt_input_sense, ice);
			kctl->id.index = idx;
			err = snd_ctl_add(ice->card, kctl);
			if (err < 0)
				return err;
		}
		err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_ews88mt_output_sense, ice));
		if (err < 0)
			return err;
		break;
	case ICE1712_SUBDEVICE_EWS88D:
		for (idx = 0; idx < sizeof(snd_ice1712_ews88d_controls)/sizeof(snd_ice1712_ews88d_controls[0]); idx++) {
			err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_ews88d_controls[idx], ice));
			if (err < 0)
				return err;
		}
		break;
	case ICE1712_SUBDEVICE_DMX6FIRE:
		for (idx = 0; idx < sizeof(snd_ice1712_6fire_controls)/sizeof(snd_ice1712_6fire_controls[0]); idx++) {
			err = snd_ctl_add(ice->card, snd_ctl_new1(&snd_ice1712_6fire_controls[idx], ice));
			if (err < 0)
				return err;
		}
		break;
	}
	return 0;
}


/* entry point */
struct snd_ice1712_card_info snd_ice1712_ews_cards[] __devinitdata = {
	{
		ICE1712_SUBDEVICE_EWX2496,
		"TerraTec EWX 24/96",
		snd_ice1712_ews_init,
		snd_ice1712_ews_add_controls,
	},
	{
		ICE1712_SUBDEVICE_EWS88MT,
		"TerraTec EWS 88MT",
		snd_ice1712_ews_init,
		snd_ice1712_ews_add_controls,
	},
	{
		ICE1712_SUBDEVICE_EWS88D,
		"TerraTec EWS 88D",
		snd_ice1712_ews_init,
		snd_ice1712_ews_add_controls,
	},
	{
		ICE1712_SUBDEVICE_DMX6FIRE,
		"TerraTec DMX 6Fire",
		snd_ice1712_ews_init,
		snd_ice1712_ews_add_controls,
	},
	{ } /* terminator */
};
