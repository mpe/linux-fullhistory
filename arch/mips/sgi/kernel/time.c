/* $Id: time.c,v 1.2 1998/04/05 11:24:00 ralf Exp $
 * time.c: Generic SGI time_init() code, this will dispatch to the
 *         appropriate per-architecture time/counter init code.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/init.h>

extern void indy_timer_init(void);

void __init time_init(void)
{
	/* XXX assume INDY for now XXX */
	indy_timer_init();
}
