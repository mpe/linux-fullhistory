#ifndef _ASM_IA64_CACHE_H
#define _ASM_IA64_CACHE_H

#include <linux/config.h>

/*
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */

/* Bytes per L1 (data) cache line.  */
#define LOG_L1_CACHE_BYTES	6
#define L1_CACHE_BYTES		(1 << LOG_L1_CACHE_BYTES)

#ifdef CONFIG_SMP
# define SMP_LOG_CACHE_BYTES	LOG_L1_CACHE_BYTES
# define SMP_CACHE_BYTES	L1_CACHE_BYTES
#else
  /*
   * The "aligned" directive can only _increase_ alignment, so this is
   * safe and provides an easy way to avoid wasting space on a
   * uni-processor:
   */
# define SMP_LOG_CACHE_BYTES	3
# define SMP_CACHE_BYTES	(1 << 3)
#endif

#endif /* _ASM_IA64_CACHE_H */
