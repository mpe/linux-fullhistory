/*
 * DECstation specific hardware access code.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 by Paul Antoine
 */
#include <linux/linkage.h>
#include <linux/types.h>
#include <asm/mc146818rtc.h>
#include <asm/vector.h>

asmlinkage void decstation_handle_int(void);
extern unsigned char maxine_rtc_read_data(unsigned long);
extern void maxine_rtc_write_data(unsigned char, unsigned long);

/*
 * FIXME: Don't have any of the goo required to access fd etc.
 */
struct feature decstation_feature = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,
	maxine_rtc_read_data,
	maxine_rtc_write_data
};
