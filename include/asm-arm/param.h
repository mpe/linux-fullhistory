/*
 * linux/include/asm-arm/param.h
 *
 * Copyright (C) 1995-1999 Russell King
 */
#ifndef __ASM_PARAM_H
#define __ASM_PARAM_H

#include <asm/arch/param.h>	/* for HZ */
#include <asm/proc/page.h>	/* for EXEC_PAGE_SIZE */

#ifndef HZ
#define HZ 100
#endif

#ifndef NGROUPS
#define NGROUPS         32
#endif

#ifndef NOGROUP
#define NOGROUP         (-1)
#endif

/* max length of hostname */
#define MAXHOSTNAMELEN  64

#endif

