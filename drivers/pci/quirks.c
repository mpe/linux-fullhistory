/*
 * $Id: quirks.c,v 1.5 1998/05/02 19:24:14 mj Exp $
 *
 *  This file contains work-arounds for many known PCI hardware
 *  bugs.  Devices present only on certain architectures (host
 *  bridges et cetera) should be handled in arch-specific code.
 *
 *  Copyright (c) 1999 Martin Mares <mj@ucw.cz>
 *
 *  The bridge optimization stuff has been removed. If you really
 *  have a silly BIOS which is unable to set your host bridge right,
 *  use the PowerTweak utility (see http://powertweak.sourceforge.net).
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>

#undef DEBUG

/* Deal with broken BIOS'es that neglect to enable passive release,
   which can cause problems in combination with the 82441FX/PPro MTRRs */
static void __init quirk_passive_release(struct pci_dev *dev)
{
	struct pci_dev *d = NULL;
	unsigned char dlc;

	/* We have to make sure a particular bit is set in the PIIX3
	   ISA bridge, so we have to go out and find it. */
	while ((d = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_0, d))) {
		pci_read_config_byte(d, 0x82, &dlc);
		if (!(dlc & 1<<1)) {
			printk(KERN_ERR "PCI: PIIX3: Enabling Passive Release on %s\n", d->slot_name);
			dlc |= 1<<1;
			pci_write_config_byte(d, 0x82, dlc);
		}
	}
}

/*  The VIA VP2/VP3/MVP3 seem to have some 'features'. There may be a workaround
    but VIA don't answer queries. If you happen to have good contacts at VIA
    ask them for me please -- Alan 
    
    This appears to be BIOS not version dependent. So presumably there is a 
    chipset level fix */
    

int isa_dma_bridge_buggy;		/* Exported */
    
static void __init quirk_isa_dma_hangs(struct pci_dev *dev)
{
	if (!isa_dma_bridge_buggy) {
		isa_dma_bridge_buggy=1;
		printk(KERN_INFO "Activating ISA DMA hang workarounds.\n");
	}
}

int pci_pci_problems;

/*
 *	Chipsets where PCI->PCI transfers vanish or hang
 */

static void __init quirk_nopcipci(struct pci_dev *dev)
{
	if((pci_pci_problems&PCIPCI_FAIL)==0)
	{
		printk(KERN_INFO "Disabling direct PCI/PCI transfers.\n");
		pci_pci_problems|=PCIPCI_FAIL;
	}
}

/*
 *	Triton requires workarounds to be used by the drivers
 */
 
static void __init quirk_triton(struct pci_dev *dev)
{
	if((pci_pci_problems&PCIPCI_TRITON)==0)
	{
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems|=PCIPCI_TRITON;
	}
}

/*
 *	VIA Apollo KT133 needs PCI latency patch
 *	Made according to a windows driver based patch by George E. Breese
 *	see PCI Latency Adjust on http://www.viahardware.com/download/viatweak.shtm
 *      Also see http://home.tiscalinet.de/au-ja/review-kt133a-1-en.html for
 *      the info on which Mr Breese based his work.
 *
 *	Updated based on further information from the site and also on
 *	information provided by VIA 
 */
static void __init quirk_vialatency(struct pci_dev *dev)
{
	struct pci_dev *p;
	u8 rev;
	u8 busarb;
	/* Ok we have a potential problem chipset here. Now see if we have
	   a buggy southbridge */
	   
	p=pci_find_device(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C686, NULL);
	if(p!=NULL)
	{
		pci_read_config_byte(p, PCI_CLASS_REVISION, &rev);
		/* 0x40 - 0x4f == 686B, 0x10 - 0x2f == 686A; thanks Dan Hollis */
		/* Check for buggy part revisions */
		if (rev < 0x40 || rev > 0x42) 
			return;
	}
	else
	{
		p = pci_find_device(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8231, NULL);
		if(p==NULL)	/* No problem parts */
			return;
		pci_read_config_byte(p, PCI_CLASS_REVISION, &rev);
		/* Check for buggy part revisions */
		if (rev < 0x10 || rev > 0x12) 
			return;
	}
	
	/*
	 *	Ok we have the problem. Now set the PCI master grant to 
	 *	occur every master grant. The apparent bug is that under high
	 *	PCI load (quite common in Linux of course) you can get data
	 *	loss when the CPU is held off the bus for 3 bus master requests
	 *	This happens to include the IDE controllers....
	 *
	 *	VIA only apply this fix when an SB Live! is present but under
	 *	both Linux and Windows this isnt enough, and we have seen
	 *	corruption without SB Live! but with things like 3 UDMA IDE
	 *	controllers. So we ignore that bit of the VIA recommendation..
	 */

	pci_read_config_byte(dev, 0x76, &busarb);
	/* Set bit 4 and bi 5 of byte 76 to 0x01 
	   "Master priority rotation on every PCI master grant */
	busarb &= ~(1<<5);
	busarb |= (1<<4);
	pci_write_config_byte(dev, 0x76, busarb);
	printk(KERN_INFO "Applying VIA southbridge workaround.\n");
}

