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
#include <linux/malloc.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/sbus.h>

#include "audio.h"
#include "amd7930.h"

#define MAX_DRIVERS 1

/* Private information we store for each amd7930 chip. */
struct amd7930_info {
	/* Current buffer that the driver is playing. */
	volatile __u8 * output_ptr;
	volatile unsigned long output_count;

	/* Current record buffer. */
	volatile __u8 * input_ptr;
	volatile unsigned long input_count;

	/* Device registers information. */
	struct amd7930 *regs;
	unsigned long regs_size;
	struct amd7930_map map;

	/* Device interrupt information. */
	int irq;
	volatile int ints_on;
};

/* Output a 16-bit quantity in the order that the amd7930 expects. */
#define amd7930_out16(regs,v) ({ regs->dr = v & 0xFF; regs->dr = (v >> 8) & 0xFF; })


/*
 * gx, gr & stg gains.  this table must contain 256 elements with
 * the 0th being "infinity" (the magic value 9008).  The remaining
 * elements match sun's gain curve (but with higher resolution):
 * -18 to 0dB in .16dB steps then 0 to 12dB in .08dB steps.
 */
static __const__ __u16 gx_coeff[256] = {
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

static __const__ __u16 ger_coeff[] = {
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
};
#define NR_GER_COEFFS (sizeof(ger_coeff) / sizeof(ger_coeff[0]))

/* Enable amd7930 interrupts atomically. */
static __inline__ void amd7930_enable_ints(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;
	register unsigned long flags;

	if (info->ints_on)
		return;

	save_and_cli(flags);
	info->regs->cr = AMR_INIT;
	info->regs->dr = AM_INIT_ACTIVE;
	restore_flags(flags);

	info->ints_on = 1;
}

/* Disable amd7930 interrupts atomically. */
static __inline__ void amd7930_disable_ints(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;
	register unsigned long flags;

	if (!info->ints_on)
		return;

	save_and_cli(flags);
	info->regs->cr = AMR_INIT;
	info->regs->dr = AM_INIT_ACTIVE | AM_INIT_DISABLE_INTS;
	restore_flags(flags);

	info->ints_on = 0;
}  

/* Commit the local copy of the MAP registers to the amd7930. */
static void amd7930_commit_map(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;
	struct amd7930      *regs = info->regs;
	struct amd7930_map  *map  = &info->map;
	unsigned long flags;

	save_and_cli(flags);

	regs->cr = AMR_MAP_GX;
	amd7930_out16(regs, map->gx);

	regs->cr = AMR_MAP_GR;
	amd7930_out16(regs, map->gr);

	regs->cr = AMR_MAP_STGR;
	amd7930_out16(regs, map->stgr);

	regs->cr = AMR_MAP_GER;
	amd7930_out16(regs, map->ger);

	regs->cr = AMR_MAP_MMR1;
	regs->dr = map->mmr1;

	regs->cr = AMR_MAP_MMR2;
	regs->dr = map->mmr2;

	restore_flags(flags);
}

/* Interrupt handler (The chip takes only one byte per interrupt. Grrr!) */
static void amd7930_interrupt(int irq, void *dev_id, struct pt_regs *intr_regs)
{
	struct sparcaudio_driver *drv = (struct sparcaudio_driver *)dev_id;
	struct amd7930_info *info = (struct amd7930_info *)drv->private;
	struct amd7930 *regs = info->regs;
	__u8 dummy;

	/* Clear the interrupt. */
	dummy = regs->ir;

	/* Send the next byte of outgoing data. */
	if (info->output_ptr && info->output_count > 0) {
		/* Send the next byte and advance buffer pointer. */
		regs->bbtb = *(info->output_ptr);
		info->output_ptr++;
		info->output_count--;

		/* Done with the buffer? Notify the midlevel driver. */
		if (info->output_count == 0) {
			info->output_ptr = NULL;
			info->output_count = 0;
			sparcaudio_output_done(drv);
		}
	}

	/* Read the next byte of incoming data. */
	if (info->input_ptr && info->input_count > 0) {
		/* Get the next byte and advance buffer pointer. */
		*(info->input_ptr) = regs->bbrb;
		info->input_ptr++;
		info->input_count--;

		/* Done with the buffer? Notify the midlevel driver. */
		if (info->input_count == 0) {
			info->input_ptr = NULL;
			info->input_count = 0;
			sparcaudio_input_done(drv);
		}
	}
}


static int amd7930_open(struct inode * inode, struct file * file,
			struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;
	int level;

	/* Set the default audio parameters. */
	info->map.gx = gx_coeff[128];
	info->map.stgr = gx_coeff[0];

	level = (128 * (256 + NR_GER_COEFFS)) >> 8;
	if (level >= 256) {
		info->map.ger = ger_coeff[level-256];
		info->map.gr = gx_coeff[255];
	} else {
		info->map.ger = ger_coeff[0];
		info->map.gr = gx_coeff[level];
	}

	info->map.mmr2 |= AM_MAP_MMR2_LS;

	amd7930_commit_map(drv);

	MOD_INC_USE_COUNT;

	return 0;
}

static void amd7930_release(struct inode * inode, struct file * file,
			    struct sparcaudio_driver *drv)
{
	amd7930_disable_ints(drv);
	MOD_DEC_USE_COUNT;
}

static void amd7930_start_output(struct sparcaudio_driver *drv,
				 __u8 * buffer, unsigned long count)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	info->output_ptr = buffer;
	info->output_count = count;
	amd7930_enable_ints(drv);
}

