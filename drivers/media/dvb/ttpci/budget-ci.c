/*
 * budget-ci.c: driver for the SAA7146 based Budget DVB cards
 *
 * Compiled from various sources by Michael Hunold <michael@mihu.de>
 *
 *     msp430 IR support contributed by Jack Thomasson <jkt@Helius.COM>
 *     partially based on the Siemens DVB driver by Ralph+Marcus Metzler
 *
 * CI interface support (c) 2004 Andrew de Quincey <adq_dvb@lidskialf.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
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

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/spinlock.h>

#include "dvb_ca_en50221.h"
#include "stv0299.h"
#include "tda1004x.h"

#define DEBIADDR_IR		0x1234
#define DEBIADDR_CICONTROL	0x0000
#define DEBIADDR_CIVERSION	0x4000
#define DEBIADDR_IO		0x1000
#define DEBIADDR_ATTR		0x3000

#define CICONTROL_RESET		0x01
#define CICONTROL_ENABLETS	0x02
#define CICONTROL_CAMDETECT	0x08

#define DEBICICTL		0x00420000
#define DEBICICAM		0x02420000

#define SLOTSTATUS_NONE		1
#define SLOTSTATUS_PRESENT	2
#define SLOTSTATUS_RESET	4
#define SLOTSTATUS_READY	8
#define SLOTSTATUS_OCCUPIED	(SLOTSTATUS_PRESENT|SLOTSTATUS_RESET|SLOTSTATUS_READY)

struct budget_ci {
	struct budget budget;
	struct input_dev input_dev;
	struct tasklet_struct msp430_irq_tasklet;
	struct tasklet_struct ciintf_irq_tasklet;
	int slot_status;
	struct dvb_ca_en50221 ca;
	char ir_dev_name[50];
};

/* from reading the following remotes:
   Zenith Universal 7 / TV Mode 807 / VCR Mode 837
   Hauppauge (from NOVA-CI-s box product)
   i've taken a "middle of the road" approach and note the differences
*/
static u16 key_map[64] = {
	/* 0x0X */
	KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8,
	KEY_9,
	KEY_ENTER,
	KEY_RED,
	KEY_POWER,		/* RADIO on Hauppauge */
	KEY_MUTE,
	0,
	KEY_A,			/* TV on Hauppauge */
	/* 0x1X */
	KEY_VOLUMEUP, KEY_VOLUMEDOWN,
	0, 0,
	KEY_B,
	0, 0, 0, 0, 0, 0, 0,
	KEY_UP, KEY_DOWN,
	KEY_OPTION,		/* RESERVED on Hauppauge */
	KEY_BREAK,
	/* 0x2X */
	KEY_CHANNELUP, KEY_CHANNELDOWN,
	KEY_PREVIOUS,		/* Prev. Ch on Zenith, SOURCE on Hauppauge */
	0, KEY_RESTART, KEY_OK,
	KEY_CYCLEWINDOWS,	/* MINIMIZE on Hauppauge */
	0,
	KEY_ENTER,		/* VCR mode on Zenith */
	KEY_PAUSE,
	0,
	KEY_RIGHT, KEY_LEFT,
	0,
	KEY_MENU,		/* FULL SCREEN on Hauppauge */
	0,
	/* 0x3X */
	KEY_SLOW,
	KEY_PREVIOUS,		/* VCR mode on Zenith */
	KEY_REWIND,
	0,
	KEY_FASTFORWARD,
	KEY_PLAY, KEY_STOP,
	KEY_RECORD,
	KEY_TUNER,		/* TV/VCR on Zenith */
	0,
	KEY_C,
	0,
	KEY_EXIT,
	KEY_POWER2,
	KEY_TUNER,		/* VCR mode on Zenith */
	0,
};

static void msp430_ir_debounce(unsigned long data)
{
	struct input_dev *dev = (struct input_dev *) data;

	if (dev->rep[0] == 0 || dev->rep[0] == ~0) {
		input_event(dev, EV_KEY, key_map[dev->repeat_key], !!0);
		return;
	}

	dev->rep[0] = 0;
	dev->timer.expires = jiffies + HZ * 350 / 1000;
	add_timer(&dev->timer);
	input_event(dev, EV_KEY, key_map[dev->repeat_key], 2);	/* REPEAT */
}

