/*
 * Bt8xx based DVB adapter driver 
 *
 * Copyright (C) 2002,2003 Florian Schirmer <jolt@tuxbox.org>
 *
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"

#include "dvb-bt8xx.h"

#include "bt878.h"

static int debug;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

#define dprintk( args... ) \
	do { \
		if (debug) printk(KERN_DEBUG args); \
	} while (0)

static void dvb_bt8xx_task(unsigned long data)
{
	struct dvb_bt8xx_card *card = (struct dvb_bt8xx_card *)data;

	//printk("%d ", card->bt->finished_block);

	while (card->bt->last_block != card->bt->finished_block) {
		(card->bt->TS_Size ? dvb_dmx_swfilter_204 : dvb_dmx_swfilter)
			(&card->demux,
			 &card->bt->buf_cpu[card->bt->last_block *
					    card->bt->block_bytes],
			 card->bt->block_bytes);
		card->bt->last_block = (card->bt->last_block + 1) %
					card->bt->block_count;
	}
}

static int dvb_bt8xx_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct dvb_bt8xx_card *card = dvbdmx->priv;
	int rc;

	dprintk("dvb_bt8xx: start_feed\n");
	
	if (!dvbdmx->dmx.frontend)
		return -EINVAL;

	down(&card->lock);
	card->nfeeds++;
	rc = card->nfeeds;
	if (card->nfeeds == 1)
		bt878_start(card->bt, card->gpio_mode,
			    card->op_sync_orin, card->irq_err_ignore);
	up(&card->lock);
	return rc;
}

static int dvb_bt8xx_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct dvb_bt8xx_card *card = dvbdmx->priv;

	dprintk("dvb_bt8xx: stop_feed\n");
	
	if (!dvbdmx->dmx.frontend)
		return -EINVAL;
		
	down(&card->lock);
	card->nfeeds--;
	if (card->nfeeds == 0)
		bt878_stop(card->bt);
	up(&card->lock);

	return 0;
}

static int is_pci_slot_eq(struct pci_dev* adev, struct pci_dev* bdev)
{
	if ((adev->subsystem_vendor == bdev->subsystem_vendor) &&
		(adev->subsystem_device == bdev->subsystem_device) &&
		(adev->bus->number == bdev->bus->number) &&
		(PCI_SLOT(adev->devfn) == PCI_SLOT(bdev->devfn)))
		return 1;
	return 0;
}

static struct bt878 __init *dvb_bt8xx_878_match(unsigned int bttv_nr, struct pci_dev* bttv_pci_dev)
{
	unsigned int card_nr;
	
	/* Hmm, n squared. Hope n is small */
	for (card_nr = 0; card_nr < bt878_num; card_nr++) {
		if (is_pci_slot_eq(bt878[card_nr].dev, bttv_pci_dev))
			return &bt878[card_nr];
	}
	return NULL;
}


static int thomson_dtt7579_demod_init(struct dvb_frontend* fe)
{
	static u8 mt352_clock_config [] = { 0x89, 0x38, 0x38 };
	static u8 mt352_reset [] = { 0x50, 0x80 };
	static u8 mt352_adc_ctl_1_cfg [] = { 0x8E, 0x40 };
	static u8 mt352_agc_cfg [] = { 0x67, 0x28, 0x20 };
	static u8 mt352_gpp_ctl_cfg [] = { 0x75, 0x33 };
	static u8 mt352_capt_range_cfg[] = { 0x75, 0x32 };

	mt352_write(fe, mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(fe, mt352_reset, sizeof(mt352_reset));
	mt352_write(fe, mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));

	mt352_write(fe, mt352_agc_cfg, sizeof(mt352_agc_cfg));
        mt352_write(fe, mt352_gpp_ctl_cfg, sizeof(mt352_gpp_ctl_cfg));
	mt352_write(fe, mt352_capt_range_cfg, sizeof(mt352_capt_range_cfg));

	return 0;
}

