/*
 * budget-av.c: driver for the SAA7146 based Budget DVB cards
 *              with analog video in
 *
 * Compiled from various sources by Michael Hunold <michael@mihu.de>
 *
 * CI interface support (c) 2004 Olivier Gournet <ogournet@anevia.com> &
 *                               Andrew de Quincey <adq_dvb@lidskialf.net>
 *
 * Copyright (C) 2002 Ralph Metzler <rjkm@metzlerbros.de>
 *
 * Copyright (C) 1999-2002 Ralph  Metzler
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 *
 * the project's page is at http://www.linuxtv.org/dvb/
 */

#include "budget.h"
#include "stv0299.h"
#include "tda10021.h"
#include "tda1004x.h"
#include <media/saa7146_vv.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/spinlock.h>

#include "dvb_ca_en50221.h"

#define DEBICICAM		0x02420000

struct budget_av {
	struct budget budget;
	struct video_device *vd;
	int cur_input;
	int has_saa7113;
	struct tasklet_struct ciintf_irq_tasklet;
	int slot_status;
	struct dvb_ca_en50221 ca;
};

static int enable_ci = 0;


/****************************************************************************
 * INITIALIZATION
 ****************************************************************************/

static u8 i2c_readreg(struct i2c_adapter *i2c, u8 id, u8 reg)
{
	u8 mm1[] = { 0x00 };
	u8 mm2[] = { 0x00 };
	struct i2c_msg msgs[2];

	msgs[0].flags = 0;
	msgs[1].flags = I2C_M_RD;
	msgs[0].addr = msgs[1].addr = id / 2;
	mm1[0] = reg;
	msgs[0].len = 1;
	msgs[1].len = 1;
	msgs[0].buf = mm1;
	msgs[1].buf = mm2;

	i2c_transfer(i2c, msgs, 2);

	return mm2[0];
}

static int i2c_readregs(struct i2c_adapter *i2c, u8 id, u8 reg, u8 * buf, u8 len)
{
	u8 mm1[] = { reg };
	struct i2c_msg msgs[2] = {
		{.addr = id / 2,.flags = 0,.buf = mm1,.len = 1},
		{.addr = id / 2,.flags = I2C_M_RD,.buf = buf,.len = len}
	};

	if (i2c_transfer(i2c, msgs, 2) != 2)
		return -EIO;

	return 0;
}

static int i2c_writereg(struct i2c_adapter *i2c, u8 id, u8 reg, u8 val)
{
	u8 msg[2] = { reg, val };
	struct i2c_msg msgs;

	msgs.flags = 0;
	msgs.addr = id / 2;
	msgs.len = 2;
	msgs.buf = msg;
	return i2c_transfer(i2c, &msgs, 1);
}

static int ciintf_read_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address)
{
	struct budget_av *budget_av = (struct budget_av *) ca->data;
	int result;

	if (slot != 0)
		return -EINVAL;

	saa7146_setgpio(budget_av->budget.dev, 1, SAA7146_GPIO_OUTHI);
	udelay(1);

	result = ttpci_budget_debiread(&budget_av->budget, DEBICICAM, address & 0xfff, 1, 0, 0);

	if (result == -ETIMEDOUT)
		budget_av->slot_status = 0;
	return result;
}

static int ciintf_write_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address, u8 value)
{
	struct budget_av *budget_av = (struct budget_av *) ca->data;
	int result;

	if (slot != 0)
		return -EINVAL;

	saa7146_setgpio(budget_av->budget.dev, 1, SAA7146_GPIO_OUTHI);
	udelay(1);

	result = ttpci_budget_debiwrite(&budget_av->budget, DEBICICAM, address & 0xfff, 1, value, 0, 0);

	if (result == -ETIMEDOUT)
		budget_av->slot_status = 0;
	return result;
}

static int ciintf_read_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address)
{
	struct budget_av *budget_av = (struct budget_av *) ca->data;
	int result;

	if (slot != 0)
		return -EINVAL;

	saa7146_setgpio(budget_av->budget.dev, 1, SAA7146_GPIO_OUTLO);
	udelay(1);

	result = ttpci_budget_debiread(&budget_av->budget, DEBICICAM, address & 3, 1, 0, 0);

	if (result == -ETIMEDOUT)
		budget_av->slot_status = 0;
	return result;
}

