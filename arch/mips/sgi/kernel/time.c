/* $Id: time.c,v 1.2 1998/05/01 01:35:20 ralf Exp $
 * time.c: Generic SGI time_init() code, this will dispatch to the
 *         appropriate per-architecture time/counter init code.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/init.h>

extern void indy_timer_init(void);

__initfunc(void time_init(void))
{
	/* XXX assume INDY for now XXX */
	indy_timer_init();
}
