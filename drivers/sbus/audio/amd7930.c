/*
 * drivers/sbus/audio/amd7930.c
 *
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@noc.rutgers.edu)
 *
 * This is the lowlevel driver for the AMD7930 audio chip found on all
 * sun4c machines and some sun4m machines.
 *
 * XXX Add note about the fun of getting the docs.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>
#include "audio.h"
#include "amd7930.h"

/*
 * Chip interface
 */
struct mapreg {
        u_short mr_x[8];
        u_short mr_r[8];
        u_short mr_gx;
        u_short mr_gr;
        u_short mr_ger;
        u_short mr_stgr;
        u_short mr_ftgr;
        u_short mr_atgr;
        u_char  mr_mmr1;
        u_char  mr_mmr2;
} map;


/* Write 16 bits of data from variable v to the data port of the audio chip */
#define	WAMD16(amd, v) ((amd)->dr = (v), (amd)->dr = (v) >> 8)

/* The following tables stolen from former (4.4Lite's) sys/sparc/bsd_audio.c */

/*
 * gx, gr & stg gains.  this table must contain 256 elements with
 * the 0th being "infinity" (the magic value 9008).  The remaining
 * elements match sun's gain curve (but with higher resolution):
 * -18 to 0dB in .16dB steps then 0 to 12dB in .08dB steps.
 */
static const u_short gx_coeff[256] = {
	0x9008, 0x8b7c, 0x8b51, 0x8b45, 0x8b42, 0x8b3b, 0x8b36, 0x8b33,
	0x8b32, 0x8b2a, 0x8b2b, 0x8b2c, 0x8b25, 0x8b23, 0x8b22, 0x8b22,
	0x9122, 0x8b1a, 0x8aa3, 0x8aa3, 0x8b1c, 0x8aa6, 0x912d, 0x912b,
	0x8aab, 0x8b12, 0x8aaa, 0x8ab2, 0x9132, 0x8ab4, 0x913c, 0x8abb,
	0x9142, 0x9144, 0x9151, 0x8ad5, 0x8aeb, 0x8a79, 0x8a5a, 0x8a4a,
	0x8b03, 0x91c2, 0x91bb, 0x8a3f, 0x8a33, 0x91b2, 0x9212, 0x9213,
	0x8a2c, 0x921d, 0x8a23, 0x921a, 0x9222, 0x9223, 0x922d, 0x9231,
	0x9234, 0x9242, 0x925b, 0x92dd, 0x92c1, 0x92b3, 0x92ab, 0x92a4,
	0x92a2, 0x932b, 0x9341, 0x93d3, 0x93b2, 0x93a2, 0x943c, 0x94b2,
	0x953a, 0x9653, 0x9782, 0x9e21, 0x9d23, 0x9cd2, 0x9c23, 0x9baa,
	0x9bde, 0x9b33, 0x9b22, 0x9b1d, 0x9ab2, 0xa142, 0xa1e5, 0x9a3b,
	0xa213, 0xa1a2, 0xa231, 0xa2eb, 0xa313, 0xa334, 0xa421, 0xa54b,
	0xada4, 0xac23, 0xab3b, 0xaaab, 0xaa5c, 0xb1a3, 0xb2ca, 0xb3bd,
	0xbe24, 0xbb2b, 0xba33, 0xc32b, 0xcb5a, 0xd2a2, 0xe31d, 0x0808,
	0x72ba, 0x62c2, 0x5c32, 0x52db, 0x513e, 0x4cce, 0x43b2, 0x4243,
	0x41b4, 0x3b12, 0x3bc3, 0x3df2, 0x34bd, 0x3334, 0x32c2, 0x3224,
	0x31aa, 0x2a7b, 0x2aaa, 0x2b23, 0x2bba, 0x2c42, 0x2e23, 0x25bb,
	0x242b, 0x240f, 0x231a, 0x22bb, 0x2241, 0x2223, 0x221f, 0x1a33,
	0x1a4a, 0x1acd, 0x2132, 0x1b1b, 0x1b2c, 0x1b62, 0x1c12, 0x1c32,
	0x1d1b, 0x1e71, 0x16b1, 0x1522, 0x1434, 0x1412, 0x1352, 0x1323,
	0x1315, 0x12bc, 0x127a, 0x1235, 0x1226, 0x11a2, 0x1216, 0x0a2a,
	0x11bc, 0x11d1, 0x1163, 0x0ac2, 0x0ab2, 0x0aab, 0x0b1b, 0x0b23,
	0x0b33, 0x0c0f, 0x0bb3, 0x0c1b, 0x0c3e, 0x0cb1, 0x0d4c, 0x0ec1,
	0x079a, 0x0614, 0x0521, 0x047c, 0x0422, 0x03b1, 0x03e3, 0x0333,
	0x0322, 0x031c, 0x02aa, 0x02ba, 0x02f2, 0x0242, 0x0232, 0x0227,
	0x0222, 0x021b, 0x01ad, 0x0212, 0x01b2, 0x01bb, 0x01cb, 0x01f6,
	0x0152, 0x013a, 0x0133, 0x0131, 0x012c, 0x0123, 0x0122, 0x00a2,
	0x011b, 0x011e, 0x0114, 0x00b1, 0x00aa, 0x00b3, 0x00bd, 0x00ba,
	0x00c5, 0x00d3, 0x00f3, 0x0062, 0x0051, 0x0042, 0x003b, 0x0033,
	0x0032, 0x002a, 0x002c, 0x0025, 0x0023, 0x0022, 0x001a, 0x0021,
	0x001b, 0x001b, 0x001d, 0x0015, 0x0013, 0x0013, 0x0012, 0x0012,
	0x000a, 0x000a, 0x0011, 0x0011, 0x000b, 0x000b, 0x000c, 0x000e,
};

