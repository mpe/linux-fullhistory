
/*
 *      Copyright (C) 1993-1995 Bas Laarhoven.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 *      This file contains the reading code
 *      for the QIC-117 floppy-tape driver for Linux.
 */

#include <linux/ftape.h>

#include "tracing.h"

/*      Global vars.
 */
/*      tracing
 *      set it to:     to get:
 *       0              bugs
 *       1              + errors
 *       2              + warnings
 *       3              + information
 *       4              + more information
 *       5              + program flow
 *       6              + fdc/dma info
 *       7              + data flow
 *       8              + everything else
 */
int tracing = 3;		/* Default level: report only errors */

#ifndef NO_TRACE_AT_ALL

byte trace_id = 0;
int function_nest_level = 0;

/*      Local vars.
 */
static char spacing[] = "*                              ";

int trace_call(int level, char *file, char *name)
{
	char *indent;

	if (tracing >= level && level <= TOP_LEVEL) {
		/*    Since printk seems not to work with "%*s" format
		 *    we'll use this work-around.
		 */
		if (function_nest_level < sizeof(spacing)) {
			indent = spacing + sizeof(spacing) - 1 - function_nest_level;
		} else {
			indent = spacing;
		}
		printk(KERN_INFO "[%03d]%s+%s (%s)\n", (int) trace_id++, indent, file, name);
	}
	return function_nest_level++;
}

void trace_exit(int level, char *file, char *name)
{
	char *indent;

	if (tracing >= level && level <= TOP_LEVEL) {
		/*    Since printk seems not to work with "%*s" format
		 *    we'll use this work-around.
		 */
		if (function_nest_level < sizeof(spacing)) {
			indent = spacing + sizeof(spacing) - 1 - function_nest_level;
		} else {
			indent = spacing;
		}
		printk(KERN_INFO "[%03d]%s-%s (%s)\n", (int) trace_id++, indent, file, name);
	}
}

void trace_log(char *file, char *name)
{
	char *indent;

	/*    Since printk seems not to work with "%*s" format
	 *    we'll use this work-around.
	 */
	if (function_nest_level < sizeof(spacing)) {
		indent = spacing + sizeof(spacing) - 1 - function_nest_level;
	} else {
		indent = spacing;
	}
	printk(KERN_INFO "[%03d]%s%s (%s) - ", (int) trace_id++, indent, file, name);
}

#endif				/* NO_TRACE_AT_ALL */
