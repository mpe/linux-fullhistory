/* $Id: k1275d.c,v 1.1 1996/12/27 08:49:12 jj Exp $
 * k1275d.c: Sun IEEE 1275 PROM kernel daemon
 *
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/openprom.h>
#include <asm/oplib.h>

#define p1275sect __attribute__ ((__section__ (".p1275")))

static void prom_doit (void) p1275sect;

static void prom_doit (void)
{
	/* FIXME: The PROM stack is grossly misaligned.
	 * It needs some special way of handling spills/fills.
	 * Either using %otherwin or doing flushw; wrpr somevalue, %wstate
	 * should be done here to make difference between PROM stack
	 * saving (32bit, misaligned stack) and kernel fill/spill).
	 */
	__asm__ __volatile__ ("
	save	%%sp, -0xc0, %%sp;
	mov	%%sp, %%l1;
	mov	%0, %%sp;
	call	%1;
	 mov	%0, %%o0;
	mov	%%l1, %%sp;
	restore;
	" : : "r" (prom_cif_stack), "r" (prom_cif_handler));
}

static void (*prom_cif_handler)(long *) p1275sect;
static void prom_cif_stack;
static void (*prom_do_it)(void);
static long prom_args [23] p1275sect;
static char prom_buffer [4096] p1275sect;

long prom_handle_command(char *service, long fmt, ...)
{
	char *p = prom_buffer;
	unsigned long flags;
	int nargs, nrets, i;
	va_list list;
	long attrs, x;
	
	save_and_cli(flags);
	prom_args[0] = p;				/* service */
	strcpy (p, function);
	p = strchr (p, 0) + 1;
	prom_args[1] = nargs = (fmt & 0x0f);		/* nargs */
	prom_args[2] = nrets = ((fmt & 0xf0) >> 4); 	/* nrets */
	attrs = (fmt & (~0xff));
	va_start(list, fmt);
	for (i = 0; i < nargs; i++, attrs >>= 3) {
		switch (attrs & 0x7) {
		case P1275_ARG_NUMBER:
			prom_args[i + 3] = va_arg(list, long); break;
		case P1275_ARG_IN_STRING:
			strcpy (p, va_arg(list, char *));
			prom_args[i + 3] = p;
			p = strchr (p, 0) + 1;
			break;
		case P1275_ARG_OUT_BUF:
			va_arg(list, char *);
			prom_args[i + 3] = p;
			x = va_arg(list, long);
			i++; attrs >>= 3;
			p += (int)x;
			prom_args[i + 3] = x;
			break;
		case P1275_ARG_OUT_32B:
			va_arg(list, char *);
			prom_args[i + 3] = p;
			p += 32;
			break;
		case P1275_ARG_IN_FUNCTION:
			/* FIXME: This should make a function in our <4G
			 * section, which will call the argument,
			 * so that PROM can call it.
			 */
			prom_args[i + 3] = va_arg(list, long); break;
		}
	}
	va_end(list);
	(*prom_do_it)();
	attrs = fmt & (~0xff);
	va_start(list, fmt);
	for (i = 0; i < nargs; i++, attrs >>= 3) {
		switch (attrs & 0x7) {
		case P1275_ARG_NUMBER:
		case P1275_ARG_IN_STRING:
		case P1275_ARG_IN_FUNCTION:
			break;
		case P1275_ARG_OUT_BUF:
			p = va_arg(list, char *);
			x = va_arg(list, long);
			memcpy (p, (char *)prom_args[i + 3], (int)x);
			i++; attrs >>= 3;
			break;
		case P1275_ARG_OUT_32B:
			p = va_arg(list, char *);
			memcpy (p, (char *)prom_args[i + 3], 32);
			break;
		}
	}
	va_end(list);
	x = prom_args [nargs + 3];
	restore_flags(flags);
	return x;
}

void prom_cif_init(void *cif_handler, void *cif_stack)
{
	prom_cif_handler = (void (*)(long *))cif_handler;
	prom_cif_stack = cif_stack;
	prom_command = prom_handle_command;
        prom_do_it = prom_doit;
}