/*
 *	VIA Apollo VP3 needs ETBF on BT848/878
 */
 
static void __init quirk_viaetbf(struct pci_dev *dev)
{
	if((pci_pci_problems&PCIPCI_VIAETBF)==0)
	{
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems|=PCIPCI_VIAETBF;
	}
}
static void __init quirk_vsfx(struct pci_dev *dev)
{
	if((pci_pci_problems&PCIPCI_VSFX)==0)
	{
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems|=PCIPCI_VSFX;
	}
}


/*
 *	Natoma has some interesting boundary conditions with Zoran stuff
 *	at least
 */
 
static void __init quirk_natoma(struct pci_dev *dev)
{
	if((pci_pci_problems&PCIPCI_NATOMA)==0)
	{
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems|=PCIPCI_NATOMA;
	}
}

/*
 *  S3 868 and 968 chips report region size equal to 32M, but they decode 64M.
 *  If it's needed, re-allocate the region.
 */

static void __init quirk_s3_64M(struct pci_dev *dev)
{
	struct resource *r = &dev->resource[0];

	if ((r->start & 0x3ffffff) || r->end != r->start + 0x3ffffff) {
		r->start = 0;
		r->end = 0x3ffffff;
	}
}

static void __init quirk_io_region(struct pci_dev *dev, unsigned region, unsigned size, int nr)
{
	region &= ~(size-1);
	if (region) {
		struct resource *res = dev->resource + nr;

		res->name = dev->name;
		res->start = region;
		res->end = region + size - 1;
		res->flags = IORESOURCE_IO;
		pci_claim_resource(dev, nr);
	}
}	

/*
 * Let's make the southbridge information explicit instead
 * of having to worry about people probing the ACPI areas,
 * for example.. (Yes, it happens, and if you read the wrong
 * ACPI register it will put the machine to sleep with no
 * way of waking it up again. Bummer).
 *
 * ALI M7101: Two IO regions pointed to by words at
 *	0xE0 (64 bytes of ACPI registers)
 *	0xE2 (32 bytes of SMB registers)
 */
static void __init quirk_ali7101_acpi(struct pci_dev *dev)
{
	u16 region;

	pci_read_config_word(dev, 0xE0, &region);
	quirk_io_region(dev, region, 64, PCI_BRIDGE_RESOURCES);
	pci_read_config_word(dev, 0xE2, &region);
	quirk_io_region(dev, region, 32, PCI_BRIDGE_RESOURCES+1);
}

/*
 * PIIX4 ACPI: Two IO regions pointed to by longwords at
 *	0x40 (64 bytes of ACPI registers)
 *	0x90 (32 bytes of SMB registers)
 */
static void __init quirk_piix4_acpi(struct pci_dev *dev)
{
	u32 region;

	pci_read_config_dword(dev, 0x40, &region);
	quirk_io_region(dev, region, 64, PCI_BRIDGE_RESOURCES);
	pci_read_config_dword(dev, 0x90, &region);
	quirk_io_region(dev, region, 32, PCI_BRIDGE_RESOURCES+1);
}

/*
 * VIA ACPI: One IO region pointed to by longword at
 *	0x48 or 0x20 (256 bytes of ACPI registers)
 */
static void __init quirk_vt82c586_acpi(struct pci_dev *dev)
{
	u8 rev;
	u32 region;

	pci_read_config_byte(dev, PCI_CLASS_REVISION, &rev);
	if (rev & 0x10) {
		pci_read_config_dword(dev, 0x48, &region);
		region &= PCI_BASE_ADDRESS_IO_MASK;
		quirk_io_region(dev, region, 256, PCI_BRIDGE_RESOURCES);
	}
}

/*
 * VIA VT82C686 ACPI: Three IO region pointed to by (long)words at
 *	0x48 (256 bytes of ACPI registers)
 *	0x70 (128 bytes of hardware monitoring register)
 *	0x90 (16 bytes of SMB registers)
 */
