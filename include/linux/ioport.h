/*
 * ioport.h	Definitions of routines for detecting, reserving and
 *		allocating system resources.
 *
 * Authors:	Linus Torvalds
 */

#ifndef _LINUX_IOPORT_H
#define _LINUX_IOPORT_H

/*
 * Resources are tree-like, allowing
 * nesting etc..
 */
struct resource {
	const char *name;
	unsigned long start, end;
	unsigned long flags;
	struct resource *parent, *sibling, *child;
};

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
