/*
    Driver for STV0297 demodulator

    Copyright (C) 2004 Andrew de Quincey <adq_dvb@lidskialf.net>
    Copyright (C) 2003-2004 Dennis Noermann <dennis.noermann@noernet.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/delay.h>

#include "dvb_frontend.h"
#include "stv0297.h"

struct stv0297_state {
	struct i2c_adapter *i2c;
	struct dvb_frontend_ops ops;
	const struct stv0297_config *config;
	struct dvb_frontend frontend;

	unsigned long base_freq;
	u8 pwm;
};

#if 1
#define dprintk(x...) printk(x)
#else
#define dprintk(x...)
#endif

#define STV0297_CLOCK_KHZ   28900

static u8 init_tab[] = {
	0x00, 0x09,
	0x01, 0x69,
	0x03, 0x00,
	0x04, 0x00,
	0x07, 0x00,
	0x08, 0x00,
	0x20, 0x00,
	0x21, 0x40,
	0x22, 0x00,
	0x23, 0x00,
	0x24, 0x40,
	0x25, 0x88,
	0x30, 0xff,
	0x31, 0x00,
	0x32, 0xff,
	0x33, 0x00,
	0x34, 0x50,
	0x35, 0x7f,
	0x36, 0x00,
	0x37, 0x20,
	0x38, 0x00,
	0x40, 0x1c,
	0x41, 0xff,
	0x42, 0x29,
	0x43, 0x00,
	0x44, 0xff,
	0x45, 0x00,
	0x46, 0x00,
	0x49, 0x04,
	0x4a, 0xff,
	0x4b, 0x7f,
	0x52, 0x30,
	0x55, 0xae,
	0x56, 0x47,
	0x57, 0xe1,
	0x58, 0x3a,
	0x5a, 0x1e,
	0x5b, 0x34,
	0x60, 0x00,
	0x63, 0x00,
	0x64, 0x00,
	0x65, 0x00,
	0x66, 0x00,
	0x67, 0x00,
	0x68, 0x00,
	0x69, 0x00,
	0x6a, 0x02,
	0x6b, 0x00,
	0x70, 0xff,
	0x71, 0x00,
	0x72, 0x00,
	0x73, 0x00,
	0x74, 0x0c,
	0x80, 0x00,
	0x81, 0x00,
	0x82, 0x00,
	0x83, 0x00,
	0x84, 0x04,
	0x85, 0x80,
	0x86, 0x24,
	0x87, 0x78,
	0x88, 0x00,
	0x89, 0x00,
	0x90, 0x01,
	0x91, 0x01,
	0xa0, 0x00,
	0xa1, 0x00,
	0xa2, 0x00,
	0xb0, 0x91,
	0xb1, 0x0b,
	0xc0, 0x53,
	0xc1, 0x70,
	0xc2, 0x12,
	0xd0, 0x00,
	0xd1, 0x00,
	0xd2, 0x00,
	0xd3, 0x00,
	0xd4, 0x00,
	0xd5, 0x00,
	0xde, 0x00,
	0xdf, 0x00,
	0x61, 0x49,
	0x62, 0x0b,
	0x53, 0x08,
	0x59, 0x08,
};


static int stv0297_writereg(struct stv0297_state *state, u8 reg, u8 data)
{
	int ret;
	u8 buf[] = { reg, data };
	struct i2c_msg msg = {.addr = state->config->demod_address,.flags = 0,.buf = buf,.len = 2 };

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: writereg error (reg == 0x%02x, val == 0x%02x, "
			"ret == %i)\n", __FUNCTION__, reg, data, ret);

	return (ret != 1) ? -1 : 0;
}

static int stv0297_readreg(struct stv0297_state *state, u8 reg)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = { {.addr = state->config->demod_address,.flags = 0,.buf = b0,.len =
				  1},
	{.addr = state->config->demod_address,.flags = I2C_M_RD,.buf = b1,.len = 1}
	};

	// this device needs a STOP between the register and data
	if ((ret = i2c_transfer(state->i2c, &msg[0], 1)) != 1) {
		dprintk("%s: readreg error (reg == 0x%02x, ret == %i)\n", __FUNCTION__, reg, ret);
		return -1;
	}
	if ((ret = i2c_transfer(state->i2c, &msg[1], 1)) != 1) {
		dprintk("%s: readreg error (reg == 0x%02x, ret == %i)\n", __FUNCTION__, reg, ret);
		return -1;
	}

	return b1[0];
}

static int stv0297_writereg_mask(struct stv0297_state *state, u8 reg, u8 mask, u8 data)
{
	int val;

	val = stv0297_readreg(state, reg);
	val &= ~mask;
	val |= (data & mask);
	stv0297_writereg(state, reg, val);

	return 0;
}

static int stv0297_readregs(struct stv0297_state *state, u8 reg1, u8 * b, u8 len)
{
	int ret;
	struct i2c_msg msg[] = { {.addr = state->config->demod_address,.flags = 0,.buf =
				  &reg1,.len = 1},
	{.addr = state->config->demod_address,.flags = I2C_M_RD,.buf = b,.len = len}
	};

	// this device needs a STOP between the register and data
	if ((ret = i2c_transfer(state->i2c, &msg[0], 1)) != 1) {
		dprintk("%s: readreg error (reg == 0x%02x, ret == %i)\n", __FUNCTION__, reg1, ret);
		return -1;
	}
	if ((ret = i2c_transfer(state->i2c, &msg[1], 1)) != 1) {
		dprintk("%s: readreg error (reg == 0x%02x, ret == %i)\n", __FUNCTION__, reg1, ret);
		return -1;
	}

	return 0;
}

static u32 stv0297_get_symbolrate(struct stv0297_state *state)
{
	u64 tmp;

	tmp = stv0297_readreg(state, 0x55);
	tmp |= stv0297_readreg(state, 0x56) << 8;
	tmp |= stv0297_readreg(state, 0x57) << 16;
	tmp |= stv0297_readreg(state, 0x58) << 24;

	tmp *= STV0297_CLOCK_KHZ;
	tmp >>= 32;

	return (u32) tmp;
}

static void stv0297_set_symbolrate(struct stv0297_state *state, u32 srate)
{
	long tmp;

	tmp = 131072L * srate;	/* 131072 = 2^17  */
	tmp = tmp / (STV0297_CLOCK_KHZ / 4);	/* 1/4 = 2^-2 */
	tmp = tmp * 8192L;	/* 8192 = 2^13 */

	stv0297_writereg(state, 0x55, (unsigned char) (tmp & 0xFF));
	stv0297_writereg(state, 0x56, (unsigned char) (tmp >> 8));
	stv0297_writereg(state, 0x57, (unsigned char) (tmp >> 16));
	stv0297_writereg(state, 0x58, (unsigned char) (tmp >> 24));
}