static void __init quirk_vt82c686_acpi(struct pci_dev *dev)
{
	u16 hm;
	u32 smb;

	quirk_vt82c586_acpi(dev);

	pci_read_config_word(dev, 0x70, &hm);
	hm &= PCI_BASE_ADDRESS_IO_MASK;
	quirk_io_region(dev, hm, 128, PCI_BRIDGE_RESOURCES + 1);

	pci_read_config_dword(dev, 0x90, &smb);
	smb &= PCI_BASE_ADDRESS_IO_MASK;
	quirk_io_region(dev, smb, 16, PCI_BRIDGE_RESOURCES + 2);
}


#ifdef CONFIG_X86_IO_APIC 
extern int nr_ioapics;

/*
 * VIA 686A/B: If an IO-APIC is active, we need to route all on-chip
 * devices to the external APIC.
 *
 * TODO: When we have device-specific interrupt routers,
 * this code will go away from quirks.
 */
static void __init quirk_via_ioapic(struct pci_dev *dev)
{
	u8 tmp;
	
	if (nr_ioapics < 1)
		tmp = 0;    /* nothing routed to external APIC */
	else
		tmp = 0x1f; /* all known bits (4-0) routed to external APIC */
		
	printk(KERN_INFO "PCI: %sbling Via external APIC routing\n",
	       tmp == 0 ? "Disa" : "Ena");

	/* Offset 0x58: External APIC IRQ output control */
	pci_write_config_byte (dev, 0x58, tmp);
}

#endif /* CONFIG_X86_IO_APIC */


/*
 * Via 686A/B:  The PCI_INTERRUPT_LINE register for the on-chip
 * devices, USB0/1, AC97, MC97, and ACPI, has an unusual feature:
 * when written, it makes an internal connection to the PIC.
 * For these devices, this register is defined to be 4 bits wide.
 * Normally this is fine.  However for IO-APIC motherboards, or
 * non-x86 architectures (yes Via exists on PPC among other places),
 * we must mask the PCI_INTERRUPT_LINE value versus 0xf to get
 * interrupts delivered properly.
 *
 * TODO: When we have device-specific interrupt routers,
 * quirk_via_irqpic will go away from quirks.
 */

/*
 * FIXME: it is questionable that quirk_via_acpi
 * is needed.  It shows up as an ISA bridge, and does not
 * support the PCI_INTERRUPT_LINE register at all.  Therefore
 * it seems like setting the pci_dev's 'irq' to the
 * value of the ACPI SCI interrupt is only done for convenience.
 *	-jgarzik
 */
static void __init quirk_via_acpi(struct pci_dev *d)
{
	/*
	 * VIA ACPI device: SCI IRQ line in PCI config byte 0x42
	 */
	u8 irq;
	pci_read_config_byte(d, 0x42, &irq);
	irq &= 0xf;
	if (irq && (irq != 2))
		d->irq = irq;
}

static void __init quirk_via_irqpic(struct pci_dev *dev)
{
	u8 irq, new_irq = dev->irq & 0xf;

	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);

	if (new_irq != irq) {
		printk(KERN_INFO "PCI: Via IRQ fixup for %s, from %d to %d\n",
		       dev->slot_name, irq, new_irq);

		udelay(15);
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, new_irq);
	}
}


/*
 * PIIX3 USB: We have to disable USB interrupts that are
 * hardwired to PIRQD# and may be shared with an
 * external device.
 *
 * Legacy Support Register (LEGSUP):
 *     bit13:  USB PIRQ Enable (USBPIRQDEN),
 *     bit4:   Trap/SMI On IRQ Enable (USBSMIEN).
 *
 * We mask out all r/wc bits, too.
 */
static void __init quirk_piix3_usb(struct pci_dev *dev)
{
	u16 legsup;

	pci_read_config_word(dev, 0xc0, &legsup);
	legsup &= 0x50ef;
	pci_write_config_word(dev, 0xc0, legsup);
}

/*
 * VIA VT82C598 has its device ID settable and many BIOSes
 * set it to the ID of VT82C597 for backward compatibility.
 * We need to switch it off to be able to recognize the real
 * type of the chip.
 */
static void __init quirk_vt82c598_id(struct pci_dev *dev)
{
	pci_write_config_byte(dev, 0xfc, 0);
	pci_read_config_word(dev, PCI_DEVICE_ID, &dev->device);
}

/*
 * CardBus controllers have a legacy base address that enables them
 * to respond as i82365 pcmcia controllers.  We don't want them to
 * do this even if the Linux CardBus driver is not loaded, because
 * the Linux i82365 driver does not (and should not) handle CardBus.
 */
static void __init quirk_cardbus_legacy(struct pci_dev *dev)
{
	if ((PCI_CLASS_BRIDGE_CARDBUS << 8) ^ dev->class)
		return;
	pci_write_config_dword(dev, PCI_CB_LEGACY_MODE_BASE, 0);
}

