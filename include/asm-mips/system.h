/*
 * include/asm-mips/system.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Ralf Baechle
 */

#ifndef _ASM_MIPS_SYSTEM_H_
#define _ASM_MIPS_SYSTEM_H_

#include <linux/segment.h>
#include <mips/mipsregs.h>

/*
 * move_to_user_mode() doesn't switch to user mode on the mips, since
 * that would run us into problems: The kernel is located at virtual
 * address 0x80000000. If we now would switch over to user mode, we
 * we would immediately get an address error exception.
 * Anyway - we don't have problems with a task running in kernel mode,
 * as long it's code is foolproof.
 */
#define move_to_user_mode()

#define sti() \
__asm__ __volatile__( \
	"mfc0\t$1,"STR(CP0_STATUS)"\n\t" \
	"ori\t$1,$1,1\n\t" \
	"mtc0\t$1,"STR(CP0_STATUS)"\n\t" \
	: /* no outputs */ \
	: /* no inputs */ \
	: "$1","memory")

#define cli() \
__asm__ __volatile__( \
	"mfc0\t$1,"STR(CP0_STATUS)"\n\t" \
	"srl\t$1,$1,1\n\t" \
	"sll\t$1,$1,1\n\t" \
	"mtc0\t$1,"STR(CP0_STATUS)"\n\t" \
	: /* no outputs */ \
	: /* no inputs */ \
	: "$1","memory")

#define nop() __asm__ __volatile__ ("nop")

#define save_flags(x) \
__asm__ __volatile__( \
	".set\tnoreorder\n\t" \
	".set\tnoat\n\t" \
	"mfc0\t%0,$12\n\t" \
	".set\tat\n\t" \
	".set\treorder" \
	: "=r" (x) \
	: /* no inputs */ \
	: "memory")

#define restore_flags(x) \
__asm__ __volatile__( \
	".set\tnoreorder\n\t" \
	".set\tnoat\n\t" \
	"mtc0\t%0,$12\n\t" \
	".set\tat\n\t" \
	".set\treorder" \
	: /* no output */ \
	: "r" (x) \
	: "memory")

#endif /* _ASM_MIPS_SYSTEM_H_ */
