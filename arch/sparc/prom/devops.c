/* $Id: devops.c,v 1.12 2000/01/29 01:09:12 anton Exp $
 * devops.c:  Device operations using the PROM.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/openprom.h>
#include <asm/oplib.h>

extern void restore_current(void);

/* Open the device described by the string 'dstr'.  Returns the handle
 * to that device used for subsequent operations on that device.
 * Returns -1 on failure.
 */
int
prom_devopen(char *dstr)
{
	int handle;
	unsigned long flags;
	save_flags(flags); cli();
	switch(prom_vers) {
	case PROM_V0:
		handle = (*(romvec->pv_v0devops.v0_devopen))(dstr);
		if(handle == 0) handle = -1;
		break;
	case PROM_V2:
	case PROM_V3:
		handle = (*(romvec->pv_v2devops.v2_dev_open))(dstr);
		break;
	default:
		handle = -1;
		break;
	};
	restore_current();
	restore_flags(flags);

	return handle;
}

/* Close the device described by device handle 'dhandle'. */
int
prom_devclose(int dhandle)
{
	unsigned long flags;
	save_flags(flags); cli();
	switch(prom_vers) {
	case PROM_V0:
		(*(romvec->pv_v0devops.v0_devclose))(dhandle);
		break;
	case PROM_V2:
	case PROM_V3:
		(*(romvec->pv_v2devops.v2_dev_close))(dhandle);
		break;
	default:
		break;
	};
	restore_current();
	restore_flags(flags);
	return 0;
}

/* Seek to specified location described by 'seekhi' and 'seeklo'
 * for device 'dhandle'.
 */
void
prom_seek(int dhandle, unsigned int seekhi, unsigned int seeklo)
{
	unsigned long flags;
	save_flags(flags); cli();
	switch(prom_vers) {
	case PROM_V0:
		(*(romvec->pv_v0devops.v0_seekdev))(dhandle, seekhi, seeklo);
		break;
	case PROM_V2:
	case PROM_V3:
		(*(romvec->pv_v2devops.v2_dev_seek))(dhandle, seekhi, seeklo);
		break;
	default:
		break;
	};
	restore_current();
	restore_flags(flags);

	return;
}
