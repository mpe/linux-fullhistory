/* $Id: devops.c,v 1.3 1995/11/25 00:59:59 davem Exp $
 * devops.c:  Device operations using the PROM.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/openprom.h>
#include <asm/oplib.h>

/* Open the device described by the string 'dstr'.  Returns the handle
 * to that device used for subsequent operations on that device.
 * Returns -1 on failure.
 */
int
prom_devopen(char *dstr)
{
	int handle;
	switch(prom_vers) {
	case PROM_V0:
		handle = (*(romvec->pv_v0devops.v0_devopen))(dstr);
		if(handle == 0) return -1;
		return handle;
		break;
	case PROM_V2:
	case PROM_V3:
	case PROM_P1275:
		handle = (*(romvec->pv_v2devops.v2_dev_open))(dstr);
		return handle;
		break;
	};

	return -1;
}

/* Close the device described by device handle 'dhandle'. */
void
prom_close(int dhandle)
{
	switch(prom_vers) {
	case PROM_V0:
		(*(romvec->pv_v0devops.v0_devclose))(dhandle);
		return;
	case PROM_V2:
	case PROM_V3:
	case PROM_P1275:
		(*(romvec->pv_v2devops.v2_dev_close))(dhandle);
		return;
	};
	return;
}

/* Seek to specified location described by 'seekhi' and 'seeklo'
 * for device 'dhandle'.
 */
void
prom_seek(int dhandle, unsigned int seekhi, unsigned int seeklo)
{
	switch(prom_vers) {
	case PROM_V0:
		(*(romvec->pv_v0devops.v0_seekdev))(dhandle, seekhi, seeklo);
		break;
	case PROM_V2:
	case PROM_V3:
	case PROM_P1275:
		(*(romvec->pv_v2devops.v2_dev_seek))(dhandle, seekhi, seeklo);
		break;
	};

	return;
}
