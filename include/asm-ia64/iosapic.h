#ifndef __ASM_IA64_IOSAPIC_H
#define __ASM_IA64_IOSAPIC_H

#define	IOSAPIC_DEFAULT_ADDR	0xFEC00000

#define	IOSAPIC_REG_SELECT	0x0
#define	IOSAPIC_WINDOW		0x10
#define	IOSAPIC_EOI		0x40

#define	IOSAPIC_VERSION	0x1

/*
 * Redirection table entry
 */
#define	IOSAPIC_RTE_LOW(i)	(0x10+i*2)
#define	IOSAPIC_RTE_HIGH(i)	(0x11+i*2)

#define	IOSAPIC_DEST_SHIFT		16

/*
 * Delivery mode
 */
#define	IOSAPIC_DELIVERY_SHIFT		8
#define	IOSAPIC_FIXED			0x0
#define	IOSAPIC_LOWEST_PRIORITY	0x1
#define	IOSAPIC_PMI			0x2
#define	IOSAPIC_NMI			0x4
#define	IOSAPIC_INIT			0x5
#define	IOSAPIC_EXTINT			0x7

/*
 * Interrupt polarity
 */
#define	IOSAPIC_POLARITY_SHIFT		13
#define	IOSAPIC_POL_HIGH		0
#define	IOSAPIC_POL_LOW		1

/*
 * Trigger mode
 */
#define	IOSAPIC_TRIGGER_SHIFT		15
#define	IOSAPIC_EDGE			0
#define	IOSAPIC_LEVEL			1

/*
 * Mask bit
 */
#define	IOSAPIC_MASK_SHIFT		16
#define	IOSAPIC_UNMASK			0
#define	IOSAPIC_MSAK			1

#ifndef __ASSEMBLY__

#ifdef CONFIG_IOSAPIC
extern void __init iosapic_system_init (int pcat_compat);
extern void __init iosapic_init (unsigned long address,
				    unsigned int gsi_base);
extern int gsi_to_vector (unsigned int gsi);
extern int gsi_to_irq (unsigned int gsi);
extern void __init iosapic_parse_prt (void);
extern void iosapic_enable_intr (unsigned int vector);
extern int iosapic_register_intr (unsigned int gsi, unsigned long polarity,
				  unsigned long trigger);
extern void __init iosapic_override_isa_irq (unsigned int isa_irq, unsigned int gsi,
				      unsigned long polarity,
				      unsigned long trigger);
extern int __init iosapic_register_platform_intr (u32 int_type,
					   unsigned int gsi,
					   int pmi_vector,
					   u16 eid, u16 id,
					   unsigned long polarity,
					   unsigned long trigger);
extern unsigned int iosapic_version (char *addr);

extern void iosapic_pci_fixup (int);
#else
#define iosapic_system_init(pcat_compat)			do { } while (0)
#define iosapic_init(address,gsi_base)				do { } while (0)
#define iosapic_register_intr(gsi,polarity,trigger)		(gsi)
#define iosapic_override_isa_irq(isa_irq,gsi,polarity,trigger)	do { } while (0)
#define iosapic_register_platform_intr(type,gsi,pmi,eid,id, \
	polarity,trigger)					(gsi)
#endif

# endif /* !__ASSEMBLY__ */
#endif /* __ASM_IA64_IOSAPIC_H */
