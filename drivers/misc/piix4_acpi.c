/*
 * linux/drivers/misc/piix4_acpi.c
 *
 *	(C) Copyright 1999 Linus Torvalds
 *
 * A PM driver for the ACPI portion of the Intel PIIX4
 * chip.
 *
 * This has been known to occasionally work on some laptops.
 *
 * It probably only works on Intel PII machines that support
 * the STPCLK protocol.
 */

#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/io.h>

extern void (*acpi_idle)(void);

/*
 * This first part should be common to all ACPI
 * CPU sleep functionality. Assuming we get the
 * timing heuristics in a better shape than "none" ;)
 */

typedef void (*sleep_fn_t)(void);

/*
 * Our "sleep mode" is a fixed point number
 * with two binary places, ranging between
 * [0 .. 3[
 */
#define Cx_SHIFT	2
#define MAXMODE		((3 << Cx_SHIFT)-1)

/*
 * NOTE!
 *
 * Right now this always defaults to C3, which is just broken.
 * The exit latency is usually too high for much busy IO activity,
 * and generally it's not always the best thing to do.
 *
 * We should just read the cycle counter around all the cases,
 * and if we pause for a long time we go to a deeper sleep, while
 * a short wait makes us go into a lighter sleep.
 */
static void common_acpi_idle(sleep_fn_t *sleep)
{
	int mode = MAXMODE;

	while (1) {
		while (!current->need_resched) {
			unsigned int time;

			time = get_cycles();
			sleep[(mode) >> Cx_SHIFT]();
			time = get_cycles() - time;

			/*
			 * Yeah, yeah, yeah.
			 *  if (time > Large && mode < MAXMODE) mode++;
			 *  if (time < Small && mode > 0) mode--;
			 * Yadda-yadda-yadda.
			 *
			 * "Large" is on the order of half a timer tick.
			 * "Small" is on the order of Large >> 2 or so.
			 *
			 * Somebody should _really_ look at the exact
			 * details. The ACPI bios would give some made-up
			 * numbers, they might be useful (or maybe not:
			 * they are probably tuned for whatever Windows
			 * does, so don't take them for granted).
			 */
		}
		schedule();
		check_pgt_cache();
	}
}		

/* Ok, here starts the magic PIIX4 knowledge */

/*
 * Ehh.. We "know" about the northbridge
 * bus arbitration stuff. Maybe somebody
 * should actually verify this some day?
 */
#define NORTHBRIDGE_CONTROL	0x22
#define		NB_ARBITRATE	0x01

/*
 * PIIX4 ACPI IO offsets and defines
 */
#define PMEN		0x02
#define PMCNTRL		0x04
#define PMTMR		0x08
#define GPSTS		0x0c
#define GPEN		0x0E

#define PCNTRL		0x10
#define   CC_EN			0x0200
#define   BST_EN		0x0400
#define   SLEEP_EN		0x0800
#define   STPCLK_EN		0x1000
#define   CLKRUN_EN		0x2000

#define PLVL2		0x14
#define PLVL3		0x15

/*
 * PIIX4 ACPI PCI configurations offsets and defines
 */
#define DEVACTB		0x58
#define   BRLD_EN_IRQ0	0x01
#define   BRLD_EN_IRQ	0x02

#define PMREGMISC	0x80
#define   PMIOSE		0x01

static unsigned int piix4_base_address = 0;

static void piix4_c1_sleep(void)
{
	asm volatile("sti ; hlt" : : : "memory");
}

static void piix4_c2_sleep(void)
{
	outl(CLKRUN_EN | CC_EN, piix4_base_address + PCNTRL);
	inb(piix4_base_address + PLVL2);
}

static void piix4_c3_sleep(void)
{
	__cli();
	outl(CLKRUN_EN | CC_EN | STPCLK_EN | SLEEP_EN, piix4_base_address + PCNTRL);
	outb(NB_ARBITRATE, NORTHBRIDGE_CONTROL);
	inb(piix4_base_address + PLVL3);
	outb(0, NORTHBRIDGE_CONTROL);
	__sti();
}

static sleep_fn_t piix4_sleep[] = {
	piix4_c1_sleep,		/* low-latency C1 (ie "sti ; hlt") */
	piix4_c2_sleep,		/* medium latency C2 (ie LVL2 stopckl) */
	piix4_c3_sleep		/* high-latency C3 (ie LVL3 sleep) */
};

static void piix4_acpi_idle(void)
{
	common_acpi_idle(piix4_sleep);
}

static int __init piix4_acpi_init(void)
{
	/* This is the PIIX4 ACPI device */
	struct pci_dev *dev;
	u32 base, val;
	u16 cmd;
	u8 pmregmisc;

#ifdef __SMP__
	/*
	 * We can't really do idle things with multiple CPU's, I'm
	 * afraid.  We'd need a per-CPU ACPI device.
	 */
	if (smp_num_cpus > 1)
		return -1;
#endif
	dev = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_3, NULL);

	if (!dev)
		return -1;

	/*
	 * Read the IO base value, and verify that it makes sense
	 *
	 * We could enable this if it wasn't enabled before, but
	 * let's walk before we run..
	 */
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (!(cmd & PCI_COMMAND_IO))
		return -1;

	pci_read_config_byte(dev, PMREGMISC, &pmregmisc);
	if (!(pmregmisc & PMIOSE))
		return -1;

	pci_read_config_dword(dev, 0x40, &base);
	if (!(base & PCI_BASE_ADDRESS_SPACE_IO))
		return -1;

	base &= PCI_BASE_ADDRESS_IO_MASK;
	if (!base)
		return -1;

	printk("Found PIIX4 ACPI device at %04x\n", base);
	piix4_base_address = base;

	/* Enable stopcklock, sleep and bursts, along with clock control */
	outl(CLKRUN_EN | CC_EN | STPCLK_EN | SLEEP_EN, piix4_base_address + PCNTRL);

	/* Make all unmasked interrupts be BREAK events */
	pci_read_config_dword(dev, DEVACTB, &val);
	pci_write_config_dword(dev, DEVACTB, val | BRLD_EN_IRQ0 | BRLD_EN_IRQ);

	/* Set up the new idle handler.. */
	acpi_idle = piix4_acpi_idle;
	return 0;
}

__initcall(piix4_acpi_init);
