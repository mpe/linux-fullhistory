/*
 * budget.c: driver for the SAA7146 based Budget DVB cards 
 *
 * Compiled from various sources by Michael Hunold <michael@mihu.de> 
 *
 * Copyright (C) 2002 Ralph Metzler <rjkm@metzlerbros.de>
 *
 * Copyright (C) 1999-2002 Ralph  Metzler 
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * 26feb2004 Support for FS Activy Card (Grundig tuner) by
 *           Michael Dreher <michael@5dot1.de>,
 *           Oliver Endriss <o.endriss@gmx.de> and
 *           Andreas 'randy' Weinberger
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
#include "ves1x93.h"
#include "ves1820.h"
#include "l64781.h"
#include "tda8083.h"

static void Set22K (struct budget *budget, int state)
{
	struct saa7146_dev *dev=budget->dev;
	dprintk(2, "budget: %p\n", budget);
	saa7146_setgpio(dev, 3, (state ? SAA7146_GPIO_OUTHI : SAA7146_GPIO_OUTLO));
}


/* Diseqc functions only for TT Budget card */
/* taken from the Skyvision DVB driver by
   Ralph Metzler <rjkm@metzlerbros.de> */

static void DiseqcSendBit (struct budget *budget, int data)
{
	struct saa7146_dev *dev=budget->dev;
	dprintk(2, "budget: %p\n", budget);

	saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTHI);
	udelay(data ? 500 : 1000);
	saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
	udelay(data ? 1000 : 500);
}


static void DiseqcSendByte (struct budget *budget, int data)
{
	int i, par=1, d;

	dprintk(2, "budget: %p\n", budget);

	for (i=7; i>=0; i--) {
		d = (data>>i)&1;
		par ^= d;
		DiseqcSendBit(budget, d);
	}

	DiseqcSendBit(budget, par);
}


static int SendDiSEqCMsg (struct budget *budget, int len, u8 *msg, unsigned long burst)
{
	struct saa7146_dev *dev=budget->dev;
	int i;

	dprintk(2, "budget: %p\n", budget);

	saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
	mdelay(16);

	for (i=0; i<len; i++)
		DiseqcSendByte(budget, msg[i]);

	mdelay(16);

	if (burst!=-1) {
		if (burst)
			DiseqcSendByte(budget, 0xff);
		else {
			saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTHI);
			udelay(12500);
			saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
		}
		msleep(20);
	}

	return 0;
}

/*
 *   Routines for the Fujitsu Siemens Activy budget card
 *   22 kHz tone and DiSEqC are handled by the frontend.
 *   Voltage must be set here.
 */
static int SetVoltage_Activy (struct budget *budget, fe_sec_voltage_t voltage)
{
	struct saa7146_dev *dev=budget->dev;

       dprintk(2, "budget: %p\n", budget);

	switch (voltage) {
		case SEC_VOLTAGE_13:
			saa7146_setgpio(dev, 2, SAA7146_GPIO_OUTLO);
			break;
		case SEC_VOLTAGE_18:
			saa7146_setgpio(dev, 2, SAA7146_GPIO_OUTHI);
			break;
		default:
			return -EINVAL;
	}

	return 0;
}

static int siemens_budget_set_voltage(struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;

	return SetVoltage_Activy (budget, voltage);
}

static int budget_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;

	switch (tone) {
               case SEC_TONE_ON:
                       Set22K (budget, 1);
                       break;
               case SEC_TONE_OFF:
                       Set22K (budget, 0);
                       break;

               default:
                       return -EINVAL;
	}

	return 0;
}

static int budget_diseqc_send_master_cmd(struct dvb_frontend* fe, struct dvb_diseqc_master_cmd* cmd)
       {
	struct budget* budget = (struct budget*) fe->dvb->priv;

               SendDiSEqCMsg (budget, cmd->msg_len, cmd->msg, 0);

	return 0;
       }

static int budget_diseqc_send_burst(struct dvb_frontend* fe, fe_sec_mini_cmd_t minicmd)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;

	SendDiSEqCMsg (budget, 0, NULL, minicmd);

       return 0;
}

