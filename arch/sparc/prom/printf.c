/* printf.c:  Internal prom library printf facility.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

/* This routine is internal to the prom library, no one else should know
 * about or use it!  It's simple and smelly anyway....
 */

#include <stdarg.h>

#include <asm/openprom.h>
#include <asm/oplib.h>

char hexstring[] = "0123456789abcdef";

void
prom_printf(char *fmt, ...)
{
	va_list args;
	unsigned int ui_val;
	int i_val, n_ctr;
	char c_val;
	char nstr_buf[32];
	char *s_val;
	
	va_start(args, fmt);
	while(*fmt) {
		if(*fmt != '%') {
			if(*fmt == '\n')
				prom_putchar('\r');
			prom_putchar(*fmt++);
			continue;
		}

		fmt++;
		if(!*fmt) break;
		n_ctr = 0;
		switch(*fmt) {
		case 'c':
			c_val = va_arg(args, char);
			if(c_val == '\n')
				prom_putchar('\r');
			prom_putchar(c_val);
			fmt++;
			break;
		case 's':
			s_val = va_arg(args, char *);
			while(*s_val != 0) {
				prom_putchar(*s_val);
				s_val++;
			}
			fmt++;
			break;
		case 'd':
			/* Base 10 */
			i_val = va_arg(args, int);
			if(i_val==0x0)
				prom_putchar('0');
			else
				while(i_val != 0x0) {
					nstr_buf[n_ctr] = hexstring[i_val%0xa];
					i_val = ((unsigned long)i_val) / (unsigned) 0xa;
					n_ctr++;
				};
			while(--n_ctr >= 0)
				prom_putchar(nstr_buf[n_ctr]);
			fmt++;
			break;
		case 'x':
			/* Base 16 */
			ui_val = va_arg(args, unsigned int);
			if(ui_val==0x0)
				prom_putchar('0');
			else
				while(ui_val != 0x0) {
					nstr_buf[n_ctr] = hexstring[ui_val%0x10];
					ui_val = ((unsigned long) ui_val) / (unsigned) 0x10;
					n_ctr++;
				};
			while(--n_ctr >= 0)
				prom_putchar(nstr_buf[n_ctr]);
			fmt++;
			break;
		case 'o':
			/* Base 8 */
			ui_val = va_arg(args, unsigned int);
			if(ui_val==0x0)
				prom_putchar('0');
			else
				while(ui_val != 0x0) {
					nstr_buf[n_ctr] = hexstring[ui_val%0x8];
					ui_val = ((unsigned long) ui_val) / (unsigned) 0x8;
				};
			while(--n_ctr >= 0)
				prom_putchar(nstr_buf[n_ctr]);
			fmt++;
			break;
		default:
			/* Uh oh, something we can't handle... skip it */
			fmt++;
			break;
		};
	}

	/* We are done... */
	return;
}
