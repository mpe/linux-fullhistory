/*
 * Advanced Configuration and Power Interface 
 *
 * Based on 'ACPI Specification 1.0b' February 2, 1999 and 
 * 'IA-64 Extensions to ACPI Specification' Revision 0.6
 * 
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999,2000 Walt Drummond <drummond@valinux.com>
 */

#include <linux/config.h>

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/acpi-ext.h>
#include <asm/page.h>
#include <asm/efi.h>
#include <asm/io.h>
#include <asm/iosapic.h>

#undef ACPI_DEBUG		/* Guess what this does? */

#ifdef CONFIG_SMP
extern unsigned long ipi_base_addr;
#endif

/* These are ugly but will be reclaimed by the kernel */
int __initdata acpi_cpus = 0;	
int __initdata acpi_apic_map[32];
int __initdata cpu_cnt = 0;

void (*pm_idle) (void);

/*
 * Identify usable CPU's and remember them for SMP bringup later.
 */
static void __init
acpi_lsapic(char *p) 
{
	int add = 1;

	acpi_entry_lsapic_t *lsapic = (acpi_entry_lsapic_t *) p;

	if ((lsapic->flags & LSAPIC_PRESENT) == 0) 
		return;

	printk("      CPU %d (%.04x:%.04x): ", cpu_cnt, lsapic->eid, lsapic->id);

	if ((lsapic->flags & LSAPIC_ENABLED) == 0) {
		printk("Disabled.\n");
		add = 0;
	} else if (lsapic->flags & LSAPIC_PERFORMANCE_RESTRICTED) {
		printk("Performance Restricted; ignoring.\n");
		add = 0;
	}
	
	if (add) {
		printk("Available.\n");
		acpi_cpus++;
		acpi_apic_map[cpu_cnt] = (lsapic->id << 8) | lsapic->eid;
	}
	
	cpu_cnt++;
}

/*
 * Find all IOSAPICs and tag the iosapic_vector structure with the appropriate 
 * base addresses.
 */
static void __init
acpi_iosapic(char *p) 
{
	/*
	 * This is not good.  ACPI is not necessarily limited to CONFIG_IA64_SV, yet
	 * ACPI does not necessarily imply IOSAPIC either.  Perhaps there should be
	 * a means for platform_setup() to register ACPI handlers?
	 */
#ifdef CONFIG_IA64_DIG
	acpi_entry_iosapic_t *iosapic = (acpi_entry_iosapic_t *) p;
	unsigned int ver, v;
	int l, pins;

	ver = iosapic_version(iosapic->address);
	pins = (ver >> 16) & 0xff;
	
	printk("IOSAPIC Version %x.%x: address 0x%lx IRQs 0x%x - 0x%x\n", 
	       (ver & 0xf0) >> 4, (ver & 0x0f), iosapic->address, 
	       iosapic->irq_base, iosapic->irq_base + pins);
	
	for (l = 0; l < pins; l++) {
		v = iosapic->irq_base + l;
		if (v < 16)
			v = isa_irq_to_vector(v);
		if (v > IA64_MAX_VECTORED_IRQ) {
			printk("    !!! bad IOSAPIC interrupt vector: %u\n", v);
			continue;
		}
		/* XXX Check for IOSAPIC collisions */
		iosapic_addr(v) = (unsigned long) ioremap(iosapic->address, 0);
		iosapic_baseirq(v) = iosapic->irq_base;
	}
	iosapic_init(iosapic->address);
#endif
}


/*
 * Configure legacy IRQ information in iosapic_vector
 */
static void __init
acpi_legacy_irq(char *p)
{
	/*
	 * This is not good.  ACPI is not necessarily limited to CONFIG_IA64_DIG, yet
	 * ACPI does not necessarily imply IOSAPIC either.  Perhaps there should be
	 * a means for platform_setup() to register ACPI handlers?
	 */
#ifdef CONFIG_IA64_IRQ_ACPI
	acpi_entry_int_override_t *legacy = (acpi_entry_int_override_t *) p;
	unsigned char vector; 
	int i;

	vector = isa_irq_to_vector(legacy->isa_irq);

	/*
	 * Clobber any old pin mapping.  It may be that it gets replaced later on
	 */
	for (i = 0; i < IA64_MAX_VECTORED_IRQ; i++) {
		if (i == vector) 
			continue;
		if (iosapic_pin(i) == iosapic_pin(vector))
			iosapic_pin(i) = 0xff;
        }

	iosapic_pin(vector) = legacy->pin;
	iosapic_bus(vector) = BUS_ISA;	/* This table only overrides the ISA devices */
	iosapic_busdata(vector) = 0;
	
	/* 
	 * External timer tick is special... 
	 */
	if (vector != TIMER_IRQ)
		iosapic_dmode(vector) = IO_SAPIC_LOWEST_PRIORITY;
	else 
		iosapic_dmode(vector) = IO_SAPIC_FIXED;
	
	/* See MPS 1.4 section 4.3.4 */
	switch (legacy->flags) {
	case 0x5:
		iosapic_polarity(vector) = IO_SAPIC_POL_HIGH;
		iosapic_trigger(vector) = IO_SAPIC_EDGE;
		break;
	case 0x8:
		iosapic_polarity(vector) = IO_SAPIC_POL_LOW;
		iosapic_trigger(vector) = IO_SAPIC_EDGE;
		break;
	case 0xd:
		iosapic_polarity(vector) = IO_SAPIC_POL_HIGH;
		iosapic_trigger(vector) = IO_SAPIC_LEVEL;
		break;
	case 0xf:
		iosapic_polarity(vector) = IO_SAPIC_POL_LOW;
		iosapic_trigger(vector) = IO_SAPIC_LEVEL;
		break;
	default:
		printk("    ACPI Legacy IRQ 0x%02x: Unknown flags 0x%x\n", legacy->isa_irq,
		       legacy->flags);
		break;
	}

#ifdef ACPI_DEBUG
	printk("Legacy ISA IRQ %x -> IA64 Vector %x IOSAPIC Pin %x Active %s %s Trigger\n", 
	       legacy->isa_irq, vector, iosapic_pin(vector), 
	       ((iosapic_polarity(vector) == IO_SAPIC_POL_LOW) ? "Low" : "High"),
	       ((iosapic_trigger(vector) == IO_SAPIC_LEVEL) ? "Level" : "Edge"));
#endif /* ACPI_DEBUG */

#endif /* CONFIG_IA64_IRQ_ACPI */
}