static int thomson_dtt7579_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params, u8* pllbuf)
{
	u32 div;
	unsigned char bs = 0;
	unsigned char cp = 0;

	#define IF_FREQUENCYx6 217    /* 6 * 36.16666666667MHz */
	div = (((params->frequency + 83333) * 3) / 500000) + IF_FREQUENCYx6;

	if (params->frequency < 542000000) cp = 0xb4;
	else if (params->frequency < 771000000) cp = 0xbc;
	else cp = 0xf4;

        if (params->frequency == 0) bs = 0x03;
	else if (params->frequency < 443250000) bs = 0x02;
	else bs = 0x08;

	pllbuf[0] = 0xc0; // Note: non-linux standard PLL i2c address
	pllbuf[1] = div >> 8;
   	pllbuf[2] = div & 0xff;
   	pllbuf[3] = cp;
   	pllbuf[4] = bs;

	return 0;
}

static struct mt352_config thomson_dtt7579_config = {

	.demod_address = 0x0f,
	.demod_init = thomson_dtt7579_demod_init,
	.pll_set = thomson_dtt7579_pll_set,
};

static int cx24108_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
   u32 freq = params->frequency;
   
   int i, a, n, pump; 
   u32 band, pll;
   
   
   u32 osci[]={950000,1019000,1075000,1178000,1296000,1432000,
               1576000,1718000,1856000,2036000,2150000};
   u32 bandsel[]={0,0x00020000,0x00040000,0x00100800,0x00101000,
               0x00102000,0x00104000,0x00108000,0x00110000,
               0x00120000,0x00140000};
   
#define XTAL 1011100 /* Hz, really 1.0111 MHz and a /10 prescaler */
        printk("cx24108 debug: entering SetTunerFreq, freq=%d\n",freq);
        
        /* This is really the bit driving the tuner chip cx24108 */
        
        if(freq<950000) freq=950000; /* kHz */
        if(freq>2150000) freq=2150000; /* satellite IF is 950..2150MHz */
        
        /* decide which VCO to use for the input frequency */
        for(i=1;(i<sizeof(osci)/sizeof(osci[0]))&&(osci[i]<freq);i++);
        printk("cx24108 debug: select vco #%d (f=%d)\n",i,freq);
        band=bandsel[i];
        /* the gain values must be set by SetSymbolrate */
        /* compute the pll divider needed, from Conexant data sheet,
           resolved for (n*32+a), remember f(vco) is f(receive) *2 or *4,
           depending on the divider bit. It is set to /4 on the 2 lowest 
           bands  */
        n=((i<=2?2:1)*freq*10L)/(XTAL/100);
        a=n%32; n/=32; if(a==0) n--;
        pump=(freq<(osci[i-1]+osci[i])/2);
        pll=0xf8000000|
            ((pump?1:2)<<(14+11))|
            ((n&0x1ff)<<(5+11))|
            ((a&0x1f)<<11);
        /* everything is shifted left 11 bits to left-align the bits in the
           32bit word. Output to the tuner goes MSB-aligned, after all */
        printk("cx24108 debug: pump=%d, n=%d, a=%d\n",pump,n,a);
        cx24110_pll_write(fe,band);
        /* set vga and vca to their widest-band settings, as a precaution.
           SetSymbolrate might not be called to set this up */
        cx24110_pll_write(fe,0x500c0000);
        cx24110_pll_write(fe,0x83f1f800);
        cx24110_pll_write(fe,pll);
/*        writereg(client,0x56,0x7f);*/

	return 0;
}

static int pinnsat_pll_init(struct dvb_frontend* fe)
{
   return 0;
}


static struct cx24110_config pctvsat_config = {

	.demod_address = 0x55,
	.pll_init = pinnsat_pll_init,
	.pll_set = cx24108_pll_set,
};