static void stv0297_set_sweeprate(struct stv0297_state *state, short fshift, long symrate)
{
	long tmp;

	tmp = (long) fshift *262144L;	/* 262144 = 2*18 */
	tmp /= symrate;
	tmp *= 1024;		/* 1024 = 2*10   */

	// adjust
	if (tmp >= 0) {
		tmp += 500000;
	} else {
		tmp -= 500000;
	}
	tmp /= 1000000;

	stv0297_writereg(state, 0x60, tmp & 0xFF);
	stv0297_writereg_mask(state, 0x69, 0xF0, (tmp >> 4) & 0xf0);
}

static void stv0297_set_carrieroffset(struct stv0297_state *state, long offset)
{
	long tmp;

	/* symrate is hardcoded to 10000 */
	tmp = offset * 26844L;	/* (2**28)/10000 */
	if (tmp < 0)
		tmp += 0x10000000;
	tmp &= 0x0FFFFFFF;

	stv0297_writereg(state, 0x66, (unsigned char) (tmp & 0xFF));
	stv0297_writereg(state, 0x67, (unsigned char) (tmp >> 8));
	stv0297_writereg(state, 0x68, (unsigned char) (tmp >> 16));
	stv0297_writereg_mask(state, 0x69, 0x0F, (tmp >> 24) & 0x0f);
}