static void msp430_ir_interrupt(unsigned long data)
{
	struct budget_ci *budget_ci = (struct budget_ci *) data;
	struct input_dev *dev = &budget_ci->input_dev;
	unsigned int code =
		ttpci_budget_debiread(&budget_ci->budget, DEBINOSWAP, DEBIADDR_IR, 2, 1, 0) >> 8;

	if (code & 0x40) {
		code &= 0x3f;

		if (timer_pending(&dev->timer)) {
			if (code == dev->repeat_key) {
				++dev->rep[0];
				return;
			}
			del_timer(&dev->timer);
			input_event(dev, EV_KEY, key_map[dev->repeat_key], !!0);
		}

		if (!key_map[code]) {
			printk("DVB (%s): no key for %02x!\n", __FUNCTION__, code);
			return;
		}

		/* initialize debounce and repeat */
		dev->repeat_key = code;
		/* Zenith remote _always_ sends 2 sequences */
		dev->rep[0] = ~0;
		/* 350 milliseconds */
		dev->timer.expires = jiffies + HZ * 350 / 1000;
		/* MAKE */
		input_event(dev, EV_KEY, key_map[code], !0);
		add_timer(&dev->timer);
	}
}

static int msp430_ir_init(struct budget_ci *budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;
	int i;

	memset(&budget_ci->input_dev, 0, sizeof(struct input_dev));

	sprintf(budget_ci->ir_dev_name, "Budget-CI dvb ir receiver %s", saa->name);
	budget_ci->input_dev.name = budget_ci->ir_dev_name;

	set_bit(EV_KEY, budget_ci->input_dev.evbit);

	for (i = 0; i < sizeof(key_map) / sizeof(*key_map); i++)
		if (key_map[i])
			set_bit(key_map[i], budget_ci->input_dev.keybit);

	input_register_device(&budget_ci->input_dev);

	budget_ci->input_dev.timer.function = msp430_ir_debounce;

	saa7146_write(saa, IER, saa7146_read(saa, IER) | MASK_06);

	saa7146_setgpio(saa, 3, SAA7146_GPIO_IRQHI);

	return 0;
}

static void msp430_ir_deinit(struct budget_ci *budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;
	struct input_dev *dev = &budget_ci->input_dev;

	saa7146_write(saa, IER, saa7146_read(saa, IER) & ~MASK_06);
	saa7146_setgpio(saa, 3, SAA7146_GPIO_INPUT);

	if (del_timer(&dev->timer))
		input_event(dev, EV_KEY, key_map[dev->repeat_key], !!0);

	input_unregister_device(dev);
}

static int ciintf_read_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;

	if (slot != 0)
		return -EINVAL;

	return ttpci_budget_debiread(&budget_ci->budget, DEBICICAM,
				     DEBIADDR_ATTR | (address & 0xfff), 1, 1, 0);
}

static int ciintf_write_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address, u8 value)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;

	if (slot != 0)
		return -EINVAL;

	return ttpci_budget_debiwrite(&budget_ci->budget, DEBICICAM,
				      DEBIADDR_ATTR | (address & 0xfff), 1, value, 1, 0);
}

static int ciintf_read_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;

	if (slot != 0)
		return -EINVAL;

	return ttpci_budget_debiread(&budget_ci->budget, DEBICICAM,
				     DEBIADDR_IO | (address & 3), 1, 1, 0);
}

static int ciintf_write_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address, u8 value)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;

	if (slot != 0)
		return -EINVAL;

	return ttpci_budget_debiwrite(&budget_ci->budget, DEBICICAM,
				      DEBIADDR_IO | (address & 3), 1, value, 1, 0);
}

