/* 
 * File...........: linux/drivers/s390/block/dasd_3370_erp.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000
 */

#include <asm/ccwcache.h>

#include "dasd_int.h"
#include "dasd_3370_erp.h"
#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#define PRINTK_HEADER "dasd_erp(3370)"
#endif				/* PRINTK_HEADER */

/*
 * DASD_3370_ERP_EXAMINE 
 *
 * DESCRIPTION
 *   Checks only for fatal/no/recover error. 
 *   A detailed examination of the sense data is done later outside
 *   the interrupt handler.
 *
 *   The logic is based on the 'IBM 3880 Storage Control Reference' manual
 *   'Chapter 7. 3370 Sense Data'.
 *
 * RETURN VALUES
 *   dasd_era_none      no error 
 *   dasd_era_fatal     for all fatal (unrecoverable errors)
 *   dasd_era_recover   for all others.
 */
dasd_era_t 
dasd_3370_erp_examine (ccw_req_t * cqr, devstat_t * stat)
{
	char *sense = stat->ii.sense.data;

	/* check for successful execution first */
	if (stat->cstat == 0x00 &&
	    stat->dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END))
		    return dasd_era_none;
	if (sense[0] & 0x80) {	/* CMD reject */
		return dasd_era_fatal;
	}
	if (sense[0] & 0x40) {	/* Drive offline */
		return dasd_era_recover;
	}
	if (sense[0] & 0x20) {	/* Bus out parity */
		return dasd_era_recover;
	}
	if (sense[0] & 0x10) {	/* equipment check */
		if (sense[1] & 0x80) {
			return dasd_era_fatal;
		}
		return dasd_era_recover;
	}
	if (sense[0] & 0x08) {	/* data check */
		if (sense[1] & 0x80) {
			return dasd_era_fatal;
		}
		return dasd_era_recover;
	}
	if (sense[0] & 0x04) {	/* overrun */
		if (sense[1] & 0x80) {
			return dasd_era_fatal;
		}
		return dasd_era_recover;
	}
	if (sense[1] & 0x40) {	/* invalid blocksize */
		return dasd_era_fatal;
	}
	if (sense[1] & 0x04) {	/* file protected */
		return dasd_era_recover;
	}
	if (sense[1] & 0x01) {	/* operation incomplete */
		return dasd_era_recover;
	}
	if (sense[2] & 0x80) {	/* check data erroor */
		return dasd_era_recover;
	}
	if (sense[2] & 0x10) {	/* Env. data present */
		return dasd_era_recover;
	}
	/* examine the 24 byte sense data */
	return dasd_era_recover;

}				/* END dasd_3370_erp_examine */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