static void amd7930_stop_output(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	info->output_ptr = NULL;
	info->output_count = 0;

	if (!info->input_ptr)
		amd7930_disable_ints(drv);
}

static void amd7930_start_input(struct sparcaudio_driver *drv,
				__u8 * buffer, unsigned long count)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	info->input_ptr = buffer;
	info->input_count = count;
	amd7930_enable_ints(drv);
}

static void amd7930_stop_input(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	info->input_ptr = NULL;
	info->input_count = 0;

	if (!info->output_ptr)
		amd7930_disable_ints(drv);
}

static void amd7930_sunaudio_getdev(struct sparcaudio_driver *drv,
				 audio_device_t * audinfo)
{
	strncpy(audinfo->name, "amd7930", sizeof(audinfo->name) - 1);
	strncpy(audinfo->version, "x", sizeof(audinfo->version) - 1);
	strncpy(audinfo->config, "audio", sizeof(audinfo->config) - 1);
}


/*
 *	Device detection and initialization.
 */

static struct sparcaudio_driver drivers[MAX_DRIVERS];
static int num_drivers;

static struct sparcaudio_operations amd7930_ops = {
	amd7930_open,
	amd7930_release,
	NULL,			/* amd7930_ioctl */
	amd7930_start_output,
	amd7930_stop_output,
	amd7930_start_input,
	amd7930_stop_input,
	amd7930_sunaudio_getdev,
};

/* Attach to an amd7930 chip given its PROM node. */
static int amd7930_attach(struct sparcaudio_driver *drv, int node,
			  struct linux_sbus *sbus, struct linux_sbus_device *sdev)
{
	struct linux_prom_registers regs;
	struct linux_prom_irqs irq;
	struct amd7930_info *info;
	int err;

	/* Allocate our private information structure. */
	drv->private = kmalloc(sizeof(struct amd7930_info), GFP_KERNEL);
	if (!drv->private)
		return -ENOMEM;

	/* Point at the information structure and initialize it. */
	drv->ops = &amd7930_ops;
	info = (struct amd7930_info *)drv->private;
	info->output_ptr = info->input_ptr = NULL;
	info->output_count = info->input_count = 0;
	info->ints_on = 1; /* force disable below */