static int ciintf_slot_reset(struct dvb_ca_en50221 *ca, int slot)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;
	struct saa7146_dev *saa = budget_ci->budget.dev;

	if (slot != 0)
		return -EINVAL;

	// trigger on RISING edge during reset so we know when READY is re-asserted
	saa7146_setgpio(saa, 0, SAA7146_GPIO_IRQHI);
	budget_ci->slot_status = SLOTSTATUS_RESET;
	ttpci_budget_debiwrite(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1, 0, 1, 0);
	msleep(1);
	ttpci_budget_debiwrite(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1,
			       CICONTROL_RESET, 1, 0);

	saa7146_setgpio(saa, 1, SAA7146_GPIO_OUTHI);
	ttpci_budget_set_video_port(saa, BUDGET_VIDEO_PORTB);
	return 0;
}

static int ciintf_slot_shutdown(struct dvb_ca_en50221 *ca, int slot)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;
	struct saa7146_dev *saa = budget_ci->budget.dev;

	if (slot != 0)
		return -EINVAL;

	saa7146_setgpio(saa, 1, SAA7146_GPIO_OUTHI);
	ttpci_budget_set_video_port(saa, BUDGET_VIDEO_PORTB);
	return 0;
}

static int ciintf_slot_ts_enable(struct dvb_ca_en50221 *ca, int slot)
{
	struct budget_ci *budget_ci = (struct budget_ci *) ca->data;
	struct saa7146_dev *saa = budget_ci->budget.dev;
	int tmp;

	if (slot != 0)
		return -EINVAL;

	saa7146_setgpio(saa, 1, SAA7146_GPIO_OUTLO);

	tmp = ttpci_budget_debiread(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1, 1, 0);
	ttpci_budget_debiwrite(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1,
			       tmp | CICONTROL_ENABLETS, 1, 0);

	ttpci_budget_set_video_port(saa, BUDGET_VIDEO_PORTA);
	return 0;
}

static void ciintf_interrupt(unsigned long data)
{
	struct budget_ci *budget_ci = (struct budget_ci *) data;
	struct saa7146_dev *saa = budget_ci->budget.dev;
	unsigned int flags;

	// ensure we don't get spurious IRQs during initialisation
	if (!budget_ci->budget.ci_present)
		return;

	// read the CAM status
	flags = ttpci_budget_debiread(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1, 1, 0);
	if (flags & CICONTROL_CAMDETECT) {

		// GPIO should be set to trigger on falling edge if a CAM is present
		saa7146_setgpio(saa, 0, SAA7146_GPIO_IRQLO);

		if (budget_ci->slot_status & SLOTSTATUS_NONE) {
			// CAM insertion IRQ
			budget_ci->slot_status = SLOTSTATUS_PRESENT;
			dvb_ca_en50221_camchange_irq(&budget_ci->ca, 0,
						     DVB_CA_EN50221_CAMCHANGE_INSERTED);

		} else if (budget_ci->slot_status & SLOTSTATUS_RESET) {
			// CAM ready (reset completed)
			budget_ci->slot_status = SLOTSTATUS_READY;
			dvb_ca_en50221_camready_irq(&budget_ci->ca, 0);

		} else if (budget_ci->slot_status & SLOTSTATUS_READY) {
			// FR/DA IRQ
			dvb_ca_en50221_frda_irq(&budget_ci->ca, 0);
		}
	} else {

		// trigger on rising edge if a CAM is not present - when a CAM is inserted, we
		// only want to get the IRQ when it sets READY. If we trigger on the falling edge,
		// the CAM might not actually be ready yet.
		saa7146_setgpio(saa, 0, SAA7146_GPIO_IRQHI);

		// generate a CAM removal IRQ if we haven't already
		if (budget_ci->slot_status & SLOTSTATUS_OCCUPIED) {
			// CAM removal IRQ
			budget_ci->slot_status = SLOTSTATUS_NONE;
			dvb_ca_en50221_camchange_irq(&budget_ci->ca, 0,
						     DVB_CA_EN50221_CAMCHANGE_REMOVED);
		}
	}
}