/*
 * second stage play gain.
 */
static const u_short ger_coeff[] = {
	0x431f, /* 5. dB */
	0x331f, /* 5.5 dB */
	0x40dd, /* 6. dB */
	0x11dd, /* 6.5 dB */
	0x440f, /* 7. dB */
	0x411f, /* 7.5 dB */
	0x311f, /* 8. dB */
	0x5520, /* 8.5 dB */
	0x10dd, /* 9. dB */
	0x4211, /* 9.5 dB */
	0x410f, /* 10. dB */
	0x111f, /* 10.5 dB */
	0x600b, /* 11. dB */
	0x00dd, /* 11.5 dB */
	0x4210, /* 12. dB */
	0x110f, /* 13. dB */
	0x7200, /* 14. dB */
	0x2110, /* 15. dB */
	0x2200, /* 15.9 dB */
	0x000b, /* 16.9 dB */
	0x000f  /* 18. dB */
#define NGER (sizeof(ger_coeff) / sizeof(ger_coeff[0]))
};

#if 0
int
amd7930_commit_settings(addr)
	void *addr;
{
	register struct amd7930_softc *sc = addr;
	register struct mapreg *map;
	register volatile struct amd7930 *amd;
	register int s, level;

	DPRINTF(("sa_commit.\n"));

	map = &sc->sc_map;
	amd = sc->sc_au.au_amd;

	map->mr_gx = gx_coeff[sc->sc_rlevel];
	map->mr_stgr = gx_coeff[sc->sc_mlevel];

	level = (sc->sc_plevel * (256 + NGER)) >> 8;
	if (level >= 256) {
		map->mr_ger = ger_coeff[level - 256];
		map->mr_gr = gx_coeff[255];
	} else {
		map->mr_ger = ger_coeff[0];
		map->mr_gr = gx_coeff[level];
	}

	if (sc->sc_out_port == SUNAUDIO_SPEAKER)
		map->mr_mmr2 |= AMD_MMR2_LS;
	else
		map->mr_mmr2 &= ~AMD_MMR2_LS;

	s = splaudio();

	amd->cr = AMDR_MAP_MMR1;
	amd->dr = map->mr_mmr1;
	amd->cr = AMDR_MAP_GX;
	WAMD16(amd, map->mr_gx);
	amd->cr = AMDR_MAP_STG;
	WAMD16(amd, map->mr_stgr);
	amd->cr = AMDR_MAP_GR;
	WAMD16(amd, map->mr_gr);
	amd->cr = AMDR_MAP_GER;
	WAMD16(amd, map->mr_ger);
	amd->cr = AMDR_MAP_MMR2;
	amd->dr = map->mr_mmr2;

	splx(s);
	return(0);
}
#endif

static int amd7930_node, amd7930_irq, amd7930_regs_size, amd7930_ints_on = 0;
static struct amd7930 *amd7930_regs = NULL;
static __u8 * ptr;
static size_t count;

/* Enable amd7930 interrupts atomically. */
static __inline__ void amd7930_enable_ints(void)
{
	register unsigned long flags;

	if (amd7930_ints_on)
		return;

	save_and_cli(flags);
	amd7930_regs->cr = AMR_INIT;
	amd7930_regs->dr = AM_INIT_ACTIVE;
	restore_flags(flags);

	amd7930_ints_on = 1;
}

/* Disable amd7930 interrupts atomically. */
static __inline__ void amd7930_disable_ints(void)
{
	register unsigned long flags;

	if (!amd7930_ints_on)
		return;

	save_and_cli(flags);
	amd7930_regs->cr = AMR_INIT;
	amd7930_regs->dr = AM_INIT_ACTIVE | AM_INIT_DISABLE_INTS;
	restore_flags(flags);

	amd7930_ints_on = 0;
}  


/* Audio interrupt handler. */
static void amd7930_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	__u8 dummy;

	/* Clear the interrupt. */
	dummy = amd7930_regs->ir;

	/* Send the next byte of outgoing data. */
	if (ptr && count > 0) {
		/* Send the next byte and advance the head pointer. */
		amd7930_regs->bbtb = *ptr;
		ptr++;
		count--;

		/* Empty buffer? Notify the midlevel driver. */
		if (count == 0)
			sparcaudio_output_done();
	}
}

