/* $Id: timex.h,v 1.1 1998/08/17 10:20:18 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 by Ralf Baechle
 *
 * FIXME: For some of the supported machines this is dead wrong.
 */
#ifndef __ASM_MIPS_TIMEX_H
#define __ASM_MIPS_TIMEX_H

#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */
#define CLOCK_TICK_FACTOR	20	/* Factor of both 1000000 and CLOCK_TICK_RATE */
#define FINETUNE ((((((long)LATCH * HZ - CLOCK_TICK_RATE) << SHIFT_HZ) * \
	(1000000/CLOCK_TICK_FACTOR) / (CLOCK_TICK_RATE/CLOCK_TICK_FACTOR)) \
		<< (SHIFT_SCALE-SHIFT_HZ)) / HZ)

#endif /*  __ASM_MIPS_TIMEX_H */