static int microtune_mt7202dtf_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct dvb_bt8xx_card *card = (struct dvb_bt8xx_card *) fe->dvb->priv;
	u8 cfg, cpump, band_select;
	u8 data[4];
	u32 div;
	struct i2c_msg msg = { .addr = 0x60, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (36000000 + params->frequency + 83333) / 166666;
	cfg = 0x88;

	if (params->frequency < 175000000) cpump = 2;
	else if (params->frequency < 390000000) cpump = 1;
	else if (params->frequency < 470000000) cpump = 2;
	else if (params->frequency < 750000000) cpump = 2;
	else cpump = 3;

	if (params->frequency < 175000000) band_select = 0x0e;
	else if (params->frequency < 470000000) band_select = 0x05;
	else band_select = 0x03;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = ((div >> 10) & 0x60) | cfg;
	data[3] = cpump | band_select;

	i2c_transfer(card->i2c_adapter, &msg, 1);
	return (div * 166666 - 36000000);
}

static int microtune_mt7202dtf_request_firmware(struct dvb_frontend* fe, const struct firmware **fw, char* name)
{
	struct dvb_bt8xx_card* bt = (struct dvb_bt8xx_card*) fe->dvb->priv;

	return request_firmware(fw, name, &bt->bt->dev->dev);
}

static struct sp887x_config microtune_mt7202dtf_config = {

	.demod_address = 0x70,
	.pll_set = microtune_mt7202dtf_pll_set,
	.request_firmware = microtune_mt7202dtf_request_firmware,
};



static int advbt771_samsung_tdtc9251dh0_demod_init(struct dvb_frontend* fe)
{
	static u8 mt352_clock_config [] = { 0x89, 0x38, 0x2d };
	static u8 mt352_reset [] = { 0x50, 0x80 };
	static u8 mt352_adc_ctl_1_cfg [] = { 0x8E, 0x40 };
	static u8 mt352_agc_cfg [] = { 0x67, 0x10, 0x23, 0x00, 0xFF, 0xFF,
	                               0x00, 0xFF, 0x00, 0x40, 0x40 };
	static u8 mt352_av771_extra[] = { 0xB5, 0x7A };
	static u8 mt352_capt_range_cfg[] = { 0x75, 0x32 };


	mt352_write(fe, mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(fe, mt352_reset, sizeof(mt352_reset));
	mt352_write(fe, mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));

	mt352_write(fe, mt352_agc_cfg,sizeof(mt352_agc_cfg));
	udelay(2000);
	mt352_write(fe, mt352_av771_extra,sizeof(mt352_av771_extra));
	mt352_write(fe, mt352_capt_range_cfg, sizeof(mt352_capt_range_cfg));

	return 0;
}

static int advbt771_samsung_tdtc9251dh0_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params, u8* pllbuf)
{
	u32 div;
	unsigned char bs = 0;
	unsigned char cp = 0;

	#define IF_FREQUENCYx6 217    /* 6 * 36.16666666667MHz */
	div = (((params->frequency + 83333) * 3) / 500000) + IF_FREQUENCYx6;

	if (params->frequency < 150000000) cp = 0xB4;
	else if (params->frequency < 173000000) cp = 0xBC;
	else if (params->frequency < 250000000) cp = 0xB4;
	else if (params->frequency < 400000000) cp = 0xBC;
	else if (params->frequency < 420000000) cp = 0xF4;
	else if (params->frequency < 470000000) cp = 0xFC;
	else if (params->frequency < 600000000) cp = 0xBC;
	else if (params->frequency < 730000000) cp = 0xF4;
	else cp = 0xFC;

	if (params->frequency < 150000000) bs = 0x01;
	else if (params->frequency < 173000000) bs = 0x01;
	else if (params->frequency < 250000000) bs = 0x02;
	else if (params->frequency < 400000000) bs = 0x02;
	else if (params->frequency < 420000000) bs = 0x02;
	else if (params->frequency < 470000000) bs = 0x02;
	else if (params->frequency < 600000000) bs = 0x08;
	else if (params->frequency < 730000000) bs = 0x08;
	else bs = 0x08;

	pllbuf[0] = 0xc2; // Note: non-linux standard PLL i2c address
	pllbuf[1] = div >> 8;
   	pllbuf[2] = div & 0xff;
   	pllbuf[3] = cp;
   	pllbuf[4] = bs;

	return 0;
}

static struct mt352_config advbt771_samsung_tdtc9251dh0_config = {

	.demod_address = 0x0f,
	.demod_init = advbt771_samsung_tdtc9251dh0_demod_init,
	.pll_set = advbt771_samsung_tdtc9251dh0_pll_set,
};