static int ciintf_write_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address, u8 value)
{
	struct budget_av *budget_av = (struct budget_av *) ca->data;
	int result;

	if (slot != 0)
		return -EINVAL;

	saa7146_setgpio(budget_av->budget.dev, 1, SAA7146_GPIO_OUTLO);
	udelay(1);

	result = ttpci_budget_debiwrite(&budget_av->budget, DEBICICAM, address & 3, 1, value, 0, 0);

	if (result == -ETIMEDOUT)
		budget_av->slot_status = 0;
	return result;
}

static int ciintf_slot_reset(struct dvb_ca_en50221 *ca, int slot)
{
	struct budget_av *budget_av = (struct budget_av *) ca->data;
	struct saa7146_dev *saa = budget_av->budget.dev;
	int max = 20;

	if (slot != 0)
		return -EINVAL;

	dprintk(1, "ciintf_slot_reset\n");

	/* reset the card */
	saa7146_setgpio(saa, 0, SAA7146_GPIO_OUTHI);
	msleep(100);
	saa7146_setgpio(saa, 0, SAA7146_GPIO_OUTLO);

	while (--max > 0 && ciintf_read_attribute_mem(ca, slot, 0) != 0x1d)
		msleep(100);

	ttpci_budget_set_video_port(saa, BUDGET_VIDEO_PORTB);
	return 0;
}

static int ciintf_slot_shutdown(struct dvb_ca_en50221 *ca, int slot)
{
	struct budget_av *budget_av = (struct budget_av *) ca->data;
	struct saa7146_dev *saa = budget_av->budget.dev;

	if (slot != 0)
		return -EINVAL;

	dprintk(1, "ciintf_slot_shutdown\n");

	ttpci_budget_set_video_port(saa, BUDGET_VIDEO_PORTB);
	budget_av->slot_status = 0;
	return 0;
}

static int ciintf_slot_ts_enable(struct dvb_ca_en50221 *ca, int slot)
{
	struct budget_av *budget_av = (struct budget_av *) ca->data;
	struct saa7146_dev *saa = budget_av->budget.dev;

	if (slot != 0)
		return -EINVAL;

	dprintk(1, "ciintf_slot_ts_enable: %d\n", budget_av->slot_status);

	ttpci_budget_set_video_port(saa, BUDGET_VIDEO_PORTA);
	return 0;
}

static int ciintf_poll_slot_status(struct dvb_ca_en50221 *ca, int slot, int open)
{
	struct budget_av *budget_av = (struct budget_av *) ca->data;
	struct saa7146_dev *saa = budget_av->budget.dev;
	int cam = 0;

	if (slot != 0)
		return -EINVAL;

	if (!budget_av->slot_status) {
		saa7146_setgpio(saa, 3, SAA7146_GPIO_INPUT);
		udelay(1);
		cam = saa7146_read(saa, PSR) & MASK_06;
		saa7146_setgpio(saa, 3, SAA7146_GPIO_OUTLO);

		if (cam)
			budget_av->slot_status = 1;
	} else if (!open) {
		saa7146_setgpio(budget_av->budget.dev, 1, SAA7146_GPIO_OUTLO);
		if (ttpci_budget_debiread(&budget_av->budget, DEBICICAM, 0, 1, 0, 1) == -ETIMEDOUT)
			budget_av->slot_status = 0;
	}

	if (budget_av->slot_status == 1)
		return DVB_CA_EN50221_POLL_CAM_PRESENT | DVB_CA_EN50221_POLL_CAM_READY;

	return 0;
}