/*
static long stv0297_get_carrieroffset(struct stv0297_state *state)
{
	s64 tmp;

	stv0297_writereg(state, 0x6B, 0x00);

	tmp = stv0297_readreg(state, 0x66);
	tmp |= (stv0297_readreg(state, 0x67) << 8);
	tmp |= (stv0297_readreg(state, 0x68) << 16);
	tmp |= (stv0297_readreg(state, 0x69) & 0x0F) << 24;

	tmp *= stv0297_get_symbolrate(state);
	tmp >>= 28;

	return (s32) tmp;
}
*/

static void stv0297_set_initialdemodfreq(struct stv0297_state *state, long freq)
{
	s32 tmp;

	if (freq > 10000)
		freq -= STV0297_CLOCK_KHZ;

	tmp = (STV0297_CLOCK_KHZ * 1000) / (1 << 16);
	tmp = (freq * 1000) / tmp;
	if (tmp > 0xffff)
		tmp = 0xffff;

	stv0297_writereg_mask(state, 0x25, 0x80, 0x80);
	stv0297_writereg(state, 0x21, tmp >> 8);
	stv0297_writereg(state, 0x20, tmp);
}

static int stv0297_set_qam(struct stv0297_state *state, fe_modulation_t modulation)
{
	int val = 0;

	switch (modulation) {
	case QAM_16:
		val = 0;
		break;

	case QAM_32:
		val = 1;
		break;

	case QAM_64:
		val = 4;
		break;

	case QAM_128:
		val = 2;
		break;

	case QAM_256:
		val = 3;
		break;

	default:
		return -EINVAL;
	}

	stv0297_writereg_mask(state, 0x00, 0x70, val << 4);

	return 0;
}

static int stv0297_set_inversion(struct stv0297_state *state, fe_spectral_inversion_t inversion)
{
	int val = 0;

	switch (inversion) {
	case INVERSION_OFF:
		val = 0;
		break;

	case INVERSION_ON:
		val = 1;
		break;

	default:
		return -EINVAL;
	}

	stv0297_writereg_mask(state, 0x83, 0x08, val << 3);

	return 0;
}

int stv0297_enable_plli2c(struct dvb_frontend *fe)
{
	struct stv0297_state *state = (struct stv0297_state *) fe->demodulator_priv;

	stv0297_writereg(state, 0x87, 0x78);
	stv0297_writereg(state, 0x86, 0xc8);

	return 0;
}

static int stv0297_init(struct dvb_frontend *fe)
{
	struct stv0297_state *state = (struct stv0297_state *) fe->demodulator_priv;
	int i;

	/* soft reset */
	stv0297_writereg_mask(state, 0x80, 1, 1);
	stv0297_writereg_mask(state, 0x80, 1, 0);

	/* reset deinterleaver */
	stv0297_writereg_mask(state, 0x81, 1, 1);
	stv0297_writereg_mask(state, 0x81, 1, 0);

	/* load init table */
	for (i = 0; i < sizeof(init_tab); i += 2) {
		stv0297_writereg(state, init_tab[i], init_tab[i + 1]);
	}

	/* set a dummy symbol rate */
	stv0297_set_symbolrate(state, 6900);

	/* invert AGC1 polarity */
	stv0297_writereg_mask(state, 0x88, 0x10, 0x10);

	/* setup bit error counting */
	stv0297_writereg_mask(state, 0xA0, 0x80, 0x00);
	stv0297_writereg_mask(state, 0xA0, 0x10, 0x00);
	stv0297_writereg_mask(state, 0xA0, 0x08, 0x00);
	stv0297_writereg_mask(state, 0xA0, 0x07, 0x04);

	/* min + max PWM */
	stv0297_writereg(state, 0x4a, 0x00);
	stv0297_writereg(state, 0x4b, state->pwm);
	msleep(200);

	if (state->config->pll_init)
		state->config->pll_init(fe);

	return 0;
}

static int stv0297_sleep(struct dvb_frontend *fe)
{
	struct stv0297_state *state = (struct stv0297_state *) fe->demodulator_priv;

	stv0297_writereg_mask(state, 0x80, 1, 1);

	return 0;
}

static int stv0297_read_status(struct dvb_frontend *fe, fe_status_t * status)
{
	struct stv0297_state *state = (struct stv0297_state *) fe->demodulator_priv;

	u8 sync = stv0297_readreg(state, 0xDF);

	*status = 0;
	if (sync & 0x80)
		*status |=
			FE_HAS_SYNC | FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_LOCK;
	return 0;
}

