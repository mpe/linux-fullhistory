/* $Id: p1275.c,v 1.7 1997/03/18 17:59:55 jj Exp $
 * p1275.c: Sun IEEE 1275 PROM low level interface routines
 *
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>

#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/spitfire.h>
#include <asm/system.h>
#include <asm/pstate.h>

/* If you change layout of this structure, please change the prom_doit
   function below as well. */
typedef struct {
	unsigned prom_doit_code [24];		/* 0x8000 */
	long prom_sync_routine;			/* 0x8060 */
	void (*prom_cif_handler)(long *);	/* 0x8068 */
	unsigned long prom_cif_stack;		/* 0x8070 */
	unsigned long prom_args [23];		/* 0x8078 */
	char prom_buffer [7888];
} at0x8000;

static void (*prom_do_it)(void);

void prom_cif_interface (void) __attribute__ ((__section__ (".p1275")));

/* At most 14 insns */
void prom_cif_interface (void)
{
	__asm__ __volatile__ ("
	sethi	%%hi(0x8000), %%o0
	ldx	[%%o0 + 0x070], %%o1	! prom_cif_stack
	save	%%o1, -0xc0, %%sp
	ldx	[%%i0 + 0x068], %%l2	! prom_cif_handler
	rdpr	%%pstate, %%l4
	mov	%%g4, %%l0
	mov	%%g6, %%l1
	wrpr	%%l4, %0, %%pstate	! turn on address masking
	call	%%l2
	 or	%%i0, 0x078, %%o0	! prom_args
	wrpr	%%l4, 0, %%pstate	! put pstate back
	mov	%%l0, %%g4
	ret
	 restore %%l1, 0, %%g6
	save	%%sp, -0xc0, %%sp	! If you change the offset of the save 
	rdpr	%%pstate, %%l4		! here, please change the 0x8038 
	andn	%%l4, %0, %%l3		! constant below as well
	wrpr	%%l3, %%pstate
	ldx	[%%o0 + 0x060], %%l2
	call	%%l2
	 nop
	wrpr	%%l4, 0, %%pstate
	ret
	 restore
	" : : "i" (PSTATE_AM));
}

long p1275_cmd (char *service, long fmt, ...)
{
	char *p, *q;
	unsigned long flags;
	int nargs, nrets, i;
	va_list list;
	long attrs, x;
	long ctx = 0;
	at0x8000 *low = (at0x8000 *)(0x8000);
	
	p = low->prom_buffer;
	save_and_cli(flags);
	ctx = spitfire_get_primary_context ();
	if (ctx) {
		flushw_user ();
		spitfire_set_primary_context (0);
	}
	low->prom_args[0] = (unsigned long)p;			/* service */
	strcpy (p, service);
	p = (char *)(((long)(strchr (p, 0) + 8)) & ~7);
	low->prom_args[1] = nargs = (fmt & 0x0f);		/* nargs */
	low->prom_args[2] = nrets = ((fmt & 0xf0) >> 4); 	/* nrets */
	attrs = fmt >> 8;
	va_start(list, fmt);
	for (i = 0; i < nargs; i++, attrs >>= 3) {
		switch (attrs & 0x7) {
		case P1275_ARG_NUMBER:
			low->prom_args[i + 3] = (unsigned)va_arg(list, long); break;
		case P1275_ARG_IN_STRING:
			strcpy (p, va_arg(list, char *));
			low->prom_args[i + 3] = (unsigned long)p;
			p = (char *)(((long)(strchr (p, 0) + 8)) & ~7);
			break;
		case P1275_ARG_OUT_BUF:
			(void) va_arg(list, char *);
			low->prom_args[i + 3] = (unsigned long)p;
			x = va_arg(list, long);
			i++; attrs >>= 3;
			p = (char *)(((long)(p + (int)x + 7)) & ~7);
			low->prom_args[i + 3] = x;
			break;
		case P1275_ARG_IN_BUF:
			q = va_arg(list, char *);
			low->prom_args[i + 3] = (unsigned long)p;
			x = va_arg(list, long);
			i++; attrs >>= 3;
			memcpy (p, q, (int)x);
			p = (char *)(((long)(p + (int)x + 7)) & ~7);
			low->prom_args[i + 3] = x;
			break;
		case P1275_ARG_OUT_32B:
			(void) va_arg(list, char *);
			low->prom_args[i + 3] = (unsigned long)p;
			p += 32;
			break;
		case P1275_ARG_IN_FUNCTION:
			low->prom_args[i + 3] = 0x8038;
			low->prom_sync_routine = va_arg(list, long); break;
		}
	}
	va_end(list);
	
	(*prom_do_it)();

	attrs = fmt >> 8;
	va_start(list, fmt);
	for (i = 0; i < nargs; i++, attrs >>= 3) {
		switch (attrs & 0x7) {
		case P1275_ARG_NUMBER:
			(void) va_arg(list, long); break;
		case P1275_ARG_IN_STRING:
			(void) va_arg(list, char *); break;
		case P1275_ARG_IN_FUNCTION:
			(void) va_arg(list, long); break;
		case P1275_ARG_IN_BUF:
			(void) va_arg(list, char *);
			(void) va_arg(list, long);
			i++; attrs >>= 3;
			break;
		case P1275_ARG_OUT_BUF:
			p = va_arg(list, char *);
			x = va_arg(list, long);
			memcpy (p, (char *)(low->prom_args[i + 3]), (int)x);
			i++; attrs >>= 3;
			break;
		case P1275_ARG_OUT_32B:
			p = va_arg(list, char *);
			memcpy (p, (char *)(low->prom_args[i + 3]), 32);
			break;
		}
	}
	va_end(list);
	x = low->prom_args [nargs + 3];
	
	if (ctx)
		spitfire_set_primary_context (ctx);
	restore_flags(flags);
	return x;
}

void prom_cif_init(void *cif_handler, void *cif_stack)
{
	at0x8000 *low = (at0x8000 *)(0x8000);

	low->prom_cif_handler = (void (*)(long *))cif_handler;
	low->prom_cif_stack = (unsigned long)cif_stack;
        prom_do_it = (void (*)(void))(0x8000);
}