static int amd7930_open(struct inode * inode, struct file * file, struct sparcaudio_driver *drv)
{
	int level;

	/* Set the default audio parameters. */
	map.mr_gx = gx_coeff[128];
	map.mr_stgr = gx_coeff[0];

	level = (128 * (256 + NGER)) >> 8;
	if (level >= 256) {
		map.mr_ger = ger_coeff[level-256];
		map.mr_gr = gx_coeff[255];
	} else {
		map.mr_ger = ger_coeff[0];
		map.mr_gr = gx_coeff[level];
	}

	map.mr_mmr2 |= AM_MAP_MMR2_LS;

	cli();

	amd7930_regs->cr = AMR_MAP_MMR1;
	amd7930_regs->dr = map.mr_mmr1;
	amd7930_regs->cr = AMR_MAP_GX;
	WAMD16(amd7930_regs,map.mr_gx);
	amd7930_regs->cr = AMR_MAP_STG;
	WAMD16(amd7930_regs,map.mr_stgr);
	amd7930_regs->cr = AMR_MAP_GR;
	WAMD16(amd7930_regs,map.mr_gr);
	amd7930_regs->cr = AMR_MAP_GER;
	WAMD16(amd7930_regs,map.mr_ger);
	amd7930_regs->cr = AMR_MAP_MMR2;
	amd7930_regs->dr = map.mr_mmr2;

	sti();

	MOD_INC_USE_COUNT;

	return 0;
}

static void amd7930_release(struct inode * inode, struct file * file, struct sparcaudio_driver *drv)
{
	amd7930_disable_ints();
	MOD_DEC_USE_COUNT;
}

static void amd7930_start_output(struct sparcaudio_driver *drv, __u8 * buffer, size_t the_count)
{
	count = the_count;
	ptr = buffer;
	amd7930_enable_ints();
}

static void amd7930_stop_output(struct sparcaudio_driver *drv)
{
	amd7930_disable_ints();
	ptr = NULL;
	count = 0;
}


static struct sparcaudio_operations amd7930_ops = {
	amd7930_open,
	amd7930_release,
	NULL,			/* amd7930_ioctl */
	amd7930_start_output,
	amd7930_stop_output,
};

static struct sparcaudio_driver amd7930_drv = {
	"amd7930",
	&amd7930_ops,
};

/* Probe for the amd7930 chip and then attach the driver. */
#ifdef MODULE
int init_module(void)
#else
__initfunc(int amd7930_init(void))
#endif
{
	struct linux_prom_registers regs[1];
	struct linux_prom_irqs irq;
	int err;

#ifdef MODULE
	register_symtab(0);
#endif

	/* Find the PROM "audio" node. */
	amd7930_node = prom_getchild(prom_root_node);
	amd7930_node = prom_searchsiblings(amd7930_node, "audio");
	if (!amd7930_node)
		return -EIO;

	/* Map the registers into memory. */
	prom_getproperty(amd7930_node, "reg", (char *)regs, sizeof(regs));
	amd7930_regs_size = regs[0].reg_size;
	amd7930_regs = sparc_alloc_io(regs[0].phys_addr, 0, regs[0].reg_size,
				      "amd7930", regs[0].which_io, 0);
	if (!amd7930_regs) {
		printk(KERN_ERR "amd7930: could not allocate registers\n");
		return -EIO;
	}

	/* Disable amd7930 interrupt generation. */
	amd7930_disable_ints();

	/* Initialize the MUX unit to connect the MAP to the CPU. */
	amd7930_regs->cr = AMR_MUX_1_4;
	amd7930_regs->dr = (AM_MUX_CHANNEL_Bb << 4) | AM_MUX_CHANNEL_Ba;
	amd7930_regs->dr = 0;
	amd7930_regs->dr = 0;
	amd7930_regs->dr = AM_MUX_MCR4_ENABLE_INTS;

	/* Attach the interrupt handler to the audio interrupt. */
	prom_getproperty(amd7930_node, "intr", (char *)&irq, sizeof(irq));
	amd7930_irq = irq.pri;
	request_irq(amd7930_irq, amd7930_interrupt, SA_INTERRUPT, "amd7930", NULL);
	enable_irq(amd7930_irq);

	memset(&map, 0, sizeof(map));
	map.mr_mmr1 = AM_MAP_MMR1_GX | AM_MAP_MMR1_GER | AM_MAP_MMR1_GR | AM_MAP_MMR1_STG;

	/* Register ourselves with the midlevel audio driver. */
	err = register_sparcaudio_driver(&amd7930_drv);
	if (err < 0) {
		/* XXX We should do something. Complain for now. */
		printk(KERN_ERR "amd7930: really screwed now\n");
		return -EIO;
	}

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	amd7930_disable_ints();
	disable_irq(amd7930_irq);
	free_irq(amd7930_irq, NULL);
	sparc_free_io(amd7930_regs, amd7930_regs_size);
}
#endif
