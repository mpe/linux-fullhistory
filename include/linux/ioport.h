/*
 * ioport.h	Definitions of routines for detecting, reserving and
 *		allocating system resources.
 *
 * Authors:	Donald Becker (becker@cesdis.gsfc.nasa.gov)
 *		David Hinds (dhinds@zen.stanford.edu)
 */

#ifndef _LINUX_IOPORT_H
#define _LINUX_IOPORT_H

#define RES_IO		0
#define RES_MEM		1

extern void reserve_setup(char *str, int *ints);

extern struct resource_entry *iolist, *memlist;

extern int get_resource_list(int class, char *buf);
extern int check_resource(int class,
			  unsigned long from, unsigned long extent);
extern void request_resource(int class,
			     unsigned long from, unsigned long extent,
			     const char *name);
extern void release_resource(int class,
			     unsigned long from, unsigned long extent);
extern unsigned long occupy_resource(int class,
				     unsigned long base, unsigned long end,
				     unsigned long num, unsigned long align,
				     const char *name);
extern void vacate_resource(int class,
			    unsigned long from, unsigned long extent);

#define get_ioport_list(buf)	get_resource_list(RES_IO, buf)
#define get_mem_list(buf)	get_resource_list(RES_MEM, buf)

#define HAVE_PORTRESERVE
/*
 * Call check_region() before probing for your hardware.
 * Once you have found you hardware, register it with request_region().
 * If you unload the driver, use release_region to free ports.
 */
#define check_region(f,e)		check_resource(RES_IO,f,e)
#define request_region(f,e,n)		request_resource(RES_IO,f,e,n)
#define release_region(f,e)		release_resource(RES_IO,f,e)
#define occupy_region(b,e,n,a,s)	occupy_resource(RES_IO,b,e,n,a,s)
#define vacate_region(f,e)		vacate_resource(RES_IO,f,e)

#define HAVE_MEMRESERVE
#define check_mem_region(f,e)		check_resource(RES_MEM,f,e)
#define request_mem_region(f,e,n)	request_resource(RES_MEM,f,e,n)
#define release_mem_region(f,e)		release_resource(RES_MEM,f,e)
#define occupy_mem_region(b,e,n,a,s)	occupy_resource(RES_MEM,b,e,n,a,s)
#define vacate_mem_region(f,e)		vacate_resource(RES_MEM,f,e)

#define HAVE_AUTOIRQ
extern void autoirq_setup(int waittime);
extern int autoirq_report(int waittime);

#endif	/* _LINUX_IOPORT_H */
