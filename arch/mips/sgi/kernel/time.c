/* $Id: time.c,v 1.1 1997/06/06 09:36:39 ralf Exp $
 * time.c: Generic SGI time_init() code, this will dispatch to the
 *         appropriate per-architecture time/counter init code.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */

extern void indy_timer_init(void);

void time_init(void)
{
	/* XXX assume INDY for now XXX */
	indy_timer_init();
}