	/* Map the registers into memory. */
	prom_getproperty(node, "reg", (char *)&regs, sizeof(regs));
	if (sbus && sdev)
		prom_apply_sbus_ranges(sbus, &regs, 1, sdev);
	info->regs_size = regs.reg_size;
	info->regs = sparc_alloc_io(regs.phys_addr, 0, regs.reg_size,
					   "amd7930", regs.which_io, 0);
	if (!info->regs) {
		printk(KERN_ERR "amd7930: could not allocate registers\n");
		kfree(drv->private);
		return -EIO;
	}

	/* Disable amd7930 interrupt generation. */
	amd7930_disable_ints(drv);

	/* Initialize the MUX unit to connect the MAP to the CPU. */
	info->regs->cr = AMR_MUX_1_4;
	info->regs->dr = (AM_MUX_CHANNEL_Bb << 4) | AM_MUX_CHANNEL_Ba;
	info->regs->dr = 0;
	info->regs->dr = 0;
	info->regs->dr = AM_MUX_MCR4_ENABLE_INTS;

	/* Attach the interrupt handler to the audio interrupt. */
	prom_getproperty(node, "intr", (char *)&irq, sizeof(irq));
	info->irq = irq.pri;
	request_irq(info->irq, amd7930_interrupt,
		    SA_INTERRUPT, "amd7930", drv);
	enable_irq(info->irq);

	/* Initalize the local copy of the MAP registers. */
	memset(&info->map, 0, sizeof(info->map));
	info->map.mmr1 = AM_MAP_MMR1_GX | AM_MAP_MMR1_GER
			 | AM_MAP_MMR1_GR | AM_MAP_MMR1_STG;

	/* Register the amd7930 with the midlevel audio driver. */
	err = register_sparcaudio_driver(drv);
	if (err < 0) {
		printk(KERN_ERR "amd7930: unable to register\n");
		disable_irq(info->irq);
		free_irq(info->irq, drv);
		sparc_free_io(info->regs, info->regs_size);
		kfree(drv->private);
		return -EIO;
	}

	/* Announce the hardware to the user. */
	printk(KERN_INFO "amd7930 at 0x%lx irq %d\n",
		(unsigned long)info->regs, info->irq);

	/* Success! */
	return 0;
}

#ifdef MODULE
/* Detach from an amd7930 chip given the device structure. */
static void amd7930_detach(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	unregister_sparcaudio_driver(drv);
	amd7930_disable_ints(drv);
	disable_irq(info->irq);
	free_irq(info->irq, drv);
	sparc_free_io(info->regs, info->regs_size);
	kfree(drv->private);
}
#endif


/* Probe for the amd7930 chip and then attach the driver. */
#ifdef MODULE
int init_module(void)
#else
__initfunc(int amd7930_init(void))
#endif
{
	struct linux_sbus *bus;
	struct linux_sbus_device *sdev;
	int node;

#if 0
#ifdef MODULE
	register_symtab(0);
#endif
#endif

	/* Try to find the sun4c "audio" node first. */
	node = prom_getchild(prom_root_node);
	node = prom_searchsiblings(node, "audio");
	if (node && amd7930_attach(&drivers[0], node, NULL, NULL) == 0)
		num_drivers = 1;
	else
		num_drivers = 0;

	/* Probe each SBUS for amd7930 chips. */
	for_all_sbusdev(sdev,bus) {
		if (!strcmp(sdev->prom_name, "audio")) {
			/* Don't go over the max number of drivers. */
			if (num_drivers >= MAX_DRIVERS)
				continue;

			if (amd7930_attach(&drivers[num_drivers],
					   sdev->prom_node, sdev->my_bus, sdev) == 0)
				num_drivers++;
		}
	}

	/* Only return success if we found some amd7930 chips. */
	return (num_drivers > 0) ? 0 : -EIO;
}

#ifdef MODULE
void cleanup_module(void)
{
	register int i;

	for (i = 0; i < num_drivers; i++) {
		amd7930_detach(&drivers[i]);
		num_drivers--;
	}
}
#endif