static int stv0297_read_ber(struct dvb_frontend *fe, u32 * ber)
{
	struct stv0297_state *state = (struct stv0297_state *) fe->demodulator_priv;
	u8 BER[3];

	stv0297_writereg(state, 0xA0, 0x80);	// Start Counting bit errors for 4096 Bytes
	mdelay(25);		// Hopefully got 4096 Bytes
	stv0297_readregs(state, 0xA0, BER, 3);
	mdelay(25);
	*ber = (BER[2] << 8 | BER[1]) / (8 * 4096);

	return 0;
}


static int stv0297_read_signal_strength(struct dvb_frontend *fe, u16 * strength)
{
	struct stv0297_state *state = (struct stv0297_state *) fe->demodulator_priv;
	u8 STRENGTH[2];

	stv0297_readregs(state, 0x41, STRENGTH, 2);
	*strength = (STRENGTH[1] & 0x03) << 8 | STRENGTH[0];

	return 0;
}

static int stv0297_read_snr(struct dvb_frontend *fe, u16 * snr)
{
	struct stv0297_state *state = (struct stv0297_state *) fe->demodulator_priv;
	u8 SNR[2];

	stv0297_readregs(state, 0x07, SNR, 2);
	*snr = SNR[1] << 8 | SNR[0];

	return 0;
}

static int stv0297_read_ucblocks(struct dvb_frontend *fe, u32 * ucblocks)
{
	struct stv0297_state *state = (struct stv0297_state *) fe->demodulator_priv;

	*ucblocks = (stv0297_readreg(state, 0xD5) << 8)
		| stv0297_readreg(state, 0xD4);

	return 0;
}

