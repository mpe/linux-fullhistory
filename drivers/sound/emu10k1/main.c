
/*
 **********************************************************************
 *     main.c - Creative EMU10K1 audio driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *     November 2, 1999     Alan Cox        cleaned up stuff
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 *
 *      Supported devices:
 *      /dev/dsp:     Standard /dev/dsp device, OSS-compatible
 *      /dev/mixer:   Standard /dev/mixer device, OSS-compatible
 *      /dev/midi:    Raw MIDI UART device, mostly OSS-compatible
 *
 *      Revision history:
 *      0.1 beta Initial release
 *      0.2 Lowered initial mixer vol. Improved on stuttering wave playback. Added MIDI UART support.
 *      0.3 Fixed mixer routing bug, added APS, joystick support.
 *      0.4 Added rear-channel, SPDIF support.
 *	0.5 Source cleanup, SMP fixes, multiopen support, 64 bit arch fixes,
 *	    moved bh's to tasklets, moved to the new PCI driver initialization style.
 **********************************************************************
 */

/* These are only included once per module */
#include <linux/module.h>
#include <linux/init.h>

#include "hwaccess.h"
#include "efxmgr.h"
#include "cardwo.h"
#include "cardwi.h"
#include "cardmo.h"
#include "cardmi.h"
#include "recmgr.h"

#define DRIVER_VERSION "0.5"

/* FIXME: is this right? */
#define EMU10K1_DMA_MASK                0xffffffff	/* DMA buffer mask for pci_alloc_consist */

#ifndef PCI_VENDOR_ID_CREATIVE
#define PCI_VENDOR_ID_CREATIVE 0x1102
#endif

#ifndef PCI_DEVICE_ID_CREATIVE_EMU10K1
#define PCI_DEVICE_ID_CREATIVE_EMU10K1 0x0002
#endif

#define EMU10K1_EXTENT	0x20	/* 32 byte I/O space */

enum {
	EMU10K1 = 0,
};

static char *card_names[] __devinitdata = {
	"EMU10K1",
};

static struct pci_device_id emu10k1_pci_tbl[] __devinitdata = {
	{PCI_VENDOR_ID_CREATIVE, PCI_DEVICE_ID_CREATIVE_EMU10K1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, EMU10K1},
	{0,}
};

MODULE_DEVICE_TABLE(pci, emu10k1_pci_tbl);

/* Global var instantiation */

LIST_HEAD(emu10k1_devs);

extern struct file_operations emu10k1_audio_fops;
extern struct file_operations emu10k1_mixer_fops;
extern struct file_operations emu10k1_midi_fops;

extern void emu10k1_interrupt(int, void *, struct pt_regs *s);
extern int emu10k1_mixer_wrch(struct emu10k1_card *, unsigned int, int);

static int __devinit audio_init(struct emu10k1_card *card)
{
	if ((card->waveout = kmalloc(sizeof(struct emu10k1_waveout), GFP_KERNEL)) == NULL) {
		printk(KERN_WARNING "emu10k1: Unable to allocate emu10k1_waveout: out of memory\n");
		return CTSTATUS_ERROR;
	}
	memset(card->waveout, 0, sizeof(struct emu10k1_waveout));

	/* Assign default global volume, reverb, chorus */
	card->waveout->globalvol = 0xffffffff;
	card->waveout->left = 0xffff;
	card->waveout->right = 0xffff;
	card->waveout->mute = 0;
	card->waveout->globalreverb = 0xffffffff;
	card->waveout->globalchorus = 0xffffffff;

	if ((card->wavein = kmalloc(sizeof(struct emu10k1_wavein), GFP_KERNEL))
	    == NULL) {
		printk(KERN_WARNING "emu10k1: Unable to allocate emu10k1_wavein: out of memory\n");
		return CTSTATUS_ERROR;
	}
	memset(card->wavein, 0, sizeof(struct emu10k1_wavein));

	card->wavein->recsrc = WAVERECORD_AC97;

	return CTSTATUS_SUCCESS;
}

