
/*
    card-ad1816a.c - driver for ADI SoundPort AD1816A based soundcards.
    Copyright (C) 2000 by Massimo Piccioni <dafastidio@libero.it>

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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
*/

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/wait.h>
#ifndef LINUX_ISAPNP_H
#include <linux/isapnp.h>
#define isapnp_card pci_bus
#define isapnp_dev pci_dev
#endif
#include <sound/core.h>
#define SNDRV_GET_ID
#include <sound/initval.h>
#include <sound/ad1816a.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>

#define chip_t ad1816a_t

#define PFX "ad1816a: "

EXPORT_NO_SYMBOLS;
MODULE_AUTHOR("Massimo Piccioni <dafastidio@libero.it>");
MODULE_DESCRIPTION("AD1816A, AD1815");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Highscreen,Sound-Boostar 16 3D},"
		"{Analog Devices,AD1815},"
		"{Analog Devices,AD1816A},"
		"{TerraTec,Base 64},"
		"{TerraTec,AudioSystem EWS64S},"
		"{Aztech/Newcom SC-16 3D},"
		"{Shark Predator ISA}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 1-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP;	/* Enable this card */
static long snd_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long snd_mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long snd_fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int snd_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* Pnp setup */
static int snd_mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* Pnp setup */
static int snd_dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* PnP setup */
static int snd_dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* PnP setup */

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for ad1816a based soundcard.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for ad1816a based soundcard.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable ad1816a based soundcard.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_port, "Port # for ad1816a driver.");
MODULE_PARM_SYNTAX(snd_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_mpu_port, "MPU-401 port # for ad1816a driver.");
MODULE_PARM_SYNTAX(snd_mpu_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_fm_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_fm_port, "FM port # for ad1816a driver.");
MODULE_PARM_SYNTAX(snd_fm_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_irq, "IRQ # for ad1816a driver.");
MODULE_PARM_SYNTAX(snd_irq, SNDRV_IRQ_DESC);
MODULE_PARM(snd_mpu_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_mpu_irq, "MPU-401 IRQ # for ad1816a driver.");
MODULE_PARM_SYNTAX(snd_mpu_irq, SNDRV_IRQ_DESC);
MODULE_PARM(snd_dma1, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma1, "1st DMA # for ad1816a driver.");
MODULE_PARM_SYNTAX(snd_dma1, SNDRV_DMA_DESC);
MODULE_PARM(snd_dma2, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_dma2, "2nd DMA # for ad1816a driver.");
MODULE_PARM_SYNTAX(snd_dma2, SNDRV_DMA_DESC);

struct snd_card_ad1816a {
#ifdef __ISAPNP__
	struct isapnp_dev *dev;
	struct isapnp_dev *devmpu;
#endif	/* __ISAPNP__ */
};

static snd_card_t *snd_ad1816a_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

#ifdef __ISAPNP__

