/* -*- linux-c -*-
 *
 *	$Id: sysrq.h,v 1.2 1997/05/31 18:33:41 mj Exp $
 *
 *	Linux Magic System Request Key Hacks
 *
 *	(c) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <linux/config.h>

extern int emergency_sync_scheduled;

#define EMERG_SYNC 1
#define EMERG_REMOUNT 2

extern void do_emergency_sync(void);

#ifdef CONFIG_MAGIC_SYSRQ
#define CHECK_EMERGENCY_SYNC			\
	if (emergency_sync_scheduled)		\
		do_emergency_sync();
#else
#define CHECK_EMERGENCY_SYNC
#endif
