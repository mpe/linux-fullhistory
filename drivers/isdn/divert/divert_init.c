/* 
 * $Id: divert_init.c,v 1.3 1999/07/05 20:21:39 werner Exp $
 *
 * Module init for DSS1 diversion services for i4l.
 *
 * Copyright 1999       by Werner Cornelius (werner@isdn4linux.de)
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log: divert_init.c,v $
 * Revision 1.3  1999/07/05 20:21:39  werner
 * changes to use diversion sources for all kernel versions.
 * removed static device, only proc filesystem used
 *
 * Revision 1.2  1999/07/04 21:37:30  werner
 * Ported from kernel version 2.0
 *
 *
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include "isdn_divert.h"

/********************/
/* needed externals */
/********************/
extern int printk(const char *fmt,...);

/****************************************/
/* structure containing interface to hl */
/****************************************/
isdn_divert_if divert_if =
  { DIVERT_IF_MAGIC,  /* magic value */
    DIVERT_CMD_REG,   /* register cmd */
    ll_callback,      /* callback routine from ll */
    NULL,             /* command still not specified */
    NULL,             /* drv_to_name */
    NULL,             /* name_to_drv */
  };

/*************************/
/* Module interface code */
/* no cmd line parms     */
/*************************/
int init_module(void)
{ int i;

  if (divert_dev_init())
   { printk(KERN_WARNING "dss1_divert: cannot install device, not loaded\n");
     return(-EIO);
   }
  if ((i = DIVERT_REG_NAME(&divert_if)) != DIVERT_NO_ERR)
   { divert_dev_deinit();
     printk(KERN_WARNING "dss1_divert: error %d registering module, not loaded\n",i);
     return(-EIO);
   } 
#if (LINUX_VERSION_CODE < 0x020111)
  register_symtab(0);
#endif
  printk(KERN_INFO "dss1_divert module successfully installed\n");
  return(0);
} /* init_module */

/**********************/
/* Module deinit code */
/**********************/
void cleanup_module(void)
{ int flags;
  int i;

  save_flags(flags);
  cli();
  divert_if.cmd = DIVERT_CMD_REL; /* release */
  if ((i = DIVERT_REG_NAME(&divert_if)) != DIVERT_NO_ERR)
   { printk(KERN_WARNING "dss1_divert: error %d releasing module\n",i);
     restore_flags(flags);
     return;
   } 
  if (divert_dev_deinit()) 
   { printk(KERN_WARNING "dss1_divert: device busy, remove cancelled\n");
     restore_flags(flags);
     return;
   }
  restore_flags(flags);
  deleterule(-1); /* delete all rules and free mem */
  deleteprocs();
  printk(KERN_INFO "dss1_divert module successfully removed \n");
} /* cleanup_module */


