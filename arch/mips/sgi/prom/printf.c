/*
 * printf.c: Putting things on the screen using SGI arcs
 *           PROM facilities.
 *
 * Copyright (C) 1996 David S. Miller (dm@sgi.com)
 *
 * $Id: printf.c,v 1.2 1998/03/27 08:53:48 ralf Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/sgialib.h>

static char ppbuf[1024];

__initfunc(void prom_printf(char *fmt, ...))
{
	va_list args;
	char ch, *bptr;
	int i;

	va_start(args, fmt);
	i = vsprintf(ppbuf, fmt, args);

	bptr = ppbuf;

	while((ch = *(bptr++)) != 0) {
		if(ch == '\n')
			prom_putchar('\r');

		prom_putchar(ch);
	}
	va_end(args);
	return;
}