/*
 * Info on platform interrupt sources: NMI. PMI, INIT, etc.
 */
static void __init
acpi_platform(char *p)
{
	acpi_entry_platform_src_t *plat = (acpi_entry_platform_src_t *) p;

	printk("PLATFORM: IOSAPIC %x -> Vector %lx on CPU %.04u:%.04u\n",
	       plat->iosapic_vector, plat->global_vector, plat->eid, plat->id);
}

/*
 * Parse the ACPI Multiple SAPIC Table
 */
static void __init
acpi_parse_msapic(acpi_sapic_t *msapic)
{
	char *p, *end;

	memset(&acpi_apic_map, -1, sizeof(acpi_apic_map));

#ifdef CONFIG_SMP
	/* Base address of IPI Message Block */
	ipi_base_addr = ioremap(msapic->interrupt_block, 0);
#endif
	
	p = (char *) (msapic + 1);
	end = p + (msapic->header.length - sizeof(acpi_sapic_t));

	while (p < end) {
		
		switch (*p) {
		case ACPI_ENTRY_LOCAL_SAPIC:
			acpi_lsapic(p);
			break;
	
		case ACPI_ENTRY_IO_SAPIC:
			acpi_iosapic(p);
			break;

		case ACPI_ENTRY_INT_SRC_OVERRIDE:
			acpi_legacy_irq(p);
			break;
		
		case ACPI_ENTRY_PLATFORM_INT_SOURCE:
			acpi_platform(p);
			break;
		
		default:
			break;
		}

		/* Move to next table entry. */
		p += *(p + 1);
	}

	/* Make bootup pretty */
	printk("      %d CPUs available, %d CPUs total\n", acpi_cpus, cpu_cnt);
}

int __init 
acpi_parse(acpi_rsdp_t *rsdp)
{
	acpi_rsdt_t *rsdt;
	acpi_desc_table_hdr_t *hdrp;
	long tables, i;

	if (!rsdp) {
		printk("Uh-oh, no ACPI Root System Description Pointer table!\n");
		return 0;
	}

	if (strncmp(rsdp->signature, ACPI_RSDP_SIG, ACPI_RSDP_SIG_LEN)) {
		printk("Uh-oh, ACPI RSDP signature incorrect!\n");
		return 0;
	}

	rsdp->rsdt = __va(rsdp->rsdt);
	rsdt = rsdp->rsdt;
	if (strncmp(rsdt->header.signature, ACPI_RSDT_SIG, ACPI_RSDT_SIG_LEN)) {
		printk("Uh-oh, ACPI RDST signature incorrect!\n");
		return 0;
	}

	printk("ACPI: %.6s %.8s %d.%d\n", rsdt->header.oem_id, rsdt->header.oem_table_id, 
	       rsdt->header.oem_revision >> 16, rsdt->header.oem_revision & 0xffff);
	
	tables = (rsdt->header.length - sizeof(acpi_desc_table_hdr_t)) / 8;
	for (i = 0; i < tables; i++) {
		hdrp = (acpi_desc_table_hdr_t *) __va(rsdt->entry_ptrs[i]);

		/* Only interested int the MSAPIC table for now ... */
		if (strncmp(hdrp->signature, ACPI_SAPIC_SIG, ACPI_SAPIC_SIG_LEN) != 0)
			continue;

		acpi_parse_msapic((acpi_sapic_t *) hdrp);
	} /* while() */

	if (acpi_cpus == 0) {
		printk("ACPI: Found 0 CPUS; assuming 1\n");
		acpi_cpus = 1; /* We've got at least one of these, no? */
	}
	return 1;
}

const char *
acpi_get_sysname (void)
{       
	/* the following should go away once we have an ACPI parser: */
#ifdef CONFIG_IA64_GENERIC
	return "hpsim";
#else
# if defined (CONFIG_IA64_HP_SIM)
	return "hpsim";
# elif defined (CONFIG_IA64_SGI_SN1_SIM)
	return "sn1";
# elif defined (CONFIG_IA64_DIG)
	return "dig";
# else
#	error Unknown platform.  Fix acpi.c.
# endif
#endif
}        
