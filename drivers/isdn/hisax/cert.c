/* $Id: cert.c,v 2.2 1999/08/07 17:35:05 keil Exp $

 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *		This file is (c) under GNU PUBLIC LICENSE
 *		For changes and modifications please read
 *		../../../Documentation/isdn/HiSax.cert
 *
 * $Log: cert.c,v $
 * Revision 2.2  1999/08/07 17:35:05  keil
 * approval for Eicon Technology Diva 2.01 PCI
 *
 * Revision 2.1  1998/11/15 23:51:15  keil
 * certification stuff
 *
 * Revision 1.2.2.1  1998/11/03 21:46:37  keil
 * first version
 *
 *
 */
 
#include <linux/kernel.h>

int
certification_check(int output) {

#ifdef CERTIFICATION
#if CERTIFICATION == 0
	if (output) {
		printk(KERN_INFO "HiSax: Approval certification valid\n");
		printk(KERN_INFO "HiSax: Approved with ELSA Quickstep series cards\n");
		printk(KERN_INFO "HiSax: Approval registration numbers:\n");
		printk(KERN_INFO "HiSax: German D133361J CETECOM ICT Services GmbH\n");
		printk(KERN_INFO "HiSax: EU (D133362J) CETECOM ICT Services GmbH\n");
		printk(KERN_INFO "HiSax: Approved with Eicon Technology Diva 2.01 PCI cards\n");
	}
	return(0);
#endif
#if CERTIFICATION == 1
	if (output) {
		printk(KERN_INFO "HiSax: Approval certification failed because of\n");
		printk(KERN_INFO "HiSax: unauthorized source code changes\n");
	}
	return(1);
#endif
#if CERTIFICATION == 127
	if (output) {
		printk(KERN_INFO "HiSax: Approval certification not possible\n");
		printk(KERN_INFO "HiSax: because \"md5sum\" is not available\n");
	}
	return(2);
#endif
#else
	if (output) {
		printk(KERN_INFO "HiSax: Certification not verified\n");
	}
	return(3);
#endif
}
