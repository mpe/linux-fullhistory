#ifndef _LINUX_STRING_H_
#define _LINUX_STRING_H_

#include <linux/types.h>	/* for size_t */

#ifndef NULL
#define NULL ((void *) 0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Include machine specific inline routines
 */
#include <asm/string.h>

#ifdef __USE_PORTABLE_STRINGS_H_
/*
 * include/generic/string.h imports all the string functions,
 * for which no appropriate assembler replacements have been provided.
 */
#include <asm-generic/string.h>
#endif

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_STRING_H_ */
