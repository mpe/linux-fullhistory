/*
 * ioport.h	Definitions of routines for detecting, reserving and
 *		allocating system resources.
 *
 * Authors:	Linus Torvalds
 */

#ifndef _LINUX_IOPORT_H
#define _LINUX_IOPORT_H

#define DEVICE_IO_NOTSET	(~0)
#define DEVICE_IO_AUTO		((~0)-1)

#define DEVICE_IO_FLAG_WRITEABLE	(1<<0)
#define DEVICE_IO_FLAG_CACHEABLE	(1<<1)
#define DEVICE_IO_FLAG_RANGELENGTH	(1<<2)
#define DEVICE_IO_FLAG_SHADOWABLE	(1<<4)
#define DEVICE_IO_FLAG_EXPANSIONROM	(1<<5)

#define DEVICE_IO_TYPE_8BIT		0
#define DEVICE_IO_TYPE_16BIT		1
#define DEVICE_IO_TYPE_8AND16BIT	2

/*
 * Resources are tree-like, allowing
 * nesting etc..
 */
struct resource {
	const char *name;
	unsigned long start, end;
	unsigned long flags;
	unsigned char bits;		/* decoded bits */
	unsigned char fixed;		/* fixed range */
	unsigned short hw_flags;	/* hardware flags */
	unsigned short type;		/* region type */
	struct resource *parent, *sibling, *child;
};

/*
 * PCI-like IO resources have these defined flags.
 * The low four bits come directly from the PCI specs,
 * the rest are extended sw flags..
 */
#define IORESOURCE_IOPORT	0x01	/* 0 - memory mapped, 1 - IO ports */
#define IORESOURCE_MEMTYPE_MASK	0x06	/* PCI-specific mapping info */
#define IORESOURCE_PREFETCH	0x08	/* No side effects */
#define IORESOURCE_BUSY		0x10	/* Driver uses this resource */

/* PC/ISA/whatever - the normal PC address spaces: IO and memory */
extern struct resource ioport_resource;
extern struct resource iomem_resource;

extern int get_resource_list(struct resource *, char *buf, int size);

extern int request_resource(struct resource *root, struct resource *new);
extern int release_resource(struct resource *new);

/* Convenience shorthand with allocation */
#define request_region(start,n,name)	__request_region(&ioport_resource, (start), (n), (name))
extern struct resource * __request_region(struct resource *, unsigned long start, unsigned long n, const char *name);

/* Compatibility cruft */
#define check_region(start,n)	__check_region(&ioport_resource, (start), (n))
#define release_region(start,n)	__release_region(&ioport_resource, (start), (n))
extern int __check_region(struct resource *, unsigned long, unsigned long);
extern void __release_region(struct resource *, unsigned long, unsigned long);

#define get_ioport_list(buf)	get_resource_list(&ioport_resource, buf, PAGE_SIZE)
#define get_mem_list(buf)	get_resource_list(&iomem_resource, buf, PAGE_SIZE)

#define HAVE_AUTOIRQ
extern void autoirq_setup(int waittime);
extern int autoirq_report(int waittime);

#endif	/* _LINUX_IOPORT_H */