static int stv0297_set_frontend(struct dvb_frontend *fe, struct dvb_frontend_parameters *p)
{
	struct stv0297_state *state = (struct stv0297_state *) fe->demodulator_priv;
	int u_threshold;
	int initial_u;
	int blind_u;
	int delay;
	int sweeprate;
	int carrieroffset;
	unsigned long starttime;
	unsigned long timeout;
	fe_spectral_inversion_t inversion;

	switch (p->u.qam.modulation) {
	case QAM_16:
	case QAM_32:
	case QAM_64:
		delay = 100;
		sweeprate = 1500;
		break;

	case QAM_128:
		delay = 150;
		sweeprate = 1000;
		break;

	case QAM_256:
		delay = 200;
		sweeprate = 500;
		break;

	default:
		return -EINVAL;
	}

	// determine inversion dependant parameters
	inversion = p->inversion;
	if (state->config->invert)
		inversion = (inversion == INVERSION_ON) ? INVERSION_OFF : INVERSION_ON;
	carrieroffset = -330;
	switch (inversion) {
	case INVERSION_OFF:
		break;

	case INVERSION_ON:
		sweeprate = -sweeprate;
		carrieroffset = -carrieroffset;
		break;

	default:
		return -EINVAL;
	}

	stv0297_init(fe);
	state->config->pll_set(fe, p);

	/* clear software interrupts */
	stv0297_writereg(state, 0x82, 0x0);

	/* set initial demodulation frequency */
	stv0297_set_initialdemodfreq(state, 7250);

	/* setup AGC */
	stv0297_writereg_mask(state, 0x43, 0x10, 0x00);
	stv0297_writereg(state, 0x41, 0x00);
	stv0297_writereg_mask(state, 0x42, 0x03, 0x01);
	stv0297_writereg_mask(state, 0x36, 0x60, 0x00);
	stv0297_writereg_mask(state, 0x36, 0x18, 0x00);
	stv0297_writereg_mask(state, 0x71, 0x80, 0x80);
	stv0297_writereg(state, 0x72, 0x00);
	stv0297_writereg(state, 0x73, 0x00);
	stv0297_writereg_mask(state, 0x74, 0x0F, 0x00);
	stv0297_writereg_mask(state, 0x43, 0x08, 0x00);
	stv0297_writereg_mask(state, 0x71, 0x80, 0x00);

	/* setup STL */
	stv0297_writereg_mask(state, 0x5a, 0x20, 0x20);
	stv0297_writereg_mask(state, 0x5b, 0x02, 0x02);
	stv0297_writereg_mask(state, 0x5b, 0x02, 0x00);
	stv0297_writereg_mask(state, 0x5b, 0x01, 0x00);
	stv0297_writereg_mask(state, 0x5a, 0x40, 0x40);

	/* disable frequency sweep */
	stv0297_writereg_mask(state, 0x6a, 0x01, 0x00);

	/* reset deinterleaver */
	stv0297_writereg_mask(state, 0x81, 0x01, 0x01);
	stv0297_writereg_mask(state, 0x81, 0x01, 0x00);

	/* ??? */
	stv0297_writereg_mask(state, 0x83, 0x20, 0x20);
	stv0297_writereg_mask(state, 0x83, 0x20, 0x00);

	/* reset equaliser */
	u_threshold = stv0297_readreg(state, 0x00) & 0xf;
	initial_u = stv0297_readreg(state, 0x01) >> 4;
	blind_u = stv0297_readreg(state, 0x01) & 0xf;
	stv0297_writereg_mask(state, 0x84, 0x01, 0x01);
	stv0297_writereg_mask(state, 0x84, 0x01, 0x00);
	stv0297_writereg_mask(state, 0x00, 0x0f, u_threshold);
	stv0297_writereg_mask(state, 0x01, 0xf0, initial_u << 4);
	stv0297_writereg_mask(state, 0x01, 0x0f, blind_u);

	/* data comes from internal A/D */
	stv0297_writereg_mask(state, 0x87, 0x80, 0x00);

	/* clear phase registers */
	stv0297_writereg(state, 0x63, 0x00);
	stv0297_writereg(state, 0x64, 0x00);
	stv0297_writereg(state, 0x65, 0x00);
	stv0297_writereg(state, 0x66, 0x00);
	stv0297_writereg(state, 0x67, 0x00);
	stv0297_writereg(state, 0x68, 0x00);
	stv0297_writereg_mask(state, 0x69, 0x0f, 0x00);

	/* set parameters */
	stv0297_set_qam(state, p->u.qam.modulation);
	stv0297_set_symbolrate(state, p->u.qam.symbol_rate / 1000);
	stv0297_set_sweeprate(state, sweeprate, p->u.qam.symbol_rate / 1000);
	stv0297_set_carrieroffset(state, carrieroffset);
	stv0297_set_inversion(state, inversion);

	/* kick off lock */
	stv0297_writereg_mask(state, 0x88, 0x08, 0x08);
	stv0297_writereg_mask(state, 0x5a, 0x20, 0x00);
	stv0297_writereg_mask(state, 0x6a, 0x01, 0x01);
	stv0297_writereg_mask(state, 0x43, 0x40, 0x40);
	stv0297_writereg_mask(state, 0x5b, 0x30, 0x00);
	stv0297_writereg_mask(state, 0x03, 0x0c, 0x0c);
	stv0297_writereg_mask(state, 0x03, 0x03, 0x03);
	stv0297_writereg_mask(state, 0x43, 0x10, 0x10);

	/* wait for WGAGC lock */
	starttime = jiffies;
	timeout = jiffies + (200 * HZ) / 1000;
	while (time_before(jiffies, timeout)) {
		msleep(10);
		if (stv0297_readreg(state, 0x43) & 0x08)
			break;
	}
	if (time_after(jiffies, timeout)) {
		goto timeout;
	}
	msleep(20);

	/* wait for equaliser partial convergence */
	timeout = jiffies + (50 * HZ) / 1000;
	while (time_before(jiffies, timeout)) {
		msleep(10);

		if (stv0297_readreg(state, 0x82) & 0x04) {
			break;
		}
	}
	if (time_after(jiffies, timeout)) {
		goto timeout;
	}

	/* wait for equaliser full convergence */
	timeout = jiffies + (delay * HZ) / 1000;
	while (time_before(jiffies, timeout)) {
		msleep(10);

		if (stv0297_readreg(state, 0x82) & 0x08) {
			break;
		}
	}
	if (time_after(jiffies, timeout)) {
		goto timeout;
	}

	/* disable sweep */
	stv0297_writereg_mask(state, 0x6a, 1, 0);
	stv0297_writereg_mask(state, 0x88, 8, 0);

	/* wait for main lock */
	timeout = jiffies + (20 * HZ) / 1000;
	while (time_before(jiffies, timeout)) {
		msleep(10);

		if (stv0297_readreg(state, 0xDF) & 0x80) {
			break;
		}
	}
	if (time_after(jiffies, timeout)) {
		goto timeout;
	}
	msleep(100);

	/* is it still locked after that delay? */
	if (!(stv0297_readreg(state, 0xDF) & 0x80)) {
		goto timeout;
	}

	/* success!! */
	stv0297_writereg_mask(state, 0x5a, 0x40, 0x00);
	state->base_freq = p->frequency;
	return 0;

timeout:
	stv0297_writereg_mask(state, 0x6a, 0x01, 0x00);
	return 0;
}

