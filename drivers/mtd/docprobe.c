
/* Linux driver for Disk-On-Chip devices    */
/* Probe routines common to all DoC devices */
/* (c) 1999 Machine Vision Holdings, Inc.   */
/* Author: David Woodhouse <dwmw2@mvhi.com> */
/* $Id: docprobe.c,v 1.10 2000/07/13 14:23:20 dwmw2 Exp $ */



/* DOC_PASSIVE_PROBE:
   In order to ensure that the BIOS checksum is correct at boot time, and 
   hence that the onboard BIOS extension gets executed, the DiskOnChip 
   goes into reset mode when it is read sequentially: all registers 
   return 0xff until the chip is woken up again by writing to the 
   DOCControl register. 

   Unfortunately, this means that the probe for the DiskOnChip is unsafe, 
   because one of the first things it does is write to where it thinks 
   the DOCControl register should be - which may well be shared memory 
   for another device. I've had machines which lock up when this is 
   attempted. Hence the possibility to do a passive probe, which will fail 
   to detect a chip in reset mode, but is at least guaranteed not to lock
   the machine.

   If you have this problem, uncomment the following line:
#define DOC_PASSIVE_PROBE
*/


#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/types.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/doc2000.h>

/* Where to look for the devices? */

#if defined (__alpha__) || defined(__i386__)
static unsigned long __initdata doc_locations[] = { 
		0xc8000, 0xca000, 0xcc000, 0xce000, 
		0xd0000, 0xd2000, 0xd4000, 0xd6000,
		0xd8000, 0xda000, 0xdc000, 0xde000, 
		0xe0000, 0xe2000, 0xe4000, 0xe6000, 
		0xe8000, 0xea000, 0xec000, 0xee000, 0 };
#elif defined(__ppc__)
static unsigned long __initdata doc_locations[] = {
		0xe4000000, 0};
#else 
#warning Unknown architecture for DiskOnChip. No default probe locations defined
#endif


/* doccheck: Probe a given memory window to see if there's a DiskOnChip present */

static inline int __init doccheck(unsigned long potential, unsigned long physadr)
{
	unsigned long window=potential;
	unsigned char tmp, ChipID;
#ifndef DOC_PASSIVE_PROBE
	unsigned char tmp2;
#endif

	/* Routine copied from the Linux DOC driver */
	
	/* Check for 0x55 0xAA signature at beginning of window */
	if (ReadDOC(window, Sig1) != 0x55 || ReadDOC(window, Sig2) != 0xaa)
		return 0;

#ifndef DOC_PASSIVE_PROBE	
	/* It's not possible to cleanly detect the DiskOnChip - the
	 * bootup procedure will put the device into reset mode, and
	 * it's not possible to talk to it without actually writing
	 * to the DOCControl register. So we store the current contents
	 * of the DOCControl register's location, in case we later decide
	 * that it's not a DiskOnChip, and want to put it back how we
	 * found it. 
	 */
	tmp2 = ReadDOC(window, DOCControl);
	
	/* Reset the DiskOnChip ASIC */
	WriteDOC(DOC_MODE_CLR_ERR | DOC_MODE_MDWREN | DOC_MODE_RESET, 
		 window, DOCControl);
	WriteDOC(DOC_MODE_CLR_ERR | DOC_MODE_MDWREN | DOC_MODE_RESET, 
		 window, DOCControl);
	
	/* Enable the DiskOnChip ASIC */
	WriteDOC(DOC_MODE_CLR_ERR | DOC_MODE_MDWREN | DOC_MODE_NORMAL, 
		 window, DOCControl);
	WriteDOC(DOC_MODE_CLR_ERR | DOC_MODE_MDWREN | DOC_MODE_NORMAL, 
		 window, DOCControl);
#endif /* !DOC_PASSIVE_PROBE */	

	ChipID = ReadDOC(window, ChipID);
  
	switch (ChipID) {
	case DOC_ChipID_Doc2k:
		/* Check the TOGGLE bit in the ECC register */
		tmp = ReadDOC(window, 2k_ECCStatus) & DOC_TOGGLE_BIT;
		if ((ReadDOC(window, 2k_ECCStatus) & DOC_TOGGLE_BIT) != tmp)
				return ChipID;
		break;
		
	case DOC_ChipID_DocMil:
		/* Check the TOGGLE bit in the ECC register */
		tmp = ReadDOC(window, ECCConf) & DOC_TOGGLE_BIT;
		if ((ReadDOC(window, ECCConf) & DOC_TOGGLE_BIT) != tmp)
				return ChipID;
		break;
		
	default:
		printk(KERN_WARNING "Possible DiskOnChip with unknown ChipID %2.2X found at 0x%lx\n",
		       ChipID, physadr);

#ifndef DOC_PASSIVE_PROBE
		/* Put back the contents of the DOCControl register, in case it's not
		 * actually a DiskOnChip.
		 */
		WriteDOC(tmp2, window, DOCControl);
#endif
		return 0;
	}

	printk(KERN_WARNING "DiskOnChip failed TOGGLE test, dropping.\n");

#ifndef DOC_PASSIVE_PROBE
	/* Put back the contents of the DOCControl register: it's not a DiskOnChip */
	WriteDOC(tmp2, window, DOCControl);
#endif
	return 0;
}   


