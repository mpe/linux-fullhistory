/* $Id: devops.c,v 1.1 1996/12/27 08:49:11 jj Exp $
 * devops.c:  Device operations using the PROM.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/openprom.h>
#include <asm/oplib.h>

/* Open the device described by the string 'dstr'.  Returns the handle
 * to that device used for subsequent operations on that device.
 * Returns -1 on failure.
 */
int
prom_devopen(char *dstr)
{
	return (*prom_command)("open", P1275_ARG(0,P1275_ARG_IN_STRING)|
				       P1275_INOUT(1,1),
				       dstr);
}

/* Close the device described by device handle 'dhandle'. */
int
prom_devclose(int dhandle)
{
	(*prom_command)("close", P1275_INOUT(1,0), dhandle);
	return 0;
}

/* Seek to specified location described by 'seekhi' and 'seeklo'
 * for device 'dhandle'.
 */
void
prom_seek(int dhandle, unsigned int seekhi, unsigned int seeklo)
{
	(*prom_command)("seek", P1275_INOUT(3,1), dhandle, seekhi, seeklo);
}
