/*
 *  Driver for Trident 4DWave DX/NX & SiS SI7018 Audio PCI soundcard
 *
 *  Driver was originated by Trident <audio@tridentmicro.com>
 *  			     Fri Feb 19 15:55:28 MST 1999
 *
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
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/trident.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>, <audio@tridentmicro.com>");
MODULE_DESCRIPTION("Trident 4D-WaveDX/NX & SiS SI7018");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Trident,4DWave DX},"
		"{Trident,4DWave NX},"
		"{SiS,SI7018 PCI Audio},"
		"{Best Union,Miss Melody 4DWave PCI},"
		"{HIS,4DWave PCI},"
		"{Warpspeed,ONSpeed 4DWave PCI},"
		"{Aztech Systems,PCI 64-Q3D},"
		"{Addonics,SV 750},"
		"{CHIC,True Sound 4Dwave},"
		"{Shark,Predator4D-PCI},"
		"{Jaton,SonicWave 4D},"
		"{Hoontech,SoundTrack Digital 4DWave NX}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int pcm_channels[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 32};
static int wavetable_size[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 8192};

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for Trident 4DWave PCI soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for Trident 4DWave PCI soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable Trident 4DWave PCI soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(pcm_channels, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(pcm_channels, "Number of hardware channels assigned for PCM.");
MODULE_PARM_SYNTAX(pcm_channels, SNDRV_ENABLED ",default:32,allows:{{1,32}}");
MODULE_PARM(wavetable_size, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(wavetable_size, "Maximum memory size in kB for wavetable synth.");
MODULE_PARM_SYNTAX(wavetable_size, SNDRV_ENABLED ",default:8192,skill:advanced");

static struct pci_device_id snd_trident_ids[] = {
	{ 0x1023, 0x2000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },	/* Trident 4DWave DX PCI Audio */
	{ 0x1023, 0x2001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },	/* Trident 4DWave NX PCI Audio */
	{ 0x1039, 0x7018, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },	/* SiS SI7018 PCI Audio */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_trident_ids);

static int __devinit snd_trident_probe(struct pci_dev *pci,
				       const struct pci_device_id *pci_id)
{
	static int dev;
	snd_card_t *card;
	trident_t *trident;
	const char *str;
	int err, pcm_dev = 0;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	if ((err = snd_trident_create(card, pci,
				      pcm_channels[dev],
				      ((pci->vendor << 16) | pci->device) == TRIDENT_DEVICE_ID_SI7018 ? 1 : 2,
				      wavetable_size[dev],
				      &trident)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_trident_pcm(trident, pcm_dev++, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	switch (trident->device) {
	case TRIDENT_DEVICE_ID_DX:
	case TRIDENT_DEVICE_ID_NX:
		if ((err = snd_trident_foldback_pcm(trident, pcm_dev++, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
		break;
	}
	if (trident->device == TRIDENT_DEVICE_ID_NX || trident->device == TRIDENT_DEVICE_ID_SI7018) {
		if ((err = snd_trident_spdif_pcm(trident, pcm_dev++, NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
	if ((err = snd_mpu401_uart_new(card, 0, MPU401_HW_TRID4DWAVE,
				       trident->midi_port, 1,
				       trident->irq, 0, &trident->rmidi)) < 0) {
		snd_card_free(card);
		return err;
	}

#if defined(CONFIG_SND_SEQUENCER) || (defined(MODULE) && defined(CONFIG_SND_SEQUENCER_MODULE))
	if ((err = snd_trident_attach_synthesizer(trident)) < 0) {
		snd_card_free(card);
		return err;
	}
#endif

	snd_trident_gameport(trident);

	switch (trident->device) {
	case TRIDENT_DEVICE_ID_DX:
		str = "TRID4DWAVEDX";
		break;
	case TRIDENT_DEVICE_ID_NX:
		str = "TRID4DWAVENX";
		break;
	case TRIDENT_DEVICE_ID_SI7018:
		str = "SI7018";
		break;
	default:
		str = "Unknown";
	}
	strcpy(card->driver, str);
	if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
		strcpy(card->shortname, "SiS ");
	} else {
		strcpy(card->shortname, "Trident ");
	}
	strcat(card->shortname, card->driver);
	sprintf(card->longname, "%s PCI Audio at 0x%lx, irq %d",
		card->shortname, trident->port, trident->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, trident);
	dev++;
	return 0;
}

static void __devexit snd_trident_remove(struct pci_dev *pci)
{
	trident_t *trident = snd_magic_cast(trident_t, pci_get_drvdata(pci), return);
	if (trident)
		snd_card_free(trident->card);
	pci_set_drvdata(pci, NULL);
}

#ifdef CONFIG_PM
static int snd_card_trident_suspend(struct pci_dev *pci, u32 state)
{
	trident_t *chip = snd_magic_cast(trident_t, pci_get_drvdata(pci), return -ENXIO);
	snd_trident_suspend(chip);
	return 0;
}
static int snd_card_trident_resume(struct pci_dev *pci)
{
	trident_t *chip = snd_magic_cast(trident_t, pci_get_drvdata(pci), return -ENXIO);
	snd_trident_resume(chip);
	return 0;
}
#endif

static struct pci_driver driver = {
	.name = "Trident4DWaveAudio",
	.id_table = snd_trident_ids,
	.probe = snd_trident_probe,
	.remove = __devexit_p(snd_trident_remove),
#ifdef CONFIG_PM
	.suspend = snd_card_trident_suspend,
	.resume = snd_card_trident_resume,
#endif
};

static int __init alsa_card_trident_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		printk(KERN_ERR "Trident 4DWave PCI soundcard not found or device busy\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_trident_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_trident_init)
module_exit(alsa_card_trident_exit)

#ifndef MODULE

/* format is: snd-trident=enable,index,id,
			  pcm_channels,wavetable_size */

static int __init alsa_card_trident_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2 &&
	       get_option(&str,&pcm_channels[nr_dev]) == 2 &&
	       get_option(&str,&wavetable_size[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-trident=", alsa_card_trident_setup);

#endif /* ifndef MODULE */