/*
 * The AMD io apic can hang the box when an apic irq is masked.
 * We check all revs >= B0 (yet not in the pre production!) as the bug
 * is currently marked NoFix
 *
 * We have multiple reports of hangs with this chipset that went away with
 * noapic specified. For the moment we assume its the errata. We may be wrong
 * of course. However the advice is demonstrably good even if so..
 */
 
static void __init quirk_amd_ioapic(struct pci_dev *dev)
{
	u8 rev;

	pci_read_config_byte(dev, PCI_REVISION_ID, &rev);
	if(rev >= 0x02)
	{
		printk(KERN_WARNING "I/O APIC: AMD Errata #22 may be present. In the event of instability try\n");
		printk(KERN_WARNING "        : booting with the \"noapic\" option.\n");
	}
}

/*
 * Following the PCI ordering rules is optional on the AMD762. I'm not
 * sure what the designers were smoking but let's not inhale...
 *
 * To be fair to AMD, it follows the spec by default, its BIOS people
 * who turn it off!
 */
 
static void __init quirk_amd_ordering(struct pci_dev *dev)
{
	u32 pcic;
	pci_read_config_dword(dev, 0x4C, &pcic);
	if((pcic&6)!=6)
	{
		pcic |= 6;
		printk(KERN_WARNING "BIOS failed to enable PCI standards compliance, fixing this error.\n");
		pci_write_config_dword(dev, 0x4C, pcic);
		pci_read_config_dword(dev, 0x84, &pcic);
		pcic |= (1<<23);	/* Required in this mode */
		pci_write_config_dword(dev, 0x84, pcic);
	}
}

/*
 *  The main table of quirks.
 */

static struct pci_fixup pci_fixups[] __initdata = {
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_passive_release },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_passive_release },
	/*
	 * Its not totally clear which chipsets are the problematic ones
	 * We know 82C586 and 82C596 variants are affected.
	 */
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_0,	quirk_isa_dma_hangs },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C596,	quirk_isa_dma_hangs },
	{ PCI_FIXUP_FINAL,      PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82371SB_0,  quirk_isa_dma_hangs },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_868,		quirk_s3_64M },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_968,		quirk_s3_64M },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82437, 	quirk_triton }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82437VX, 	quirk_triton }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82439, 	quirk_triton }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82439TX, 	quirk_triton }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82441, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443LX_0, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443LX_1, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443BX_0, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443BX_1, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443BX_2, 	quirk_natoma },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_5597,		quirk_nopcipci },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_496,		quirk_nopcipci },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8363_0,	quirk_vialatency },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8371_1,	quirk_vialatency },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	0x3112	/* Not out yet ? */,	quirk_vialatency },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C576,	quirk_vsfx },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C597_0,	quirk_viaetbf },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C597_0,	quirk_vt82c598_id },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_3,	quirk_vt82c586_acpi },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686_4,	quirk_vt82c686_acpi },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82371AB_3,	quirk_piix4_acpi },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_AL,	PCI_DEVICE_ID_AL_M7101,		quirk_ali7101_acpi },
 	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82371SB_2,	quirk_piix3_usb },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82371AB_2,	quirk_piix3_usb },
	{ PCI_FIXUP_FINAL,	PCI_ANY_ID,		PCI_ANY_ID,			quirk_cardbus_legacy },

#ifdef CONFIG_X86_IO_APIC 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686,	quirk_via_ioapic },
#endif
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_3,	quirk_via_acpi },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686_4,	quirk_via_acpi },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_2,	quirk_via_irqpic },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686_5,	quirk_via_irqpic },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686_6,	quirk_via_irqpic },

	{ PCI_FIXUP_FINAL, 	PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_VIPER_7410,	quirk_amd_ioapic },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_FE_GATE_700C, quirk_amd_ordering },

	{ 0 }
};


static void pci_do_fixups(struct pci_dev *dev, int pass, struct pci_fixup *f)
{
	while (f->pass) {
		if (f->pass == pass &&
 		    (f->vendor == dev->vendor || f->vendor == (u16) PCI_ANY_ID) &&
 		    (f->device == dev->device || f->device == (u16) PCI_ANY_ID)) {
#ifdef DEBUG
			printk(KERN_INFO "PCI: Calling quirk %p for %s\n", f->hook, dev->slot_name);
#endif
			f->hook(dev);
		}
		f++;
	}
}

void pci_fixup_device(int pass, struct pci_dev *dev)
{
	pci_do_fixups(dev, pass, pcibios_fixups);
	pci_do_fixups(dev, pass, pci_fixups);
}
