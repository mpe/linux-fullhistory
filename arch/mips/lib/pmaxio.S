#include <asm/regdef.h>
#include <asm/decstation.h>

		.text
		.set	reorder
/*
 * pmax_printf - call the PROM printf() function
 */
		.globl	pmax_printf
pmax_printf:
		lw	v0,pmax_rex_base
		lw	v0,REX_PRINTF(v0)
		j	v0

/*
 * pmax_getchar - call the PROM getchar() function
 */
		.globl	pmax_getch
pmax_getch:
		lw	v0,pmax_rex_base
		lw	v0,REX_GETCHAR(v0)
		j	v0

/*
 * pmax_putchar - call the PROM putchar() function
 */
		.globl	pmax_putch
pmax_putch:
		lw	v0,pmax_rex_base
		lw	v0,REX_PUTCHAR(v0)
		j	v0

/*
 * pmax_halt - call the PROM halt() function
 */
		.globl	pmax_halt
pmax_halt:
		lw	v0,pmax_rex_base
		lw	v0,REX_HALT(v0)
		j	v0