static void __devinit mixer_init(struct emu10k1_card *card)
{
	int count;
	struct initvol {
		int mixch;
		int vol;
	} initvol[] = {
		{
		SOUND_MIXER_VOLUME, 0x5050}, {
		SOUND_MIXER_OGAIN, 0x3232}, {
		SOUND_MIXER_SPEAKER, 0x3232}, {
		SOUND_MIXER_PHONEIN, 0x3232}, {
		SOUND_MIXER_MIC, 0x0000}, {
		SOUND_MIXER_LINE, 0x0000}, {
		SOUND_MIXER_CD, 0x3232}, {
		SOUND_MIXER_LINE1, 0x3232}, {
		SOUND_MIXER_LINE3, 0x3232}, {
		SOUND_MIXER_DIGITAL1, 0x6464}, {
		SOUND_MIXER_DIGITAL2, 0x6464}, {
		SOUND_MIXER_PCM, 0x6464}, {
		SOUND_MIXER_RECLEV, 0x3232}, {
		SOUND_MIXER_TREBLE, 0x3232}, {
		SOUND_MIXER_BASS, 0x3232}, {
	SOUND_MIXER_LINE2, 0x4b4b}};

	int initdig[] = { 0, 1, 2, 3, 6, 7, 16, 17, 18, 19, 22, 23, 64, 65, 66, 67, 70, 71,
		84, 85
	};

	/* Reset */
	sblive_writeac97(card, AC97_RESET, 0);

#if 0
	/* Check status word */
	{
		u16 reg;

		sblive_readac97(card, AC97_RESET, &reg);
		DPD(2, "RESET 0x%x\n", reg);
		sblive_readac97(card, AC97_MASTERTONE, &reg);
		DPD(2, "MASTER_TONE 0x%x\n", reg);
	}
#endif

	/* Set default recording source to mic in */
	sblive_writeac97(card, AC97_RECORDSELECT, 0);

	/* Set default AC97 "PCM" volume to acceptable max */
	//sblive_writeac97(card, AC97_PCMOUTVOLUME, 0);
	//sblive_writeac97(card, AC97_LINE2, 0);

	/* Set default volumes for all mixer channels */

	for (count = 0; count < sizeof(card->digmix) / sizeof(card->digmix[0]); count++) {
		card->digmix[count] = 0x80000000;
		sblive_writeptr(card, FXGPREGBASE + 0x10 + count, 0, 0);
	}

	for (count = 0; count < sizeof(initdig) / sizeof(initdig[0]); count++) {
		card->digmix[initdig[count]] = 0x7fffffff;
		sblive_writeptr(card, FXGPREGBASE + 0x10 + initdig[count], 0, 0x7fffffff);
	}

	for (count = 0; count < sizeof(initvol) / sizeof(initvol[0]); count++) {
		emu10k1_mixer_wrch(card, initvol[count].mixch, initvol[count].vol);
	}

	card->modcnt = 0;	// Should this be here or in open() ?

	return;
}

