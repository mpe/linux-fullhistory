/*
 * linux/include/asm-arm/timex.h
 *
 * Architecture Specific TIME specifications
 *
 * Copyright (C) 1997,1998 Russell King
 */
#ifndef _ASMARM_TIMEX_H
#define _ASMARM_TIMEX_H

#include <asm/arch/timex.h>

typedef unsigned long cycles_t;

extern cycles_t cacheflush_time;

static inline cycles_t get_cycles (void)
{
	return 0;
}

#endif