static void DoC_Probe(unsigned long physadr)
{
	unsigned long docptr;
	struct DiskOnChip *this;
	struct mtd_info *mtd;
	int ChipID;
	char namebuf[15];
	char *name = namebuf;
	char *im_funcname = NULL;
	char *im_modname = NULL;
	void (*initroutine)(struct mtd_info *) = NULL;

	docptr = (unsigned long)ioremap(physadr, 0x2000);
	
	if (!docptr)
		return;
	
	if ((ChipID = doccheck(docptr, physadr))) {
		
		mtd = kmalloc(sizeof(struct DiskOnChip) + sizeof(struct mtd_info), GFP_KERNEL);

		if (!mtd) {
			printk("Cannot allocate memory for data structures. Dropping.\n");
			iounmap((void *)docptr);
			return;
		}
		
		this = (struct DiskOnChip *)(&mtd[1]);
		
		memset((char *)mtd,0, sizeof(struct mtd_info));
		memset((char *)this, 0, sizeof(struct DiskOnChip));

		mtd->priv = this;
		this->virtadr = docptr;
		this->physadr = physadr;
		this->ChipID = ChipID;
		sprintf(namebuf, "with ChipID %2.2X", ChipID);
		
		switch(ChipID) {
		case DOC_ChipID_Doc2k:
			name="2000";
			im_funcname = "DoC2k_init";
			im_modname = "doc2000";
			break;

		case DOC_ChipID_DocMil:
			name="Millennium";
			im_funcname = "DoCMil_init";
			im_modname = "doc2001";
			break;
		}
		if (im_funcname)
			initroutine = inter_module_get_request(im_funcname, im_modname);
		if (initroutine) {
			(*initroutine)(mtd);
			inter_module_put(im_funcname);
			return;
		}
		printk("Cannot find driver for DiskOnChip %s at 0x%X\n", name, physadr);
	}
	iounmap((void *)docptr);
}


/****************************************************************************
 *
 * Module stuff
 *
 ****************************************************************************/

#if LINUX_VERSION_CODE < 0x20300
#ifdef MODULE
#define init_doc init_module
#endif
#define __exit
#endif

int __init init_doc(void)
{
	int i;
	
	printk(KERN_NOTICE "M-Systems DiskOnChip driver. (C) 1999 Machine Vision Holdings, Inc.\n");
#ifdef PRERELEASE
	printk(KERN_INFO "$Id: docprobe.c,v 1.10 2000/07/13 14:23:20 dwmw2 Exp $\n");
#endif
	
	for (i=0; doc_locations[i]; i++) {
		DoC_Probe(doc_locations[i]);
	}
	
	return 0;
	
}


#if LINUX_VERSION_CODE > 0x20300
module_init(init_doc);
#endif

