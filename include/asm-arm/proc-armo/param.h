/*
 * linux/include/asm-arm/proc-armo/param.h
 *
 * Copyright (C) 1995, 1996 Russell King
 */

#ifndef __ASM_PROC_PARAM_H
#define __ASM_PROC_PARAM_H

#ifndef HZ
#define HZ 100
#endif

#define EXEC_PAGESIZE   32768

#ifndef NGROUPS
#define NGROUPS         32
#endif

#ifndef NOGROUP
#define NOGROUP         (-1)
#endif

#define MAXHOSTNAMELEN  64      /* max length of hostname */

#endif