static struct dst_config dst_config = {

	.demod_address = 0x55,
};


static int vp3021_alps_tded4_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct dvb_bt8xx_card *card = (struct dvb_bt8xx_card *) fe->dvb->priv;
	u8 buf[4];
	u32 div;
	struct i2c_msg msg = { .addr = 0x60, .flags = 0, .buf = buf, .len = sizeof(buf) };

	div = (params->frequency + 36166667) / 166667;

	buf[0] = (div >> 8) & 0x7F;
	buf[1] = div & 0xFF;
	buf[2] = 0x85;
	if ((params->frequency >= 47000000) && (params->frequency < 153000000))
		buf[3] = 0x01;
	else if ((params->frequency >= 153000000) && (params->frequency < 430000000))
		buf[3] = 0x02;
	else if ((params->frequency >= 430000000) && (params->frequency < 824000000))
		buf[3] = 0x0C;
	else if ((params->frequency >= 824000000) && (params->frequency < 863000000))
		buf[3] = 0x8C;
	else
		return -EINVAL;

	i2c_transfer(card->i2c_adapter, &msg, 1);
	return 0;
}

static struct nxt6000_config vp3021_alps_tded4_config = {

	.demod_address = 0x0a,
	.clock_inversion = 1,
	.pll_set = vp3021_alps_tded4_pll_set,
};


static void frontend_init(struct dvb_bt8xx_card *card, u32 type)
{
	switch(type) {
#ifdef BTTV_DVICO_DVBT_LITE
	case BTTV_DVICO_DVBT_LITE:
		card->fe = mt352_attach(&thomson_dtt7579_config, card->i2c_adapter);
		if (card->fe != NULL) {
			card->fe->ops->info.frequency_min = 174000000;
			card->fe->ops->info.frequency_max = 862000000;
			break;
		}
		break;
#endif

#ifdef BTTV_TWINHAN_VP3021
	case BTTV_TWINHAN_VP3021:
#else
	case BTTV_NEBULA_DIGITV:
#endif
		card->fe = nxt6000_attach(&vp3021_alps_tded4_config, card->i2c_adapter);
		if (card->fe != NULL) {
			break;
		}
		break;

	case BTTV_AVDVBT_761:
		card->fe = sp887x_attach(&microtune_mt7202dtf_config, card->i2c_adapter);
		if (card->fe != NULL) {
			break;
		}
		break;

	case BTTV_AVDVBT_771:
		card->fe = mt352_attach(&advbt771_samsung_tdtc9251dh0_config, card->i2c_adapter);
		if (card->fe != NULL) {
			card->fe->ops->info.frequency_min = 174000000;
			card->fe->ops->info.frequency_max = 862000000;
			break;
		}
		break;

	case BTTV_TWINHAN_DST:
		card->fe = dst_attach(&dst_config, card->i2c_adapter, card->bt);
		if (card->fe != NULL) {
			break;
		}
		break;
	
	case BTTV_PINNACLESAT:
		card->fe = cx24110_attach(&pctvsat_config, card->i2c_adapter);
		if (card->fe != NULL) {
			break;
		}
		break;
	}

	if (card->fe == NULL) {
		printk("dvb-bt8xx: A frontend driver was not found for device %04x/%04x subsystem %04x/%04x\n",
		       card->bt->dev->vendor,
		       card->bt->dev->device,
		       card->bt->dev->subsystem_vendor,
		       card->bt->dev->subsystem_device);
	} else {
		if (dvb_register_frontend(card->dvb_adapter, card->fe)) {
			printk("dvb-bt8xx: Frontend registration failed!\n");
			if (card->fe->ops->release)
				card->fe->ops->release(card->fe);
			card->fe = NULL;
		}
	}
}

