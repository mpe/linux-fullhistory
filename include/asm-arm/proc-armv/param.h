/*
 * linux/include/asm-arm/proc-armv/param.h
 *
 * Copyright (C) 1996 Russell King
 */

#ifndef __ASM_PROC_PARAM_H
#define __ASM_PROC_PARAM_H

#ifndef HZ
#define HZ 100
#endif

#define EXEC_PAGESIZE   4096

#ifndef NGROUPS
#define NGROUPS         32
#endif

#ifndef NOGROUP
#define NOGROUP         (-1)
#endif

#define MAXHOSTNAMELEN  64      /* max length of hostname */

#endif