static int alps_bsrv2_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;
	u8 pwr = 0;
	u8 buf[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = buf, .len = sizeof(buf) };
	u32 div = (params->frequency + 479500) / 125;

	if (params->frequency > 2000000) pwr = 3;
	else if (params->frequency > 1800000) pwr = 2;
	else if (params->frequency > 1600000) pwr = 1;
	else if (params->frequency > 1200000) pwr = 0;
	else if (params->frequency >= 1100000) pwr = 1;
	else pwr = 2;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = ((div & 0x18000) >> 10) | 0x95;
	buf[3] = (pwr << 6) | 0x30;

        // NOTE: since we're using a prescaler of 2, we set the
	// divisor frequency to 62.5kHz and divide by 125 above

	if (i2c_transfer (&budget->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

static struct ves1x93_config alps_bsrv2_config =
{
	.demod_address = 0x08,
	.xin = 90100000UL,
	.invert_pwm = 0,
	.pll_set = alps_bsrv2_pll_set,
};

static u8 alps_bsru6_inittab[] = {
	0x01, 0x15,
	0x02, 0x00,
	0x03, 0x00,
        0x04, 0x7d,   /* F22FR = 0x7d, F22 = f_VCO / 128 / 0x7d = 22 kHz */
	0x05, 0x35,   /* I2CT = 0, SCLT = 1, SDAT = 1 */
	0x06, 0x40,   /* DAC not used, set to high impendance mode */
	0x07, 0x00,   /* DAC LSB */
	0x08, 0x40,   /* DiSEqC off, LNB power on OP2/LOCK pin on */
	0x09, 0x00,   /* FIFO */
	0x0c, 0x51,   /* OP1 ctl = Normal, OP1 val = 1 (LNB Power ON) */
	0x0d, 0x82,   /* DC offset compensation = ON, beta_agc1 = 2 */
	0x0e, 0x23,   /* alpha_tmg = 2, beta_tmg = 3 */
	0x10, 0x3f,   // AGC2  0x3d
	0x11, 0x84,
	0x12, 0xb5,   // Lock detect: -64  Carrier freq detect:on
	0x15, 0xc9,   // lock detector threshold
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
	0x28, 0x00,  // out imp: normal  out type: parallel FEC mode:0
	0x29, 0x1e,  // 1/2 threshold
	0x2a, 0x14,  // 2/3 threshold
	0x2b, 0x0f,  // 3/4 threshold
	0x2c, 0x09,  // 5/6 threshold
	0x2d, 0x05,  // 7/8 threshold
	0x2e, 0x01,
	0x31, 0x1f,  // test all FECs
	0x32, 0x19,  // viterbi and synchro search
	0x33, 0xfc,  // rs control
	0x34, 0x93,  // error control
	0x0f, 0x52,
	0xff, 0xff
};

static int alps_bsru6_set_symbol_rate(struct dvb_frontend* fe, u32 srate, u32 ratio)
{
	u8 aclk = 0;
	u8 bclk = 0;

	if (srate < 1500000) { aclk = 0xb7; bclk = 0x47; }
	else if (srate < 3000000) { aclk = 0xb7; bclk = 0x4b; }
	else if (srate < 7000000) { aclk = 0xb7; bclk = 0x4f; }
	else if (srate < 14000000) { aclk = 0xb7; bclk = 0x53; }
	else if (srate < 30000000) { aclk = 0xb6; bclk = 0x53; }
	else if (srate < 45000000) { aclk = 0xb4; bclk = 0x51; }

	stv0299_writereg (fe, 0x13, aclk);
	stv0299_writereg (fe, 0x14, bclk);
	stv0299_writereg (fe, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg (fe, 0x20, (ratio >>  8) & 0xff);
	stv0299_writereg (fe, 0x21, (ratio      ) & 0xf0);

	return 0;
	}

static int alps_bsru6_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;
	u8 data[4];
	u32 div;
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	if ((params->frequency < 950000) || (params->frequency > 2150000)) return -EINVAL;

	div = (params->frequency + (125 - 1)) / 125; // round correctly
	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x80 | ((div & 0x18000) >> 10) | 4;
	data[3] = 0xC4;

	if (params->frequency > 1530000) data[3] = 0xc0;

	if (i2c_transfer (&budget->i2c_adap, &msg, 1) != 1) return -EIO;
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

static int alps_tdbe2_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x62, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (params->frequency + 35937500 + 31250) / 62500;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x85 | ((div >> 10) & 0x60);
	data[3] = (params->frequency < 174000000 ? 0x88 : params->frequency < 470000000 ? 0x84 : 0x81);

	if (i2c_transfer (&budget->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

static struct ves1820_config alps_tdbe2_config = {
	.demod_address = 0x09,
	.xin = 57840000UL,
	.invert = 1,
	.selagc = VES1820_SELAGC_SIGNAMPERR,
	.pll_set = alps_tdbe2_pll_set,
};

static int grundig_29504_401_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;
	u32 div;
	u8 cfg, cpump, band_select;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (36125000 + params->frequency) / 166666;

	cfg = 0x88;

	if (params->frequency < 175000000) cpump = 2;
	else if (params->frequency < 390000000) cpump = 1;
	else if (params->frequency < 470000000) cpump = 2;
	else if (params->frequency < 750000000) cpump = 1;
	else cpump = 3;

	if (params->frequency < 175000000) band_select = 0x0e;
	else if (params->frequency < 470000000) band_select = 0x05;
	else band_select = 0x03;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = ((div >> 10) & 0x60) | cfg;
	data[3] = (cpump << 6) | band_select;

	if (i2c_transfer (&budget->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
	}

static struct l64781_config grundig_29504_401_config = {
	.demod_address = 0x55,
	.pll_set = grundig_29504_401_pll_set,
};

static int grundig_29504_451_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct budget* budget = (struct budget*) fe->dvb->priv;
	u32 div;
	u8 data[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = data, .len = sizeof(data) };

	div = params->frequency / 125;
	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = 0x8e;
	data[3] = 0x00;

	if (i2c_transfer (&budget->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

static struct tda8083_config grundig_29504_451_config = {
	.demod_address = 0x68,
	.pll_set = grundig_29504_451_pll_set,
};

static u8 read_pwm(struct budget* budget)
{
	u8 b = 0xff;
	u8 pwm;
	struct i2c_msg msg[] = { { .addr = 0x50,.flags = 0,.buf = &b,.len = 1 },
				 { .addr = 0x50,.flags = I2C_M_RD,.buf = &pwm,.len = 1} };

        if ((i2c_transfer(&budget->i2c_adap, msg, 2) != 2) || (pwm == 0xff))
		pwm = 0x48;

	return pwm;
}

static void frontend_init(struct budget *budget)
{
	switch(budget->dev->pci->subsystem_device) {
	case 0x1003: // Hauppauge/TT Nova budget (stv0299/ALPS BSRU6(tsa5059) OR ves1893/ALPS BSRV2(sp5659))
	case 0x1013:
		// try the ALPS BSRV2 first of all
		budget->dvb_frontend = ves1x93_attach(&alps_bsrv2_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops->diseqc_send_master_cmd = budget_diseqc_send_master_cmd;
		        budget->dvb_frontend->ops->diseqc_send_burst = budget_diseqc_send_burst;
			budget->dvb_frontend->ops->set_tone = budget_set_tone;
			break;
		}

		// try the ALPS BSRU6 now
		budget->dvb_frontend = stv0299_attach(&alps_bsru6_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops->diseqc_send_master_cmd = budget_diseqc_send_master_cmd;
			budget->dvb_frontend->ops->diseqc_send_burst = budget_diseqc_send_burst;
			budget->dvb_frontend->ops->set_tone = budget_set_tone;
			break;
		}
		break;

   	case 0x1004: // Hauppauge/TT DVB-C budget (ves1820/ALPS TDBE2(sp5659))

		budget->dvb_frontend = ves1820_attach(&alps_tdbe2_config, &budget->i2c_adap, read_pwm(budget));
		if (budget->dvb_frontend) break;
		break;

   	case 0x1005: // Hauppauge/TT Nova-T budget (L64781/Grundig 29504-401(tsa5060))

		budget->dvb_frontend = l64781_attach(&grundig_29504_401_config, &budget->i2c_adap);
		if (budget->dvb_frontend) break;
		break;

	case 0x4f61: // Fujitsu Siemens Activy Budget-S PCI (tda8083/Grundig 29504-451(tsa5522))

		// grundig 29504-451
                budget->dvb_frontend = tda8083_attach(&grundig_29504_451_config, &budget->i2c_adap);
		if (budget->dvb_frontend) {
			budget->dvb_frontend->ops->set_voltage = siemens_budget_set_voltage;
			break;
		}
		break;
	}

	if (budget->dvb_frontend == NULL) {
		printk("budget: A frontend driver was not found for device %04x/%04x subsystem %04x/%04x\n",
		       budget->dev->pci->vendor,
		       budget->dev->pci->device,
		       budget->dev->pci->subsystem_vendor,
		       budget->dev->pci->subsystem_device);
	} else {
		if (dvb_register_frontend(budget->dvb_adapter, budget->dvb_frontend)) {
			printk("budget: Frontend registration failed!\n");
			if (budget->dvb_frontend->ops->release)
				budget->dvb_frontend->ops->release(budget->dvb_frontend);
			budget->dvb_frontend = NULL;
		}
	}
}

static int budget_attach (struct saa7146_dev* dev, struct saa7146_pci_extension_data *info)
{
	struct budget *budget = NULL;
	int err;

	budget = kmalloc(sizeof(struct budget), GFP_KERNEL);
	if( NULL == budget ) {
		return -ENOMEM;
	}

	dprintk(2, "dev:%p, info:%p, budget:%p\n", dev, info, budget);

	dev->ext_priv = budget;

	if ((err = ttpci_budget_init (budget, dev, info, THIS_MODULE))) {
		printk("==> failed\n");
		kfree (budget);
		return err;
	}

	budget->dvb_adapter->priv = budget;
	frontend_init(budget);

	return 0;
}


static int budget_detach (struct saa7146_dev* dev)
{
	struct budget *budget = (struct budget*) dev->ext_priv;
	int err;

	if (budget->dvb_frontend) dvb_unregister_frontend(budget->dvb_frontend);

	err = ttpci_budget_deinit (budget);

	kfree (budget);
	dev->ext_priv = NULL;

	return err;
}



static struct saa7146_extension budget_extension;

MAKE_BUDGET_INFO(ttbs,	"TT-Budget/WinTV-NOVA-S  PCI",	BUDGET_TT);
MAKE_BUDGET_INFO(ttbc,	"TT-Budget/WinTV-NOVA-C  PCI",	BUDGET_TT);
MAKE_BUDGET_INFO(ttbt,	"TT-Budget/WinTV-NOVA-T  PCI",	BUDGET_TT);
MAKE_BUDGET_INFO(satel,	"SATELCO Multimedia PCI",	BUDGET_TT_HW_DISEQC);
MAKE_BUDGET_INFO(fsacs, "Fujitsu Siemens Activy Budget-S PCI", BUDGET_FS_ACTIVY);

static struct pci_device_id pci_tbl[] = {
	MAKE_EXTENSION_PCI(ttbs,  0x13c2, 0x1003),
	MAKE_EXTENSION_PCI(ttbc,  0x13c2, 0x1004),
	MAKE_EXTENSION_PCI(ttbt,  0x13c2, 0x1005),
	MAKE_EXTENSION_PCI(satel, 0x13c2, 0x1013),
	MAKE_EXTENSION_PCI(fsacs, 0x1131, 0x4f61),
	{
		.vendor    = 0,
	}
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_extension budget_extension = {
	.name		= "budget dvb\0",
	.flags	 	= 0,
	
	.module		= THIS_MODULE,
	.pci_tbl	= pci_tbl,
	.attach		= budget_attach,
	.detach		= budget_detach,

	.irq_mask	= MASK_10,
	.irq_func	= ttpci_budget_irq10_handler,
};	


static int __init budget_init(void) 
{
	return saa7146_register_extension(&budget_extension);
}


static void __exit budget_exit(void)
{
	saa7146_unregister_extension(&budget_extension); 
}

module_init(budget_init);
module_exit(budget_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ralph Metzler, Marcus Metzler, Michael Hunold, others");
MODULE_DESCRIPTION("driver for the SAA7146 based so-called "
		   "budget PCI DVB cards by Siemens, Technotrend, Hauppauge");