static int ciintf_init(struct budget_ci *budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;
	int flags;
	int result;

	memset(&budget_ci->ca, 0, sizeof(struct dvb_ca_en50221));

	// enable DEBI pins
	saa7146_write(saa, MC1, saa7146_read(saa, MC1) | (0x800 << 16) | 0x800);

	// test if it is there
	if ((ttpci_budget_debiread(&budget_ci->budget, DEBICICTL, DEBIADDR_CIVERSION, 1, 1, 0) & 0xa0) != 0xa0) {
		result = -ENODEV;
		goto error;
	}
	// determine whether a CAM is present or not
	flags = ttpci_budget_debiread(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1, 1, 0);
	budget_ci->slot_status = SLOTSTATUS_NONE;
	if (flags & CICONTROL_CAMDETECT)
		budget_ci->slot_status = SLOTSTATUS_PRESENT;

	// register CI interface
	budget_ci->ca.owner = THIS_MODULE;
	budget_ci->ca.read_attribute_mem = ciintf_read_attribute_mem;
	budget_ci->ca.write_attribute_mem = ciintf_write_attribute_mem;
	budget_ci->ca.read_cam_control = ciintf_read_cam_control;
	budget_ci->ca.write_cam_control = ciintf_write_cam_control;
	budget_ci->ca.slot_reset = ciintf_slot_reset;
	budget_ci->ca.slot_shutdown = ciintf_slot_shutdown;
	budget_ci->ca.slot_ts_enable = ciintf_slot_ts_enable;
	budget_ci->ca.data = budget_ci;
	if ((result = dvb_ca_en50221_init(budget_ci->budget.dvb_adapter,
					  &budget_ci->ca,
					  DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE |
					  DVB_CA_EN50221_FLAG_IRQ_FR |
					  DVB_CA_EN50221_FLAG_IRQ_DA, 1)) != 0) {
		printk("budget_ci: CI interface detected, but initialisation failed.\n");
		goto error;
	}
	// Setup CI slot IRQ
	tasklet_init(&budget_ci->ciintf_irq_tasklet, ciintf_interrupt, (unsigned long) budget_ci);
	if (budget_ci->slot_status != SLOTSTATUS_NONE) {
		saa7146_setgpio(saa, 0, SAA7146_GPIO_IRQLO);
	} else {
		saa7146_setgpio(saa, 0, SAA7146_GPIO_IRQHI);
	}
	saa7146_write(saa, IER, saa7146_read(saa, IER) | MASK_03);
	ttpci_budget_debiwrite(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1,
			       CICONTROL_RESET, 1, 0);

	// success!
	printk("budget_ci: CI interface initialised\n");
	budget_ci->budget.ci_present = 1;

	// forge a fake CI IRQ so the CAM state is setup correctly
	flags = DVB_CA_EN50221_CAMCHANGE_REMOVED;
	if (budget_ci->slot_status != SLOTSTATUS_NONE)
		flags = DVB_CA_EN50221_CAMCHANGE_INSERTED;
	dvb_ca_en50221_camchange_irq(&budget_ci->ca, 0, flags);

	return 0;

error:
	saa7146_write(saa, MC1, saa7146_read(saa, MC1) | (0x800 << 16));
	return result;
}

static void ciintf_deinit(struct budget_ci *budget_ci)
{
	struct saa7146_dev *saa = budget_ci->budget.dev;

	// disable CI interrupts
	saa7146_write(saa, IER, saa7146_read(saa, IER) & ~MASK_03);
	saa7146_setgpio(saa, 0, SAA7146_GPIO_INPUT);
	tasklet_kill(&budget_ci->ciintf_irq_tasklet);
	ttpci_budget_debiwrite(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1, 0, 1, 0);
	msleep(1);
	ttpci_budget_debiwrite(&budget_ci->budget, DEBICICTL, DEBIADDR_CICONTROL, 1,
			       CICONTROL_RESET, 1, 0);

	// disable TS data stream to CI interface
	saa7146_setgpio(saa, 1, SAA7146_GPIO_INPUT);

	// release the CA device
	dvb_ca_en50221_release(&budget_ci->ca);

	// disable DEBI pins
	saa7146_write(saa, MC1, saa7146_read(saa, MC1) | (0x800 << 16));
}