static int __devinit midi_init(struct emu10k1_card *card)
{
	if ((card->mpuout = kmalloc(sizeof(struct emu10k1_mpuout), GFP_KERNEL))
	    == NULL) {
		printk(KERN_WARNING "emu10k1: Unable to allocate emu10k1_mpuout: out of memory\n");
		return CTSTATUS_ERROR;
	}

	memset(card->mpuout, 0, sizeof(struct emu10k1_mpuout));

	card->mpuout->intr = 1;
	card->mpuout->status = FLAGS_AVAILABLE;
	card->mpuout->state = CARDMIDIOUT_STATE_DEFAULT;

	tasklet_init(&card->mpuout->tasklet, emu10k1_mpuout_bh, (unsigned long) card);

	spin_lock_init(&card->mpuout->lock);

	if ((card->mpuin = kmalloc(sizeof(struct emu10k1_mpuin), GFP_KERNEL)) == NULL) {
		kfree(card->mpuout);
		printk(KERN_WARNING "emu10k1: Unable to allocate emu10k1_mpuin: out of memory\n");
		return CTSTATUS_ERROR;
	}

	memset(card->mpuin, 0, sizeof(struct emu10k1_mpuin));

	card->mpuin->status = FLAGS_AVAILABLE;

	tasklet_init(&card->mpuin->tasklet, emu10k1_mpuin_bh, (unsigned long) card->mpuin);

	spin_lock_init(&card->mpuin->lock);

	/* Reset the MPU port */
	if (emu10k1_mpu_reset(card) != CTSTATUS_SUCCESS) {
		ERROR();
		return CTSTATUS_ERROR;
	}

	return CTSTATUS_SUCCESS;
}

static void __devinit voice_init(struct emu10k1_card *card)
{
	struct voice_manager *voicemgr = &card->voicemgr;
	struct emu_voice *voice;
	int i;

	voicemgr->card = card;
	voicemgr->lock = SPIN_LOCK_UNLOCKED;

	voice = voicemgr->voice;
	for (i = 0; i < NUM_G; i++) {
		voice->card = card;
		voice->usage = VOICEMGR_USAGE_FREE;
		voice->num = i;
		voice->linked_voice = NULL;
		voice++;
	}

	return;
}

static void __devinit timer_init(struct emu10k1_card *card)
{
	INIT_LIST_HEAD(&card->timers);
	card->timer_delay = TIMER_STOPPED;
	card->timer_lock = SPIN_LOCK_UNLOCKED;

	return;
}

static void __devinit addxmgr_init(struct emu10k1_card *card)
{
	u32 count;

	for (count = 0; count < MAXPAGES; count++)
		card->emupagetable[count] = 0;

	/* Mark first page as used */
	/* This page is reserved by the driver */
	card->emupagetable[0] = 0x8001;
	card->emupagetable[1] = MAXPAGES - RESERVED - 1;

	return;
}

static void __devinit fx_init(struct emu10k1_card *card)
{
	int i, j, k, l;
	u32 pc = 0;

	for (i = 0; i < 512; i++)
		OP(6, 0x40, 0x40, 0x40, 0x40);

	for (i = 0; i < 256; i++)
		sblive_writeptr(card, FXGPREGBASE + i, 0, 0);

	pc = 0;

	for (j = 0; j < 2; j++) {

		OP(4, 0x100, 0x40, j, 0x44);
		OP(4, 0x101, 0x40, j + 2, 0x44);

		for (i = 0; i < 6; i++) {
			k = i * 16 + j;
			OP(0, 0x102, 0x40, 0x110 + k, 0x100);
			OP(0, 0x102, 0x102, 0x112 + k, 0x101);
			OP(0, 0x102, 0x102, 0x114 + k, 0x10 + j);
			OP(0, 0x102, 0x102, 0x116 + k, 0x12 + j);
			OP(0, 0x102, 0x102, 0x118 + k, 0x14 + j);
			OP(0, 0x102, 0x102, 0x11a + k, 0x16 + j);
			OP(0, 0x102, 0x102, 0x11c + k, 0x18 + j);
			OP(0, 0x102, 0x102, 0x11e + k, 0x1a + j);

			k = 0x190 + i * 8 + j * 4;
			OP(0, 0x40, 0x40, 0x102, 0x170 + j);
			OP(7, k + 1, k, k + 1, 0x174 + j);
			OP(7, k, 0x102, k, 0x172 + j);
			OP(7, k + 3, k + 2, k + 3, 0x178 + j);
			OP(0, k + 2, 0x56, k + 2, 0x176 + j);
			OP(6, k + 2, k + 2, k + 2, 0x40);

			l = 0x1c0 + i * 8 + j * 4;
			OP(0, 0x40, 0x40, k + 2, 0x180 + j);
			OP(7, l + 1, l, l + 1, 0x184 + j);
			OP(7, l, k + 2, l, 0x182 + j);
			OP(7, l + 3, l + 2, l + 3, 0x188 + j);
			OP(0, l + 2, 0x56, l + 2, 0x186 + j);
			OP(4, l + 2, 0x40, l + 2, 0x46);

			OP(6, 0x20 + (i * 2) + j, l + 2, 0x40, 0x40);

		}
	}
	sblive_writeptr(card, DBG, 0, 0);

	return;
}

