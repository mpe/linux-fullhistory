/*********************************************************************
 *                
 * Filename:      irvtd.c
 * Version:       
 * Description:   A virtual tty driver implementaion,
 *                which also may be called as "Port Emulation Entity"
 *                in IrCOMM specification.
 * Status:        Experimental.
 * Author:        Takahide Higuchi <thiguchi@pluto.dti.ne.jp>
 * Source:        irlpt.c
 *
 *     Copyright (c) 1998, Takahide Higuchi, <thiguchi@pluto.dti.ne.jp>,
 *     All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     I, Takahide Higuchi, provide no warranty for any of this software.
 *     This material is provided "AS-IS" and at no charge.
 *
 ********************************************************************/

/* #include <linux/module.h> */

#include <linux/init.h>

#include <net/irda/irda.h>
#include <net/irda/irlmp.h>

#include <net/irda/irvtd.h>
#include <net/irda/irvtd_driver.h>

struct irvtd_cb **irvtd = NULL;
extern struct ircomm_cb **ircomm;

#if 0
static char *rcsid = "$Id: irvtd.c,v 1.2 1998/09/27 08:37:04 takahide Exp $";
#endif
static char *version = "IrVTD, $Revision: 1.2 $ $Date: 1998/09/27 08:37:04 $ (Takahide Higuchi)";


/************************************************************
 *    init & cleanup this module 
 ************************************************************/

/*
 * Function init_module(void)
 *
 *   Initializes the ircomm control structure
 *   This Function is called when you do insmod.
 */

__initfunc(int irvtd_init(void))
{
	int i;

	DEBUG( 4, "irvtd:init_module:\n");
	printk( KERN_INFO "%s\n", version);

	/* we allocate master array */

	irvtd = (struct irvtd_cb **) kmalloc( sizeof(void *) *
						COMM_MAX_TTY,GFP_KERNEL);
	if ( irvtd == NULL) {
		printk( KERN_WARNING "irvtd: Can't allocate array!\n");
		return -ENOMEM;
	}

	memset( irvtd, 0, sizeof(void *) * COMM_MAX_TTY);


	/* we initialize structure */

	for (i=0; i < COMM_MAX_TTY; i++){
		irvtd[i] = kmalloc( sizeof(struct irvtd_cb), GFP_KERNEL);	
		if(irvtd[i] == NULL){
			printk(KERN_ERR "ircomm_open(): kmalloc failed!\n");
			return -ENOMEM;
		}

		memset( irvtd[i], 0, sizeof(struct irvtd_cb));
		irvtd[i]->magic = IRVTD_MAGIC;
	}

	/* 
	 * initialize a "port emulation entity"
	 */

	if(irvtd_register_ttydriver()){
		printk( KERN_WARNING "IrCOMM: Error in ircomm_register_device\n");
		return -ENODEV;
	}


	DEBUG( 4, "irvtd:init_module:done\n");
	return 0;
}

void irvtd_cleanup(void)
{
	int i;
	DEBUG( 4, "--> ircomm:cleanup_module\n");

	/*
	 * free some resources
	 */
	if (irvtd) {
		for (i=0; i<COMM_MAX_TTY; i++) {
			if (irvtd[i]) {
				DEBUG( 4, "freeing structures\n");
				/* irvtd_close();  :{| */
				kfree(irvtd[i]);
				irvtd[i] = NULL;
			}
		}
		DEBUG( 4, "freeing master array\n");
		kfree(irvtd);
		irvtd = NULL;
	}



	DEBUG( 0, "unregister_ttydriver..\n");
 	irvtd_unregister_ttydriver();

	DEBUG( 4, "ircomm:cleanup_module -->\n");
}

#ifdef MODULE

int init_module(void) 
{
	irvtd_init();
	return 0;
}


/*
 * Function ircomm_cleanup (void)
 *   This is called when you rmmod.
 */

void cleanup_module(void)
{
	irvtd_cleanup();
}

#endif /* MODULE */