static void budget_ci_irq(struct saa7146_dev *dev, u32 * isr)
{
	struct budget_ci *budget_ci = (struct budget_ci *) dev->ext_priv;

	dprintk(8, "dev: %p, budget_ci: %p\n", dev, budget_ci);

	if (*isr & MASK_06)
		tasklet_schedule(&budget_ci->msp430_irq_tasklet);

	if (*isr & MASK_10)
		ttpci_budget_irq10_handler(dev, isr);

	if ((*isr & MASK_03) && (budget_ci->budget.ci_present))
		tasklet_schedule(&budget_ci->ciintf_irq_tasklet);
}


static u8 alps_bsru6_inittab[] = {
	0x01, 0x15,
	0x02, 0x00,
	0x03, 0x00,
	0x04, 0x7d,		/* F22FR = 0x7d, F22 = f_VCO / 128 / 0x7d = 22 kHz */
	0x05, 0x35,		/* I2CT = 0, SCLT = 1, SDAT = 1 */
	0x06, 0x40,		/* DAC not used, set to high impendance mode */
	0x07, 0x00,		/* DAC LSB */
	0x08, 0x40,		/* DiSEqC off, LNB power on OP2/LOCK pin on */
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
	0x0f, 0x52,
	0xff, 0xff
};

static int alps_bsru6_set_symbol_rate(struct dvb_frontend *fe, u32 srate, u32 ratio)
{
	u8 aclk = 0;
	u8 bclk = 0;

	if (srate < 1500000) {
		aclk = 0xb7;
		bclk = 0x47;
	} else if (srate < 3000000) {
		aclk = 0xb7;
		bclk = 0x4b;
	} else if (srate < 7000000) {
		aclk = 0xb7;
		bclk = 0x4f;
	} else if (srate < 14000000) {
		aclk = 0xb7;
		bclk = 0x53;
	} else if (srate < 30000000) {
		aclk = 0xb6;
		bclk = 0x53;
	} else if (srate < 45000000) {
		aclk = 0xb4;
		bclk = 0x51;
	}

	stv0299_writereg(fe, 0x13, aclk);
	stv0299_writereg(fe, 0x14, bclk);
	stv0299_writereg(fe, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg(fe, 0x20, (ratio >> 8) & 0xff);
	stv0299_writereg(fe, 0x21, (ratio) & 0xf0);

	return 0;
}

static int alps_bsru6_pll_set(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct budget_ci *budget_ci = (struct budget_ci *) fe->dvb->priv;
	u8 buf[4];
	u32 div;
	struct i2c_msg msg = {.addr = 0x61,.flags = 0,.buf = buf,.len = sizeof(buf) };

	if ((params->frequency < 950000) || (params->frequency > 2150000))
		return -EINVAL;

	div = (params->frequency + (125 - 1)) / 125;	// round correctly
	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x80 | ((div & 0x18000) >> 10) | 4;
	buf[3] = 0xC4;

	if (params->frequency > 1530000)
		buf[3] = 0xc0;

	if (i2c_transfer(&budget_ci->budget.i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct stv0299_config alps_bsru6_config = {

	.demod_address = 0x68,
	.inittab = alps_bsru6_inittab,
	.mclk = 88000000UL,
	.invert = 1,
	.enhanced_tuning = 0,
	.skip_reinit = 0,
	.lock_output = STV0229_LOCKOUTPUT_1,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 100,
	.set_symbol_rate = alps_bsru6_set_symbol_rate,
	.pll_set = alps_bsru6_pll_set,
};




static u8 philips_su1278_tt_inittab[] = {
	0x01, 0x0f,
	0x02, 0x30,
	0x03, 0x00,
	0x04, 0x5b,
	0x05, 0x85,
	0x06, 0x02,
	0x07, 0x00,
	0x08, 0x02,
	0x09, 0x00,
	0x0C, 0x01,
	0x0D, 0x81,
	0x0E, 0x44,
	0x0f, 0x14,
	0x10, 0x3c,
	0x11, 0x84,
	0x12, 0xda,
	0x13, 0x97,
	0x14, 0x95,
	0x15, 0xc9,
	0x16, 0x19,
	0x17, 0x8c,
	0x18, 0x59,
	0x19, 0xf8,
	0x1a, 0xfe,
	0x1c, 0x7f,
	0x1d, 0x00,
	0x1e, 0x00,
	0x1f, 0x50,
	0x20, 0x00,
	0x21, 0x00,
	0x22, 0x00,
	0x23, 0x00,
	0x28, 0x00,
	0x29, 0x28,
	0x2a, 0x14,
	0x2b, 0x0f,
	0x2c, 0x09,
	0x2d, 0x09,
	0x31, 0x1f,
	0x32, 0x19,
	0x33, 0xfc,
	0x34, 0x93,
	0xff, 0xff
};

static int philips_su1278_tt_set_symbol_rate(struct dvb_frontend *fe, u32 srate, u32 ratio)
{
	stv0299_writereg(fe, 0x0e, 0x44);
	if (srate >= 10000000) {
		stv0299_writereg(fe, 0x13, 0x97);
		stv0299_writereg(fe, 0x14, 0x95);
		stv0299_writereg(fe, 0x15, 0xc9);
		stv0299_writereg(fe, 0x17, 0x8c);
		stv0299_writereg(fe, 0x1a, 0xfe);
		stv0299_writereg(fe, 0x1c, 0x7f);
		stv0299_writereg(fe, 0x2d, 0x09);
	} else {
		stv0299_writereg(fe, 0x13, 0x99);
		stv0299_writereg(fe, 0x14, 0x8d);
		stv0299_writereg(fe, 0x15, 0xce);
		stv0299_writereg(fe, 0x17, 0x43);
		stv0299_writereg(fe, 0x1a, 0x1d);
		stv0299_writereg(fe, 0x1c, 0x12);
		stv0299_writereg(fe, 0x2d, 0x05);
	}
	stv0299_writereg(fe, 0x0e, 0x23);
	stv0299_writereg(fe, 0x0f, 0x94);
	stv0299_writereg(fe, 0x10, 0x39);
	stv0299_writereg(fe, 0x15, 0xc9);

	stv0299_writereg(fe, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg(fe, 0x20, (ratio >> 8) & 0xff);
	stv0299_writereg(fe, 0x21, (ratio) & 0xf0);

	return 0;
}

static int philips_su1278_tt_pll_set(struct dvb_frontend *fe,
				     struct dvb_frontend_parameters *params)
{
	struct budget_ci *budget_ci = (struct budget_ci *) fe->dvb->priv;
	u32 div;
	u8 buf[4];
	struct i2c_msg msg = {.addr = 0x60,.flags = 0,.buf = buf,.len = sizeof(buf) };

	if ((params->frequency < 950000) || (params->frequency > 2150000))
		return -EINVAL;

	div = (params->frequency + (500 - 1)) / 500;	// round correctly
	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x80 | ((div & 0x18000) >> 10) | 2;
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

	if (i2c_transfer(&budget_ci->budget.i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct stv0299_config philips_su1278_tt_config = {

	.demod_address = 0x68,
	.inittab = philips_su1278_tt_inittab,
	.mclk = 64000000UL,
	.invert = 0,
	.enhanced_tuning = 1,
	.skip_reinit = 1,
	.lock_output = STV0229_LOCKOUTPUT_1,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 50,
	.set_symbol_rate = philips_su1278_tt_set_symbol_rate,
	.pll_set = philips_su1278_tt_pll_set,
};



static int philips_tdm1316l_pll_init(struct dvb_frontend *fe)
{
	struct budget_ci *budget_ci = (struct budget_ci *) fe->dvb->priv;
	static u8 td1316_init[] = { 0x0b, 0xf5, 0x85, 0xab };
	static u8 disable_mc44BC374c[] = { 0x1d, 0x74, 0xa0, 0x68 };
	struct i2c_msg tuner_msg = {.addr = 0x63,.flags = 0,.buf = td1316_init,.len =
			sizeof(td1316_init) };

	// setup PLL configuration
	if (i2c_transfer(&budget_ci->budget.i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;
	msleep(1);

	// disable the mc44BC374c (do not check for errors)
	tuner_msg.addr = 0x65;
	tuner_msg.buf = disable_mc44BC374c;
	tuner_msg.len = sizeof(disable_mc44BC374c);
	if (i2c_transfer(&budget_ci->budget.i2c_adap, &tuner_msg, 1) != 1) {
		i2c_transfer(&budget_ci->budget.i2c_adap, &tuner_msg, 1);
	}

	return 0;
}

static int philips_tdm1316l_pll_set(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct budget_ci *budget_ci = (struct budget_ci *) fe->dvb->priv;
	u8 tuner_buf[4];
	struct i2c_msg tuner_msg = {.addr = 0x63,.flags = 0,.buf = tuner_buf,.len = sizeof(tuner_buf) };
	int tuner_frequency = 0;
	u8 band, cp, filter;

	// determine charge pump
	tuner_frequency = params->frequency + 36130000;
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
	else if (params->frequency < 159000000)
		band = 1;
	else if (params->frequency < 444000000)
		band = 2;
	else if (params->frequency < 861000000)
		band = 4;
	else
		return -EINVAL;

	// setup PLL filter and TDA9889
	switch (params->u.ofdm.bandwidth) {
	case BANDWIDTH_6_MHZ:
		tda1004x_write_byte(fe, 0x0C, 0x14);
		filter = 0;
		break;

	case BANDWIDTH_7_MHZ:
		tda1004x_write_byte(fe, 0x0C, 0x80);
		filter = 0;
		break;

	case BANDWIDTH_8_MHZ:
		tda1004x_write_byte(fe, 0x0C, 0x14);
		filter = 1;
		break;

	default:
		return -EINVAL;
	}

	// calculate divisor
	// ((36130000+((1000000/6)/2)) + Finput)/(1000000/6)
	tuner_frequency = (((params->frequency / 1000) * 6) + 217280) / 1000;

	// setup tuner buffer
	tuner_buf[0] = tuner_frequency >> 8;
	tuner_buf[1] = tuner_frequency & 0xff;
	tuner_buf[2] = 0xca;
	tuner_buf[3] = (cp << 5) | (filter << 3) | band;

	if (i2c_transfer(&budget_ci->budget.i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;

	msleep(1);
	return 0;
}

static int philips_tdm1316l_request_firmware(struct dvb_frontend *fe,
					     const struct firmware **fw, char *name)
{
	struct budget_ci *budget_ci = (struct budget_ci *) fe->dvb->priv;

	return request_firmware(fw, name, &budget_ci->budget.dev->pci->dev);
}

static struct tda1004x_config philips_tdm1316l_config = {

	.demod_address = 0x8,
	.invert = 0,
	.invert_oclk = 0,
	.pll_init = philips_tdm1316l_pll_init,
	.pll_set = philips_tdm1316l_pll_set,
	.request_firmware = philips_tdm1316l_request_firmware,
};



static void frontend_init(struct budget_ci *budget_ci)
{
	switch (budget_ci->budget.dev->pci->subsystem_device) {
	case 0x100c:		// Hauppauge/TT Nova-CI budget (stv0299/ALPS BSRU6(tsa5059))
		budget_ci->budget.dvb_frontend =
			stv0299_attach(&alps_bsru6_config, &budget_ci->budget.i2c_adap);
		if (budget_ci->budget.dvb_frontend) {
			break;
		}
		break;

	case 0x100f:		// Hauppauge/TT Nova-CI budget (stv0299b/Philips su1278(tsa5059))
		budget_ci->budget.dvb_frontend =
			stv0299_attach(&philips_su1278_tt_config, &budget_ci->budget.i2c_adap);
		if (budget_ci->budget.dvb_frontend) {
			break;
		}
		break;

	case 0x1011:		// Hauppauge/TT Nova-T budget (tda10045/Philips tdm1316l(tda6651tt) + TDA9889)
		budget_ci->budget.dvb_frontend =
			tda10045_attach(&philips_tdm1316l_config, &budget_ci->budget.i2c_adap);
		if (budget_ci->budget.dvb_frontend) {
			break;
		}
		break;
	}

	if (budget_ci->budget.dvb_frontend == NULL) {
		printk("budget-ci: A frontend driver was not found for device %04x/%04x subsystem %04x/%04x\n",
		       budget_ci->budget.dev->pci->vendor,
		       budget_ci->budget.dev->pci->device,
		       budget_ci->budget.dev->pci->subsystem_vendor,
		       budget_ci->budget.dev->pci->subsystem_device);
	} else {
		if (dvb_register_frontend
		    (budget_ci->budget.dvb_adapter, budget_ci->budget.dvb_frontend)) {
			printk("budget-ci: Frontend registration failed!\n");
			if (budget_ci->budget.dvb_frontend->ops->release)
				budget_ci->budget.dvb_frontend->ops->release(budget_ci->budget.dvb_frontend);
			budget_ci->budget.dvb_frontend = NULL;
		}
	}
}

static int budget_ci_attach(struct saa7146_dev *dev, struct saa7146_pci_extension_data *info)
{
	struct budget_ci *budget_ci;
	int err;

	if (!(budget_ci = kmalloc(sizeof(struct budget_ci), GFP_KERNEL)))
		return -ENOMEM;

	dprintk(2, "budget_ci: %p\n", budget_ci);

	budget_ci->budget.ci_present = 0;

	dev->ext_priv = budget_ci;

	if ((err = ttpci_budget_init(&budget_ci->budget, dev, info, THIS_MODULE))) {
		kfree(budget_ci);
		return err;
	}

	tasklet_init(&budget_ci->msp430_irq_tasklet, msp430_ir_interrupt,
		     (unsigned long) budget_ci);

	msp430_ir_init(budget_ci);

	ciintf_init(budget_ci);

	budget_ci->budget.dvb_adapter->priv = budget_ci;
	frontend_init(budget_ci);

	return 0;
}

static int budget_ci_detach(struct saa7146_dev *dev)
{
	struct budget_ci *budget_ci = (struct budget_ci *) dev->ext_priv;
	struct saa7146_dev *saa = budget_ci->budget.dev;
	int err;

	if (budget_ci->budget.ci_present)
		ciintf_deinit(budget_ci);
	if (budget_ci->budget.dvb_frontend)
		dvb_unregister_frontend(budget_ci->budget.dvb_frontend);
	err = ttpci_budget_deinit(&budget_ci->budget);

	tasklet_kill(&budget_ci->msp430_irq_tasklet);

	msp430_ir_deinit(budget_ci);

	// disable frontend and CI interface
	saa7146_setgpio(saa, 2, SAA7146_GPIO_INPUT);

	kfree(budget_ci);

	return err;
}

static struct saa7146_extension budget_extension;

MAKE_BUDGET_INFO(ttbci, "TT-Budget/WinTV-NOVA-CI PCI", BUDGET_TT_HW_DISEQC);
MAKE_BUDGET_INFO(ttbt2, "TT-Budget/WinTV-NOVA-T	 PCI", BUDGET_TT);

static struct pci_device_id pci_tbl[] = {
	MAKE_EXTENSION_PCI(ttbci, 0x13c2, 0x100c),
	MAKE_EXTENSION_PCI(ttbci, 0x13c2, 0x100f),
	MAKE_EXTENSION_PCI(ttbt2, 0x13c2, 0x1011),
	{
	 .vendor = 0,
	 }
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_extension budget_extension = {
	.name = "budget_ci dvb\0",
	.flags = 0,

	.module = THIS_MODULE,
	.pci_tbl = &pci_tbl[0],
	.attach = budget_ci_attach,
	.detach = budget_ci_detach,

	.irq_mask = MASK_03 | MASK_06 | MASK_10,
	.irq_func = budget_ci_irq,
};

static int __init budget_ci_init(void)
{
	return saa7146_register_extension(&budget_extension);
}

static void __exit budget_ci_exit(void)
{
	saa7146_unregister_extension(&budget_extension);
}

module_init(budget_ci_init);
module_exit(budget_ci_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Hunold, Jack Thomasson, Andrew de Quincey, others");
MODULE_DESCRIPTION("driver for the SAA7146 based so-called "
		   "budget PCI DVB cards w/ CI-module produced by "
		   "Siemens, Technotrend, Hauppauge");