static int __devinit hw_init(struct emu10k1_card *card)
{
	int nCh;

#ifdef TANKMEM
	u32 size = 0;
#endif
	u32 sizeIdx = 0;
	u32 pagecount, tmp;

	/* Disable audio and lock cache */
	sblive_writefn0(card, HCFG, HCFG_LOCKSOUNDCACHE | HCFG_LOCKTANKCACHE | HCFG_MUTEBUTTONENABLE);

	/* Reset recording buffers */
	sblive_writeptr(card, MICBS, 0, ADCBS_BUFSIZE_NONE);
	sblive_writeptr(card, MICBA, 0, 0);
	sblive_writeptr(card, FXBS, 0, ADCBS_BUFSIZE_NONE);
	sblive_writeptr(card, FXBA, 0, 0);
	sblive_writeptr(card, ADCBS, 0, ADCBS_BUFSIZE_NONE);
	sblive_writeptr(card, ADCBA, 0, 0);

	/* Disable channel interrupt */
	sblive_writefn0(card, INTE, DISABLE);
	sblive_writeptr(card, CLIEL, 0, 0);
	sblive_writeptr(card, CLIEH, 0, 0);
	sblive_writeptr(card, SOLEL, 0, 0);
	sblive_writeptr(card, SOLEH, 0, 0);

	/* Init envelope engine */
	for (nCh = 0; nCh < NUM_G; nCh++) {
		sblive_writeptr(card, DCYSUSV, nCh, ENV_OFF);
		sblive_writeptr(card, IP, nCh, 0);
		sblive_writeptr(card, VTFT, nCh, 0xffff);
		sblive_writeptr(card, CVCF, nCh, 0xffff);
		sblive_writeptr(card, PTRX, nCh, 0);
		sblive_writeptr(card, CPF, nCh, 0);
		sblive_writeptr(card, CCR, nCh, 0);

		sblive_writeptr(card, PSST, nCh, 0);
		sblive_writeptr(card, DSL, nCh, 0x10);
		sblive_writeptr(card, CCCA, nCh, 0);
		sblive_writeptr(card, Z1, nCh, 0);
		sblive_writeptr(card, Z2, nCh, 0);
		sblive_writeptr(card, FXRT, nCh, 0xd01c0000);

		sblive_writeptr(card, ATKHLDM, nCh, 0);
		sblive_writeptr(card, DCYSUSM, nCh, 0);
		sblive_writeptr(card, IFATN, nCh, 0xffff);
		sblive_writeptr(card, PEFE, nCh, 0);
		sblive_writeptr(card, FMMOD, nCh, 0);
		sblive_writeptr(card, TREMFRQ, nCh, 24);	/* 1 Hz */
		sblive_writeptr(card, FM2FRQ2, nCh, 24);	/* 1 Hz */
		sblive_writeptr(card, TEMPENV, nCh, 0);

		/*** These are last so OFF prevents writing ***/
		sblive_writeptr(card, LFOVAL2, nCh, 0);
		sblive_writeptr(card, LFOVAL1, nCh, 0);
		sblive_writeptr(card, ATKHLDV, nCh, 0);
		sblive_writeptr(card, ENVVOL, nCh, 0);
		sblive_writeptr(card, ENVVAL, nCh, 0);
	}

	/*
	 ** Init to 0x02109204 :
	 ** Clock accuracy    = 0     (1000ppm)
	 ** Sample Rate       = 2     (48kHz)
	 ** Audio Channel     = 1     (Left of 2)
	 ** Source Number     = 0     (Unspecified)
	 ** Generation Status = 1     (Original for Cat Code 12)
	 ** Cat Code          = 12    (Digital Signal Mixer)
	 ** Mode              = 0     (Mode 0)
	 ** Emphasis          = 0     (None)
	 ** CP                = 1     (Copyright unasserted)
	 ** AN                = 0     (Digital audio)
	 ** P                 = 0     (Consumer)
	 */

	/* SPDIF0 */
	sblive_writeptr(card, SPCS0, 0, SPCS_CLKACCY_1000PPM | 0x002000000 |
			SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC | SPCS_GENERATIONSTATUS | 0x00001200 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT);

	/* SPDIF1 */
	sblive_writeptr(card, SPCS1, 0, SPCS_CLKACCY_1000PPM | 0x002000000 |
			SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC | SPCS_GENERATIONSTATUS | 0x00001200 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT);

	/* SPDIF2 & SPDIF3 */
	sblive_writeptr(card, SPCS2, 0, SPCS_CLKACCY_1000PPM | 0x002000000 |
			SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC | SPCS_GENERATIONSTATUS | 0x00001200 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT);

	fx_init(card);		/* initialize effects engine */

	card->tankmem = NULL;

#ifdef TANKMEM
	size = TMEMSIZE;
	sizeIdx = TMEMSIZEREG;
	while (size > 16384) {
		if ((card->tankmem = emu10k1_alloc_memphysical(size)) != NULL)
			break;

		size /= 2;
		sizeIdx -= 1;
	}

	if (card->tankmem == NULL) {
		card->tmemsize = 0;
		return CTSTATUS_ERROR;
	}

	card->tmemsize = size;
#else				/* !TANKMEM */
	card->tmemsize = 0;
#endif				/* TANKMEM */

	if ((card->virtualpagetable = emu10k1_alloc_memphysical((MAXPAGES - RESERVED) * sizeof(u32))) == NULL) {
		ERROR();
		emu10k1_free_memphysical(card->tankmem);
		return CTSTATUS_ERROR;
	}

	if ((card->silentpage = emu10k1_alloc_memphysical(EMUPAGESIZE)) == NULL) {
		ERROR();
		emu10k1_free_memphysical(card->tankmem);
		emu10k1_free_memphysical(card->virtualpagetable);
		return CTSTATUS_ERROR;
	} else
		memset(card->silentpage->virtaddx, 0, EMUPAGESIZE);

	for (pagecount = 0; pagecount < (MAXPAGES - RESERVED); pagecount++)

		((u32 *) card->virtualpagetable->virtaddx)[pagecount] = (card->silentpage->busaddx * 2) | pagecount;

	/* Init page table & tank memory base register */
	sblive_writeptr(card, PTB, 0, card->virtualpagetable->busaddx);
#ifdef TANKMEM
	sblive_writeptr(card, TCB, 0, card->tankmem->busaddx);
#else
	sblive_writeptr(card, TCB, 0, 0);
#endif
	sblive_writeptr(card, TCBS, 0, sizeIdx);

	for (nCh = 0; nCh < NUM_G; nCh++) {
		sblive_writeptr(card, MAPA, nCh, MAP_PTI_MASK | (card->silentpage->busaddx * 2));
		sblive_writeptr(card, MAPB, nCh, MAP_PTI_MASK | (card->silentpage->busaddx * 2));
	}

	/* Hokay, now enable the AUD bit */
	/* Enable Audio = 1 */
	/* Mute Disable Audio = 0 */
	/* Lock Tank Memory = 1 */
	/* Lock Sound Memory = 0 */
	/* Auto Mute = 1 */

	sblive_rmwac97(card, AC97_MASTERVOLUME, 0x8000, 0x8000);

	sblive_writeac97(card, AC97_MASTERVOLUME, 0);
	sblive_writeac97(card, AC97_PCMOUTVOLUME, 0);

	sblive_writefn0(card, HCFG, HCFG_AUDIOENABLE | HCFG_LOCKTANKCACHE | HCFG_AUTOMUTE | HCFG_JOYENABLE);

	/* TOSLink detection */
	card->has_toslink = 0;

	tmp = sblive_readfn0(card, HCFG);
	if (tmp & (HCFG_GPINPUT0 | HCFG_GPINPUT1)) {
		sblive_writefn0(card, HCFG, tmp | 0x800);

		udelay(512);

		if (tmp != (sblive_readfn0(card, HCFG) & ~0x800)) {
			card->has_toslink = 1;
			sblive_writefn0(card, HCFG, tmp);
		}
	}

	return CTSTATUS_SUCCESS;
}

