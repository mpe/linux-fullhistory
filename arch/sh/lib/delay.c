/*
 *	Precise Delay Loops for SuperH
 *
 *	Copyright (C) 1999 Niibe Yutaka
 */

#include <linux/sched.h>
#include <linux/delay.h>

inline void __const_udelay(unsigned long xloops)
{
	xloops *= current_cpu_data.loops_per_sec;
        __delay(xloops);
}

#if 0
void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c6);  /* 2**32 / 1000000 */
}
#endif
