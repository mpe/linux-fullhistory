/*
 * printf.c: Putting things on the screen using SGI arcs
 *           PROM facilities.
 *
 * Copyright (C) 1996 David S. Miller (dm@sgi.com)
 *
 * $Id: printf.c,v 1.2 1999/06/12 18:42:38 ulfc Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/sgialib.h>

static char ppbuf[1024];

#ifdef CONFIG_SGI_PROM_CONSOLE
void prom_printf(char *fmt, ...)
#else
__initfunc(void prom_printf(char *fmt, ...))
#endif
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