static int ciintf_init(struct budget_av *budget_av)
{
	struct saa7146_dev *saa = budget_av->budget.dev;
	int result;

	memset(&budget_av->ca, 0, sizeof(struct dvb_ca_en50221));

	/* setup GPIOs */
	saa7146_setgpio(saa, 1, SAA7146_GPIO_OUTHI);
	saa7146_setgpio(saa, 2, SAA7146_GPIO_OUTLO);
	saa7146_setgpio(saa, 3, SAA7146_GPIO_OUTLO);

	/* Reset the card */
	saa7146_setgpio(saa, 0, SAA7146_GPIO_OUTHI);
	msleep(50);
	saa7146_setgpio(saa, 0, SAA7146_GPIO_OUTLO);
	msleep(100);

	/* Enable DEBI pins */
	saa7146_write(saa, MC1, saa7146_read(saa, MC1) | (0x800 << 16) | 0x800);

	/* register CI interface */
	budget_av->ca.owner = THIS_MODULE;
	budget_av->ca.read_attribute_mem = ciintf_read_attribute_mem;
	budget_av->ca.write_attribute_mem = ciintf_write_attribute_mem;
	budget_av->ca.read_cam_control = ciintf_read_cam_control;
	budget_av->ca.write_cam_control = ciintf_write_cam_control;
	budget_av->ca.slot_reset = ciintf_slot_reset;
	budget_av->ca.slot_shutdown = ciintf_slot_shutdown;
	budget_av->ca.slot_ts_enable = ciintf_slot_ts_enable;
	budget_av->ca.poll_slot_status = ciintf_poll_slot_status;
	budget_av->ca.data = budget_av;
	if ((result = dvb_ca_en50221_init(budget_av->budget.dvb_adapter,
					  &budget_av->ca, 0, 1)) != 0) {
		printk("budget_av: CI interface detected, but initialisation failed.\n");
		goto error;
	}
	// success!
	printk("ciintf_init: CI interface initialised\n");
	budget_av->budget.ci_present = 1;
	return 0;

error:
	saa7146_write(saa, MC1, saa7146_read(saa, MC1) | (0x800 << 16));
	return result;
}

static void ciintf_deinit(struct budget_av *budget_av)
{
	struct saa7146_dev *saa = budget_av->budget.dev;

	saa7146_setgpio(saa, 0, SAA7146_GPIO_INPUT);
	saa7146_setgpio(saa, 1, SAA7146_GPIO_INPUT);
	saa7146_setgpio(saa, 2, SAA7146_GPIO_INPUT);
	saa7146_setgpio(saa, 3, SAA7146_GPIO_INPUT);

	/* release the CA device */
	dvb_ca_en50221_release(&budget_av->ca);

	/* disable DEBI pins */
	saa7146_write(saa, MC1, saa7146_read(saa, MC1) | (0x800 << 16));
}


static const u8 saa7113_tab[] = {
	0x01, 0x08,
	0x02, 0xc0,
	0x03, 0x33,
	0x04, 0x00,
	0x05, 0x00,
	0x06, 0xeb,
	0x07, 0xe0,
	0x08, 0x28,
	0x09, 0x00,
	0x0a, 0x80,
	0x0b, 0x47,
	0x0c, 0x40,
	0x0d, 0x00,
	0x0e, 0x01,
	0x0f, 0x44,

	0x10, 0x08,
	0x11, 0x0c,
	0x12, 0x7b,
	0x13, 0x00,
	0x15, 0x00, 0x16, 0x00, 0x17, 0x00,

	0x57, 0xff,
	0x40, 0x82, 0x58, 0x00, 0x59, 0x54, 0x5a, 0x07,
	0x5b, 0x83, 0x5e, 0x00,
	0xff
};

static int saa7113_init(struct budget_av *budget_av)
{
	struct budget *budget = &budget_av->budget;
	const u8 *data = saa7113_tab;

	if (i2c_writereg(&budget->i2c_adap, 0x4a, 0x01, 0x08) != 1) {
		dprintk(1, "saa7113 not found on KNC card\n");
		return -ENODEV;
	}

	dprintk(1, "saa7113 detected and initializing\n");

	while (*data != 0xff) {
		i2c_writereg(&budget->i2c_adap, 0x4a, *data, *(data + 1));
		data += 2;
	}

	dprintk(1, "saa7113  status=%02x\n", i2c_readreg(&budget->i2c_adap, 0x4a, 0x1f));

	return 0;
}

static int saa7113_setinput(struct budget_av *budget_av, int input)
{
	struct budget *budget = &budget_av->budget;

	if (1 != budget_av->has_saa7113)
		return -ENODEV;

	if (input == 1) {
		i2c_writereg(&budget->i2c_adap, 0x4a, 0x02, 0xc7);
		i2c_writereg(&budget->i2c_adap, 0x4a, 0x09, 0x80);
	} else if (input == 0) {
		i2c_writereg(&budget->i2c_adap, 0x4a, 0x02, 0xc0);
		i2c_writereg(&budget->i2c_adap, 0x4a, 0x09, 0x00);
	} else
		return -EINVAL;

	budget_av->cur_input = input;
	return 0;
}