static struct isapnp_card *snd_ad1816a_isapnp_cards[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;
static const struct isapnp_card_id *snd_ad1816a_isapnp_id[SNDRV_CARDS] __devinitdata = SNDRV_DEFAULT_PTR;

#define ISAPNP_AD1816A(_va, _vb, _vc, _device, _fa, _fb, _fc, _audio, _mpu401) \
	{ \
		ISAPNP_CARD_ID(_va, _vb, _vc, _device), \
		devs : { ISAPNP_DEVICE_ID(_fa, _fb, _fc, _audio), \
			 ISAPNP_DEVICE_ID(_fa, _fb, _fc, _mpu401), } \
	}

static struct isapnp_card_id snd_ad1816a_pnpids[] __devinitdata = {
	/* Highscreen Sound-Boostar 16 3D */
	ISAPNP_AD1816A('M','D','K',0x1605,'A','D','S',0x7180,0x7181),
	/* Highscreen Sound-Boostar 16 3D - added by Stefan Behnel */
	ISAPNP_AD1816A('L','W','C',0x1061,'A','D','S',0x7180,0x7181),
	/* Analog Devices AD1815 */
	ISAPNP_AD1816A('A','D','S',0x7150,'A','D','S',0x7150,0x7151),
	/* Analog Devices AD1816A - added by Kenneth Platz <kxp@atl.hp.com> */
	ISAPNP_AD1816A('A','D','S',0x7181,'A','D','S',0x7180,0x7181),
	/* Analog Devices AD1816A - Terratec Base 64 */
	ISAPNP_AD1816A('T','E','R',0x1411,'A','D','S',0x7180,0x7181),
	/* Analog Devices AD1816A - Terratec AudioSystem EWS64S */
	ISAPNP_AD1816A('T','E','R',0x1112,'A','D','S',0x7180,0x7181),
	/* Analog Devices AD1816A - Aztech/Newcom SC-16 3D */
	ISAPNP_AD1816A('A','Z','T',0x1022,'A','Z','T',0x1018,0x2002),
	/* Shark Predator ISA - added by Ken Arromdee */
	ISAPNP_AD1816A('S','M','M',0x7180,'A','D','S',0x7180,0x7181),
	{ ISAPNP_CARD_END, }
};

ISAPNP_CARD_TABLE(snd_ad1816a_pnpids);

#endif	/* __ISAPNP__ */

#define	DRIVER_NAME	"snd-card-ad1816a"


#ifdef __ISAPNP__
static int __init snd_card_ad1816a_isapnp(int dev,
					  struct snd_card_ad1816a *acard)
{
	const struct isapnp_card_id *id = snd_ad1816a_isapnp_id[dev];
	struct isapnp_card *card = snd_ad1816a_isapnp_cards[dev];
	struct isapnp_dev *pdev;

	acard->dev = isapnp_find_dev(card, id->devs[0].vendor, id->devs[0].function, NULL);
	if (acard->dev->active) {
		acard->dev = NULL;
		return -EBUSY;
	}
	acard->devmpu = isapnp_find_dev(card, id->devs[1].vendor, id->devs[1].function, NULL);
	if (acard->devmpu->active) {
		acard->dev = acard->devmpu = NULL;
		return -EBUSY;
	}

	pdev = acard->dev;
	if (pdev->prepare(pdev) < 0)
		return -EAGAIN;

	if (snd_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[2], snd_port[dev], 16);
	if (snd_fm_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[1], snd_fm_port[dev], 4);
	if (snd_dma1[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[0], snd_dma1[dev],
			1);
	if (snd_dma2[dev] != SNDRV_AUTO_DMA)
		isapnp_resource_change(&pdev->dma_resource[1], snd_dma2[dev],
			1);
	if (snd_irq[dev] != SNDRV_AUTO_IRQ)
		isapnp_resource_change(&pdev->irq_resource[0], snd_irq[dev], 1);

	if (pdev->activate(pdev) < 0) {
		printk(KERN_ERR PFX "AUDIO isapnp configure failure\n");
		return -EBUSY;
	}

	snd_port[dev] = pdev->resource[2].start;
	snd_fm_port[dev] = pdev->resource[1].start;
	snd_dma1[dev] = pdev->dma_resource[0].start;
	snd_dma2[dev] = pdev->dma_resource[1].start;
	snd_irq[dev] = pdev->irq_resource[0].start;

	pdev = acard->devmpu;
	if (pdev == NULL || pdev->prepare(pdev) < 0) {
		snd_mpu_port[dev] = -1;
		acard->devmpu = NULL;
		return 0;
	}

	if (snd_mpu_port[dev] != SNDRV_AUTO_PORT)
		isapnp_resource_change(&pdev->resource[0], snd_mpu_port[dev],
			2);
	if (snd_mpu_irq[dev] != SNDRV_AUTO_IRQ)
		isapnp_resource_change(&pdev->irq_resource[0], snd_mpu_irq[dev],
			1);

	if (pdev->activate(pdev) < 0) {
		/* not fatal error */
		printk(KERN_ERR PFX "MPU-401 isapnp configure failure\n");
		snd_mpu_port[dev] = -1;
		acard->devmpu = NULL;
	} else {
		snd_mpu_port[dev] = pdev->resource[0].start;
		snd_mpu_irq[dev] = pdev->irq_resource[0].start;
	}

	return 0;
}

static void snd_card_ad1816a_deactivate(struct snd_card_ad1816a *acard)
{
	if (acard->dev) {
		acard->dev->deactivate(acard->dev);
		acard->dev = NULL;
	}
	if (acard->devmpu) {
		acard->devmpu->deactivate(acard->devmpu);
		acard->devmpu = NULL;
	}
}
#endif	/* __ISAPNP__ */

static void snd_card_ad1816a_free(snd_card_t *card)
{
	struct snd_card_ad1816a *acard = (struct snd_card_ad1816a *)card->private_data;

	if (acard) {
#ifdef __ISAPNP__
		snd_card_ad1816a_deactivate(acard);
#endif	/* __ISAPNP__ */
	}
}

static int __init snd_card_ad1816a_probe(int dev)
{
	int error;
	snd_card_t *card;
	struct snd_card_ad1816a *acard;
	ad1816a_t *chip;
	opl3_t *opl3;

	if ((card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE,
				 sizeof(struct snd_card_ad1816a))) == NULL)
		return -ENOMEM;
	acard = (struct snd_card_ad1816a *)card->private_data;
	card->private_free = snd_card_ad1816a_free;

#ifdef __ISAPNP__
	if ((error = snd_card_ad1816a_isapnp(dev, acard))) {
		snd_card_free(card);
		return error;
	}
#else
	printk(KERN_ERR PFX "you have to enable ISA PnP support.\n");
	return -ENOSYS;
#endif	/* __ISAPNP__ */

	if ((error = snd_ad1816a_create(card, snd_port[dev],
					snd_irq[dev],
					snd_dma1[dev],
					snd_dma2[dev],
					&chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_ad1816a_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_ad1816a_mixer(chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	if (snd_mpu_port[dev] > 0) {
		if (snd_mpu401_uart_new(card, 0, MPU401_HW_MPU401,
					snd_mpu_port[dev], 0, snd_mpu_irq[dev], SA_INTERRUPT,
					NULL) < 0)
			printk(KERN_ERR PFX "no MPU-401 device at 0x%lx.\n", snd_mpu_port[dev]);
	}

	if (snd_fm_port[dev] > 0) {
		if (snd_opl3_create(card,
				    snd_fm_port[dev], snd_fm_port[dev] + 2,
				    OPL3_HW_AUTO, 0, &opl3) < 0) {
			printk(KERN_ERR PFX "no OPL device at 0x%lx-0x%lx.\n", snd_fm_port[dev], snd_fm_port[dev] + 2);
		} else {
			if ((error = snd_opl3_timer_new(opl3, 1, 2)) < 0) {
				snd_card_free(card);
				return error;
			}
			if ((error = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
				snd_card_free(card);
				return error;
			}
		}
	}

	strcpy(card->driver, "AD1816A");
	strcpy(card->shortname, "ADI SoundPort AD1816A");
	sprintf(card->longname, "%s soundcard, SS at 0x%lx, irq %d, dma %d&%d",
		card->shortname, chip->port, snd_irq[dev], snd_dma1[dev], snd_dma2[dev]);

	if ((error = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return error;
	}
	snd_ad1816a_cards[dev] = card;
	return 0;
}

#ifdef __ISAPNP__
static int __init snd_ad1816a_isapnp_detect(struct isapnp_card *card,
					    const struct isapnp_card_id *id)
{
	static int dev = 0;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!snd_enable[dev])
			continue;
		snd_ad1816a_isapnp_cards[dev] = card;
		snd_ad1816a_isapnp_id[dev] = id;
		res = snd_card_ad1816a_probe(dev);
		if (res < 0)
			return res;
		dev++;
		return 0;
	}
        return -ENODEV;
}
#endif

static int __init alsa_card_ad1816a_init(void)
{
	int cards = 0;

#ifdef __ISAPNP__
	cards += isapnp_probe_cards(snd_ad1816a_pnpids, snd_ad1816a_isapnp_detect);
#else
	printk(KERN_ERR PFX "you have to enable ISA PnP support.\n");
#endif
#ifdef MODULE
	if (!cards)
		printk(KERN_ERR "no AD1816A based soundcards found.\n");
#endif	/* MODULE */
	return cards ? 0 : -ENODEV;
}

static void __exit alsa_card_ad1816a_exit(void)
{
	int dev;

	for (dev = 0; dev < SNDRV_CARDS; dev++)
		snd_card_free(snd_ad1816a_cards[dev]);
}

module_init(alsa_card_ad1816a_init)
module_exit(alsa_card_ad1816a_exit)

#ifndef MODULE

/* format is: snd-ad1816a=snd_enable,snd_index,snd_id,snd_port,
			  snd_mpu_port,snd_fm_port,snd_irq,snd_mpu_irq,
			  snd_dma1,snd_dma2 */

static int __init alsa_card_ad1816a_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_mpu_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_fm_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_mpu_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma1[nr_dev]) == 2 &&
	       get_option(&str,&snd_dma2[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-ad1816a=", alsa_card_ad1816a_setup);

#endif /* ifndef MODULE */
