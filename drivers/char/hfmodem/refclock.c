/*****************************************************************************/

/*
 *      refclock.c  --  Linux soundcard HF FSK driver,
 *                      Reference clock routines.
 *
 *      Copyright (C) 1997  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *        Swiss Federal Institute of Technology (ETH), Electronics Lab
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 */

/*****************************************************************************/

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/hfmodem.h>
#include <asm/processor.h>

/* --------------------------------------------------------------------- */

/*
 * currently this module is supposed to support both module styles, i.e.
 * the old one present up to about 2.1.9, and the new one functioning
 * starting with 2.1.21. The reason is I have a kit allowing to compile
 * this module also under 2.0.x which was requested by several people.
 * This will go in 2.2
 */
#include <linux/version.h>

#if LINUX_VERSION_CODE >= 0x20123
#include <linux/init.h>
#else
#define __init
#define __initdata
#define __initfunc(x) x
#endif

/* --------------------------------------------------------------------- */
/*
 * command line params
 */

static unsigned int scale_tvusec = 1UL<<24;

#ifdef __i386__
static unsigned int scale_rdtsc = 0;
static int rdtsc_ok = 1;
#endif /* __i386__ */

/* --------------------------------------------------------------------- */

#ifdef __i386__
__initfunc(static void i386_capability(void))
{
	if (boot_cpu_data.x86_capability & X86_FEATURE_TSC)
		rdtsc_ok = 1;
	else
		printk(KERN_INFO "%s: cpu does not support the rdtsc instruction\n", hfmodem_drvname);
}
#endif /* __i386__ */   

/* --------------------------------------------------------------------- */

__initfunc(void hfmodem_refclock_probe(void))
{
#ifdef __i386__
	if (rdtsc_ok) {
		rdtsc_ok = 0;
		i386_capability();
		if (rdtsc_ok) {
			unsigned int tmp0, tmp1, tmp2, tmp3;
			__asm__("rdtsc" : "=a" (tmp0), "=d" (tmp1));
			__asm__("rdtsc" : "=a" (tmp2), "=d" (tmp3));
			if (tmp0 == tmp2 && tmp1 == tmp3) {
				rdtsc_ok = 0;
				printk(KERN_WARNING "%s: rdtsc unusable, does not change\n",
				       hfmodem_drvname);
			}
		}
	}
	printk(KERN_INFO "%s: using %s as timing source\n", hfmodem_drvname,
	       rdtsc_ok ? "rdtsc" : "gettimeofday");
#endif /* __i386__ */
}

/* --------------------------------------------------------------------- */

void hfmodem_refclock_init(struct hfmodem_state *dev)
{
	struct timeval tv;

	dev->clk.lasttime = 0;
#ifdef __i386__
	if (rdtsc_ok) {
		__asm__("rdtsc;" : "=&d" (dev->clk.starttime_hi), "=&a" (dev->clk.starttime_lo));
		return;
	}
#endif /* __i386__ */
	do_gettimeofday(&tv);
	dev->clk.last_tvusec = tv.tv_usec;
	dev->clk.time_cnt = 0;
}

/* --------------------------------------------------------------------- */

hfmodem_time_t hfmodem_refclock_current(struct hfmodem_state *dev, hfmodem_time_t expected, int exp_valid)
{
	struct timeval tv;
	hfmodem_time_t curtime;
	long diff;

#ifdef __i386__
	if (rdtsc_ok) {
		unsigned int tmp0, tmp1;
		unsigned int tmp2, tmp3;
		
		__asm__("rdtsc;\n\t"
			"subl %2,%%eax\n\t"
			"sbbl %3,%%edx\n\t" : "=&a" (tmp0), "=&d" (tmp1) 
			: "m" (dev->clk.starttime_lo), "m" (dev->clk.starttime_hi) : "ax", "dx");
		__asm__("mull %1" : "=d" (tmp2) : "m" (scale_rdtsc), "a" (tmp0) : "ax");
		__asm__("mull %1" : "=a" (tmp3) : "m" (scale_rdtsc), "a" (tmp1) : "dx");
		curtime = tmp2 + tmp3;
		goto time_known;
	}
#endif /* __i386__ */
	do_gettimeofday(&tv);
	dev->clk.time_cnt += (unsigned)(1000000 + tv.tv_usec - dev->clk.last_tvusec) % 1000000;
	dev->clk.last_tvusec = tv.tv_usec;
	curtime = (dev->clk.time_cnt * scale_tvusec) >> 24;
  time_known:
	if (exp_valid && abs(diff = (curtime - dev->clk.lasttime - expected)) >= 1000)
		printk(KERN_DEBUG "%s: refclock adjustment %ld more than 1ms\n", 
		       hfmodem_drvname, diff);
	return (dev->clk.lasttime = curtime);
}

/* --------------------------------------------------------------------- */
