#ifndef __ASM_IA64_IOSAPIC_H
#define __ASM_IA64_IOSAPIC_H

#include <linux/config.h>

#define	IO_SAPIC_DEFAULT_ADDR	0xFEC00000

#define	IO_SAPIC_REG_SELECT	0x0
#define	IO_SAPIC_WINDOW		0x10
#define	IO_SAPIC_EOI		0x40

#define	IO_SAPIC_VERSION	0x1

/*
 * Redirection table entry
 */

#define	IO_SAPIC_RTE_LOW(i)	(0x10+i*2)
#define	IO_SAPIC_RTE_HIGH(i)	(0x11+i*2)


#define	IO_SAPIC_DEST_SHIFT		16

/*
 * Delivery mode
 */

#define	IO_SAPIC_DELIVERY_SHIFT		8
#define	IO_SAPIC_FIXED			0x0
#define	IO_SAPIC_LOWEST_PRIORITY	0x1
#define	IO_SAPIC_PMI			0x2
#define	IO_SAPIC_NMI			0x4
#define	IO_SAPIC_INIT			0x5
#define	IO_SAPIC_EXTINT			0x7

/*
 * Interrupt polarity
 */

#define	IO_SAPIC_POLARITY_SHIFT		13
#define	IO_SAPIC_POL_HIGH		0
#define	IO_SAPIC_POL_LOW		1

/*
 * Trigger mode
 */

#define	IO_SAPIC_TRIGGER_SHIFT		15
#define	IO_SAPIC_EDGE			0
#define	IO_SAPIC_LEVEL			1

/*
 * Mask bit
 */

#define	IO_SAPIC_MASK_SHIFT		16
#define	IO_SAPIC_UNMASK			0
#define	IO_SAPIC_MSAK			1

/*
 * Bus types
 */
#define  BUS_ISA         0               /* ISA Bus */
#define  BUS_PCI         1               /* PCI Bus */

#ifndef CONFIG_IA64_PCI_FIRMWARE_IRQ
struct intr_routing_entry {
      unsigned char srcbus;
      unsigned char srcbusno;
      unsigned char srcbusirq;
      unsigned char iosapic_pin;
      unsigned char dstiosapic;
      unsigned char mode;
      unsigned char trigger;
      unsigned char polarity;
};

extern        struct  intr_routing_entry      intr_routing[];
#endif

#ifndef __ASSEMBLY__

#include <asm/irq.h>

/*
 * IOSAPIC Version Register return 32 bit structure like:
 * {
 *	unsigned int version   : 8;
 *	unsigned int reserved1 : 8;
 *	unsigned int pins      : 8;
 *	unsigned int reserved2 : 8;
 * }
 */
extern unsigned int iosapic_version(unsigned long);
extern void iosapic_init(unsigned long);

struct iosapic_vector {
	unsigned long iosapic_base; /* IOSAPIC Base address */
        char pin;		    /* IOSAPIC pin (-1 == No data) */
	unsigned char bus;	    /* Bus number */
	unsigned char baseirq;	    /* Base IRQ handled by this IOSAPIC */
	unsigned char bustype;	    /* Bus type (ISA, PCI, etc) */
	unsigned int busdata;	    /* Bus specific ID */
        /* These bitfields use the values defined above */
	unsigned char dmode        : 3;
	unsigned char polarity     : 1;
	unsigned char trigger      : 1; 
	unsigned char UNUSED       : 3;
};
extern struct iosapic_vector iosapic_vector[NR_IRQS];

#define iosapic_addr(v)     iosapic_vector[v].iosapic_base
#define iosapic_pin(v)      iosapic_vector[v].pin
#define iosapic_bus(v)      iosapic_vector[v].bus
#define iosapic_baseirq(v)  iosapic_vector[v].baseirq
#define iosapic_bustype(v)  iosapic_vector[v].bustype
#define iosapic_busdata(v)  iosapic_vector[v].busdata
#define iosapic_dmode(v)    iosapic_vector[v].dmode
#define iosapic_trigger(v)  iosapic_vector[v].trigger
#define iosapic_polarity(v) iosapic_vector[v].polarity

# endif /* !__ASSEMBLY__ */
#endif /* __ASM_IA64_IOSAPIC_H */