static int philips_su1278_ty_ci_set_symbol_rate(struct dvb_frontend *fe, u32 srate, u32 ratio)
{
	u8 aclk = 0;
	u8 bclk = 0;
	u8 m1;

	aclk = 0xb5;
	if (srate < 2000000)
		bclk = 0x86;
	else if (srate < 5000000)
		bclk = 0x89;
	else if (srate < 15000000)
		bclk = 0x8f;
	else if (srate < 45000000)
		bclk = 0x95;

	m1 = 0x14;
	if (srate < 4000000)
		m1 = 0x10;

	stv0299_writereg(fe, 0x13, aclk);
	stv0299_writereg(fe, 0x14, bclk);
	stv0299_writereg(fe, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg(fe, 0x20, (ratio >> 8) & 0xff);
	stv0299_writereg(fe, 0x21, (ratio) & 0xf0);
	stv0299_writereg(fe, 0x0f, 0x80 | m1);

	return 0;
}

static int philips_su1278_ty_ci_pll_set(struct dvb_frontend *fe,
					struct dvb_frontend_parameters *params)
{
	struct budget_av *budget_av = (struct budget_av *) fe->dvb->priv;
	u32 div;
	u8 buf[4];
	struct i2c_msg msg = {.addr = 0x61,.flags = 0,.buf = buf,.len = sizeof(buf) };

	if ((params->frequency < 950000) || (params->frequency > 2150000))
		return -EINVAL;

	div = (params->frequency + (125 - 1)) / 125;	// round correctly
	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x80 | ((div & 0x18000) >> 10) | 4;
	buf[3] = 0x20;

	if (params->u.qpsk.symbol_rate < 4000000)
		buf[3] |= 1;

	if (params->frequency < 1250000)
		buf[3] |= 0;
	else if (params->frequency < 1550000)
		buf[3] |= 0x40;
	else if (params->frequency < 2050000)
		buf[3] |= 0x80;
	else if (params->frequency < 2150000)
		buf[3] |= 0xC0;

	if (i2c_transfer(&budget_av->budget.i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static u8 typhoon_cinergy1200s_inittab[] = {
	0x01, 0x15,
	0x02, 0x30,
	0x03, 0x00,
	0x04, 0x7d,		/* F22FR = 0x7d, F22 = f_VCO / 128 / 0x7d = 22 kHz */
	0x05, 0x35,		/* I2CT = 0, SCLT = 1, SDAT = 1 */
	0x06, 0x40,		/* DAC not used, set to high impendance mode */
	0x07, 0x00,		/* DAC LSB */
	0x08, 0x40,		/* DiSEqC off */
	0x09, 0x00,		/* FIFO */
	0x0c, 0x51,		/* OP1 ctl = Normal, OP1 val = 1 (LNB Power ON) */
	0x0d, 0x82,		/* DC offset compensation = ON, beta_agc1 = 2 */
	0x0e, 0x23,		/* alpha_tmg = 2, beta_tmg = 3 */
	0x10, 0x3f,		// AGC2  0x3d
	0x11, 0x84,
	0x12, 0xb5,		// Lock detect: -64  Carrier freq detect:on
	0x15, 0xc9,		// lock detector threshold
	0x16, 0x00,
	0x17, 0x00,
	0x18, 0x00,
	0x19, 0x00,
	0x1a, 0x00,
	0x1f, 0x50,
	0x20, 0x00,
	0x21, 0x00,
	0x22, 0x00,
	0x23, 0x00,
	0x28, 0x00,		// out imp: normal  out type: parallel FEC mode:0
	0x29, 0x1e,		// 1/2 threshold
	0x2a, 0x14,		// 2/3 threshold
	0x2b, 0x0f,		// 3/4 threshold
	0x2c, 0x09,		// 5/6 threshold
	0x2d, 0x05,		// 7/8 threshold
	0x2e, 0x01,
	0x31, 0x1f,		// test all FECs
	0x32, 0x19,		// viterbi and synchro search
	0x33, 0xfc,		// rs control
	0x34, 0x93,		// error control
	0x0f, 0x92,
	0xff, 0xff
};

static struct stv0299_config typhoon_config = {
	.demod_address = 0x68,
	.inittab = typhoon_cinergy1200s_inittab,
	.mclk = 88000000UL,
	.invert = 0,
	.enhanced_tuning = 0,
	.skip_reinit = 0,
	.lock_output = STV0229_LOCKOUTPUT_1,
	.volt13_op0_op1 = STV0299_VOLT13_OP0,
	.min_delay_ms = 100,
	.set_symbol_rate = philips_su1278_ty_ci_set_symbol_rate,
	.pll_set = philips_su1278_ty_ci_pll_set,
};


static struct stv0299_config cinergy_1200s_config = {
	.demod_address = 0x68,
	.inittab = typhoon_cinergy1200s_inittab,
	.mclk = 88000000UL,
	.invert = 0,
	.enhanced_tuning = 0,
	.skip_reinit = 0,
	.lock_output = STV0229_LOCKOUTPUT_0,
	.volt13_op0_op1 = STV0299_VOLT13_OP0,
	.min_delay_ms = 100,
	.set_symbol_rate = philips_su1278_ty_ci_set_symbol_rate,
	.pll_set = philips_su1278_ty_ci_pll_set,
};


static int philips_cu1216_pll_set(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct budget *budget = (struct budget *) fe->dvb->priv;
	u8 buf[4];
	struct i2c_msg msg = {.addr = 0x60,.flags = 0,.buf = buf,.len = sizeof(buf) };

#define TUNER_MUL 62500

	u32 div = (params->frequency + 36125000 + TUNER_MUL / 2) / TUNER_MUL;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x8e;
	buf[3] = (params->frequency < 174500000 ? 0xa1 :
		  params->frequency < 454000000 ? 0x92 : 0x34);

	if (i2c_transfer(&budget->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct tda10021_config philips_cu1216_config = {
	.demod_address = 0x0c,
	.pll_set = philips_cu1216_pll_set,
};




static int philips_tu1216_pll_init(struct dvb_frontend *fe)
{
	struct budget *budget = (struct budget *) fe->dvb->priv;
	static u8 tu1216_init[] = { 0x0b, 0xf5, 0x85, 0xab };
	struct i2c_msg tuner_msg = {.addr = 0x60,.flags = 0,.buf = tu1216_init,.len = sizeof(tu1216_init) };

	// setup PLL configuration
	if (i2c_transfer(&budget->i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;
	msleep(1);

	return 0;
}

static int philips_tu1216_pll_set(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct budget *budget = (struct budget *) fe->dvb->priv;
	u8 tuner_buf[4];
	struct i2c_msg tuner_msg = {.addr = 0x60,.flags = 0,.buf = tuner_buf,.len =
			sizeof(tuner_buf) };
	int tuner_frequency = 0;
	u8 band, cp, filter;

	// determine charge pump
	tuner_frequency = params->frequency + 36166000;
	if (tuner_frequency < 87000000)
		return -EINVAL;
	else if (tuner_frequency < 130000000)
		cp = 3;
	else if (tuner_frequency < 160000000)
		cp = 5;
	else if (tuner_frequency < 200000000)
		cp = 6;
	else if (tuner_frequency < 290000000)
		cp = 3;
	else if (tuner_frequency < 420000000)
		cp = 5;
	else if (tuner_frequency < 480000000)
		cp = 6;
	else if (tuner_frequency < 620000000)
		cp = 3;
	else if (tuner_frequency < 830000000)
		cp = 5;
	else if (tuner_frequency < 895000000)
		cp = 7;
	else
		return -EINVAL;

	// determine band
	if (params->frequency < 49000000)
		return -EINVAL;
	else if (params->frequency < 161000000)
		band = 1;
	else if (params->frequency < 444000000)
		band = 2;
	else if (params->frequency < 861000000)
		band = 4;
	else
		return -EINVAL;

	// setup PLL filter
	switch (params->u.ofdm.bandwidth) {
	case BANDWIDTH_6_MHZ:
		filter = 0;
		break;

	case BANDWIDTH_7_MHZ:
		filter = 0;
		break;

	case BANDWIDTH_8_MHZ:
		filter = 1;
		break;

	default:
		return -EINVAL;
	}

	// calculate divisor
	// ((36166000+((1000000/6)/2)) + Finput)/(1000000/6)
	tuner_frequency = (((params->frequency / 1000) * 6) + 217496) / 1000;

	// setup tuner buffer
	tuner_buf[0] = (tuner_frequency >> 8) & 0x7f;
	tuner_buf[1] = tuner_frequency & 0xff;
	tuner_buf[2] = 0xca;
	tuner_buf[3] = (cp << 5) | (filter << 3) | band;

	if (i2c_transfer(&budget->i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;

	msleep(1);
	return 0;
}

static int philips_tu1216_request_firmware(struct dvb_frontend *fe,
					   const struct firmware **fw, char *name)
{
	struct budget *budget = (struct budget *) fe->dvb->priv;

	return request_firmware(fw, name, &budget->dev->pci->dev);
}

static struct tda1004x_config philips_tu1216_config = {

	.demod_address = 0x8,
	.invert = 1,
	.invert_oclk = 1,
	.pll_init = philips_tu1216_pll_init,
	.pll_set = philips_tu1216_pll_set,
	.request_firmware = philips_tu1216_request_firmware,
};




static u8 read_pwm(struct budget_av *budget_av)
{
	u8 b = 0xff;
	u8 pwm;
	struct i2c_msg msg[] = { {.addr = 0x50,.flags = 0,.buf = &b,.len = 1},
	{.addr = 0x50,.flags = I2C_M_RD,.buf = &pwm,.len = 1}
	};

	if ((i2c_transfer(&budget_av->budget.i2c_adap, msg, 2) != 2)
	    || (pwm == 0xff))
		pwm = 0x48;

	return pwm;
}


static void frontend_init(struct budget_av *budget_av)
{
	switch (budget_av->budget.dev->pci->subsystem_device) {
	case 0x4f56:		// Typhoon/KNC1 DVB-S budget (stv0299/Philips SU1278(tsa5059))
		budget_av->budget.dvb_frontend =
			stv0299_attach(&typhoon_config, &budget_av->budget.i2c_adap);
		if (budget_av->budget.dvb_frontend != NULL) {
			break;
		}
		break;

	case 0x0020:		// KNC1 DVB-C budget (tda10021/Philips CU1216(tua6034))
		budget_av->budget.dvb_frontend =
			tda10021_attach(&philips_cu1216_config,
					&budget_av->budget.i2c_adap, read_pwm(budget_av));
		if (budget_av->budget.dvb_frontend != NULL) {
			break;
		}
		break;

	case 0x0030:		// KNC1 DVB-T budget (tda10046/Philips TU1216(tda6651tt))
		budget_av->budget.dvb_frontend =
			tda10046_attach(&philips_tu1216_config, &budget_av->budget.i2c_adap);
		if (budget_av->budget.dvb_frontend != NULL) {
			break;
		}
		break;

	case 0x1154:		// TerraTec Cinergy 1200 DVB-S (stv0299/Philips SU1278(tsa5059))
		budget_av->budget.dvb_frontend =
			stv0299_attach(&cinergy_1200s_config, &budget_av->budget.i2c_adap);
		if (budget_av->budget.dvb_frontend != NULL) {
			break;
		}
		break;

	case 0x1156:		// Terratec Cinergy 1200 DVB-C (tda10021/Philips CU1216(tua6034))
		budget_av->budget.dvb_frontend =
			tda10021_attach(&philips_cu1216_config,
					&budget_av->budget.i2c_adap, read_pwm(budget_av));
		if (budget_av->budget.dvb_frontend) {
			break;
		}
		break;

	case 0x1157:		// Terratec Cinergy 1200 DVB-T (tda10046/Philips TU1216(tda6651tt))
		budget_av->budget.dvb_frontend =
			tda10046_attach(&philips_tu1216_config, &budget_av->budget.i2c_adap);
		if (budget_av->budget.dvb_frontend) {
			break;
		}
		break;
	}

	if (budget_av->budget.dvb_frontend == NULL) {
		printk("budget_av: A frontend driver was not found for device %04x/%04x subsystem %04x/%04x\n",
		       budget_av->budget.dev->pci->vendor,
		       budget_av->budget.dev->pci->device,
		       budget_av->budget.dev->pci->subsystem_vendor,
		       budget_av->budget.dev->pci->subsystem_device);
	} else {
		if (dvb_register_frontend
		    (budget_av->budget.dvb_adapter, budget_av->budget.dvb_frontend)) {
			printk("budget-av: Frontend registration failed!\n");
			if (budget_av->budget.dvb_frontend->ops->release)
				budget_av->budget.dvb_frontend->ops->release(budget_av->budget.dvb_frontend);
			budget_av->budget.dvb_frontend = NULL;
		}
	}
}


static void budget_av_irq(struct saa7146_dev *dev, u32 * isr)
{
	struct budget_av *budget_av = (struct budget_av *) dev->ext_priv;

	dprintk(8, "dev: %p, budget_av: %p\n", dev, budget_av);

	if (*isr & MASK_10)
		ttpci_budget_irq10_handler(dev, isr);
}

static int budget_av_detach(struct saa7146_dev *dev)
{
	struct budget_av *budget_av = (struct budget_av *) dev->ext_priv;
	int err;

	dprintk(2, "dev: %p\n", dev);

	if (1 == budget_av->has_saa7113) {
		saa7146_setgpio(dev, 0, SAA7146_GPIO_OUTLO);

		msleep(200);

		saa7146_unregister_device(&budget_av->vd, dev);
	}

	if (budget_av->budget.ci_present)
		ciintf_deinit(budget_av);

	if (budget_av->budget.dvb_frontend != NULL)
		dvb_unregister_frontend(budget_av->budget.dvb_frontend);
	err = ttpci_budget_deinit(&budget_av->budget);

	kfree(budget_av);

	return err;
}

static struct saa7146_ext_vv vv_data;

static int budget_av_attach(struct saa7146_dev *dev, struct saa7146_pci_extension_data *info)
{
	struct budget_av *budget_av;
	u8 *mac;
	int err;

	dprintk(2, "dev: %p\n", dev);

	if (!(budget_av = kmalloc(sizeof(struct budget_av), GFP_KERNEL)))
		return -ENOMEM;

	memset(budget_av, 0, sizeof(struct budget_av));

	budget_av->budget.ci_present = 0;

	dev->ext_priv = budget_av;

	if ((err = ttpci_budget_init(&budget_av->budget, dev, info, THIS_MODULE))) {
		kfree(budget_av);
		return err;
	}

	/* knc1 initialization */
	saa7146_write(dev, DD1_STREAM_B, 0x04000000);
	saa7146_write(dev, DD1_INIT, 0x07000600);
	saa7146_write(dev, MC2, MASK_09 | MASK_25 | MASK_10 | MASK_26);

	saa7146_setgpio(dev, 0, SAA7146_GPIO_OUTHI);
	msleep(500);

	if (0 == saa7113_init(budget_av)) {
		budget_av->has_saa7113 = 1;

		if (0 != saa7146_vv_init(dev, &vv_data)) {
			/* fixme: proper cleanup here */
			ERR(("cannot init vv subsystem.\n"));
			return err;
		}

		if ((err = saa7146_register_device(&budget_av->vd, dev, "knc1", VFL_TYPE_GRABBER))) {
			/* fixme: proper cleanup here */
			ERR(("cannot register capture v4l2 device.\n"));
			return err;
		}

		/* beware: this modifies dev->vv ... */
		saa7146_set_hps_source_and_sync(dev, SAA7146_HPS_SOURCE_PORT_A,
						SAA7146_HPS_SYNC_PORT_A);

		saa7113_setinput(budget_av, 0);
	} else {
		budget_av->has_saa7113 = 0;

		saa7146_setgpio(dev, 0, SAA7146_GPIO_OUTLO);
	}

	/* fixme: find some sane values here... */
	saa7146_write(dev, PCI_BT_V1, 0x1c00101f);

	mac = budget_av->budget.dvb_adapter->proposed_mac;
	if (i2c_readregs(&budget_av->budget.i2c_adap, 0xa0, 0x30, mac, 6)) {
		printk("KNC1-%d: Could not read MAC from KNC1 card\n",
		       budget_av->budget.dvb_adapter->num);
		memset(mac, 0, 6);
	} else {
		printk("KNC1-%d: MAC addr = %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
		       budget_av->budget.dvb_adapter->num,
		       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	}

	budget_av->budget.dvb_adapter->priv = budget_av;
	frontend_init(budget_av);

	if (enable_ci)
		ciintf_init(budget_av);

	return 0;
}

#define KNC1_INPUTS 2
static struct v4l2_input knc1_inputs[KNC1_INPUTS] = {
	{0, "Composite", V4L2_INPUT_TYPE_TUNER, 1, 0, V4L2_STD_PAL_BG | V4L2_STD_NTSC_M, 0},
	{1, "S-Video", V4L2_INPUT_TYPE_CAMERA, 2, 0, V4L2_STD_PAL_BG | V4L2_STD_NTSC_M, 0},
};

static struct saa7146_extension_ioctls ioctls[] = {
	{VIDIOC_ENUMINPUT, SAA7146_EXCLUSIVE},
	{VIDIOC_G_INPUT, SAA7146_EXCLUSIVE},
	{VIDIOC_S_INPUT, SAA7146_EXCLUSIVE},
	{0, 0}
};

static int av_ioctl(struct saa7146_fh *fh, unsigned int cmd, void *arg)
{
	struct saa7146_dev *dev = fh->dev;
	struct budget_av *budget_av = (struct budget_av *) dev->ext_priv;

	switch (cmd) {
	case VIDIOC_ENUMINPUT:{
		struct v4l2_input *i = arg;

		dprintk(1, "VIDIOC_ENUMINPUT %d.\n", i->index);
		if (i->index < 0 || i->index >= KNC1_INPUTS) {
			return -EINVAL;
		}
		memcpy(i, &knc1_inputs[i->index], sizeof(struct v4l2_input));
		return 0;
	}
	case VIDIOC_G_INPUT:{
		int *input = (int *) arg;

		*input = budget_av->cur_input;

		dprintk(1, "VIDIOC_G_INPUT %d.\n", *input);
		return 0;
	}
	case VIDIOC_S_INPUT:{
		int input = *(int *) arg;
		dprintk(1, "VIDIOC_S_INPUT %d.\n", input);
		return saa7113_setinput(budget_av, input);
	}
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static struct saa7146_standard standard[] = {
	{.name = "PAL",.id = V4L2_STD_PAL,
	 .v_offset = 0x17,.v_field = 288,
	 .h_offset = 0x14,.h_pixels = 680,
	 .v_max_out = 576,.h_max_out = 768 },

	{.name = "NTSC",.id = V4L2_STD_NTSC,
	 .v_offset = 0x16,.v_field = 240,
	 .h_offset = 0x06,.h_pixels = 708,
	 .v_max_out = 480,.h_max_out = 640, },
};

static struct saa7146_ext_vv vv_data = {
	.inputs = 2,
	.capabilities = 0,	// perhaps later: V4L2_CAP_VBI_CAPTURE, but that need tweaking with the saa7113
	.flags = 0,
	.stds = &standard[0],
	.num_stds = sizeof(standard) / sizeof(struct saa7146_standard),
	.ioctls = &ioctls[0],
	.ioctl = av_ioctl,
};

static struct saa7146_extension budget_extension;

MAKE_BUDGET_INFO(knc1s, "KNC1 DVB-S", BUDGET_KNC1S);
MAKE_BUDGET_INFO(knc1c, "KNC1 DVB-C", BUDGET_KNC1C);
MAKE_BUDGET_INFO(knc1t, "KNC1 DVB-T", BUDGET_KNC1T);
MAKE_BUDGET_INFO(cin1200s, "TerraTec Cinergy 1200 DVB-S", BUDGET_CIN1200S);
MAKE_BUDGET_INFO(cin1200c, "Terratec Cinergy 1200 DVB-C", BUDGET_CIN1200C);
MAKE_BUDGET_INFO(cin1200t, "Terratec Cinergy 1200 DVB-T", BUDGET_CIN1200T);

static struct pci_device_id pci_tbl[] = {
	MAKE_EXTENSION_PCI(knc1s, 0x1131, 0x4f56),
	MAKE_EXTENSION_PCI(knc1c, 0x1894, 0x0020),
	MAKE_EXTENSION_PCI(knc1t, 0x1894, 0x0030),
	MAKE_EXTENSION_PCI(cin1200s, 0x153b, 0x1154),
	MAKE_EXTENSION_PCI(cin1200c, 0x153b, 0x1156),
	MAKE_EXTENSION_PCI(cin1200t, 0x153b, 0x1157),
	{
	 .vendor = 0,
	}
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_extension budget_extension = {
	.name = "budget dvb /w video in\0",
	.pci_tbl = pci_tbl,

	.module = THIS_MODULE,
	.attach = budget_av_attach,
	.detach = budget_av_detach,

	.irq_mask = MASK_10,
	.irq_func = budget_av_irq,
};

static int __init budget_av_init(void)
{
	return saa7146_register_extension(&budget_extension);
}

static void __exit budget_av_exit(void)
{
	saa7146_unregister_extension(&budget_extension);
}

module_init(budget_av_init);
module_exit(budget_av_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ralph Metzler, Marcus Metzler, Michael Hunold, others");
MODULE_DESCRIPTION("driver for the SAA7146 based so-called "
		   "budget PCI DVB w/ analog input and CI-module (e.g. the KNC cards)");
module_param_named(enable_ci, enable_ci, int, 0644);
MODULE_PARM_DESC(enable_ci, "Turn on/off CI module (default:off).");
