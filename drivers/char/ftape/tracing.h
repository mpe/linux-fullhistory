#ifndef _TRACING_H
#define _TRACING_H

/*
 * Copyright (C) 1994-1995 Bas Laarhoven.

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
 $Source: /home/bas/distr/ftape-2.03b/RCS/tracing.h,v $
 $Author: bas $
 *
 $Revision: 1.10 $
 $Date: 1995/04/22 07:30:15 $
 $State: Beta $
 *
 *      This file contains definitions that eases the debugging
 *      of the QIC-40/80 floppy-tape driver for Linux.
 */

#include <linux/kernel.h>

#ifdef NO_TRACE_AT_ALL
static inline void trace_dummy(void)
{
}

#define TRACE_FUN( level, name) int _trace_dummy
#define TRACE_EXIT              _trace_dummy= 0
#define TRACE_(l,m)             trace_dummy()
#define TRACE(l,m)              trace_dummy()
#define TRACEi(l,m,i)           trace_dummy()
#define TRACElx(l,m,i)          trace_dummy()
#define TRACEx1(l,m,a)          trace_dummy()
#define TRACEx2(l,m,a,b)        trace_dummy()
#define TRACEx3(l,m,a,b,c)      trace_dummy()
#define TRACEx4(l,m,a,b,c,d)    trace_dummy()
#define TRACEx5(l,m,a,b,c,d,e)  trace_dummy()
#define TRACEx6(l,m,a,b,c,d,e,f)  trace_dummy()
#else
#ifdef NO_TRACE
#define TOP_LEVEL 2
#else
#define TOP_LEVEL 10
#endif

#define TRACE_FUN( level, name) \
  char _trace_fun[] = name; \
  int _function_nest_level = trace_call( level, __FILE__, _trace_fun); \
  int _tracing = level

#define TRACE_EXIT \
  function_nest_level = _function_nest_level; \
  trace_exit( _tracing, __FILE__, _trace_fun)

#define TRACE_(l,m) \
{ \
  if (tracing >= (l) && (l) <= TOP_LEVEL) { \
    trace_log( __FILE__, _trace_fun); \
    m; \
  } \
}
#define TRACE(l,m) TRACE_(l,printk(m".\n"))
#define TRACEi(l,m,i) TRACE_(l,printk(m" %d.\n",i))
#define TRACElx(l,m,i) TRACE_(l,printk(m" 0x%08lx.\n",i))
#define TRACEx1(l,m,a) TRACE_(l,printk(m".\n",a))
#define TRACEx2(l,m,a,b) TRACE_(l,printk(m".\n",a,b))
#define TRACEx3(l,m,a,b,c) TRACE_(l,printk(m".\n",a,b,c))
#define TRACEx4(l,m,a,b,c,d) TRACE_(l,printk(m".\n",a,b,c,d))
#define TRACEx5(l,m,a,b,c,d,e) TRACE_(l,printk(m".\n",a,b,c,d,e))
#define TRACEx6(l,m,a,b,c,d,e,f) TRACE_(l,printk(m".\n",a,b,c,d,e,f))

/*      Global variables declared in tracing.c
 */
extern unsigned char trace_id;
extern int tracing;		/* sets default level */
extern int function_nest_level;

/*      Global functions declared in tracing.c
 */
extern int trace_call(int level, char *file, char *name);
extern void trace_exit(int level, char *file, char *name);
extern void trace_log(char *file, char *name);

#endif				/* NO_TRACE_AT_ALL */

#endif