static int stv0297_get_frontend(struct dvb_frontend *fe, struct dvb_frontend_parameters *p)
{
	struct stv0297_state *state = (struct stv0297_state *) fe->demodulator_priv;
	int reg_00, reg_83;

	reg_00 = stv0297_readreg(state, 0x00);
	reg_83 = stv0297_readreg(state, 0x83);

	p->frequency = state->base_freq;
	p->inversion = (reg_83 & 0x08) ? INVERSION_ON : INVERSION_OFF;
	if (state->config->invert)
		p->inversion = (p->inversion == INVERSION_ON) ? INVERSION_OFF : INVERSION_ON;
	p->u.qam.symbol_rate = stv0297_get_symbolrate(state) * 1000;
	p->u.qam.fec_inner = FEC_NONE;

	switch ((reg_00 >> 4) & 0x7) {
	case 0:
		p->u.qam.modulation = QAM_16;
		break;
	case 1:
		p->u.qam.modulation = QAM_32;
		break;
	case 2:
		p->u.qam.modulation = QAM_128;
		break;
	case 3:
		p->u.qam.modulation = QAM_256;
		break;
	case 4:
		p->u.qam.modulation = QAM_64;
		break;
	}

	return 0;
}

static void stv0297_release(struct dvb_frontend *fe)
{
	struct stv0297_state *state = (struct stv0297_state *) fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops stv0297_ops;

struct dvb_frontend *stv0297_attach(const struct stv0297_config *config,
				    struct i2c_adapter *i2c, int pwm)
{
	struct stv0297_state *state = NULL;

	/* allocate memory for the internal state */
	state = (struct stv0297_state *) kmalloc(sizeof(struct stv0297_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	memcpy(&state->ops, &stv0297_ops, sizeof(struct dvb_frontend_ops));
	state->base_freq = 0;
	state->pwm = pwm;

	/* check if the demod is there */
	if ((stv0297_readreg(state, 0x80) & 0x70) != 0x20)
		goto error;

	/* create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops stv0297_ops = {

	.info = {
		 .name = "ST STV0297 DVB-C",
		 .type = FE_QAM,
		 .frequency_min = 64000000,
		 .frequency_max = 1300000000,
		 .frequency_stepsize = 62500,
		 .symbol_rate_min = 870000,
		 .symbol_rate_max = 11700000,
		 .caps = FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 |
		 FE_CAN_QAM_128 | FE_CAN_QAM_256 | FE_CAN_FEC_AUTO},

	.release = stv0297_release,

	.init = stv0297_init,
	.sleep = stv0297_sleep,

	.set_frontend = stv0297_set_frontend,
	.get_frontend = stv0297_get_frontend,

	.read_status = stv0297_read_status,
	.read_ber = stv0297_read_ber,
	.read_signal_strength = stv0297_read_signal_strength,
	.read_snr = stv0297_read_snr,
	.read_ucblocks = stv0297_read_ucblocks,
};

MODULE_DESCRIPTION("ST STV0297 DVB-C Demodulator driver");
MODULE_AUTHOR("Dennis Noermann and Andrew de Quincey");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(stv0297_attach);
EXPORT_SYMBOL(stv0297_enable_plli2c);