static int __devinit emu10k1_init(struct emu10k1_card *card)
{
	/* Init Card */
	if (hw_init(card) != CTSTATUS_SUCCESS)
		return CTSTATUS_ERROR;

	voice_init(card);
	timer_init(card);
	addxmgr_init(card);

	DPD(2, "  hw control register -> %x\n", sblive_readfn0(card, HCFG));

	return CTSTATUS_SUCCESS;
}

static void __devexit audio_exit(struct emu10k1_card *card)
{
	kfree(card->waveout);
	kfree(card->wavein);
	return;
}

static void __devexit midi_exit(struct emu10k1_card *card)
{
	tasklet_unlock_wait(&card->mpuout->tasklet);
	kfree(card->mpuout);

	tasklet_unlock_wait(&card->mpuin->tasklet);
	kfree(card->mpuin);

	return;
}

static void __devexit emu10k1_exit(struct emu10k1_card *card)
{
	int ch;

	sblive_writefn0(card, INTE, DISABLE);

	/** Shutdown the chip **/
	for (ch = 0; ch < NUM_G; ch++)
		sblive_writeptr(card, DCYSUSV, ch, ENV_OFF);

	for (ch = 0; ch < NUM_G; ch++) {
		sblive_writeptr(card, VTFT, ch, 0);
		sblive_writeptr(card, CVCF, ch, 0);
		sblive_writeptr(card, PTRX, ch, 0);
		sblive_writeptr(card, CPF, ch, 0);
	}

	/* Reset recording buffers */
	sblive_writeptr(card, MICBS, 0, ADCBS_BUFSIZE_NONE);
	sblive_writeptr(card, MICBA, 0, 0);
	sblive_writeptr(card, FXBS, 0, ADCBS_BUFSIZE_NONE);
	sblive_writeptr(card, FXBA, 0, 0);
	sblive_writeptr(card, FXWC, 0, 0);
	sblive_writeptr(card, ADCBS, 0, ADCBS_BUFSIZE_NONE);
	sblive_writeptr(card, ADCBA, 0, 0);
	sblive_writeptr(card, TCBS, 0, TCBS_BUFFSIZE_16K);
	sblive_writeptr(card, TCB, 0, 0);
	sblive_writeptr(card, DBG, 0, 0x8000);

	/* Disable channel interrupt */
	sblive_writeptr(card, CLIEL, 0, 0);
	sblive_writeptr(card, CLIEH, 0, 0);
	sblive_writeptr(card, SOLEL, 0, 0);
	sblive_writeptr(card, SOLEH, 0, 0);

	/* Disable audio and lock cache */
	sblive_writefn0(card, HCFG, HCFG_LOCKSOUNDCACHE | HCFG_LOCKTANKCACHE | HCFG_MUTEBUTTONENABLE);
	sblive_writeptr(card, PTB, 0, 0);

	emu10k1_free_memphysical(card->silentpage);
	emu10k1_free_memphysical(card->virtualpagetable);
#ifdef TANKMEM
	emu10k1_free_memphysical(card->tankmem);
#endif
	return;
}

