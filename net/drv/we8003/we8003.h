/*
 * we8003.h	Define the interface of the WE8003 Ethernet driver.
 *
 * Version:	@(#)we8003.h	1.0.0	(02/11/93)
 *
 * Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 */
#ifndef _LINUX_WE8003_H
#define _LINUX_WE8003_H


#define CONF_WE8003	0		/* add this driver to kernel	*/
#define FORCE_8BIT	0		/* for forcing the WD8003 mode	*/


#define	WE_DEBUG
#ifdef	WE_DEBUG
#   define PRINTK(x)	printk x
#else
#   define PRINTK(x)	/**/
#endif


#define	NR_WE8003	4		/* max number of units		*/
#define WE_NAME		"WE8003.%d"	/* our DDI ID string		*/


extern struct ddi_device *we_ptrs[NR_WE8003];	/* pointers to DDI blocks	*/


extern int	we8003_init(struct ddi_device *dev);

#endif	/* _LINUX_WE8003_H */
