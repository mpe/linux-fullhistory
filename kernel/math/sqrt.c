/*
 * linux/kernel/math/sqrt.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * simple and stupid temporary real fsqrt() routine
 *
 * There are probably better ways to do this, but this should work ok.
 */

#include <linux/math_emu.h>
#include <linux/sched.h>

static void shift_right(int * c)
{
	__asm__("shrl $1,12(%0) ; rcrl $1,8(%0) ; rcrl $1,4(%0) ; rcrl $1,(%0)"
		::"r" ((long) c));
}

static int sqr64(unsigned long * a, unsigned long * b)
{
	unsigned long tmp[4];

	__asm__("movl (%0),%%eax  ; mull %%eax\n\t"
		"movl %%eax,(%1)   ; movl %%edx,4(%1)\n\t"
		"movl 4(%0),%%eax ; mull %%eax\n\t"
		"movl %%eax,8(%1)  ; movl %%edx,12(%1)\n\t"
		"movl (%0),%%eax  ; mull 4(%0)\n\t"
		"addl %%eax,%%eax   ; adcl %%edx,%%edx\n\t"
		"adcl $0,12(%1)   ; addl %%eax,4(%1)\n\t"
		"adcl %%edx,8(%1)  ; adcl $0,12(%1)"
		::"b" ((long) a),"c" ((long) tmp)
		:"ax","bx","cx","dx");
	if (tmp[3] > b[3] ||
	   (tmp[3] == b[3] && (tmp[2] > b[2] ||
	   (tmp[2] == b[2] && (tmp[1] > b[1] ||
	   (tmp[1] == b[1] && tmp[0] > b[0]))))))
		return 0;
	return 1;
}

void fsqrt(const temp_real * s, temp_real * d)
{
	unsigned long src[4];
	unsigned long res[2];
	int exponent;
	unsigned long mask, *c;
	int i;

	exponent = s->exponent;
	src[0] = src[1] = 0;
	src[2] = s->a;
	src[3] = s->b;
	d->exponent = 0;
	d->a = d->b = 0;
	if (!exponent)		/* fsqrt(0.0) = 0.0 */
		return;
	if (!src[2] && !src[3])
		return;
	if (exponent & 0x8000) {
		send_sig(SIGFPE,current,0);
		return;
	}
	if (exponent & 1) {
		shift_right(src);
		exponent++;
	}
	exponent >>= 1;
	exponent += 0x1fff;
	c = res + 2;
	mask = 0;
	for (i = 64 ; i > 0 ; i--) {
		if (!(mask >>= 1)) {
			c--;
			mask = 0x80000000;
		}
		res[0] = d->a; res[1] = d->b;
		*c |= mask;
		if (sqr64(res,src)) {
			d->a = res[0];
			d->b = res[1];
		}
	}
	if (!d->a && !d->b)
		return;
	while (!(d->b & 0x80000000)) {
		__asm__("addl %%eax,%%eax ; adcl %%edx,%%edx"
			:"=a" (d->a),"=d" (d->b)
			:"0" (d->a),"1" (d->b));
		exponent--;
	}
	d->exponent = exponent;
}