/* Driver initialization routine */
static int __devinit emu10k1_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id)
{
	struct emu10k1_card *card;

	if ((card = kmalloc(sizeof(struct emu10k1_card), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "emu10k1: out of memory\n");
		return -ENOMEM;
	}
	memset(card, 0, sizeof(struct emu10k1_card));

#if LINUX_VERSION_CODE > 0x020320
	if (!pci_dma_supported(pci_dev, EMU10K1_DMA_MASK)) {
		printk(KERN_ERR "emu10k1: architecture does not support 32bit PCI busmaster DMA\n");
		kfree(card);
		return -ENODEV;
	}

	if (pci_enable_device(pci_dev)) {
		kfree(card);
		return -ENODEV;
	}

	pci_set_master(pci_dev);

	card->iobase = pci_dev->resource[0].start;

	if (request_region(card->iobase, EMU10K1_EXTENT, card_names[pci_id->driver_data]) == NULL) {
		printk(KERN_ERR "emu10k1: IO space in use\n");
		kfree(card);
		return -ENODEV;
	}
	pci_dev->driver_data = card;
	pci_dev->dma_mask = EMU10K1_DMA_MASK;
#else
	pci_set_master(pci_dev);

	card->iobase = pci_dev->base_address[0] & PCI_BASE_ADDRESS_IO_MASK;

	if (check_region(card->iobase, EMU10K1_EXTENT)) {
		printk(KERN_ERR "emu10k1: IO space in use\n");
		kfree(card);
		return -ENODEV;
	}

	request_region(card->iobase, EMU10K1_EXTENT, card_names[pci_id->driver_data]);
#endif
	card->irq = pci_dev->irq;
	card->pci_dev = pci_dev;

	/* Reserve IRQ Line */
	if (request_irq(card->irq, emu10k1_interrupt, SA_SHIRQ, card_names[pci_id->driver_data], card)) {
		printk(KERN_ERR "emu10k1: IRQ in use\n");
		goto err_irq;
	}

	pci_read_config_byte(pci_dev, PCI_REVISION_ID, &card->chiprev);

	printk(KERN_INFO "emu10k1: %s rev %d found at IO 0x%04lx, IRQ %d\n", card_names[pci_id->driver_data], card->chiprev, card->iobase, card->irq);

	spin_lock_init(&card->lock);
	card->mixeraddx = card->iobase + AC97DATA;
	init_MUTEX(&card->open_sem);
	card->open_mode = 0;
	init_waitqueue_head(&card->open_wait);

	/* Register devices */
	if ((card->audio1_num = register_sound_dsp(&emu10k1_audio_fops, -1)) < 0) {
		printk(KERN_ERR "emu10k1: cannot register first audio device!\n");
		goto err_dev0;
	}

	if ((card->audio2_num = register_sound_dsp(&emu10k1_audio_fops, -1)) < 0) {
		printk(KERN_ERR "emu10k1: cannot register second audio device!\n");
		goto err_dev1;
	}

	if ((card->mixer_num = register_sound_mixer(&emu10k1_mixer_fops, -1)) < 0) {
		printk(KERN_ERR "emu10k1: cannot register mixer device!\n");
		goto err_dev2;
	}

	if ((card->midi_num = register_sound_midi(&emu10k1_midi_fops, -1)) < 0) {
		printk(KERN_ERR "emu10k1: cannot register midi device!\n");
		goto err_dev3;
	}

	if (emu10k1_init(card) != CTSTATUS_SUCCESS) {
		printk(KERN_ERR "emu10k1: cannot initialize device!\n");
		goto err_emu10k1_init;
	}

	if (audio_init(card) != CTSTATUS_SUCCESS) {
		printk(KERN_ERR "emu10k1: cannot initialize audio!\n");
		goto err_audio_init;
	}

	if (midi_init(card) != CTSTATUS_SUCCESS) {
		printk(KERN_ERR "emu10k1: cannot initialize midi!\n");
		goto err_midi_init;
	}

	mixer_init(card);

	DPD(2, "Hardware initialized. TRAM allocated: %u bytes\n", (unsigned int) card->tmemsize);

	list_add(&card->list, &emu10k1_devs);

	return 0;

      err_midi_init:
	audio_exit(card);

      err_audio_init:
	emu10k1_exit(card);

      err_emu10k1_init:
	unregister_sound_midi(card->midi_num);

      err_dev3:
	unregister_sound_mixer(card->mixer_num);

      err_dev2:
	unregister_sound_dsp(card->audio2_num);

      err_dev1:
	unregister_sound_dsp(card->audio1_num);

      err_dev0:
	free_irq(card->irq, card);

      err_irq:
	release_region(card->iobase, EMU10K1_EXTENT);
	kfree(card);

	return -ENODEV;
}

static void __devexit emu10k1_remove(struct pci_dev *pci_dev)
{
#if LINUX_VERSION_CODE > 0x020320
	struct emu10k1_card *card = pci_dev->driver_data;
#else
	struct emu10k1_card *card = list_entry(emu10k1_devs.next, struct emu10k1_card, list);
#endif
	midi_exit(card);
	audio_exit(card);
	emu10k1_exit(card);

	unregister_sound_midi(card->midi_num);
	unregister_sound_mixer(card->mixer_num);
	unregister_sound_dsp(card->audio2_num);
	unregister_sound_dsp(card->audio1_num);

	free_irq(card->irq, card);
	release_region(card->iobase, EMU10K1_EXTENT);

	list_del(&card->list);

	kfree(card);
	return;
}

MODULE_AUTHOR("Bertrand Lee, Cai Ying. (Email to: emu10k1-devel@opensource.creative.com)");
MODULE_DESCRIPTION("Creative EMU10K1 PCI Audio Driver v" DRIVER_VERSION "\nCopyright (C) 1999 Creative Technology Ltd.");

static struct pci_driver emu10k1_pci_driver = {
	name:"emu10k1",
	id_table:emu10k1_pci_tbl,
	probe:emu10k1_probe,
	remove:emu10k1_remove,
};

#if LINUX_VERSION_CODE > 0x020320
static int __init emu10k1_init_module(void)
{
	printk(KERN_INFO "Creative EMU10K1 PCI Audio Driver, version " DRIVER_VERSION ", " __TIME__ " " __DATE__ "\n");

	return pci_module_init(&emu10k1_pci_driver);
}

static void __exit emu10k1_cleanup_module(void)
{
	pci_unregister_driver(&emu10k1_pci_driver);
	return;
}

#else

static int __init emu10k1_init_module(void)
{
	struct pci_dev *dev = NULL;
	const struct pci_device_id *pci_id = emu10k1_pci_driver.id_table;

	printk(KERN_INFO "Creative EMU10K1 PCI Audio Driver, version " DRIVER_VERSION ", " __TIME__ " " __DATE__ "\n");

	if (!pci_present())
		return -ENODEV;

	while (pci_id->vendor) {
		while ((dev = pci_find_device(pci_id->vendor, pci_id->device, dev)))
			emu10k1_probe(dev, pci_id);

		pci_id++;
	}
	return 0;
}

static void __exit emu10k1_cleanup_module(void)
{
	struct emu10k1_card *card;

	while (!list_empty(&emu10k1_devs)) {
		card = list_entry(emu10k1_devs.next, struct emu10k1_card, list);

		emu10k1_remove(card->pci_dev);
	}

	return;
}

#endif

module_init(emu10k1_init_module);
module_exit(emu10k1_cleanup_module);
