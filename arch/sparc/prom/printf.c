/* $Id: printf.c,v 1.5 1996/04/04 16:31:07 tridge Exp $
 * printf.c:  Internal prom library printf facility.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

/* This routine is internal to the prom library, no one else should know
 * about or use it!  It's simple and smelly anyway....
 */

#include <linux/config.h>
#include <linux/kernel.h>

#include <asm/openprom.h>
#include <asm/oplib.h>

static char ppbuf[1024];

void
prom_printf(char *fmt, ...)
{
	va_list args;
	char ch, *bptr;
	int i;

	va_start(args, fmt);
	i = vsprintf(ppbuf, fmt, args);

	bptr = ppbuf;

#if CONFIG_AP1000
        ap_write(1,bptr,strlen(bptr));
#else
	while((ch = *(bptr++)) != 0) {
		if(ch == '\n')
			prom_putchar('\r');

		prom_putchar(ch);
	}
#endif
	va_end(args);
	return;
}