static int __init dvb_bt8xx_load_card(struct dvb_bt8xx_card *card, u32 type)
{
	int result;

	if ((result = dvb_register_adapter(&card->dvb_adapter, card->card_name,
					   THIS_MODULE)) < 0) {
		printk("dvb_bt8xx: dvb_register_adapter failed (errno = %d)\n", result);
		return result;
		
	}
	card->dvb_adapter->priv = card;

	card->bt->adapter = card->i2c_adapter;

	memset(&card->demux, 0, sizeof(struct dvb_demux));

	card->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING | DMX_MEMORY_BASED_FILTERING;

	card->demux.priv = card;
	card->demux.filternum = 256;
	card->demux.feednum = 256;
	card->demux.start_feed = dvb_bt8xx_start_feed;
	card->demux.stop_feed = dvb_bt8xx_stop_feed;
	card->demux.write_to_decoder = NULL;
	
	if ((result = dvb_dmx_init(&card->demux)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		dvb_unregister_adapter(card->dvb_adapter);
		return result;
	}

	card->dmxdev.filternum = 256;
	card->dmxdev.demux = &card->demux.dmx;
	card->dmxdev.capabilities = 0;
	
	if ((result = dvb_dmxdev_init(&card->dmxdev, card->dvb_adapter)) < 0) {
		printk("dvb_bt8xx: dvb_dmxdev_init failed (errno = %d)\n", result);

		dvb_dmx_release(&card->demux);
		dvb_unregister_adapter(card->dvb_adapter);
		return result;
	}

	card->fe_hw.source = DMX_FRONTEND_0;

	if ((result = card->demux.dmx.add_frontend(&card->demux.dmx, &card->fe_hw)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
		dvb_unregister_adapter(card->dvb_adapter);
		return result;
	}
	
	card->fe_mem.source = DMX_MEMORY_FE;

	if ((result = card->demux.dmx.add_frontend(&card->demux.dmx, &card->fe_mem)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
		dvb_unregister_adapter(card->dvb_adapter);
		return result;
	}

	if ((result = card->demux.dmx.connect_frontend(&card->demux.dmx, &card->fe_hw)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_mem);
		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
		dvb_unregister_adapter(card->dvb_adapter);
		return result;
	}

	dvb_net_init(card->dvb_adapter, &card->dvbnet, &card->demux.dmx);

	tasklet_init(&card->bt->tasklet, dvb_bt8xx_task, (unsigned long) card);
	
	frontend_init(card, type);

	return 0;
}

static int dvb_bt8xx_probe(struct device *dev)
{
	struct bttv_sub_device *sub = to_bttv_sub_dev(dev);
	struct dvb_bt8xx_card *card;
	struct pci_dev* bttv_pci_dev;
	int ret;

	if (!(card = kmalloc(sizeof(struct dvb_bt8xx_card), GFP_KERNEL)))
		return -ENOMEM;

	memset(card, 0, sizeof(*card));
	init_MUTEX(&card->lock);
	card->bttv_nr = sub->core->nr;
	strncpy(card->card_name, sub->core->name, sizeof(sub->core->name));
	card->i2c_adapter = &sub->core->i2c_adap;

	switch(sub->core->type)
	{
	case BTTV_PINNACLESAT:
		card->gpio_mode = 0x0400c060;
		/* should be: BT878_A_GAIN=0,BT878_A_PWRDN,BT878_DA_DPM,BT878_DA_SBR,
		              BT878_DA_IOM=1,BT878_DA_APP to enable serial highspeed mode. */
		card->op_sync_orin = 0;
		card->irq_err_ignore = 0;
		break;
		
#ifdef BTTV_DVICO_DVBT_LITE
	case BTTV_DVICO_DVBT_LITE:
#endif
		card->gpio_mode = 0x0400C060;
		card->op_sync_orin = 0;
		card->irq_err_ignore = 0;
				/* 26, 15, 14, 6, 5 
		 * A_PWRDN  DA_DPM DA_SBR DA_IOM_DA
				 * DA_APP(parallel) */
				break;

#ifdef BTTV_TWINHAN_VP3021
	case BTTV_TWINHAN_VP3021:
#else
	case BTTV_NEBULA_DIGITV:
#endif
	case BTTV_AVDVBT_761:
		card->gpio_mode = (1 << 26) | (1 << 14) | (1 << 5);
		card->op_sync_orin = 0;
		card->irq_err_ignore = 0;
				/* A_PWRDN DA_SBR DA_APP (high speed serial) */
				break;

	case BTTV_AVDVBT_771: //case 0x07711461:
		card->gpio_mode = 0x0400402B;
		card->op_sync_orin = BT878_RISC_SYNC_MASK;
		card->irq_err_ignore = 0;
		/* A_PWRDN DA_SBR  DA_APP[0] PKTP=10 RISC_ENABLE FIFO_ENABLE*/
				break;

	case BTTV_TWINHAN_DST:
		card->gpio_mode = 0x2204f2c;
		card->op_sync_orin = BT878_RISC_SYNC_MASK;
		card->irq_err_ignore = BT878_APABORT | BT878_ARIPERR |
				       BT878_APPERR | BT878_AFBUS;
				/* 25,21,14,11,10,9,8,3,2 then
				 * 0x33 = 5,4,1,0
				 * A_SEL=SML, DA_MLB, DA_SBR, 
				 * DA_SDR=f, fifo trigger = 32 DWORDS
				 * IOM = 0 == audio A/D
				 * DPM = 0 == digital audio mode
				 * == async data parallel port
				 * then 0x33 (13 is set by start_capture)
				 * DA_APP = async data parallel port, 
				 * ACAP_EN = 1,
				 * RISC+FIFO ENABLE */
				break;

			default:
		printk(KERN_WARNING "dvb_bt8xx: Unknown bttv card type: %d.\n",
				sub->core->type);
		kfree(card);
		return -ENODEV;
		}

	dprintk("dvb_bt8xx: identified card%d as %s\n", card->bttv_nr, card->card_name);

	if (!(bttv_pci_dev = bttv_get_pcidev(card->bttv_nr))) {
		printk("dvb_bt8xx: no pci device for card %d\n", card->bttv_nr);
		kfree(card);
		return -EFAULT;
	}

	if (!(card->bt = dvb_bt8xx_878_match(card->bttv_nr, bttv_pci_dev))) {
		printk("dvb_bt8xx: unable to determine DMA core of card %d,\n",
		       card->bttv_nr);
		printk("dvb_bt8xx: if you have the ALSA bt87x audio driver "
		       "installed, try removing it.\n");

		kfree(card);
		return -EFAULT;

}

	init_MUTEX(&card->bt->gpio_lock);
	card->bt->bttv_nr = sub->core->nr;

	if ( (ret = dvb_bt8xx_load_card(card, sub->core->type)) ) {
		kfree(card);
		return ret;
	}

	dev_set_drvdata(dev, card);
	return 0;
}

static int dvb_bt8xx_remove(struct device *dev)
{
	struct dvb_bt8xx_card *card = dev_get_drvdata(dev);
		
		dprintk("dvb_bt8xx: unloading card%d\n", card->bttv_nr);

		bt878_stop(card->bt);
		tasklet_kill(&card->bt->tasklet);
		dvb_net_release(&card->dvbnet);
		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_mem);
		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
	if (card->fe) dvb_unregister_frontend(card->fe);
		dvb_unregister_adapter(card->dvb_adapter);
		
		kfree(card);

	return 0;
	}

static struct bttv_sub_driver driver = {
	.drv = {
		.name		= "dvb-bt8xx",
		.probe		= dvb_bt8xx_probe,
		.remove		= dvb_bt8xx_remove,
		/* FIXME:
		 * .shutdown	= dvb_bt8xx_shutdown,
		 * .suspend	= dvb_bt8xx_suspend,
		 * .resume	= dvb_bt8xx_resume,
		 */
	},
};

static int __init dvb_bt8xx_init(void)
{
	return bttv_sub_register(&driver, "dvb");
}

static void __exit dvb_bt8xx_exit(void)
{
	bttv_sub_unregister(&driver);
}

module_init(dvb_bt8xx_init);
module_exit(dvb_bt8xx_exit);
MODULE_DESCRIPTION("Bt8xx based DVB adapter driver");
MODULE_AUTHOR("Florian Schirmer <jolt@tuxbox.org>");
MODULE_LICENSE("GPL");

