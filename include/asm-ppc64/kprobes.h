#ifndef _ASM_KPROBES_H
#define _ASM_KPROBES_H
/*
 *  Kernel Probes (KProbes)
 *  include/asm-ppc64/kprobes.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2002, 2004
 *
 * 2002-Oct	Created by Vamsi Krishna S <vamsi_krishna@in.ibm.com> Kernel
 *		Probes initial implementation ( includes suggestions from
 *		Rusty Russell).
 * 2004-Nov	Modified for PPC64 by Ananth N Mavinakayanahalli
 *		<ananth@in.ibm.com>
 */
#include <linux/types.h>
#include <linux/ptrace.h>

struct pt_regs;

typedef unsigned int kprobe_opcode_t;
#define BREAKPOINT_INSTRUCTION	0x7fe00008	/* trap */
#define MAX_INSN_SIZE 1

#define JPROBE_ENTRY(pentry)	(kprobe_opcode_t *)((func_descr_t *)pentry)

/* Architecture specific copy of original instruction */
struct arch_specific_insn {
	/* copy of original instruction */
	kprobe_opcode_t insn[MAX_INSN_SIZE];
};

#ifdef CONFIG_KPROBES
extern int kprobe_exceptions_notify(struct notifier_block *self,
				    unsigned long val, void *data);
#else				/* !CONFIG_KPROBES */
static inline int kprobe_exceptions_notify(struct notifier_block *self,
					   unsigned long val, void *data)
{
	return 0;
}
#endif
#endif				/* _ASM_KPROBES_H */
